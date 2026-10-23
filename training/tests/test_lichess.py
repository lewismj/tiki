import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

try:
    import zstandard
    import lichess
except ImportError:                     # python-chess / zstandard not installed.
    lichess = None

from tiki_nnue import binary
from tiki_nnue.data import parse_line

# Analysed: an eval after every move. 4. Bxc6 and 4... dxc6 are captures, 5. O-O has a mate score.
ANALYSED = """[Event "Rated Blitz game"]
[Site "https://lichess.org/test0001"]
[Result "1-0"]
[WhiteElo "2000"]
[BlackElo "1900"]

1. e4 { [%eval 0.3] } 1... e5 { [%eval 0.25] } 2. Nf3 { [%eval 0.3] } 2... Nc6 { [%eval 0.3] } \
3. Bb5 { [%eval 0.2] } 3... a6 { [%eval 0.3] } 4. Bxc6 { [%eval 0.1] } 4... dxc6 { [%eval 0.2] } \
5. O-O { [%eval #4] } 5... f6 { [%eval -0.5] } 1-0

"""

NOT_ANALYSED = """[Event "Rated Blitz game"]
[Site "https://lichess.org/test0002"]
[Result "0-1"]
[WhiteElo "1500"]
[BlackElo "1500"]

1. d4 d5 2. c4 e6 0-1

"""

UNFINISHED = """[Event "Rated Blitz game"]
[Site "https://lichess.org/test0003"]
[Result "*"]
[WhiteElo "2000"]
[BlackElo "2000"]

1. e4 { [%eval 0.3] } 1... c5 { [%eval 0.3] } 2. Nf3 { [%eval 0.3] } *

"""


@unittest.skipIf(lichess is None, "needs python-chess and zstandard (pip install -r requirements.txt)")
class LichessTests(unittest.TestCase):

    def convert(self, **settings):
        options = dict(skip_plies=0, sample=1.0, min_elo=0, max_score=3000, seed=1)
        options.update(settings)
        with tempfile.TemporaryDirectory() as directory:
            dump = os.path.join(directory, "dump.pgn.zst")
            with open(dump, "wb") as file:
                file.write(zstandard.ZstdCompressor().compress((ANALYSED + NOT_ANALYSED + UNFINISHED).encode()))
            out = os.path.join(directory, "out.txt")
            games, positions = lichess.convert(dump, out, lichess.Converter(**options), workers=1, max_positions=0)
            with open(out) as file:
                lines = file.readlines()
        self.assertEqual(positions, len(lines))
        return games, lines

    def test_months(self):
        self.assertEqual(["2023-11", "2023-12", "2024-01", "2024-02"], lichess.months(["2023-11:2024-02"]))
        self.assertEqual(["2024-05", "2024-07"], lichess.months(["2024-05", "2024-07"]))

    def test_converts_analysed_quiet_positions(self):
        games, lines = self.convert()
        # Only games with evals are parsed, the unfinished one ('*') gives no positions.
        self.assertEqual(2, games)
        # Kept: after e4, e5, Nf3, Nc6, Bb5 and dxc6. Skipped: a6 and Bxc6 (a capture follows),
        # O-O (mate score) and f6 (no move follows).
        self.assertEqual([30, 25, 30, 30, 20, 20], [int(line.split("|")[1]) for line in lines])
        self.assertTrue(all(line.strip().endswith("| 1.0") for line in lines))
        self.assertTrue(lines[0].startswith("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq "))
        # The trainer reads them, black to move turns the white-relative score and result around.
        _, _, score, result = parse_line(lines[0])
        self.assertEqual((-30.0, 0.0), (score, result))

    def test_converts_to_binary(self):
        """The binary output is the text output, encoded."""
        _, lines = self.convert()
        with tempfile.TemporaryDirectory() as directory:
            dump = os.path.join(directory, "dump.pgn.zst")
            with open(dump, "wb") as file:
                file.write(zstandard.ZstdCompressor().compress((ANALYSED + NOT_ANALYSED + UNFINISHED).encode()))
            out = os.path.join(directory, "out.bin")
            converter = lichess.Converter(skip_plies=0, sample=1.0, min_elo=0, max_score=3000, seed=1, binary=True)
            _, positions = lichess.convert(dump, out, converter, workers=1, max_positions=0)
            records = binary.load([out])
        self.assertEqual(len(lines), positions)
        self.assertEqual(binary.encode(lines).tobytes(), records.tobytes())

    def test_filters(self):
        self.assertEqual(4, len(self.convert(skip_plies=2)[1]))         # e4 and e5 skipped.
        self.assertEqual(0, len(self.convert(min_elo=1950)[1]))         # Black is 1900.
        self.assertEqual(0, len(self.convert(max_score=15)[1]))
        self.assertEqual(0, len(self.convert(sample=0.0)[1]))


if __name__ == "__main__":
    unittest.main()
