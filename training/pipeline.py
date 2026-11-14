"""The training pipeline's stages as commands, and the jobs that run them. Used by tui.py.

A stage (download, convert, relabel, rescale, train, ...) turns the settings into a list of steps,
each one command. A job runs a stage's steps in order in a detached bash process, stopping at the
first that fails, with all output going to a log file. The interface follows the log, so a job
keeps running when the interface is closed, and is picked up again when it's reopened.

The jobs are Linux commands (the C tools, bash, PyTorch in WSL). On Linux, WSL and macOS they run
directly. On Windows the interface runs natively and each job runs in WSL, through wsl.exe: the
settings' paths are then WSL paths (/mnt/f/...; a Windows path such as F:\\lichess2 is converted),
and this module reads them through their Windows names.
"""

import json
import math
import os
import posixpath
import re
import shlex
import shutil
import signal
import subprocess
import sys
import time
from dataclasses import dataclass, field

HERE = os.path.dirname(os.path.abspath(__file__))
MARKER = "[tiki-tui]"
WINDOWS = os.name == "nt"
PYTHON = ["python3" if WINDOWS else sys.executable or "python3", "-u"]
HIDDEN = getattr(subprocess, "CREATE_NO_WINDOW", 0)     # Windows: no console window for a helper process.
join = posixpath.join                                    # The jobs' paths are Linux paths on every platform.


# ---- Paths: the jobs' (Linux) names and, on Windows, the names this process reaches the same files by ----

DRIVE_RE = re.compile(r"(?<![\w/\\.-])([A-Za-z]):[\\/]([^\s\"':]*)")


def to_linux(text):
    """Windows paths in 'text' as WSL sees them: F:\\lichess2\\data -> /mnt/f/lichess2/data."""
    return DRIVE_RE.sub(lambda match: f"/mnt/{match[1].lower()}/" + match[2].replace("\\", "/"), text)


def to_windows(path, distro):
    """A WSL path as Windows reaches it: /mnt/f/x -> F:\\x, and /home/x -> \\\\wsl.localhost\\<distro>\\home\\x."""
    if match := re.fullmatch(r"/mnt/([a-z])(/.*)?", path):
        return f"{match[1].upper()}:" + (match[2] or "/").replace("/", "\\")
    if path.startswith("/"):
        return f"\\\\wsl.localhost\\{distro}" + path.replace("/", "\\")
    return path


def path_to_linux(path):
    """One whole Windows path (spaces and all) as WSL sees it; to_linux is for text that may hold several."""
    if match := re.fullmatch(r"([A-Za-z]):[\\/](.*)", path):
        return f"/mnt/{match[1].lower()}/" + match[2].replace("\\", "/")
    return path


def wsl_distro(cache=[]):
    """The default WSL distribution's name. A failure to get it isn't remembered, and stops the stage
    (a path that can't be reached must not look like a path that doesn't exist)."""
    if not cache:
        try:
            name = subprocess.run(["wsl.exe", "-e", "sh", "-c", "echo $WSL_DISTRO_NAME"], capture_output=True,
                                  text=True, timeout=30, creationflags=HIDDEN).stdout.strip()
        except (OSError, subprocess.SubprocessError):
            name = ""
        if not name:
            raise PipelineError("WSL didn't answer (wsl.exe), so its files can't be reached")
        cache.append(name)
    return cache[0]


def linux_path(path):
    """Text of this process (a setting: a path, or several with options) as the jobs read it."""
    return to_linux(path) if WINDOWS else path


def local_path(path):
    """A path of the jobs as this process opens it."""
    if not WINDOWS or not path.startswith("/"):
        return path
    return to_windows(path, "" if path.startswith("/mnt/") else wsl_distro())


def exists(path):
    return os.path.exists(local_path(path))


class PipelineError(Exception):
    """Settings a stage can't run with; the message is shown instead of the commands."""


# ---- Settings ----

@dataclass(frozen=True)
class Field:
    key: str
    label: str
    default: object = ""
    kind: str = "text"          # text, bool or choice
    choices: tuple = ()
    help: str = ""


