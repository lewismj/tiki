#!/usr/bin/env python3
"""A terminal interface for the training pipeline: download, convert, relabel, rescale, train.

    python3 training/tui.py          (needs 'pip install textual')

Runs on Linux, WSL and macOS, and on Windows, where the jobs run in WSL (wsl.exe) and the paths in
the settings are WSL paths (/mnt/f/...; F:\\... is understood too).

Pick a stage on the left, set its options, and run it (F5). The commands a stage will run are shown
under its options. Each stage runs as a job of its own with its output below, so a download can run
beside a conversion. Train also graphs the run in its Output directory, from its train.log: the
training and validation loss by epoch, and how far the validation loss has fallen since the first
epoch (for any run, whether it was started here or not). Jobs are detached: closing the interface (Ctrl+Q) leaves them running, and
opening it again shows them. F8 stops the stage's job (again to insist). Settings shared by several
stages (the months, the directories) are the same field on each, and everything is remembered in
~/.config/tiki-training-tui (TIKI_TUI_DIR), with the jobs' logs.

The stages and their commands are in pipeline.py.
"""

import glob
import os
import shutil
import sys
import time

from rich.text import Text
from textual.app import App
from textual.design import ColorSystem
from textual.containers import Horizontal, Vertical, VerticalScroll
from textual.widgets import (Button, Checkbox, ContentSwitcher, Footer, Header, Input, Label, ListItem, ListView,
                             ProgressBar, RichLog, Select, Static)

try:
    from textual.theme import Theme
except ImportError:         # Textual from before it had themes.
    Theme = None

import pipeline
from chart import Series, chart
from pipeline import FIELDS, STAGES, PipelineError

WIDE = {"months", "download_dir", "converted_dir", "relabelled_dir", "rescaled_dir", "tables", "train_data",
        "out_dir", "resume", "train_extra", "tiki_app"}
PREVIEW_STEPS = 5
GB = 1024 ** 3

# Dolphie's colours (github.com/charles-001/dolphie, Modules/Theme.py).
COLOURS = dict(primary="#91abec", secondary="#6171a6", warning="#f6ff8f", error="#fd8383", success="#54efae",
               accent="#b565f3", background="#0a0e1b", surface="#0f1525", panel="#192036", dark=True)
LABEL, VALUE, DIM = "#c5c7d2", "bold #91abec", "#969aad"
OWN_LINE = "bold #bbc8e8"       # The job's own lines in its log.
ERROR, GOOD = "bold #fd8383", "bold #54efae"
TRAIN_COLOUR, VALIDATION_COLOUR, GAIN_COLOUR = "#44b4ff", "#54efae", "#fcd579"


def labelled(*pairs):
    """'Label value   Label value ...' as Dolphie writes them: quiet labels, bright values."""
    text = Text(no_wrap=True)
    for label, value in pairs:
        text.append(f"{label} ", style=LABEL)
        text.append(f"{value}   ", style=VALUE)
    return text


class Chart(Static):
    """A panel with a line chart (chart.py), drawn again when its size changes."""

    def __init__(self, title, **kwargs):
        super().__init__("", **kwargs)
        self.border_title = title
        self.drawing = None

    def show(self, series, **options):
        self.drawing = (series, options)
        self.draw()

    def draw(self):
        if self.drawing is not None:
            series, options = self.drawing
            self.update(chart(series, self.size.width, self.size.height, **options))

    def on_resize(self):
        self.draw()


def duration(seconds):
    seconds = int(seconds)
    return f"{seconds // 3600}:{seconds // 60 % 60:02d}:{seconds % 60:02d}"


