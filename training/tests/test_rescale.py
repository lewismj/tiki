import os
import subprocess
import sys
import tempfile
import unittest

import numpy as np

TRAINING = os.path.join(os.path.dirname(__file__), "..")
sys.path.insert(0, TRAINING)

import rescale
from tiki_nnue import binary


def write_data(path, count, scale, seed):
    """A data file whose results follow sigmoid(score / scale), with varied pieces and sides."""
    rng = np.random.default_rng(seed)
    records = np.zeros(count, dtype=binary.RECORD)
    records["pieces"] = rng.integers(0, 768, (count, 32))
    records["score"] = rng.integers(-1500, 1500, count)
    records["result"] = (rng.random(count) < 1 / (1 + np.exp(-records["score"] / scale))).astype(np.uint8) * 2
    records["side"] = rng.integers(0, 2, count)
    with open(path, "wb") as file:
        file.write(binary.MAGIC)
        file.write(records.tobytes())
    return records


def read_data(path):
    with open(path, "rb") as file:
        assert file.read(len(binary.MAGIC)) == binary.MAGIC
        return np.frombuffer(file.read(), dtype=binary.RECORD)


def run(*args):
    return subprocess.run([sys.executable, os.path.join(TRAINING, "rescale.py"), *args, "--positions", "50000"],
                          capture_output=True, text=True, cwd=TRAINING)


class RescaleTest(unittest.TestCase):
    def test_scores_rounded_and_clamped(self):
        scores = np.array([0, 1, -1, 100, -100, 20000, -20000, 32767], dtype="<i2")
        np.testing.assert_array_equal([0, 1, -1, 117, -117, 23392, -23392, 32767],
                                      rescale.rescale_scores(scores, 400 / 342))
        np.testing.assert_array_equal([0, 1, -1, 93, -93, 18605, -18605, 30481],
                                      rescale.rescale_scores(scores, 400 / 430))
        self.assertEqual(-32767, int(rescale.rescale_scores(np.array([-32767], dtype="<i2"), 1.5)[0]))

    def test_rescales_each_file_to_400(self):
        """Files on different scales all fit about 400 afterwards; only the scores change."""
        with tempfile.TemporaryDirectory() as directory:
            source, target = os.path.join(directory, "in"), os.path.join(directory, "out")
            os.makedirs(source)
            originals = {}
            for name, scale, seed in (("a.bin", 340, 1), ("b.bin", 430, 2), ("c.bin", 400, 3)):
                originals[name] = write_data(os.path.join(source, name), 200_000, scale, seed)
            open(os.path.join(source, "d.bin.part"), "wb").close()    # A conversion still running.

            proc = run(source, target)
            self.assertEqual(0, proc.returncode, proc.stderr)
            self.assertEqual(["a.bin", "b.bin", "c.bin"], sorted(os.listdir(target)))
            for name, before in originals.items():
                after = read_data(os.path.join(target, name))
                np.testing.assert_array_equal(before["pieces"], after["pieces"])
                np.testing.assert_array_equal(before["result"], after["result"])
                np.testing.assert_array_equal(before["side"], after["side"])
                s = rescale.fitted_scale(os.path.join(target, name), 200_000, 1)
                self.assertAlmostEqual(1.0, s / 400, delta=0.04, msg=name)
                # Every score scaled by the same factor: the ratio is constant away from 0.
                big = np.abs(before["score"]) > 500
                ratios = after["score"][big] / before["score"][big]
                self.assertLess(ratios.max() - ratios.min(), 0.01, name)
            # The 340 file's scores grow, the 430 file's shrink.
            self.assertGreater(np.abs(read_data(os.path.join(target, "a.bin"))["score"]).mean(),
                               np.abs(originals["a.bin"]["score"]).mean() * 1.08)
            self.assertLess(np.abs(read_data(os.path.join(target, "b.bin"))["score"]).mean(),
                            np.abs(originals["b.bin"]["score"]).mean() * 0.98)

    def test_skips_existing_outputs_and_refuses_the_same_file(self):
        with tempfile.TemporaryDirectory() as directory:
            source = os.path.join(directory, "a.bin")
            target = os.path.join(directory, "b.bin")
            write_data(source, 20_000, 340, 1)
            self.assertEqual(0, run(source, target).returncode)
            written = os.path.getmtime(target)
            proc = run(source, target)
            self.assertEqual(0, proc.returncode)
            self.assertIn("skipped", proc.stdout)
            self.assertEqual(written, os.path.getmtime(target))

            self.assertNotEqual(0, run(source, source, "--force").returncode)
            self.assertEqual(20_000, binary.count(source))      # Untouched.

    def test_fit_only_writes_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            source = os.path.join(directory, "a.bin")
            write_data(source, 20_000, 340, 1)
            proc = run("--fit-only", source)
            self.assertEqual(0, proc.returncode, proc.stderr)
            self.assertEqual(["a.bin"], os.listdir(directory))


if __name__ == "__main__":
    unittest.main()
