import os
import sys
import unittest

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

try:
    import chess
    import lc0
    from tiki_nnue import binary
    from tiki_nnue.data import batch_tensors
except ImportError:  # python-chess missing
    chess = None


def reverse_bits_in_bytes(mask):
    return int.from_bytes(bytes(int(f"{b:08b}"[::-1], 2) for b in mask.to_bytes(8, "little")), "little")


def v6_record(board, best, best_q=0.1, result_q=1.0, version=6, invariance=0):
    """A V6 record of 'board' as lc0 writes one: from the side to move's point of view (the board
    flipped for Black), each plane's bytes bit reversed, the best move as a policy index."""
    record = np.zeros(1, dtype=lc0.V6)
    record["version"], record["input_format"], record["invariance_info"] = version, 1, invariance
    record["side_to_move_or_enpassant"] = int(board.turn == chess.BLACK)
    flip = 56 if board.turn == chess.BLACK else 0
    for side, colour in enumerate((board.turn, not board.turn)):
        for piece_type in range(1, 7):
            mask = 0
            for square in board.pieces(piece_type, colour):
                mask |= 1 << (square ^ flip)
            record["planes"][0, side * 6 + piece_type - 1] = reverse_bits_in_bytes(mask)
    record["best_idx"] = lc0.MOVES.index((best.from_square ^ flip, best.to_square ^ flip))
    record["best_q"], record["root_q"], record["result_q"] = best_q, best_q / 2, result_q
    return record


def game(*records):
    return np.concatenate(records).tobytes()


def features(records):
    """The sorted feature indices of each record, for each perspective (stm, nstm)."""
    batch = batch_tensors(records)
    out = []
    for bag in (0, 2):
        indices, offsets = batch[bag].numpy(), batch[bag + 1].numpy().tolist() + [len(batch[bag])]
        out.append([sorted(indices[offsets[i]:offsets[i + 1]].tolist()) for i in range(len(records))])
    return out


QUIET = [
    ("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3", "f1b5"),
    ("r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3", "a7a6"),
    ("4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19", "d7f6"),
    ("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", "e2e4"),
]


