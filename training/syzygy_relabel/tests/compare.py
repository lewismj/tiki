"""Checks syzygy_relabel against python-chess's own Syzygy prober (chess.syzygy), record for record.

    python3 tests/compare.py <syzygy_relabel> --tb DIR [--positions N] [--seed S] [--max-pieces N]
                             [--bin FILE --records N]

Without --bin it generates random positions with the cases a relabeller can get wrong: every piece
type and count up to --max-pieces (default 5), both sides to move, positions that might castle
(king and rook on their starting squares), positions where an en passant capture may be possible,
and positions with too many pieces to probe. With --bin it takes the first --records records of a
real data file instead. Each set is relabelled by both, and the outputs and counts compared.
"""
import argparse
import os
import random
import re
import subprocess
import sys
import tempfile

import chess
import chess.syzygy

MAGIC = b"TIKIBIN1"
RECORD = 68
PAD = 0xFFFF
PIECES = "PNBRQKpnbrqk"     # tiki's piece order


def tiki_square(square):
    """python-chess square (a1 = 0) -> tiki's (a8 = 0); the same map both ways."""
    return square ^ 56


def encode(board, score, result):
    pieces = [PIECES.index(piece.symbol()) * 64 + tiki_square(square) for square, piece in board.piece_map().items()]
    rng_order = sorted(pieces)
    data = bytearray()
    for i in range(32):
        value = rng_order[i] if i < len(rng_order) else PAD
        data += value.to_bytes(2, "little")
    data += int(score).to_bytes(2, "little", signed=True)
    data += bytes([result, 0 if board.turn == chess.WHITE else 1])
    return bytes(data)


def decode(record):
    board = chess.Board(None)
    for i in range(32):
        value = int.from_bytes(record[2 * i:2 * i + 2], "little")
        if value == PAD:
            break
        board.set_piece_at(tiki_square(value & 63), chess.Piece.from_symbol(PIECES[value >> 6]))
    board.turn = chess.WHITE if record[67] == 0 else chess.BLACK
    board.castling_rights = 0
    board.ep_square = None
    return board


def may_castle(board):
    def has(square, symbol):
        piece = board.piece_at(square)
        return piece is not None and piece.symbol() == symbol
    return ((has(chess.E1, "K") and (has(chess.A1, "R") or has(chess.H1, "R"))) or
            (has(chess.E8, "k") and (has(chess.A8, "r") or has(chess.H8, "r"))))


def en_passant_squares(board):
    """Squares an en passant capture might go to: an enemy pawn that could have just advanced two
    squares (both squares behind it empty) with a pawn of the side to move beside it."""
    us = board.turn
    squares = []
    for square in board.pieces(chess.PAWN, not us):
        rank, file = chess.square_rank(square), chess.square_file(square)
        if rank != (4 if us == chess.WHITE else 3):
            continue
        step = 8 if us == chess.WHITE else -8
        passed, start = square + step, square + 2 * step
        if board.piece_at(passed) or board.piece_at(start):
            continue
        beside = [square + d for d, ok in ((-1, file > 0), (1, file < 7)) if ok]
        if any(board.piece_at(s) == chess.Piece(chess.PAWN, us) for s in beside):
            squares.append(passed)
    return squares


def result_of(wdl):
    """python-chess's WDL (-2..2, side to move) as a result byte: cursed wins and blessed losses are draws."""
    return 2 if wdl == 2 else 0 if wdl == -2 else 1


def expected(records, tablebase, max_pieces):
    """The relabelled records and the counts, as syzygy_relabel should produce them."""
    counts = dict(records=0, covered=0, castling=0, en_passant=0, failed=0, probed=0, changed=0)
    out = []
    for record in records:
        counts["records"] += 1
        board = decode(record)
        pieces = len(board.piece_map())
        if pieces > max_pieces:
            out.append(record)
            continue
        counts["covered"] += 1
        if may_castle(board):
            counts["castling"] += 1
            out.append(record)
            continue
        try:
            result = result_of(tablebase.probe_wdl(board))
            others = []
            for square in en_passant_squares(board):
                with_ep = board.copy()
                with_ep.ep_square = square
                others.append(result_of(tablebase.probe_wdl(with_ep)))
        except (chess.syzygy.MissingTableError, KeyError):
            counts["failed"] += 1
            out.append(record)
            continue
        if any(other != result for other in others):
            counts["en_passant"] += 1
            out.append(record)
            continue
        counts["probed"] += 1
        if result != record[66]:
            counts["changed"] += 1
            record = record[:66] + bytes([result]) + record[67:]
        out.append(record)
    return out, counts


