import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from tiki_nnue.features import (BLACK, INPUTS, PIECE_SQUARES, TOTAL_INPUTS, WHITE, active_features,
                                active_pair_features, all_features, feature_index, pair_index, parse_fen, pawn_id,
                                square)

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


def brute_force_pairs(pieces, perspective):
    """PawnPairs written out from the definition, independently of features.py: for each pair of pawns
    within one file of each other, each pawn's (theirs, rank from the perspective's side, file mirrored
    when its king is on files e-h), numbered 0..95, and the pair's triangular index."""
    king = next(sq for pc, sq in pieces if pc == (5 if perspective == WHITE else 11))
    king_file = king & 7
    mirrored = king_file >= 4

    def number(pc, sq):
        rank = 8 - (sq >> 3)                         # 1..8 from white's side
        if perspective == BLACK:
            rank = 9 - rank
        file = sq & 7
        if mirrored:
            file = 7 - file
        theirs = (pc >= 6) != (perspective == BLACK)
        return int(theirs) * 48 + (8 - rank) * 8 + file - 8       # the a8-first square, less the 8 of rank 8

    pawns = [(pc, sq) for pc, sq in pieces if pc in (P, p)]
    result = []
    for i in range(len(pawns)):
        for j in range(i + 1, len(pawns)):
            (pa, sa), (pb, sb) = pawns[i], pawns[j]
            if abs((sa & 7) - (sb & 7)) <= 1:
                a, b = number(pa, sa), number(pb, sb)
                result.append(INPUTS + max(a, b) * (max(a, b) - 1) // 2 + min(a, b))
    return sorted(result)


class PawnPairTests(unittest.TestCase):

    def test_known_values(self):
        """The same values as the C tests: d2 and e2 from white's side (king e1, mirrored), and d7 and e7
        from black's (king e8), are the same pair of own pawns."""
        e1, e2, d2, e7, d7, e8 = (square(s) for s in ("e1", "e2", "d2", "e7", "d7", "e8"))
        self.assertEqual(43, pawn_id(WHITE, e1, P, e2))      # e2 mirrored to d2: oriented square 51, id 43.
        self.assertEqual(44, pawn_id(WHITE, e1, P, d2))
        self.assertEqual(INPUTS + 44 * 43 // 2 + 43, pair_index(43, 44))
        self.assertEqual(23517, pair_index(pawn_id(WHITE, e1, P, e2), pawn_id(WHITE, e1, P, d2)))
        self.assertEqual(23517, pair_index(pawn_id(BLACK, e8, p, e7), pawn_id(BLACK, e8, p, d7)))
        self.assertEqual(48 + 43, pawn_id(WHITE, e1, p, square("e2")))     # Their pawn: 48 more.
        self.assertEqual(TOTAL_INPUTS - 1, pair_index(95, 94))

    def test_start_position(self):
        """16 pawns: 7 pairs of neighbouring own pawns, 7 of theirs, and 8 same-file plus 14 adjacent-file pairs
        of opposite colours: 36 pairs."""
        pieces, _ = parse_fen(POSITIONS[0])
        for perspective in (WHITE, BLACK):
            self.assertEqual(36, len(active_pair_features(pieces, perspective)))

    def test_in_range_and_unique(self):
        for fen in POSITIONS:
            pieces, _ = parse_fen(fen)
            for perspective in (WHITE, BLACK):
                pairs = active_pair_features(pieces, perspective)
                self.assertEqual(len(pairs), len(set(pairs)))
                self.assertTrue(all(INPUTS <= f < TOTAL_INPUTS for f in pairs))
                self.assertEqual(active_features(pieces, perspective) + pairs, all_features(pieces, perspective))

    def test_matches_definition(self):
        for fen in POSITIONS:
            pieces, _ = parse_fen(fen)
            for perspective in (WHITE, BLACK):
                self.assertEqual(brute_force_pairs(pieces, perspective),
                                 sorted(active_pair_features(pieces, perspective)), fen)

    def test_symmetry(self):
        for fen in POSITIONS:
            pieces, _ = parse_fen(fen)
            for perspective in (WHITE, BLACK):
                pairs = sorted(active_pair_features(pieces, perspective))
                self.assertEqual(pairs, sorted(active_pair_features(colour_flip(pieces), perspective ^ 1)))
                self.assertEqual(pairs, sorted(active_pair_features(mirror(pieces), perspective)))


if __name__ == "__main__":
    unittest.main()