class StagePage(Vertical):
    """A stage: its options, the commands it will run, and its job's progress and output."""

    def __init__(self, stage):
        super().__init__(id=f"page-{stage.name}")
        self.stage = stage
        self.inputs = {}
        self.job = None             # The record of the stage's last job (pipeline.start), running or not.
        self.process = None         # Its process, if it was started in this session.
        self.job_running = False
        self.job_progress = pipeline.Progress()
        self.log_tail = None
        self.train_tail = None      # train.log, where the trainer writes its progress.
        self.train_fraction = None
        self.detail = ""
        self.stops = 0
        self.download_seen = None   # (time, bytes) of the partial download when last looked at.
        self.history_seen = None    # (path, size, modified) of the train.log the graphs were drawn from.

    def field(self, key):
        spec = FIELDS[key]
        value = self.app.settings[key]
        if spec.kind == "bool":
            widget = Checkbox(spec.label, value=bool(value), name=key)
        elif spec.kind == "choice":
            value = value if value in spec.choices else spec.default
            widget = Select([(choice, choice) for choice in spec.choices], value=value, allow_blank=False, name=key)
        else:
            widget = Input(value=str(value), placeholder=spec.help, name=key)
        if spec.help:
            widget.tooltip = spec.help
        self.inputs[key] = widget
        return Horizontal(Label("" if spec.kind == "bool" else spec.label, classes="label"), widget, classes="cell")

    def compose(self):
        yield Static(self.stage.description, classes="description")
        with VerticalScroll(classes="form"):
            narrow = []
            for key in self.stage.fields:
                if key in WIDE:
                    yield Horizontal(self.field(key), classes="row")
                else:
                    narrow.append(key)
            for index in range(0, len(narrow), 2):
                yield Horizontal(*(self.field(key) for key in narrow[index:index + 2]), classes="row")
        yield Static("", classes="preview")
        with Horizontal(classes="controls"):
            yield Button("Run (F5)", name="run", variant="success")
            yield Button("Stop (F8)", name="stop", variant="error", disabled=True)
            yield Static("", classes="status")
        yield ProgressBar(total=100, show_eta=False)
        if self.stage.name == "train":
            yield Static("", classes="summary")
            with Horizontal(classes="charts"):
                yield Chart("Loss by epoch", id="chart-loss")
                yield Chart("Validation improvement", id="chart-gain")
        log = RichLog(max_lines=5000, wrap=False, highlight=False, markup=False)
        log.border_title = "Output"
        yield log

    def on_mount(self):
        job = self.app.jobs.get(self.stage.name)
        if job:
            self.attach(job, None, pipeline.tail_from(job["log"]))
        self.refresh_preview()
        self.set_interval(0.3, self.poll)
        if self.stage.name == "train":
            self.set_interval(3, self.refresh_history)

    # ---- Options ----

    def on_input_changed(self, event):
        self.app.set_value(event.input.name, event.value, self)

    def on_checkbox_changed(self, event):
        self.app.set_value(event.checkbox.name, bool(event.value), self)

    def on_select_changed(self, event):
        self.app.set_value(event.select.name, str(event.value), self)

    def show_value(self, key, value):
        """Shows a value changed on another stage's page."""
        widget = self.inputs.get(key)
        if widget is not None and widget.value != value:
            with widget.prevent(Input.Changed, Checkbox.Changed, Select.Changed):
                widget.value = value

    def refresh_preview(self):
        if self.stage.name == "train":
            self.refresh_history()
        preview = self.query_one(".preview", Static)
        run = self.query_one("Button.-success", Button)
        try:
            steps = self.stage.build(self.app.settings)
        except PipelineError as error:
            preview.update(Text(str(error), style=ERROR))
            run.disabled = True
            return
        text = Text()
        for step in steps[:PREVIEW_STEPS - 1 if len(steps) > PREVIEW_STEPS else PREVIEW_STEPS]:
            command = step.command()
            text.append("$ ", style=VALUE)
            text.append((command if len(command) <= 400 else command[:400] + " ...") + "\n", style=DIM)
        if len(steps) > PREVIEW_STEPS:
            text.append(f"... and {len(steps) - PREVIEW_STEPS + 1} more steps\n", style=LABEL)
        text.rstrip()
        preview.update(text)
        run.disabled = self.job_running

    # ---- The job ----

    def run_job(self):
        if self.job_running:
            return
        settings = self.app.settings
        try:
            steps = self.stage.build(settings)
        except PipelineError as error:
            self.app.notify(str(error), title=self.stage.title, severity="error")
            return
        try:
            job, process = pipeline.start(self.stage.name, steps, os.path.join(pipeline.state_dir(), "logs"))
        except OSError as error:
            self.app.notify(f"Couldn't start the job: {error}", title=self.stage.title, severity="error")
            return
        if self.stage.name == "train":
            path = pipeline.local_path(pipeline.join(pipeline.text(settings, "out_dir"), "train.log"))
            job.update(train_log=path, train_log_offset=os.path.getsize(path) if os.path.exists(path) else 0,
                       epochs=int(pipeline.text(settings, "epochs")))
        if any("lichess_download.py" in step.argv for step in steps):
            job["download_dir"] = pipeline.local_path(pipeline.text(settings, "download_dir"))
        self.query_one(RichLog).clear()
        self.attach(job, process, pipeline.LogTail(job["log"]))
        self.app.jobs[self.stage.name] = job
        self.app.save()

    def attach(self, job, process, tail):
        self.job, self.process, self.log_tail = job, process, tail
        self.job_progress = pipeline.Progress()
        pipeline.replay_markers(job["log"], tail.offset, self.job_progress)
        self.was_alive, self.alive_checked = True, 0.0
        self.train_tail = (pipeline.LogTail(job["train_log"], job.get("train_log_offset", 0))
                           if job.get("train_log") else None)
        self.train_fraction, self.detail, self.stops, self.download_seen = None, "", 0, None
        self.job_running = True
        self.poll()

    def stop_job(self):
        if not self.job_running:
            return
        sent = pipeline.stop(self.job, self.stops)
        if sent is None:
            self.app.notify("The job is still starting; try again in a moment.", title=self.stage.title)
            return
        self.stops += 1
        self.app.notify(f"Sent {sent}." + (" Stop again to insist." if sent != "SIGKILL" else ""),
                        title=self.stage.title)

    def on_button_pressed(self, event):
        if event.button.name == "run":
            self.run_job()
        else:
            self.stop_job()

    def poll(self):
        """Shows what the job has written since the last look, and notices when it has ended."""
        if not self.job_running:
            return
        # Before reading, so the job's last lines aren't missed. A job from an earlier session is looked
        # up with a helper process (tasklist, ps), so only every few seconds.
        if self.process is not None or time.time() - self.alive_checked >= 3:
            self.was_alive, self.alive_checked = pipeline.alive(self.job, self.process), time.time()
        was_alive = self.was_alive
        log = self.query_one(RichLog)
        for line in self.log_tail.read():
            own = self.job_progress.line(line)
            if pipeline.PID_RE.match(line):
                self.job["linux_pid"] = self.job_progress.pid       # What Stop signals on Windows.
                self.app.save()
                continue
            log.write(Text(line, style=OWN_LINE if own else ""))
            if own:
                self.detail = ""
            elif pipeline.PERCENT_RE.search(line):
                self.detail = " ".join(line.split())
        if self.train_tail is not None:
            for line in self.train_tail.read():
                fraction = pipeline.training_fraction(line, self.job.get("epochs", 1))
                if fraction is not None:
                    self.train_fraction = fraction
                    self.detail = line[20:]                     # Past the timestamp.
        self.watch_download()

        if self.job_progress.exit_code is not None or not was_alive:
            self.job_running = False
            announce = "exit" not in self.job                   # Not one already seen to end in an earlier session.
            self.job["exit"] = self.job_progress.exit_code
            if announce:
                self.job["ended"] = time.time()
                self.app.save()
                self.app.notify(self.state_text(), title=self.stage.title,
                                severity="information" if self.job["exit"] == 0 else "error")
        self.show_state()

    def refresh_history(self):
        """The training graphs and the line above them, from the train.log in Output: the run in progress,
        or any earlier one (started here or not). Drawn again only when the file has changed."""
        try:
            path = pipeline.local_path(pipeline.join(pipeline.text(self.app.settings, "out_dir"), "train.log"))
        except PipelineError:
            return
        try:
            seen = (path, os.path.getsize(path), os.path.getmtime(path))
        except OSError:
            seen = (path, None, None)
        if seen == self.history_seen:
            return
        self.history_seen = seen
        rows, epochs = pipeline.training_history(path)
        loss, gain = self.query_one("#chart-loss", Chart), self.query_one("#chart-gain", Chart)
        summary = self.query_one(".summary", Static)
        if not rows:
            summary.update(Text(f"No finished epochs in {path}", style=DIM))
            loss.show([])
            gain.show([])
            return
        numbers = [epoch for epoch, _, _ in rows]
        first, (last, train, validation) = rows[0], rows[-1]
        best = min(rows, key=lambda row: row[2])
        gains = [100 * (first[2] - row[2]) / first[2] for row in rows]
        x_range = (first[0], max(epochs or last, last))
        summary.update(labelled(("Epoch", f"{last}/{epochs}" if epochs else last), ("Train", f"{train:.6f}"),
                                ("Validation", f"{validation:.6f}"), ("Best", f"{best[2]:.6f} (epoch {best[0]})"),
                                ("Improvement", f"{gains[-1]:.2f}%")))
        loss.show([Series("training", TRAIN_COLOUR, numbers, [row[1] for row in rows]),
                   Series("validation", VALIDATION_COLOUR, numbers, [row[2] for row in rows])],
                  y_format="{:.5f}", x_range=x_range)
        gain.border_title = f"Validation improvement over epoch {first[0]}"
        gain.show([Series(f"% diff epoch {first[0]}", GAIN_COLOUR, numbers, gains)],
                  y_format="{:.2f}%", x_range=x_range)

    def watch_download(self):
        """The partial download's size and speed: lichess_download.py shows no progress when it isn't
        writing to a terminal."""
        directory = self.job.get("download_dir")
        if not directory or "lichess_download.py" not in self.job_progress.command:
            return
        now = time.time()
        if self.download_seen and now - self.download_seen[0] < 2:
            return
        parts = glob.glob(os.path.join(directory, "*.pgn.zst.part"))
        try:
            path = max(parts, key=os.path.getmtime)
            size = os.path.getsize(path)
        except (OSError, ValueError):
            return
        month = os.path.basename(path).removeprefix("lichess_db_standard_rated_").removesuffix(".pgn.zst.part")
        speed = ""
        if self.download_seen and size >= self.download_seen[1]:
            speed = f", {(size - self.download_seen[1]) / (now - self.download_seen[0]) / 1e6:.1f} MB/s"
        self.download_seen = (now, size)
        self.detail = f"{month}: {size / GB:.2f} GB downloaded{speed}"

    def state_text(self):
        code = self.job.get("exit")
        took = f" after {duration(self.job['ended'] - self.job['started'])}" if "ended" in self.job else ""
        if code == 0:
            return f"Finished{took}"
        if code in (130, 143):
            return f"Stopped{took}"
        if code is None:
            return f"Ended without an exit status{took} (killed?)"
        return f"Failed{took} (exit {code})"

    def show_state(self):
        progress = self.job_progress
        if self.job_running:
            status = labelled(("Running", duration(time.time() - self.job["started"])))
            if progress.steps > 1:
                status.append_text(labelled(("Step", f"{progress.step}/{progress.steps}")))
            status.append(self.detail, style=LABEL)
            mark = "●"
        else:
            good = self.job.get("exit") == 0
            status, mark = Text(self.state_text(), style=GOOD if good else ERROR), "✓" if good else "✗"
        fraction = progress.fraction() if self.train_fraction is None or progress.exit_code == 0 else self.train_fraction
        self.query_one(".status", Static).update(status)
        self.query_one(ProgressBar).update(progress=100 * fraction)
        self.query_one("Button.-error", Button).disabled = not self.job_running
        self.app.mark(self.stage.name, mark)
        if not self.job_running:
            self.refresh_preview()


