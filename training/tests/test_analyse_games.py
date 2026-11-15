"""Tests for analyse_games.py's engine-free parts: which games are examined, their turning points, the summary."""

import io
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

import chess  # noqa: E402
import chess.pgn  # noqa: E402

import analyse_games as ag  # noqa: E402

PGN = """[White "tiki"]
[Black "Leorik"]
[Result "0-1"]

1. f3 {+0.10/12 0.2s} e5 2. g4 {-0.50/9 0.1s} Qh4# 0-1

[White "Leorik"]
[Black "tiki"]
[Result "0-1"]

1. f3 e5 2. g4 Qh4# 0-1

[White "Eleanor"]
[Black "Leorik"]
[Result "1/2-1/2"]

1. e4 e5 1/2-1/2

[White "tiki"]
[Black "Eleanor"]
[Result "1/2-1/2"]

1. e4 e5 1/2-1/2
"""


class Selection(unittest.TestCase):
    def test_colour_and_outcome(self):
        self.assertEqual(chess.WHITE, ag.tiki_colour({"White": "tiki", "Black": "x"}, "tiki"))
        self.assertEqual(chess.BLACK, ag.tiki_colour({"White": "x", "Black": "tiki"}, "tiki"))
        self.assertIsNone(ag.tiki_colour({"White": "x", "Black": "y"}, "tiki"))
        self.assertEqual("win", ag.outcome("0-1", chess.BLACK))
        self.assertEqual("loss", ag.outcome("0-1", chess.WHITE))
        self.assertEqual("draw", ag.outcome("1/2-1/2", chess.WHITE))
        self.assertIsNone(ag.outcome("*", chess.WHITE))

    def test_scope(self):
        self.assertEqual([True, False, False], [ag.candidate(r, "losses") for r in ("loss", "draw", "win")])
        self.assertEqual([True, True, False], [ag.candidate(r, "missed") for r in ("loss", "draw", "win")])
        self.assertEqual([True, True, True], [ag.candidate(r, "all") for r in ("loss", "draw", "win")])
        self.assertFalse(ag.candidate(None, "all"))

    def test_read_games(self):
        root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, root, ignore_errors=True)
        path = os.path.join(root, "games.pgn")
        with open(path, "w") as handle:
            handle.write(PGN)
        jobs, score, opponents, _ = ag.read_games(path, "tiki", "missed", None)
        self.assertEqual({"win": 1, "draw": 1, "loss": 1}, score)     # The game without tiki isn't counted.
        self.assertEqual(["Eleanor", "Leorik"], opponents)
        self.assertEqual([1, 4], [index for index, _ in jobs])      # Its loss and its draw.
        self.assertEqual([1], [index for index, _ in ag.read_games(path, "tiki", "losses", None)[0]])
        self.assertEqual([1], [index for index, _ in ag.read_games(path, "tiki", "missed", 1)[0]])
        self.assertEqual([4], [index for index, _ in ag.read_games(path, "tiki", "missed", None, {4})[0]])


    def test_lost_pairs(self):
        # Round 1: lost both; round 2: a loss and a win (level); round 3: a draw and a loss; round 4: one game.
        games = [("1", "loss"), ("1", "loss"), ("2", "loss"), ("2", "win"), ("3", "draw"), ("3", "loss"),
                 ("4", "loss"), ("5", None)]
        self.assertEqual({"1", "3", "4"}, ag.lost_pairs(games))

    def test_pairs_scope(self):
        root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, root, ignore_errors=True)
        path = os.path.join(root, "games.pgn")
        game = '[Round "{}"]\n[White "{}"]\n[Black "{}"]\n[Result "{}"]\n\n1. e4 e5 {}\n\n'
        with open(path, "w") as handle:
            for round_, white, black, result in (("1", "tiki", "L", "0-1"), ("1", "L", "tiki", "1-0"),
                                                 ("2", "tiki", "L", "0-1"), ("2", "L", "tiki", "0-1"),
                                                 ("3", "tiki", "L", "1/2-1/2"), ("3", "L", "tiki", "1-0")):
                handle.write(game.format(round_, white, black, result, result))
        jobs, _, _, pairs = ag.read_games(path, "tiki", "pairs", None)
        self.assertEqual((2, 3), pairs)                                 # Rounds 1 and 3 of the 3 lost.
        self.assertEqual([1, 2, 5, 6], [index for index, _ in jobs])  # Not round 2's loss: tiki won the other.
        self.assertEqual([1, 2, 3, 5, 6], [index for index, _ in ag.read_games(path, "tiki", "missed", None)[0]])


