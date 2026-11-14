"""Tests for pipeline.py (the stages' commands and the jobs tui.py runs) and a headless run of tui.py."""

import os
import shutil
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

import pipeline  # noqa: E402
from pipeline import PipelineError, Step  # noqa: E402

# Jobs are bash scripts: run directly, or in WSL on Windows.
RUNS = shutil.which("wsl.exe" if pipeline.WINDOWS else "bash") is not None
L = pipeline.linux_path


def shown(widget):
    """The text a Static shows (the attribute's name depends on the Textual version)."""
    return str(widget.renderable if hasattr(widget, "renderable") else widget.content)


def touch(*parts):
    path = os.path.join(*parts)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "wb").close()
    return path


class Directories(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.root, ignore_errors=True)
        self.settings = pipeline.defaults()
        for key in ("download_dir", "converted_dir", "relabelled_dir", "rescaled_dir", "tables", "out_dir"):
            self.settings[key] = os.path.join(self.root, key)
        self.settings["train_data"] = self.settings["rescaled_dir"]
        self.settings["months"] = "2026-01:2026-03"

    def path(self, key, name):
        """A file of one of the settings' directories, as the jobs name it."""
        return pipeline.join(L(self.settings[key]), name)

    def touch(self, key, name):
        return touch(pipeline.local_path(self.path(key, name)))


class Months(unittest.TestCase):
    def test_ranges_and_single_months(self):
        self.assertEqual(["2025-11", "2025-12", "2026-01", "2026-02", "2026-06"],
                         pipeline.expand_months("2025-11:2026-02, 2026-06 2026-01"))
        self.assertEqual([], pipeline.expand_months("  "))

    def test_rejects_bad_months(self):
        for spec in ("2026-13", "2026-1", "2026-03:2026-01", "2026", "2026-01:"):
            with self.assertRaises(PipelineError, msg=spec):
                pipeline.expand_months(spec + (":x" if spec.endswith(":") else ""))


class Stages(Directories):
    def test_download(self):
        argv = pipeline.download_steps(self.settings)[0].argv
        self.assertEqual(["lichess_download.py", "--download-dir", L(self.settings["download_dir"]), "--months",
                          "2026-01", "2026-02", "2026-03", "--retries", "10", "--reserve-gb", "10"], argv[2:])

    def test_months_and_numbers_are_checked(self):
        with self.assertRaises(PipelineError):
            pipeline.download_steps(dict(self.settings, months=""))
        with self.assertRaises(PipelineError):
            pipeline.download_steps(dict(self.settings, retries="ten"))
        with self.assertRaises(PipelineError):
            pipeline.convert_steps(dict(self.settings, convert_threads="0"))
        for value in ("nan", "inf", "-1"):
            with self.assertRaises(PipelineError, msg=value):
                pipeline.convert_steps(dict(self.settings, sample=value))

    def test_convert_is_a_step_a_month_skipped_if_converted(self):
        steps = [step for step in pipeline.convert_steps(self.settings) if "--min-elo" in step.argv]
        self.assertEqual(3, len(steps))
        target = self.path("converted_dir", "lichess-2026-02.bin")
        self.assertEqual([self.path("download_dir", "lichess_db_standard_rated_2026-02.pgn.zst"), target],
                         steps[1].argv[-2:])
        self.assertEqual((target,), steps[1].skip_if)
        self.assertIn("2000", steps[1].argv)
        self.assertNotIn("--threads", steps[1].argv)

    def test_relabel_and_rescale_by_month_or_whole_directory(self):
        steps = pipeline.relabel_steps(self.settings)
        self.assertEqual([self.path("converted_dir", "lichess-2026-03.bin"),
                          self.path("relabelled_dir", "lichess-2026-03.bin")], steps[-1].argv[-2:])
        self.assertTrue(steps[-1].skip_if)
        self.assertFalse(pipeline.relabel_steps(dict(self.settings, relabel_force=True))[-1].skip_if)

        whole = pipeline.rescale_steps(dict(self.settings, months=""))
        self.assertEqual(["rescale.py", "--scale-to", "335", L(self.settings["relabelled_dir"]),
                          L(self.settings["rescaled_dir"])], whole[-1].argv[2:])
        fit = pipeline.rescale_steps(dict(self.settings, months="", fit_only=True))
        self.assertEqual([["rescale.py", "--fit-only", L(self.settings["relabelled_dir"])]],
                         [s.argv[2:] for s in fit])
        with self.assertRaises(PipelineError):
            pipeline.rescale_steps(dict(self.settings, rescaled_dir=self.settings["relabelled_dir"]))

    def test_prepare_only_does_what_is_missing(self):
        self.touch("rescaled_dir", "lichess-2026-01.bin")                               # Done.
        self.touch("download_dir", "lichess_db_standard_rated_2026-02.pgn.zst")         # Downloaded.
        commands = [step.command() for step in pipeline.prepare_steps(self.settings)]
        self.assertFalse([c for c in commands if "2026-01" in c])
        download = [c for c in commands if "lichess_download.py" in c]
        self.assertEqual(1, len(download))
        self.assertIn("--months 2026-03 --retries", download[0])
        self.assertEqual(2, len([c for c in commands if "rescale.py" in c]))

        for month in ("2026-02", "2026-03"):
            self.touch("rescaled_dir", f"lichess-{month}.bin")
        with self.assertRaises(PipelineError):
            pipeline.prepare_steps(self.settings)

    def test_train(self):
        argv = pipeline.train_steps(dict(self.settings, train_extra="--validation-max 0"))[0].argv
        self.assertEqual("train_streaming.py", argv[2])
        self.assertIn("--factoriser", argv)
        self.assertIn("--lr-min", argv)
        self.assertNotIn("--lr-drop", argv)
        self.assertNotIn("--resume", argv)
        self.assertEqual(["--validation-max", "0"], argv[-2:])
        step = pipeline.train_steps(dict(self.settings, lr_schedule="step", factoriser=False))[0].argv
        self.assertIn("--lr-drop", step)
        self.assertNotIn("--factoriser", step)

    def test_train_never_overwrites_a_run(self):
        for epoch in (2, 10, 9):
            self.touch("out_dir", f"tiki-{epoch}.pt")
        with self.assertRaises(PipelineError):
            pipeline.train_steps(self.settings)
        argv = pipeline.train_steps(dict(self.settings, resume_latest=True))[0].argv
        self.assertEqual(self.path("out_dir", "tiki-10.pt"), argv[argv.index("--resume") + 1])
        argv = pipeline.train_steps(dict(self.settings, resume="/x/tiki-3.pt"))[0].argv
        self.assertEqual("/x/tiki-3.pt", argv[argv.index("--resume") + 1])

    def test_every_stage_builds_from_the_defaults_with_months(self):
        for stage in pipeline.STAGES:
            self.assertTrue(stage.build(self.settings), stage.name)
            self.assertTrue(set(stage.fields) <= set(pipeline.FIELDS), stage.name)


