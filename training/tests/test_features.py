import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from tiki_nnue.features import (BLACK, INPUTS, PIECE_SQUARES, WHITE, active_features, feature_index,
                                parse_fen, square)

POSITIONS = [
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
    "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/8 b - - 3 54",
]

P, Q, K, p, k = 0, 4, 5, 6, 11


def colour_flip(pieces):
    return [((pc + 6) % 12, sq ^ 56) for pc, sq in pieces]


def mirror(pieces):
    return [(pc, sq ^ 7) for pc, sq in pieces]


class FeatureTests(unittest.TestCase):

    def test_known_values(self):
        """The same values as test_nnue_feature_index_known_values in the C tests."""
        e1, e2, d1, e7, e8, a1, a2 = (square(s) for s in ("e1", "e2", "d1", "e7", "e8", "a1", "a2"))
        self.assertEqual(21875, feature_index(WHITE, e1, P, e2))
        self.assertEqual(22396, feature_index(WHITE, e1, Q, d1))
        self.assertEqual(22467, feature_index(WHITE, e1, k, e8))
        self.assertEqual(21899, feature_index(WHITE, e1, p, e7))
        self.assertEqual(21875, feature_index(BLACK, e8, p, e7))
        self.assertEqual(28 * PIECE_SQUARES + 48, feature_index(WHITE, a1, P, a2))

    def test_in_range_and_unique(self):
        for fen in POSITIONS:
            pieces, _ = parse_fen(fen)
            for perspective in (WHITE, BLACK):
                features = active_features(pieces, perspective)
                self.assertEqual(len(pieces), len(set(features)))
                self.assertTrue(all(0 <= f < INPUTS for f in features))

    def test_symmetry(self):
        for fen in POSITIONS:
            pieces, _ = parse_fen(fen)
            for perspective in (WHITE, BLACK):
                features = sorted(active_features(pieces, perspective))
                self.assertEqual(features, sorted(active_features(colour_flip(pieces), perspective ^ 1)))
                self.assertEqual(features, sorted(active_features(mirror(pieces), perspective)))


if __name__ == "__main__":
    unittest.main()