FIELDS = {f.key: f for f in (
    Field("months", "Months", "", help="YYYY-MM, or ranges YYYY-MM:YYYY-MM, e.g. 2026-11 or 2023-01:2026-10"),
    Field("download_dir", "Dumps", "/mnt/f/lichess2", help="where the .pgn.zst dumps are (about 30 GB a month)"),
    Field("converted_dir", "Converted", "/mnt/f/lichess2/data-2000-s100", help="lichess_convert's output"),
    Field("relabelled_dir", "Relabelled", "/mnt/f/lichess2/data-2000-s100-tb", help="syzygy_relabel's output"),
    Field("rescaled_dir", "Rescaled", "/mnt/f/lichess2/data-2000-s100-tb-s335",
          help="rescale.py's output, the data the trainer reads"),
    Field("tables", "Syzygy tables", "/mnt/f/syzygy/wdl", help="WDL tables (.rtbw); several directories with ':'"),

    Field("retries", "Retries", "10", help="retries of a failed download"),
    Field("reserve_gb", "Reserve GB", "10", help="free space to leave on the disk"),

    Field("min_elo", "Min Elo", "2000", help="skip games where either player is rated lower"),
    Field("sample", "Sample", "1.0", help="fraction of positions kept"),
    Field("skip_plies", "Skip plies", "16", help="opening plies dropped"),
    Field("max_score", "Max score", "3000", help="largest score kept, centipawns"),
    Field("convert_threads", "Threads", "", help="blank: the CPUs less one"),

    Field("relabel_threads", "Threads", "", help="blank: 4"),
    Field("relabel_force", "Rewrite existing", False, "bool"),

    Field("scale_to", "Scale to", "335", help="the s every file should fit (335 keeps the eval scale)"),
    Field("fit_only", "Fit only", False, "bool", help="show each file's fitted s, write nothing"),
    Field("rescale_force", "Rewrite existing", False, "bool"),

    Field("train_data", "Data", "/mnt/f/lichess2/data-2000-s100-tb-s335",
          help="data directories or files, separated by spaces"),
    Field("out_dir", "Output", "/mnt/f/tiki/nets/" + time.strftime("run-%Y-%m-%d"),
          help="checkpoints, networks and train.log"),
    Field("epochs", "Epochs", "80"),
    Field("epoch_positions", "Epoch positions", "100000000", help="0: a whole pass over the data"),
    Field("batch_size", "Batch size", "16384"),
    Field("hidden", "Hidden", "512", help="accumulator width per side (the engine's NNUE_HIDDEN)"),
    Field("buffer", "Buffer", "16000000", help="positions shuffled together (68 bytes each, two in memory)"),
    Field("lr", "Learning rate", "0.001"),
    Field("lr_schedule", "Schedule", "cosine", "choice", ("cosine", "step")),
    Field("lr_min", "LR min", "1e-05", help="cosine: the learning rate of the last epoch"),
    Field("lr_drop", "LR drop", "10", help="step: multiply the learning rate by 0.1 every N epochs"),
    Field("wdl", "WDL", "0.3", help="weight of the game result against the score"),
    Field("loss_power", "Loss power", "2", help="2 is the MSE; Stockfish uses 2.5"),
    Field("factoriser", "Factoriser", True, "bool"),
    Field("resume_latest", "Resume latest", False, "bool", help="continue from the last checkpoint in Output"),
    Field("resume", "Resume from", "", help="a checkpoint (.pt); blank: start a new run"),
    Field("train_extra", "Other options", "", help="passed to train_streaming.py as typed"),

    Field("tiki_app", "tiki binary", "", help="blank: skip the engine cross-check (TIKI_APP)"),
)}


def defaults():
    return {key: f.default for key, f in FIELDS.items()}


def text(settings, key):
    return linux_path(str(settings.get(key, FIELDS[key].default)).strip())


def required(settings, key):
    value = text(settings, key)
    if not value:
        raise PipelineError(f"{FIELDS[key].label}: needed")
    return value


