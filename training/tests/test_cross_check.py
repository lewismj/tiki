"""Checks the trainer's export against the engine: tiki must load the exported network and give
exactly the evaluation quantised_eval() computes. Needs the engine binary, set TIKI_APP to its path
(e.g. TIKI_APP=build/engine/app/tiki), otherwise the test is skipped."""

import os
import random
import subprocess
import sys
import tempfile
import unittest

import torch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from tiki_nnue.export import export, quantise, quantised_eval
from tiki_nnue.features import parse_fen, position_bucket
from tiki_nnue.model import TikiNNUE

try:
    import chess
except ImportError:
    chess = None

FENS = [
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
    "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/8 b - - 3 54",
    "8/8/8/4k3/8/8/8/4KN2 w - - 0 1",
    # Each queen combination, for each side to move.
    "rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1",
    "4k3/8/8/8/8/8/8/3QK3 w - - 0 1",
    "4k3/8/8/8/8/8/8/3QK3 b - - 0 1",
    "3qk3/8/8/8/8/8/8/3QK3 b - - 0 1",
]


def random_fens(count, seed):
    """Positions from random games: every piece count and queen combination turns up."""
    rng = random.Random(seed)
    fens = []
    while len(fens) < count:
        board = chess.Board()
        for _ in range(rng.randrange(1, 160)):
            moves = list(board.legal_moves)
            if not moves:
                break
            board.push(rng.choice(moves))
        if not board.is_game_over():
            fens.append(board.fen())
    return fens


def random_model(seed, factoriser):
    """Larger, varied weights than the initialisation: accumulators reaching the 0..1 clamp, hidden
    units on both sides of CReLU's range, every bucket different."""
    torch.manual_seed(seed)
    model = TikiNNUE(factoriser=factoriser)
    with torch.no_grad():
        model.ft.weight.normal_(0.0, 0.15)
        model.ft_bias.uniform_(-0.5, 1.5)
        if factoriser:
            model.factor.weight.normal_(0.0, 0.1)
        model.l1.weight.normal_(0.0, 0.3)
        model.l1.bias.normal_(0.0, 0.5)
        model.l1_shared.weight.normal_(0.0, 0.3)
        model.out.weight.normal_(0.0, 0.8)
        model.out.bias.normal_(0.0, 0.3)
    model.clip_weights()
    return model


@unittest.skipUnless(os.environ.get("TIKI_APP"), "set TIKI_APP to the tiki binary")
class CrossCheckTests(unittest.TestCase):

    def check(self, model, fens):
        network = quantise(model)
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "net.nnue")
            export(model, path)
            for fen in fens:
                output = subprocess.run([os.environ["TIKI_APP"], "nnue-eval", path, fen],
                                        capture_output=True, text=True, check=True).stdout
                self.assertEqual(quantised_eval(network, fen), int(output.strip().splitlines()[-1]), fen)

    def test_engine_matches_reference(self):
        self.check(random_model(3, factoriser=False), FENS)

    def test_engine_matches_reference_factorised(self):
        self.check(random_model(4, factoriser=True), FENS)

    @unittest.skipUnless(chess, "python-chess not installed")
    def test_engine_matches_reference_in_every_bucket(self):
        fens = random_fens(150, seed=11)
        buckets = {position_bucket(*parse_fen(fen)) for fen in fens}
        self.assertGreaterEqual(len(buckets), 16)       # Most of the 32, queens on and off.
        self.check(random_model(5, factoriser=True), fens + FENS)


if __name__ == "__main__":
    unittest.main()