class Paths(unittest.TestCase):
    def test_windows_paths_become_wsl_paths(self):
        self.assertEqual("/mnt/f/lichess2/data", pipeline.to_linux("F:\\lichess2\\data"))
        self.assertEqual("/mnt/f/a /mnt/c/b/c --x 1", pipeline.to_linux("F:\\a C:/b/c --x 1"))
        self.assertEqual("/mnt/f/a:/mnt/g/b", pipeline.to_linux("F:\\a:G:\\b"))
        for unchanged in ("/mnt/f/a:/mnt/f/b", "2026-01:2026-02", "/home/x/data", "1e-05", "--lr 0.001"):
            self.assertEqual(unchanged, pipeline.to_linux(unchanged))

    def test_a_whole_path_keeps_its_spaces(self):
        self.assertEqual("/mnt/c/Users/John Smith/.config/x.sh",
                         pipeline.path_to_linux("C:\\Users\\John Smith\\.config\\x.sh"))
        self.assertEqual("/home/a b", pipeline.path_to_linux("/home/a b"))

    def test_wsl_paths_as_windows_reaches_them(self):
        self.assertEqual("F:\\x\\y", pipeline.to_windows("/mnt/f/x/y", "Ubuntu"))
        self.assertEqual("C:\\", pipeline.to_windows("/mnt/c", "Ubuntu"))
        self.assertEqual("\\\\wsl.localhost\\Ubuntu\\home\\x", pipeline.to_windows("/home/x", "Ubuntu"))
        self.assertEqual("relative/x", pipeline.to_windows("relative/x", "Ubuntu"))