def number(settings, key, kind=int, minimum=0, optional=False):
    """The setting as typed, once it's known to be a number of at least 'minimum'. None if blank and optional."""
    value = text(settings, key)
    if not value and optional:
        return None
    try:
        if not math.isfinite(kind(value)) or kind(value) < minimum:
            raise ValueError
    except (ValueError, OverflowError):
        what = "a whole number" if kind is int else "a number"
        raise PipelineError(f"{FIELDS[key].label}: {what} of at least {minimum} is needed") from None
    return value


def expand_months(spec):
    """'2025-11:2026-02 2026-06' -> ['2025-11', '2025-12', '2026-01', '2026-02', '2026-06']."""
    months = []
    for part in spec.replace(",", " ").split():
        first, _, last = part.partition(":")
        last = last or first
        for month in (first, last):
            if not re.fullmatch(r"\d{4}-(0[1-9]|1[0-2])", month):
                raise PipelineError(f"Months: '{part}' isn't YYYY-MM or YYYY-MM:YYYY-MM")
        year, month = (int(n) for n in first.split("-"))
        end = tuple(int(n) for n in last.split("-"))
        if end < (year, month):
            raise PipelineError(f"Months: '{part}' ends before it starts")
        while (year, month) <= end:
            name = f"{year:04d}-{month:02d}"
            if name not in months:
                months.append(name)
            year, month = (year + 1, 1) if month == 12 else (year, month + 1)
    return months


def months_of(settings, optional=False):
    months = expand_months(text(settings, "months"))
    if not months and not optional:
        raise PipelineError("Months: needed")
    return months


def dump_name(month):
    return f"lichess_db_standard_rated_{month}.pgn.zst"


def data_name(month):
    return f"lichess-{month}.bin"


# ---- Stages ----

@dataclass
class Step:
    argv: list
    skip_if: tuple = ()                         # Skipped, when it's reached, if any of these paths exists.
    env: dict = field(default_factory=dict)

    def command(self):
        return "".join(f"{name}={shlex.quote(value)} " for name, value in self.env.items()) + shlex.join(self.argv)


def tool_steps(name):
    """The C tool training/<name>/<name>: its command prefix, and a step that builds it if it isn't there."""
    path = join(name, name)
    build = [] if os.path.exists(os.path.join(HERE, name, name)) else [Step(["make", "-C", name])]
    line_buffered = ["stdbuf", "-oL"] if WINDOWS or shutil.which("stdbuf") else []
    return build, line_buffered + [path]


def download_steps(settings):
    months = months_of(settings)
    return [Step(PYTHON + ["lichess_download.py", "--download-dir", required(settings, "download_dir"),
                           "--months", *months, "--retries", number(settings, "retries"),
                           "--reserve-gb", number(settings, "reserve_gb", float)])]


def convert_steps(settings):
    months = months_of(settings)
    dumps, out = required(settings, "download_dir"), required(settings, "converted_dir")
    options = ["--min-elo", number(settings, "min_elo"), "--skip-plies", number(settings, "skip_plies"),
               "--max-score", number(settings, "max_score"), "--sample", number(settings, "sample", float)]
    threads = number(settings, "convert_threads", minimum=1, optional=True)
    if threads:
        options += ["--threads", threads]
    steps, tool = tool_steps("lichess_convert")
    steps.append(Step(["mkdir", "-p", out]))
    for month in months:
        target = join(out, data_name(month))
        steps.append(Step(tool + options + [join(dumps, dump_name(month)), target], skip_if=(target,)))
    return steps


def per_month(settings, source_key, target_key, command, force):
    """One step a month, from the month's file in one directory to the same name in another. Without
    months, one step over the whole directories (the tools skip the outputs that exist)."""
    source, target = required(settings, source_key), required(settings, target_key)
    if posixpath.normpath(source) == posixpath.normpath(target):
        raise PipelineError(f"{FIELDS[source_key].label} and {FIELDS[target_key].label} are the same directory")
    steps = [Step(["mkdir", "-p", target])]
    months = months_of(settings, optional=True)
    if not months:
        return steps + [Step(command + [source, target])]
    for month in months:
        out = join(target, data_name(month))
        steps.append(Step(command + [join(source, data_name(month)), out], skip_if=() if force else (out,)))
    return steps


