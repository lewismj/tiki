import os
import random
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from tiki_nnue import binary
from tiki_nnue.data import batch_tensors, collate, parse_line

try:
    import chess
except ImportError:
    chess = None

LINES = [
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 | 30 | 0.5",
    "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19 | -250 | 0.0",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1 | 400 | 1.0",
    "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/8 b - - 3 54 | 99999 | 1.0",
]


def random_lines(count, seed=1):
    """Positions from random games, with random scores and results."""
    rng = random.Random(seed)
    lines = []
    while len(lines) < count:
        board = chess.Board()
        for _ in range(rng.randrange(1, 120)):
            moves = list(board.legal_moves)
            if not moves:
                break
            board.push(rng.choice(moves))
            lines.append(f"{board.fen()} | {rng.randint(-3000, 3000)} | {rng.choice([0.0, 0.5, 1.0])}")
    return lines[:count]


def write(path, text):
    with open(path, "wb") as file:
        file.write(text.encode())


class BinaryTest(unittest.TestCase):
    def assert_batch_matches(self, lines):
        """batch_tensors on encoded lines gives the same samples as parse_line and collate."""
        expected = collate([parse_line(line) for line in lines])
        actual = batch_tensors(binary.encode(lines))
        for bag in (0, 2):      # stm and nstm: the features of each sample, in any order.
            for i in range(len(lines)):
                def sample(batch):
                    indices, offsets = np.asarray(batch[bag]), np.asarray(batch[bag + 1])
                    end = offsets[i + 1] if i + 1 < len(lines) else len(indices)
                    return sorted(indices[offsets[i]:end].tolist())
                self.assertEqual(sample(expected), sample(actual), lines[i])
        np.testing.assert_array_equal(np.clip(expected[4].numpy(), -32767, 32767), actual[4].numpy())
        np.testing.assert_array_equal(expected[5].numpy(), actual[5].numpy())

    def test_batch_features_match_parse_line(self):
        self.assert_batch_matches(LINES)

    @unittest.skipIf(chess is None, "needs python-chess")
    def test_batch_features_match_parse_line_on_random_positions(self):
        self.assert_batch_matches(random_lines(2000))

    def test_convert_and_load(self):
        """Blank lines are skipped, CRLF and a missing final newline are handled, and the files load
        back as one array in order, whatever the number of workers."""
        with tempfile.TemporaryDirectory() as directory:
            first, second = os.path.join(directory, "a.txt"), os.path.join(directory, "b.txt")
            write(first, f"{LINES[0]}\r\n\r\n  \n{LINES[1]}\r\n")
            write(second, f"\n{LINES[2]}\n{LINES[3]}")
            outputs = []
            for workers in (1, 2):
                converted, outs = [], []
                for path in (first, second):
                    out = path.replace(".txt", f"-{workers}.bin")
                    self.assertEqual(2, binary.convert_text(path, out, workers, converted.append))
                    outs.append(out)
                self.assertEqual(os.path.getsize(first) + os.path.getsize(second), sum(converted))
                loaded = []
                outputs.append(binary.load(outs, loaded.append))
                self.assertEqual(sum(os.path.getsize(out) - len(binary.MAGIC) for out in outs), sum(loaded))
            np.testing.assert_array_equal(binary.encode(LINES), outputs[0])
            np.testing.assert_array_equal(outputs[0], outputs[1])

    def test_rejects_bad_files(self):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "bad.bin")
            write(path, "not data")
            self.assertRaises(ValueError, binary.count, path)
            with open(path, "wb") as file:
                file.write(binary.MAGIC + binary.encode(LINES[:1]).tobytes()[:-1])
            self.assertRaises(ValueError, binary.count, path)


if __name__ == "__main__":
    unittest.main()