def random_board(rng, max_pieces):
    """A legal position (the side not to move isn't in check) with 2..max_pieces pieces, sometimes
    shaped to test castling or en passant."""
    while True:
        board = chess.Board(None)
        board.turn = rng.choice([chess.WHITE, chess.BLACK])
        kind = rng.random()
        squares = list(chess.SQUARES)
        rng.shuffle(squares)
        used = set()

        def put(square, symbol):
            board.set_piece_at(square, chess.Piece.from_symbol(symbol))
            used.add(square)

        if kind < 0.15:     # king and rook on their starting squares
            if rng.random() < 0.5:
                put(chess.E1, "K")
                put(rng.choice([chess.A1, chess.H1]), "R")
            else:
                put(chess.E8, "k")
                put(rng.choice([chess.A8, chess.H8]), "r")
        elif kind < 0.35:   # a pawn that may have just advanced two, with an enemy pawn beside it
            us = board.turn
            file = rng.randrange(8)
            rank = 4 if us == chess.WHITE else 3
            square = chess.square(file, rank)
            put(square, "p" if us == chess.WHITE else "P")
            side = file - 1 if file > 0 and (file == 7 or rng.random() < 0.5) else file + 1
            put(chess.square(side, rank), "P" if us == chess.WHITE else "p")
            if rng.random() < 0.2:      # sometimes block the square it passed
                step = 8 if us == chess.WHITE else -8
                blocker = square + step
                if blocker not in used:
                    put(blocker, rng.choice("NBnb"))
        for symbol in "Kk":
            if board.king(chess.WHITE if symbol == "K" else chess.BLACK) is None:
                free = [s for s in squares if s not in used]
                put(free[0], symbol)
        target = rng.randint(2, max_pieces)
        for square in squares:
            if len(used) >= target:
                break
            if square in used:
                continue
            symbol = rng.choice("PNBRQpnbrq")
            if symbol in "Pp" and chess.square_rank(square) in (0, 7):
                continue
            put(square, symbol)
        board.castling_rights = 0
        board.ep_square = None
        if board.is_valid():
            return board


def big_board(rng):
    """A position with more pieces than the tables cover, from a few random moves of a game."""
    board = chess.Board()
    for _ in range(rng.randint(0, 30)):
        moves = list(board.legal_moves)
        if not moves:
            break
        board.push(rng.choice(moves))
    return chess.Board(board.fen())


def run(tool, tb, records, max_pieces, threads, workdir, name):
    source = os.path.join(workdir, name + ".bin")
    target = os.path.join(workdir, name + "-tb.bin")
    with open(source, "wb") as f:
        f.write(MAGIC + b"".join(records))
    if os.path.exists(target):
        os.remove(target)
    proc = subprocess.run([tool, "--tb", tb, "--max-pieces", str(max_pieces), "--threads", str(threads), source, target],
                          capture_output=True, text=True)
    if proc.returncode != 0:
        sys.exit(f"{name}: syzygy_relabel failed:\n{proc.stdout}\n{proc.stderr}")
    with open(target, "rb") as f:
        data = f.read()
    if data[:8] != MAGIC:
        sys.exit(f"{name}: output has no TIKIBIN1 header")
    out = [data[8 + i * RECORD:8 + (i + 1) * RECORD] for i in range((len(data) - 8) // RECORD)]
    line = next(l for l in proc.stdout.splitlines() if l.startswith("stats:"))
    counts = {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", line)}
    return out, counts


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("tool")
    parser.add_argument("--tb", required=True, help="directory of Syzygy .rtbw tables")
    parser.add_argument("--positions", type=int, default=20000)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--max-pieces", type=int, default=5)
    parser.add_argument("--bin", help="a real TIKIBIN1 file to take records from instead")
    parser.add_argument("--records", type=int, default=1_000_000, help="records taken from --bin")
    args = parser.parse_args()

    tablebase = chess.syzygy.open_tablebase(args.tb)
    failures = 0
    with tempfile.TemporaryDirectory() as workdir:
        sets = []
        if args.bin:
            with open(args.bin, "rb") as f:
                if f.read(8) != MAGIC:
                    sys.exit(f"{args.bin} isn't TIKIBIN1 data")
                data = f.read(args.records * RECORD)
            sets.append((os.path.basename(args.bin), [data[i:i + RECORD] for i in range(0, len(data) - RECORD + 1, RECORD)]))
        else:
            for round_ in range(args.rounds):
                rng = random.Random(args.seed * 1000 + round_)
                records = []
                for _ in range(args.positions):
                    board = big_board(rng) if rng.random() < 0.1 else random_board(rng, args.max_pieces)
                    records.append(encode(board, rng.randint(-3000, 3000), rng.randrange(3)))
                sets.append((f"random-{args.seed}-{round_}", records))

        for name, records in sets:
            want, want_counts = expected(records, tablebase, args.max_pieces)
            for threads in (1, 3):
                got, got_counts = run(args.tool, args.tb, records, args.max_pieces, threads, workdir, f"{name}-t{threads}")
                summary = ", ".join(f"{k} {want_counts[k]}" for k in ("records", "probed", "changed", "castling", "en_passant", "failed"))
                bad = [i for i, (a, b) in enumerate(zip(want, got)) if a != b]
                count_diff = {k: (want_counts[k], got_counts.get(k)) for k in want_counts if want_counts[k] != got_counts.get(k)}
                if len(got) != len(want) or bad or count_diff:
                    failures += 1
                    print(f"{name} ({threads} threads): DIFFERENT ({summary})")
                    if len(got) != len(want):
                        print(f"  {len(got)} records written, {len(want)} expected")
                    for i in bad[:5]:
                        board = decode(records[i])
                        print(f"  record {i}: {board.fen()} game {records[i][66]} -> expected {want[i][66]}, got {got[i][66]}")
                    if count_diff:
                        print(f"  counts (expected, got): {count_diff}")
                else:
                    print(f"{name} ({threads} threads): identical ({summary})")
    print("PASS" if failures == 0 else f"FAIL ({failures})")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
