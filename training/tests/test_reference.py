import os
import sys
import tempfile
import unittest

import numpy as np
import torch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from tiki_nnue import export
from tiki_nnue.data import batch_tensors
from tiki_nnue.model import SCALE, TikiNNUE
from tiki_nnue.reference import BANDS, Reference, measures, read_reference, spearman

HERE = os.path.dirname(os.path.abspath(__file__))
BUNDLED = os.path.join(HERE, "..", "reference", "stockfish-1m.txt")

POSITIONS = [
    ("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3", 30, "a"),
    ("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 2 3", 30, "a"),
    ("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1", 120, "b"),
    ("4k3/4p3/8/8/8/8/8/4K3 b - - 0 1", -120, "b"),
    ("4k3/8/8/8/8/8/3QP3/4K3 w - - 0 1", 900, "b"),
]


def write_reference(path, positions):
    with open(path, "w") as file:
        file.write("# comment\n\n")
        for fen, score, name in positions:
            file.write(f"{fen} | {score} | {name}\n")


def small_model(seed):
    torch.manual_seed(seed)
    model = TikiNNUE(hidden=64)
    with torch.no_grad():
        for parameter in model.parameters():
            parameter.normal_(0, 0.05)
    model.clip_weights()
    return model


class ReferenceTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.directory.name, "reference.txt")
        write_reference(self.path, POSITIONS)

    def tearDown(self):
        self.directory.cleanup()

    def test_read(self):
        fens, scores, sets = read_reference(self.path)
        self.assertEqual(fens, [fen for fen, _, _ in POSITIONS])
        self.assertEqual(list(scores), [score for _, score, _ in POSITIONS])
        self.assertEqual(list(sets), ["a", "a", "b", "b", "b"])

    def test_bad_line_is_an_error(self):
        with open(self.path, "a") as file:
            file.write("8/8/8/8/8/8/8/K6k w - - 0 1 | 0\n")
        with self.assertRaises(ValueError):
            read_reference(self.path)

    def test_spearman(self):
        self.assertAlmostEqual(spearman([1, 2, 3, 4], [10, 20, 30, 40]), 1.0)
        self.assertAlmostEqual(spearman([1, 2, 3, 4], [4, 3, 2, 1]), -1.0)
        self.assertAlmostEqual(spearman([1, 2, 3, 4], [1, 4, 9, 100]), 1.0)

    def test_measures(self):
        reference = np.array([10, -30, 40, 100, -120, 200, 400, -600], float)
        m = measures(reference * 2, reference)
        self.assertEqual(len(m["scale"]), len(BANDS))
        for value in m["scale"]:
            self.assertAlmostEqual(value, 2.0)
        self.assertAlmostEqual(m["sign"], 1.0)
        self.assertAlmostEqual(measures(-reference, reference)["sign"], 0.0)

    def test_scores_are_white_point_of_view(self):
        """The same position with the other side to move: a net's score for the side to move becomes
        white's by the sign, so the two evaluations are each one's own side's view turned into white's."""
        model = small_model(1)
        reference = Reference(self.path)
        scores = reference.evaluate(model)
        with torch.no_grad():
            stm = (model(*batch_tensors(reference.records)[:5]) * SCALE).numpy()
        np.testing.assert_allclose(scores, stm * np.array([1, -1, 1, -1, 1]), rtol=1e-5, atol=1e-3)

    def test_compare_is_the_exported_network(self):
        """The comparison network's figures are its exported (quantised) scores, which the float model
        they came from matches to within a few centipawns."""
        model = small_model(2)
        exported = os.path.join(self.directory.name, "net.nnue")
        export.export(model, exported)
        reference = Reference(self.path, exported)
        network = export.read_network(exported)
        expected = [export.quantised_eval(network, fen) * (1 if fen.split()[1] == "w" else -1)
                    for fen, _, _ in POSITIONS]
        np.testing.assert_array_equal(reference.compare, expected)
        np.testing.assert_allclose(reference.evaluate(model), reference.compare, atol=8)

    def test_report(self):
        reference = Reference(self.path, None)
        lines = reference.report(small_model(3))
        for name in ("all", "a", "b"):
            self.assertTrue(any(line.lstrip().startswith(f"{name} net") for line in lines))
        self.assertEqual(reference.compare_report(), [])

    def test_report_with_compare(self):
        model = small_model(4)
        exported = os.path.join(self.directory.name, "net.nnue")
        export.export(model, exported)
        reference = Reference(self.path, exported)
        compare = reference.compare_report()
        for name in ("all", "a", "b"):
            self.assertTrue(any(line.lstrip().startswith(f"{name} compare") for line in compare))
        # A random net's scores are small; the ratio needs comparison scores of MIN_SCALE_SCORE or more.
        reference.compare = np.full(len(POSITIONS), 100.0)
        self.assertTrue(any("|net|/|compare|" in line for line in reference.report(model)))

    def test_bundled_reference_reads(self):
        fens, scores, sets = read_reference(BUNDLED)
        self.assertEqual(len(fens), 6000)
        self.assertEqual(set(sets), {"lichess", "fishtest"})


if __name__ == "__main__":
    unittest.main()