class Output(unittest.TestCase):
    def test_progress(self):
        progress = pipeline.Progress()
        self.assertTrue(progress.line("[tiki-tui] pid 4321"))
        self.assertEqual(4321, progress.pid)
        self.assertTrue(progress.line("[tiki-tui] step 2/4: lichess_convert a b"))
        self.assertFalse(progress.line(" 50.0%      12 s     80.1 MB/s compressed"))
        self.assertEqual((2, 4, 50.0), (progress.step, progress.steps, progress.percent))
        self.assertAlmostEqual(0.375, progress.fraction())
        self.assertTrue(progress.line("[tiki-tui] step 3/4: x"))
        self.assertEqual(0.0, progress.percent)
        self.assertFalse(progress.line("nothing to see"))
        self.assertIsNone(progress.exit_code)
        self.assertTrue(progress.line("[tiki-tui] exit 0"))
        self.assertEqual((0, 1.0), (progress.exit_code, progress.fraction()))

    def test_training_fraction(self):
        line = "2026-11-13 21:00:00 epoch 3 train: batch 3052/6104, loss 0.021500, 420,000 positions/s"
        self.assertAlmostEqual(0.25, pipeline.training_fraction(line, 10))
        done = "2026-11-13 21:00:00 epoch 3: train 0.0215, validation 0.0213, lr 0.001, 240s -> nets/tiki-3.nnue"
        self.assertAlmostEqual(0.3, pipeline.training_fraction(done, 10))
        self.assertIsNone(pipeline.training_fraction("2026-11-13 21:00:00 epoch 3: started, lr 0.001", 10))

    def test_training_history(self):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "train.log")
            self.assertEqual(([], None), pipeline.training_history(path))

            def result(epoch, train, validation, extra=""):
                return (f"2026-11-13 21:00:00 epoch {epoch}: train {train}, validation {validation}{extra}, "
                        f"lr 0.001, 200s -> nets/tiki-{epoch}.nnue\n")

            with open(path, "w") as file:
                file.write("2026-11-13 20:00:00 start (streaming): {'data': ['a.bin'], 'epochs': 5, 'lr': 0.001}\n"
                           "2026-11-13 20:00:01 epoch 1: started, lr 0.001\n"
                           "2026-11-13 20:01:00 epoch 1 train: batch 610/6104, loss 0.030000, 400,000 positions/s\n"
                           + result(1, "0.030000", "0.029000") + result(2, "0.025000", "0.026000"))
            self.assertEqual(([(1, 0.03, 0.029), (2, 0.025, 0.026)], 5), pipeline.training_history(path))

            with open(path, "a") as file:       # Resumed from epoch 1 for more epochs, with another loss power.
                file.write("2026-11-13 22:00:00 start (streaming): {'epochs': 8, 'loss_power': 2.5}\n"
                           "2026-11-13 22:00:01 resumed from nets/tiki-1.pt at epoch 2\n"
                           + result(2, "0.024000", "0.025500", " (MSE 0.021000)"))
            self.assertEqual(([(1, 0.03, 0.029), (2, 0.024, 0.0255)], 8), pipeline.training_history(path))

            with open(path, "a") as file:       # A new run in the same directory.
                file.write("2026-11-13 23:00:00 start (streaming): {'epochs': 3}\n"
                           "2026-11-13 23:00:01 epoch 1: started, lr 0.001\n" + result(1, "0.031000", "0.030000"))
            self.assertEqual(([(1, 0.031, 0.03)], 3), pipeline.training_history(path))

    def test_chart(self):
        from chart import Series, chart

        series = [Series("training", "red", [1, 2, 3, 4], [0.03, 0.025, 0.023, 0.022]),
                  Series("validation", "green", [1, 2, 3, 4], [0.031, 0.027, 0.025, 0.0245])]
        lines = chart(series, 60, 12, y_format="{:.4f}", x_range=(1, 20)).plain.split("\n")
        self.assertEqual(12, len(lines))
        self.assertTrue(all(len(line) <= 60 for line in lines))
        self.assertIn("training", lines[0])
        self.assertIn("validation", lines[0])
        self.assertTrue(lines[1].lstrip().startswith("0.03"))               # The largest value is at the top.
        self.assertEqual(["5", "10", "15", "20"], lines[-1].split())
        drawn = [index for index, line in enumerate(lines) if any(0x2800 < ord(c) <= 0x28FF for c in line)]
        self.assertTrue(len(drawn) >= 5)
        # With the x axis fixed to 20 epochs, 4 epochs take the left of the chart only.
        self.assertTrue(all(not any(0x2800 < ord(c) <= 0x28FF for c in line[30:]) for line in lines))

        self.assertEqual("No data yet", chart([], 60, 12).plain)
        self.assertEqual("", chart(series, 10, 3).plain)                    # Too small to draw.
        one = chart([Series("one", "red", [1], [0.5])], 40, 8).plain        # A single point, a flat range.
        self.assertTrue(any(0x2800 < ord(c) <= 0x28FF for c in one))

    def test_markers_before_the_tail(self):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "log")
            with open(path, "wb") as file:
                file.write(b"[tiki-tui] pid 77\n[tiki-tui] step 1/3: a\nx\n[tiki-tui] step 2/3: lichess_download.py\n"
                           + b"50.0% of it\n" * 100)
            tail, progress = pipeline.tail_from(path, limit=200), pipeline.Progress()
            pipeline.replay_markers(path, tail.offset, progress)
            self.assertEqual((77, 2, 3, "lichess_download.py"),
                             (progress.pid, progress.step, progress.steps, progress.command))
            self.assertEqual(0.0, progress.percent)

    def test_log_tail(self):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "log")
            tail = pipeline.LogTail(path)
            self.assertEqual([], tail.read())
            with open(path, "wb") as file:
                file.write(b"one\ntw")
                file.flush()
                self.assertEqual(["one"], tail.read())
                file.write(b"o\r10%\r20%\r\nlast")
                file.flush()
                self.assertEqual(["two", "10%", "20%"], tail.read())
            self.assertEqual([], tail.read())
            self.assertEqual([], pipeline.tail_from(path, limit=3).read())      # Starts at a whole line.

    def test_state_round_trip(self):
        with tempfile.TemporaryDirectory() as directory:
            os.environ["TIKI_TUI_DIR"] = directory
            self.addCleanup(os.environ.pop, "TIKI_TUI_DIR")
            self.assertEqual((pipeline.defaults(), {}), pipeline.load_state())
            settings = dict(pipeline.defaults(), months="2026-11", factoriser=False, old_setting="x")
            pipeline.save_state(settings, {"train": {"pid": 1, "log": "/x.log", "started": 1.0}})
            loaded, jobs = pipeline.load_state()
            self.assertEqual(("2026-11", False), (loaded["months"], loaded["factoriser"]))
            self.assertNotIn("old_setting", loaded)
            self.assertEqual("/x.log", jobs["train"]["log"])