class TrainingApp(App):
    TITLE = "Tiki · training"
    BINDINGS = [("f5", "run", "Run"), ("f8", "stop", "Stop"), ("ctrl+q", "quit", "Quit (jobs keep running)")]
    CSS = """
    * { scrollbar-background: #161e31; scrollbar-background-hover: #161e31; scrollbar-background-active: #161e31;
        scrollbar-color: #33405d; scrollbar-color-hover: #404f71; scrollbar-color-active: #4f608a;
        scrollbar-corner-color: #161e31; }
    Screen { background: #0a0e1b; color: #e9e9e9; }
    Header { background: #192036; color: #bbc8e8; text-style: bold; }
    Footer { background: #192036; color: #c5c7d2; }
    Footer > .footer--key { background: #192036; color: #91abec; text-style: bold; }
    Footer > .footer--highlight { background: #22293e; color: #e9e9e9; }
    Footer > .footer--highlight-key { background: #22293e; color: #91abec; text-style: bold; }
    Footer .footer-key--key { background: #192036; color: #91abec; text-style: bold; }
    Footer .footer-key--description { background: #192036; color: #c5c7d2; }
    #stages { width: 20; background: #0f1525; border-right: tall #1b233a; padding: 1 0; }
    #stages > ListItem { background: transparent; color: #c5c7d2; padding: 0 1; }
    #stages > ListItem.--highlight, #stages > ListItem.-highlight { background: #22293e; }
    #stages > ListItem.--highlight Label, #stages > ListItem.-highlight Label { color: #bbc8e8; text-style: bold; }
    #stages:focus > ListItem.--highlight, #stages:focus > ListItem.-highlight { background: #35405f; }
    #stages:focus > ListItem.--highlight Label, #stages:focus > ListItem.-highlight Label { color: #cbdbfe; }
    ContentSwitcher { width: 1fr; }
    StagePage { padding: 1 2 0 2; }
    .description { color: #969aad; margin-bottom: 1; }
    .form { height: auto; max-height: 50%; }
    .row { height: auto; }
    .cell { height: auto; width: 1fr; }
    .label { width: 17; color: #c5c7d2; }
    Input { border: none; height: 1; padding: 0 1; width: 1fr; margin-right: 2; background: #131a2c;
            color: #91abec; }
    Input:focus { border: none; background: #22293e; color: #cbdbfe; }
    Input > .input--placeholder { color: #5e6b87; }
    Checkbox { border: none; height: 1; padding: 0; background: transparent; color: #c5c7d2; }
    Checkbox:focus { border: none; }
    Checkbox > .toggle--button { background: #343d56; color: #343d56; }
    Checkbox.-on > .toggle--button { background: #305240; color: #74e3b5; }
    Checkbox:focus > .toggle--label { color: #a5b8e7; text-style: bold; }
    Select { width: 1fr; height: 1; margin-right: 2; }
    Select SelectCurrent { border: none; height: 1; padding: 0 1; background: #131a2c; }
    Select SelectCurrent Static#label { color: #91abec; }
    Select:focus SelectCurrent { border: none; background: #22293e; }
    Select > OptionList { background: #111322; border: round #43548b; }
    .preview { margin: 1 0; height: auto; max-height: 12; border-left: outer #6171a6; padding-left: 1; }
    .controls { height: 3; }
    Button { background: #282c42; color: #c5c7d2; border-top: tall #54597b; border-bottom: tall #171922; }
    Button:hover, Button:focus { background: #383e5c; border-top: tall #69709a; }
    Button.-success { background: #192c5b; color: #cbdbfe; border-top: tall #425894; border-bottom: tall #151b2b; }
    Button.-success:hover, Button.-success:focus { background: #203875; border-top: tall #4a62a4; }
    Button.-error { background: #4b2233; color: #fb9a9a; border-top: tall #7d4459; border-bottom: tall #1f1118; }
    Button.-error:hover, Button.-error:focus { background: #632b42; border-top: tall #96546c; }
    Button:disabled { background: #161b2c; color: #4d5a85; border-top: tall #232a42; border-bottom: tall #10131f; }
    .status { width: 1fr; height: 3; padding: 0 2; content-align: left middle; }
    ProgressBar { width: 1fr; }
    ProgressBar Bar { width: 1fr; }
    ProgressBar Bar > .bar--bar { color: #91abec; background: #3a3f51; }
    ProgressBar Bar > .bar--complete { color: #54efae; background: #3a3f51; }
    ProgressBar PercentageStatus { color: #91abec; text-style: bold; }
    .summary { height: 1; margin-top: 1; }
    .charts { height: 16; }
    Chart, RichLog { background: #0f1525; border: tall #1b233a; border-title-color: #bbc8e8;
                     border-title-style: bold; padding: 0 1; }
    Chart { width: 1fr; height: 100%; }
    RichLog { height: 1fr; min-height: 4; color: #e9e9e9; }
    RichLog:focus { border: tall #43548b; }
    Toast { background: #20283e; }
    """

    def __init__(self):
        super().__init__()
        self.settings, self.jobs = pipeline.load_state()
        self.settle_timer = None
        if Theme is not None:                   # Textual with themes (0.86 on).
            self.register_theme(Theme(name="dolphie", **COLOURS))
            self.theme = "dolphie"
        else:
            self.design = dict(self.design, dark=ColorSystem(**COLOURS))
            self.stylesheet.set_variables(self.get_css_variables())

    def compose(self):
        yield Header()
        with Horizontal():
            yield ListView(*(ListItem(Label("  " + stage.title, id=f"mark-{stage.name}"), name=stage.name)
                             for stage in STAGES), id="stages")
            with ContentSwitcher(initial=f"page-{STAGES[0].name}"):
                for stage in STAGES:
                    yield StagePage(stage)
        yield Footer()

    def page(self):
        return self.query_one(f"#{self.query_one(ContentSwitcher).current}", StagePage)

    def on_list_view_highlighted(self, event):
        if event.item is not None:
            self.query_one(ContentSwitcher).current = f"page-{event.item.name}"
            self.page().refresh_preview()

    def mark(self, name, mark):
        title = next(stage.title for stage in STAGES if stage.name == name)
        self.query_one(f"#mark-{name}", Label).update(f"{mark} {title}")

    def set_value(self, key, value, source):
        """A setting changed on a page: show it on the others, then (once the typing pauses) show the
        page's commands again and save."""
        if self.settings.get(key) == value:
            return
        self.settings[key] = value
        for page in self.query(StagePage):
            if page is not source:
                page.show_value(key, value)
        if self.settle_timer is not None:
            self.settle_timer.stop()
        self.settle_timer = self.set_timer(0.4, self.settled)

    def settled(self):
        self.page().refresh_preview()
        self.save()

    def save(self):
        try:
            pipeline.save_state(self.settings, self.jobs)
        except OSError as error:
            self.notify(f"Couldn't save the settings: {error}", severity="warning")

    def action_run(self):
        self.page().run_job()

    def action_stop(self):
        self.page().stop_job()

    def on_unmount(self):
        self.save()


def main():
    if pipeline.WINDOWS and shutil.which("wsl.exe") is None:
        sys.exit("On Windows the training jobs run in WSL, which isn't installed (wsl --install).")
    TrainingApp().run()


if __name__ == "__main__":
    main()