def relabel_steps(settings):
    build, tool = tool_steps("syzygy_relabel")
    command = tool + ["--tb", required(settings, "tables")]
    threads = number(settings, "relabel_threads", minimum=1, optional=True)
    if threads:
        command += ["--threads", threads]
    force = bool(settings.get("relabel_force"))
    if force:
        command.append("--force")
    return build + per_month(settings, "converted_dir", "relabelled_dir", command, force)


def rescale_steps(settings):
    command = PYTHON + ["rescale.py"]
    if settings.get("fit_only"):
        source = required(settings, "relabelled_dir")
        months = months_of(settings, optional=True)
        sources = [join(source, data_name(month)) for month in months] or [source]
        return [Step(command + ["--fit-only", path]) for path in sources]
    command += ["--scale-to", number(settings, "scale_to", float, minimum=1)]
    force = bool(settings.get("rescale_force"))
    if force:
        command.append("--force")
    return per_month(settings, "relabelled_dir", "rescaled_dir", command, force)


def prepare_steps(settings):
    """Download, convert, relabel and rescale the months that aren't in the rescaled directory yet."""
    months = months_of(settings)
    final = required(settings, "rescaled_dir")
    todo = [month for month in months if not exists(join(final, data_name(month)))]
    if not todo:
        raise PipelineError(f"Nothing to do: every month is already in {final}")
    settings = dict(settings, relabel_force=False, rescale_force=False, fit_only=False)
    dumps, converted = required(settings, "download_dir"), required(settings, "converted_dir")
    fetch = [month for month in todo if not exists(join(converted, data_name(month)))
             and not exists(join(dumps, dump_name(month)))]
    steps = download_steps(dict(settings, months=" ".join(fetch))) if fetch else []
    settings["months"] = " ".join(todo)
    return steps + convert_steps(settings) + relabel_steps(settings) + rescale_steps(settings)


def checkpoints(directory):
    """The epochs with a checkpoint (tiki-<epoch>.pt) in the directory, in order: none if there is no such
    directory. A directory that can't be read stops the stage, as its checkpoints can't be ruled out."""
    try:
        names = os.listdir(local_path(directory))
    except (FileNotFoundError, NotADirectoryError):
        return []
    except OSError as error:
        raise PipelineError(f"Can't read {directory}: {error.strerror or error}") from None
    return sorted(int(m.group(1)) for m in (re.fullmatch(r"tiki-(\d+)\.pt", name) for name in names) if m)


def train_steps(settings):
    try:
        data = shlex.split(required(settings, "train_data"))
        extra = shlex.split(text(settings, "train_extra"))
    except ValueError as error:
        raise PipelineError(f"Unbalanced quotes: {error}") from None
    out = required(settings, "out_dir")
    resume = text(settings, "resume")
    saved = checkpoints(out)
    if not resume and settings.get("resume_latest") and saved:
        resume = join(out, f"tiki-{saved[-1]}.pt")
    if not resume and saved:
        raise PipelineError(f"{out} already has checkpoints (up to epoch {saved[-1]}), which a new run would "
                            "overwrite: resume, or choose another Output")
    argv = PYTHON + ["train_streaming.py", "--data", *data, "--out", out,
                     "--epochs", number(settings, "epochs", minimum=1),
                     "--epoch-positions", number(settings, "epoch_positions"),
                     "--batch-size", number(settings, "batch_size", minimum=1),
                     "--hidden", number(settings, "hidden", minimum=1),
                     "--buffer", number(settings, "buffer", minimum=1),
                     "--lr", number(settings, "lr", float), "--lr-schedule", text(settings, "lr_schedule")]
    if text(settings, "lr_schedule") == "cosine":
        argv += ["--lr-min", number(settings, "lr_min", float)]
    else:
        argv += ["--lr-drop", number(settings, "lr_drop", minimum=1)]
    argv += ["--wdl", number(settings, "wdl", float), "--loss-power", number(settings, "loss_power", float, minimum=1)]
    if settings.get("factoriser"):
        argv.append("--factoriser")
    if resume:
        argv += ["--resume", resume]
    return [Step(argv + extra)]