class TurningPoint(unittest.TestCase):
    # tiki is white: it moves at the even plies. Expected score after each ply, from the start.
    PLIES = [0, 2, 4, 6]

    def test_a_loss_turns_at_the_costliest_move(self):
        expected = [0.5, 0.5, 0.5, 0.45, 0.45, 0.1, 0.1, 0.05]     # Ply 4: 0.45 -> 0.1.
        self.assertEqual(("loss", 4), ag.turning_point(expected, self.PLIES, "loss", "missed"))

    def test_only_tikis_moves_count(self):
        expected = [0.5, 0.1, 0.1, 0.05, 0.05, 0.0, 0.0, 0.0]          # tiki's ply 0 drops the most.
        self.assertEqual(("loss", 0), ag.turning_point(expected, self.PLIES, "loss", "missed"))
        expected = [0.5, 0.5, 0.1, 0.1, 0.1, 0.05, 0.05, 0.03]         # The opponent's ply 1 drops the most.
        self.assertEqual(("loss", 4), ag.turning_point(expected, self.PLIES, "loss", "missed"))

    def test_a_missed_win_turns_after_it_was_winning(self):
        # A bigger drop before tiki was winning (ply 0) doesn't count: the one after the peak does.
        expected = [0.7, 0.3, 0.3, 0.85, 0.85, 0.85, 0.6, 0.5]
        self.assertEqual(("missed win", 6), ag.turning_point(expected, self.PLIES, "draw", "missed"))
        self.assertEqual(("missed win", 6), ag.turning_point(expected, self.PLIES, "loss", "losses"))

    def test_a_draw_never_winning_is_skipped_unless_all(self):
        expected = [0.5, 0.5, 0.45, 0.45, 0.5, 0.5, 0.5, 0.5]
        self.assertIsNone(ag.turning_point(expected, self.PLIES, "draw", "missed"))
        self.assertEqual("draw", ag.turning_point(expected, self.PLIES, "draw", "all")[0])
        self.assertEqual("win", ag.turning_point(expected, self.PLIES, "win", "all")[0])

    def test_cause_and_phase(self):
        self.assertEqual("gradual", ag.cause(0.1, 1.0))
        self.assertEqual("depth", ag.cause(0.4, 0.5))
        self.assertEqual("eval", ag.cause(0.4, 0.49))
        # A fresh search to the game's depth that solves it: the game's search went wrong on its own.
        self.assertEqual("unreproduced", ag.cause(0.4, 0.0, 0.5))
        self.assertEqual("depth", ag.cause(0.4, 0.6, 0.2))
        self.assertEqual("gradual", ag.cause(0.1, 0.0, 1.0))
        self.assertEqual("opening", ag.phase(chess.Board()))
        self.assertEqual("middlegame", ag.phase(chess.Board("r2qk2r/8/8/8/8/8/8/R2QK2R w - - 0 30")))
        self.assertEqual("endgame", ag.phase(chess.Board("r3k2r/8/8/8/8/8/8/R3K2R w - - 0 30")))

    def test_game_depth(self):
        game = chess.pgn.read_game(io.StringIO(PGN))
        self.assertEqual(12, ag.game_depth(game.next().comment))
        self.assertEqual(-1, ag.game_depth(""))
        self.assertEqual(20, ag.game_depth("-M3/20 0.5s"))


class Summary(unittest.TestCase):
    def row(self, kind, phase, cause, drop, opponent=""):
        return {"game": 1, "kind": kind, "phase": phase, "cause": cause, "drop": drop, "opponent_move": opponent,
                "opponent_solves": "yes" if opponent == "good" else ("no" if opponent else "")}

    def test_counts_and_top(self):
        rows = [self.row("loss", "middlegame", "eval", 0.5, "good"), self.row("loss", "middlegame", "depth", 0.3, "bad"),
                self.row("missed win", "endgame", "eval", 0.7), self.row("loss", "opening", "gradual", 0.1)]
        summary = ag.summarise(rows, {"scope": "missed"})
        self.assertEqual("missed", summary["scope"])
        self.assertEqual(4, summary["examined"])
        self.assertEqual(1, summary["counts"]["loss"]["middlegame"]["eval"])
        self.assertEqual(1, summary["counts"]["missed win"]["endgame"]["eval"])
        self.assertEqual({"tried": 1, "solved": 1}, summary["opponent"]["eval"])
        self.assertEqual({"tried": 1, "solved": 0}, summary["opponent"]["depth"])
        self.assertEqual([0.7, 0.5, 0.3], [row["drop"] for row in summary["top"]])     # No gradual ones.


if __name__ == "__main__":
    unittest.main()
