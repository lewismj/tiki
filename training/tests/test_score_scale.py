import os
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import score_scale
from tiki_nnue import binary


def synthetic(count, scale, rng):
    """Scores and results (0 / 1 / 2) where a win follows sigmoid(score / scale), no draws."""
    score = rng.integers(-1500, 1500, count)
    result = (rng.random(count) < 1 / (1 + np.exp(-score / scale))).astype(np.uint8) * 2
    return score, result


class ScoreScaleTest(unittest.TestCase):
    def test_recovers_the_scale(self):
        rng = np.random.default_rng(1)
        for scale in (150, 400, 900):
            score, result = synthetic(200_000, scale, rng)
            self.assertAlmostEqual(1.0, score_scale.fit_scale(score, result / 2.0) / scale, delta=0.05)

    def test_samples_in_proportion_to_file_size(self):
        rng = np.random.default_rng(2)
        with tempfile.TemporaryDirectory() as directory:
            for name, count, value in (("a.bin", 30_000, 1), ("b.bin", 10_000, 2)):
                records = np.zeros(count, dtype=binary.RECORD)
                records["score"] = value
                with open(os.path.join(directory, name), "wb") as file:
                    file.write(binary.MAGIC)
                    file.write(records.tobytes())
            open(os.path.join(directory, "c.bin.part"), "wb").close()
            records = score_scale.sample([directory], 4_000, rng)
            self.assertEqual(4_000, len(records))
            self.assertEqual(3_000, int(np.sum(records["score"] == 1)))


if __name__ == "__main__":
    unittest.main()