@unittest.skipUnless(RUNS, "needs bash (WSL on Windows)")
class Jobs(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.directory, ignore_errors=True)

    def run_job(self, steps, stop_after=None):
        job, process = pipeline.start("test", steps, self.directory)
        tail, progress, lines = pipeline.LogTail(job["log"]), pipeline.Progress(), []
        deadline = time.time() + 20
        stops = 0
        while time.time() < deadline:
            was_alive = pipeline.alive(job, process)
            for line in tail.read():
                progress.line(line)
                lines.append(line)
            job["linux_pid"] = progress.pid
            if progress.exit_code is not None or not was_alive:
                break
            if stop_after is not None and any(stop_after in line for line in lines) and stops == 0:
                pipeline.stop(job, stops)
                stops += 1
            time.sleep(0.05)
        process.wait(timeout=10)
        return progress, lines

    def test_steps_run_in_order_in_training(self):
        progress, lines = self.run_job([Step(["echo", "a b"]), Step(["pwd"]), Step(["sh", "-c", "echo $X"],
                                                                                 env={"X": "from env"})])
        self.assertEqual(0, progress.exit_code)
        self.assertRegex(lines[0], r"^\[tiki-tui\] pid \d+$")
        self.assertEqual(["[tiki-tui] step 1/3: echo 'a b'", "a b", "[tiki-tui] step 2/3: pwd", L(pipeline.HERE)],
                         lines[1:5])
        self.assertIn("from env", lines)

    def test_a_failure_stops_the_rest(self):
        progress, lines = self.run_job([Step(["sh", "-c", "echo oops >&2; exit 3"]), Step(["echo", "never"])])
        self.assertEqual(3, progress.exit_code)
        self.assertIn("oops", lines)
        self.assertNotIn("never", lines)

    def test_skips_a_step_whose_output_exists(self):
        existing = L(touch(self.directory, "out.bin"))
        progress, lines = self.run_job([Step(["echo", "skipped step"], skip_if=(existing,)),
                                        Step(["echo", "ran"], skip_if=(existing + ".missing",))])
        self.assertEqual(0, progress.exit_code)
        self.assertNotIn("skipped step", lines)
        self.assertIn("[tiki-tui] skipped: the output exists", lines)
        self.assertIn("ran", lines)

    def test_stop_interrupts_the_step_and_still_reports(self):
        progress, lines = self.run_job([Step(["sh", "-c", "echo started; sleep 30"]), Step(["echo", "never"])],
                                       stop_after="started")
        self.assertEqual(130, progress.exit_code)
        self.assertNotIn("never", lines)

    def test_a_script_longer_than_a_command_line(self):
        steps = [Step(["true", "x" * 1000]) for _ in range(300)] + [Step(["echo", "end"])]
        self.assertGreater(len(pipeline.script(steps)), 500_000)
        progress, lines = self.run_job(steps)
        self.assertEqual(0, progress.exit_code)
        self.assertIn("end", lines)

    def test_alive_without_the_process(self):
        job, process = pipeline.start("test", [Step(["sleep", "30"])], self.directory)
        self.assertTrue(pipeline.alive(job))
        self.assertFalse(pipeline.alive({"pid": os.getpid()}))      # A process, but not a job.
        tail, progress = pipeline.LogTail(job["log"]), pipeline.Progress()
        for _ in range(200):
            for line in tail.read():
                progress.line(line)
            if progress.pid is not None:
                break
            time.sleep(0.05)
        job["linux_pid"] = progress.pid
        self.assertEqual("SIGKILL", pipeline.stop(job, 2))
        process.wait(timeout=10)
        self.assertFalse(pipeline.alive(job))