@unittest.skipIf(chess is None, "needs python-chess")
class Lc0Test(unittest.TestCase):
    def decode(self, data, **options):
        settings = dict(skip_plies=0, sample=1.0, max_score=3000, q_field="best_q", seed=1)
        settings.update(options)
        return lc0.decode(data, "game", **settings)

    def test_policy_moves_match_lc0(self):
        # The start and end of lczero-training's policy_index.py.
        names = [chess.square_name(a) + chess.square_name(b) for a, b in lc0.MOVES]
        self.assertEqual(["a1b1", "a1c1", "a1d1", "a1e1", "a1f1", "a1g1", "a1h1", "a1a2", "a1b2", "a1c2",
                          "a1a3", "a1b3", "a1c3", "a1a4", "a1d4", "a1a5", "a1e5"], names[:17])
        self.assertEqual("b1a1", names[23])
        self.assertEqual(["h7g8"] * 3 + ["h7h8"] * 3, names[-6:])
        # 1792 queen and knight moves, each once, then three promotions (q, r, b) for each pawn move to the last rank.
        self.assertEqual(1792, len(set(lc0.MOVES[:1792])))
        self.assertEqual(["a7a8"] * 3 + ["a7b8"] * 3, names[1792:1798])
        self.assertEqual(1858, len(names))

    def test_decodes_positions_for_either_side_to_move(self):
        boards = [chess.Board(fen) for fen, _ in QUIET]
        data = game(*(v6_record(board, chess.Move.from_uci(move), best_q=0.2, result_q=-1.0)
                      for board, (_, move) in zip(boards, QUIET)))
        records, (count, kept, rejected) = self.decode(data)
        self.assertEqual((4, 4, 0), (count, kept, rejected))
        # The same features as the real position: Black to move is stored as White to move with the
        # colours swapped, which is the same position for tiki's network.
        expected = binary.encode([f"{board.fen()} | 0 | 0.5" for board in boards])
        self.assertEqual(features(expected), features(records))
        np.testing.assert_array_equal([round(float(lc0.q_to_centipawns(0.2)))] * 4, records["score"])
        np.testing.assert_array_equal([0] * 4, records["result"])     # A loss for the side to move.
        np.testing.assert_array_equal([0] * 4, records["side"])

    def test_keeps_quiet_positions_only(self):
        board = chess.Board("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2")
        check = chess.Board("rnbqk1nr/pppp1ppp/8/4p3/1b1PP3/8/PPP2PPP/RNBQKBNR w KQkq - 1 3")
        promotion = chess.Board("8/4P1k1/8/8/8/8/6K1/8 w - - 0 1")
        en_passant = chess.Board("rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3")
        data = game(v6_record(board, chess.Move.from_uci("e4d5")),           # Capture.
                    v6_record(check, chess.Move.from_uci("c2c3")),            # In check.
                    v6_record(promotion, chess.Move.from_uci("e7e8q")),       # Promotion.
                    v6_record(en_passant, chess.Move.from_uci("e5f6")),       # En passant.
                    v6_record(board, chess.Move.from_uci("g1f3")))            # Quiet.
        records, (count, kept, rejected) = self.decode(data)
        self.assertEqual((5, 1, 0), (count, kept, rejected))

    def test_filters(self):
        board = chess.Board(QUIET[0][0])
        move = chess.Move.from_uci(QUIET[0][1])
        plies = game(*(v6_record(board, move) for _ in range(10)))
        self.assertEqual(4, self.decode(plies, skip_plies=6)[1][1])
        self.assertEqual(0, self.decode(game(v6_record(board, move, best_q=0.99)), max_score=3000)[1][1])
        self.assertEqual(0, self.decode(game(v6_record(board, move, version=5)))[1][1])
        self.assertEqual(0, self.decode(game(v6_record(board, move, invariance=lc0.DELETED)))[1][1])
        root = self.decode(game(v6_record(board, move, best_q=0.2)), q_field="root_q")[0]
        self.assertEqual(round(float(lc0.q_to_centipawns(0.1))), root["score"][0])

    def test_sampling_is_reproducible(self):
        board = chess.Board(QUIET[0][0])
        data = game(*(v6_record(board, chess.Move.from_uci(QUIET[0][1])) for _ in range(400)))
        first = self.decode(data, sample=0.25)[1][1]
        self.assertEqual(first, self.decode(data, sample=0.25)[1][1])
        self.assertAlmostEqual(100, first, delta=30)

    def test_rejects_records_that_are_not_positions(self):
        board = chess.Board(QUIET[0][0])
        record = v6_record(board, chess.Move.from_uci(QUIET[0][1]))
        record["planes"][0, 5] = 0      # No king.
        self.assertEqual((1, 0, 1), self.decode(record.tobytes())[1])

    def test_converter_skips_other_record_sizes(self):
        import gzip
        converter = lc0.Converter(0, 1.0, 3000, "best_q", 1)
        board = chess.Board(QUIET[0][0])
        data = v6_record(board, chess.Move.from_uci(QUIET[0][1])).tobytes()
        self.assertEqual(1, converter(("a", gzip.compress(data)))[1][1])
        self.assertEqual((0, 0, 1), converter(("b", gzip.compress(data + b"x")))[1])

    def test_choose_and_days(self):
        self.assertEqual({"20240430", "20240501"}, lc0.days("2024-04-30:2024-05-01"))
        tars = [(f"t{h}", "20240401", f"{h:02d}17") for h in range(24)]
        self.assertEqual(["t0", "t23"], [name for name, _, _ in lc0.choose(tars, {"20240401"}, 2)])
        self.assertEqual(["t12"], [name for name, _, _ in lc0.choose(tars, {"20240401"}, 1)])
        self.assertEqual(24, len(lc0.choose(tars, {"20240401"}, 0)))
        self.assertEqual([], lc0.choose(tars, {"20240402"}, 2))


if __name__ == "__main__":
    unittest.main()