def test_steps(settings):
    app = text(settings, "tiki_app")
    return [Step(PYTHON + ["-m", "unittest", "discover", "-s", "tests"], env={"TIKI_APP": app} if app else {})]


@dataclass(frozen=True)
class Stage:
    name: str
    title: str
    description: str
    fields: tuple
    build: object


STAGES = (
    Stage("prepare", "Prepare data",
          "Download, convert, relabel and rescale the months that aren't in the rescaled directory yet, "
          "with the settings of those four stages.",
          ("months", "download_dir", "converted_dir", "relabelled_dir", "rescaled_dir", "tables"), prepare_steps),
    Stage("download", "Download",
          "Lichess's monthly dumps (lichess_download.py). Months already downloaded are skipped and a partial "
          "download resumes.",
          ("months", "download_dir", "retries", "reserve_gb"), download_steps),
    Stage("convert", "Convert",
          "Unpack each month's dump into training data (lichess_convert). Months already converted are skipped.",
          ("months", "download_dir", "converted_dir", "min_elo", "sample", "skip_plies", "max_score",
           "convert_threads"), convert_steps),
    Stage("relabel", "Relabel",
          "Replace the game result of positions the Syzygy tables cover (syzygy_relabel). Without months, "
          "every file of the directory that isn't relabelled yet.",
          ("months", "converted_dir", "relabelled_dir", "tables", "relabel_threads", "relabel_force"),
          relabel_steps),
    Stage("rescale", "Rescale",
          "Put every month's scores on the same scale (rescale.py). Without months, every file of the "
          "directory that isn't rescaled yet.",
          ("months", "relabelled_dir", "rescaled_dir", "scale_to", "fit_only", "rescale_force"), rescale_steps),
    Stage("train", "Train",
          "Train a network on the data (train_streaming.py): a checkpoint and a network after each epoch.",
          ("train_data", "out_dir", "epochs", "epoch_positions", "batch_size", "hidden", "buffer", "lr",
           "lr_schedule", "lr_min", "lr_drop", "wdl", "loss_power", "factoriser", "resume_latest", "resume",
           "train_extra"), train_steps),
    Stage("tests", "Tests", "The trainer's tests (python3 -m unittest discover -s tests).",
          ("tiki_app",), test_steps),
)


# ---- Jobs ----

def script(steps):
    """A bash script that runs the steps in training/, stops at the first that fails, and ends with the
    exit marker. It starts by giving its process id, which is its process group's (see start). It only notes INT and TERM itself (they reach the running step too, as the job is one
    process group), so the marker is still written after a stop."""
    lines = ["trap : INT TERM", f'echo "{MARKER} pid $$"', "export COLUMNS=140 PYTHONUNBUFFERED=1",
             f"cd {shlex.quote(path_to_linux(HERE) if WINDOWS else HERE)} || exit 1", "code=0"]
    for index, step in enumerate(steps, 1):
        command = step.command()
        body = f"{command} || code=$?"
        if step.skip_if:
            exists = " || ".join(f"[ -e {shlex.quote(path)} ]" for path in step.skip_if)
            body = f"if {exists}; then echo {shlex.quote(MARKER + ' skipped: the output exists')}; else {body}; fi"
        announce = f"echo {shlex.quote(f'{MARKER} step {index}/{len(steps)}: {command}')}"
        lines.append(f"if [ $code -eq 0 ]; then {announce}; {body}; fi")
    lines += [f'echo "{MARKER} exit $code"', "exit $code"]
    return "\n".join(lines) + "\n"