try:
    import textual  # noqa: F401
except ImportError:
    textual = None


@unittest.skipUnless(RUNS and textual is not None, "needs bash (WSL on Windows) and textual")
class Interface(unittest.TestCase):
    def test_settings_are_shared_and_a_job_runs(self):
        import asyncio

        asyncio.run(self.settings_are_shared_and_a_job_runs())

    async def settings_are_shared_and_a_job_runs(self):
        import tui
        from textual.widgets import Button, ContentSwitcher, Label, RichLog, Static

        directory = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, directory, ignore_errors=True)
        os.environ["TIKI_TUI_DIR"] = directory
        self.addCleanup(os.environ.pop, "TIKI_TUI_DIR")
        ran = os.path.join(directory, "ran")
        stage = pipeline.Stage("tests", "Tests", "", ("tiki_app", "months"),
                               lambda settings: [Step(["sh", "-c", f"echo 40%; echo hello > '{L(ran)}'"])])
        stages = tuple(s for s in pipeline.STAGES if s.name != "tests") + (stage,)
        tui.STAGES, saved = stages, tui.STAGES
        self.addCleanup(setattr, tui, "STAGES", saved)

        app = tui.TrainingApp()
        async with app.run_test(size=(140, 50)) as pilot:
            download = app.query_one("#page-download", tui.StagePage)
            self.assertTrue(download.query_one("Button.-success", Button).disabled)     # No months yet.
            download.inputs["months"].value = "2026-01:2026-02"
            await pilot.pause(0.6)
            self.assertEqual("2026-01:2026-02", app.query_one("#page-convert", tui.StagePage).inputs["months"].value)
            self.assertEqual("2026-01:2026-02", pipeline.load_state()[0]["months"])

            app.query_one("#stages").index = len(stages) - 1
            await pilot.pause(0.2)
            self.assertEqual("page-tests", app.query_one(ContentSwitcher).current)
            page = app.page()
            self.assertIn("echo 40%", shown(page.query_one(".preview", Static)))
            await pilot.press("f5")
            for _ in range(300):
                await pilot.pause(0.1)
                if not page.job_running:
                    break
            self.assertEqual(0, page.job["exit"])
            self.assertTrue(os.path.exists(ran))
            self.assertIn("Finished", shown(page.query_one(".status", Static)))
            self.assertIn("✓", shown(app.query_one("#mark-tests", Label)))
            self.assertTrue(any("40%" in strip.text for strip in page.query_one(RichLog).lines))
            self.assertEqual(0, pipeline.load_state()[1]["tests"]["exit"])

            # The training graphs follow the train.log of Train's Output directory.
            with open(os.path.join(directory, "train.log"), "w") as file:
                file.write("2026-11-13 20:00:00 start (streaming): {'epochs': 4}\n"
                           "2026-11-13 20:00:01 epoch 1: started, lr 0.001\n")
                for epoch, validation in ((1, "0.030000"), (2, "0.027000")):
                    file.write(f"2026-11-13 21:00:00 epoch {epoch}: train 0.025000, validation {validation}, "
                               f"lr 0.001, 200s -> tiki-{epoch}.nnue\n")
            train = app.query_one("#page-train", tui.StagePage)
            train.inputs["out_dir"].value = directory
            app.query_one("#stages").index = [stage.name for stage in stages].index("train")
            await pilot.pause(0.8)
            summary = shown(train.query_one(".summary", Static))
            self.assertIn("Epoch 2/4", summary)
            self.assertIn("Improvement 10.00%", summary)
            self.assertIn("validation", shown(train.query_one("#chart-loss", Static)))
            self.assertIn("epoch 1", train.query_one("#chart-gain", Static).border_title)


if __name__ == "__main__":
    unittest.main()
