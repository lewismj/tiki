"""Checks the trainer's export against the engine: tiki must load the exported network and give
exactly the evaluation quantised_eval() computes. Needs the engine binary, set TIKI_APP to its path
(e.g. TIKI_APP=build/engine/app/tiki), otherwise the test is skipped."""

import os
import subprocess
import sys
import tempfile
import unittest

import torch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from tiki_nnue.export import export, quantise, quantised_eval
from tiki_nnue.model import TikiNNUE

FENS = [
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
    "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/8 b - - 3 54",
    "8/8/8/4k3/8/8/8/4KN2 w - - 0 1",
]


@unittest.skipUnless(os.environ.get("TIKI_APP"), "set TIKI_APP to the tiki binary")
class CrossCheckTests(unittest.TestCase):

    def test_engine_matches_reference(self):
        torch.manual_seed(3)
        model = TikiNNUE()
        # Larger weights than the initialisation, so the accumulators reach the SCReLU clamp.
        with torch.no_grad():
            model.ft.weight.mul_(10)
            model.ft_bias.uniform_(-0.5, 1.5)
            model.out.bias.uniform_(-0.2, 0.2)          # A different output per bucket.
            model.out.weight.normal_(0.0, 0.1)
        model.clip_weights()
        network = quantise(model)

        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "net.nnue")
            export(model, path)
            for fen in FENS:
                output = subprocess.run([os.environ["TIKI_APP"], "nnue-eval", path, fen],
                                        capture_output=True, text=True, check=True).stdout
                self.assertEqual(quantised_eval(network, fen), int(output.strip().splitlines()[-1]), fen)


if __name__ == "__main__":
    unittest.main()