def start(stage, steps, log_dir):
    """Starts the steps as a detached job, in a session (and so a process group) of its own. Returns
    (the job's record, which is saved, and its process). Raises OSError if it can't be started.

    The script is run from a file beside the log (it can be longer than a command line may be). On
    Windows the job is a wsl.exe that outlives the interface; WSL keeps running while that wsl.exe does."""
    os.makedirs(log_dir, exist_ok=True)
    log = os.path.join(log_dir, f"{stage}-{time.strftime('%Y%m%d-%H%M%S')}.log")
    options = dict(stdin=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    with open(script_path(log), "w", encoding="utf-8", newline="\n") as file:
        file.write(script(steps))
    if WINDOWS:
        argv = ["wsl.exe", "-e", "setsid", "--wait", "bash", path_to_linux(script_path(log))]
        options["creationflags"] = HIDDEN | subprocess.CREATE_NEW_PROCESS_GROUP
    else:
        argv = ["bash", script_path(log)]
        options["start_new_session"] = True
    with open(log, "wb") as out:
        process = subprocess.Popen(argv, stdout=out, **options)
    return {"pid": process.pid, "log": log, "started": time.time()}, process


def script_path(log):
    return log[:-4] + ".sh"


def alive(job, process=None):
    """Whether the job's bash process is still running. Without the process (a job from an earlier
    session), the pid must still be running the job's script, not be a later process with the same
    number: from /proc, or from ps where there is no /proc (macOS)."""
    if process is not None:
        return process.poll() is None
    if WINDOWS:
        try:
            listed = subprocess.run(["tasklist", "/FI", f"PID eq {job['pid']}", "/FO", "CSV", "/NH"],
                                    capture_output=True, text=True, timeout=30, creationflags=HIDDEN).stdout
        except (OSError, subprocess.SubprocessError):
            return False
        return "wsl.exe" in listed.lower()
    name = os.path.basename(script_path(job.get("log", "")))
    if not job.get("log"):
        return False
    try:
        with open(f"/proc/{job['pid']}/cmdline", "rb") as file:
            return name.encode() in file.read()
    except OSError:
        if os.path.isdir("/proc"):
            return False
    try:
        command = subprocess.run(["ps", "-p", str(job["pid"]), "-o", "command="], capture_output=True, text=True,
                                 timeout=30).stdout
    except (OSError, subprocess.SubprocessError):
        return False
    return name in command


STOP_SIGNALS = ("INT", "TERM", "KILL")


def stop(job, attempt):
    """Signals the job's process group: interrupt first, then terminate, then kill. Returns the signal's
    name, or None if the job hasn't given its process id yet (Windows, in its first moments)."""
    name = STOP_SIGNALS[min(attempt, len(STOP_SIGNALS) - 1)]
    group = job.get("linux_pid") if WINDOWS else job["pid"]
    if group is None:
        return None
    try:
        if WINDOWS:
            subprocess.run(["wsl.exe", "-e", "kill", "-s", name, "--", f"-{group}"], capture_output=True,
                           timeout=30, creationflags=HIDDEN)
        else:
            os.killpg(group, getattr(signal, "SIG" + name))
    except (OSError, subprocess.SubprocessError):
        pass
    return "SIG" + name


class LogTail:
    """Reads the lines added to a file since the last read. A carriage return ends a line too."""

    def __init__(self, path, offset=0):
        self.path, self.offset, self.partial = path, offset, b""

    def read(self):
        try:
            with open(self.path, "rb") as file:
                file.seek(self.offset)
                data = file.read()
        except OSError:
            return []
        self.offset += len(data)
        *lines, self.partial = re.split(rb"\r\n|\n|\r", self.partial + data)
        return [line.decode("utf-8", "replace") for line in lines]


def tail_from(path, limit=200_000):
    """A LogTail of the file's last 'limit' bytes, starting at a whole line."""
    try:
        size = os.path.getsize(path)
    except OSError:
        size = 0
    tail = LogTail(path, max(0, size - limit))
    if tail.offset:
        tail.read()
        tail.partial = b""
    return tail


def replay_markers(path, end, progress):
    """Gives 'progress' the job's own lines in the first 'end' bytes of its log: the part tail_from
    leaves out, so a job picked up again still knows which step it's on."""
    try:
        with open(path, "rb") as file:
            for line in file.read(end).split(b"\n"):
                if line.startswith(MARKER.encode()):
                    progress.line(line.decode("utf-8", "replace").rstrip("\r"))
    except OSError:
        pass


STEP_RE = re.compile(re.escape(MARKER) + r" step (\d+)/(\d+): (.*)")
EXIT_RE = re.compile(re.escape(MARKER) + r" exit (\d+)")
PID_RE = re.compile(re.escape(MARKER) + r" pid (\d+)")
PERCENT_RE = re.compile(r"(\d+(?:\.\d+)?)%")
BATCH_RE = re.compile(r"epoch (\d+) train: batch (\d+)/(\d+), (.*)")
EPOCH_RE = re.compile(r"epoch (\d+): train ")


@dataclass
class Progress:
    """What a job's output says about how far it is."""
    step: int = 0
    steps: int = 0
    command: str = ""
    percent: float = 0.0        # Of the current step.
    exit_code: object = None    # Set by the exit marker.
    pid: object = None          # The script's process id (its process group), from its first line.

    def line(self, line):
        """Takes a line of the job's log. Returns whether it's one of the job's own (marker) lines."""
        if match := STEP_RE.match(line):
            self.step, self.steps, self.command, self.percent = int(match[1]), int(match[2]), match[3], 0.0
        elif match := EXIT_RE.match(line):
            self.exit_code = int(match[1])
        elif match := PID_RE.match(line):
            self.pid = int(match[1])
        elif line.startswith(MARKER):
            pass
        else:
            if match := PERCENT_RE.search(line):
                self.percent = min(100.0, float(match[1]))
            return False
        return True

    def fraction(self):
        if self.exit_code == 0:
            return 1.0
        return (max(self.step - 1, 0) + self.percent / 100) / self.steps if self.steps else 0.0


def training_fraction(line, epochs):
    """How far a training run is (0..1) from a line of train.log, None if the line doesn't say."""
    if match := BATCH_RE.search(line):
        return min(1.0, (int(match[1]) - 1 + int(match[2]) / int(match[3])) / epochs)
    if match := EPOCH_RE.search(line):
        return min(1.0, int(match[1]) / epochs)
    return None


RESULT_RE = re.compile(r"epoch (\d+): train ([\d.]+), validation ([\d.]+)")
RESUMED_RE = re.compile(r"resumed from .* at epoch (\d+)")
EPOCHS_RE = re.compile(r" start.*'epochs': (\d+)")


def training_history(path):
    """([(epoch, training loss, validation loss), ...], the epochs the run was started for or None) from a
    train.log. The log is appended to, so a new run in the same directory (its epoch 1 starting) drops what
    came before, and a resumed one drops the epochs it trains again."""
    results, epochs = {}, None
    try:
        with open(path, encoding="utf-8", errors="replace") as file:
            for line in file:
                if match := RESULT_RE.search(line):
                    results[int(match[1])] = (float(match[2]), float(match[3]))
                elif " epoch 1: started" in line:
                    results.clear()
                elif match := RESUMED_RE.search(line):
                    results = {epoch: losses for epoch, losses in results.items() if epoch < int(match[1])}
                elif match := EPOCHS_RE.search(line):
                    epochs = int(match[1])
    except OSError:
        pass
    return [(epoch, *results[epoch]) for epoch in sorted(results)], epochs


# ---- Saved state: the settings and the jobs, so the interface can be closed and reopened ----

def state_dir():
    return os.environ.get("TIKI_TUI_DIR") or os.path.join(os.path.expanduser("~"), ".config", "tiki-training-tui")


def load_state():
    """(settings, jobs by stage name). Missing or unreadable state is the defaults and no jobs."""
    settings, jobs = defaults(), {}
    try:
        with open(os.path.join(state_dir(), "state.json"), encoding="utf-8") as file:
            saved = json.load(file)
        settings.update({key: value for key, value in saved.get("settings", {}).items()
                         if key in FIELDS and isinstance(value, type(FIELDS[key].default))})
        jobs = {name: job for name, job in saved.get("jobs", {}).items() if isinstance(job, dict) and "log" in job}
    except (OSError, ValueError, AttributeError):
        pass
    return settings, jobs


def save_state(settings, jobs):
    os.makedirs(state_dir(), exist_ok=True)
    path = os.path.join(state_dir(), "state.json")
    with open(path + ".part", "w", encoding="utf-8") as file:
        json.dump({"settings": settings, "jobs": jobs}, file, indent=1)
    os.replace(path + ".part", path)
