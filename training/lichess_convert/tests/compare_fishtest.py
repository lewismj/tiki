"""Checks 'lichess_convert --fishtest' against a python-chess reading of the same Fishtest PGN, byte for byte.

    python3 tests/compare_fishtest.py <lichess_convert> <games.pgn.gz> [--skip-plies N] [--max-score N]

The reference: for each mainline move, the position before it is kept when the game's ply there (counted from the
start of the game, the FEN header's full move number included) is at least --skip-plies, the side to move isn't in
check, the move is neither a capture nor a promotion, and the move's comment starts with '<pawns>/<depth>' whose
centipawns are within --max-score. The comment is the mover's score, so the label for White is its sign flipped
when Black moved. Games without a 1-0, 0-1 or 1/2-1/2 result are left out. The records are binary.encode's.
"""
import argparse
import gzip
import os
import re
import subprocess
import sys
import tempfile

import chess
import chess.pgn

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
from tiki_nnue import binary  # noqa: E402

SCORE = re.compile(r"^\s*([+-]?\d+\.\d+)/")
RESULTS = {"1-0": 1.0, "0-1": 0.0, "1/2-1/2": 0.5}


def reference(path, skip_plies, max_score):
    lines = []
    with gzip.open(path, "rt", encoding="utf-8", errors="replace") as f:
        while (game := chess.pgn.read_game(f)):
            result = RESULTS.get(game.headers.get("Result", "*"))
            if result is None: continue
            board = game.board()
            ply0 = (board.fullmove_number - 1) * 2 + (board.turn == chess.BLACK)
            for i, node in enumerate(game.mainline()):
                move, m = node.move, SCORE.match(node.comment)
                if (ply0 + i >= skip_plies and m and not board.is_check() and not board.is_capture(move)
                        and not move.promotion):
                    cp = round(float(m.group(1)) * 100)
                    if abs(cp) <= max_score:
                        white = cp if board.turn == chess.WHITE else -cp
                        lines.append(f"{board.fen()} | {white} | {result}")
                board.push(move)
    return binary.MAGIC + binary.encode(lines).tobytes()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe"); ap.add_argument("pgn")
    ap.add_argument("--skip-plies", type=int, default=16); ap.add_argument("--max-score", type=int, default=3000)
    a = ap.parse_args()
    expected = reference(a.pgn, a.skip_plies, a.max_score)
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "out.bin")
        with gzip.open(a.pgn, "rb") as src:
            subprocess.run([a.exe, "--fishtest", "--skip-plies", str(a.skip_plies), "--max-score", str(a.max_score),
                            "/dev/stdin", out], input=src.read(), check=True, stderr=subprocess.DEVNULL)
        got = open(out, "rb").read()
    n = (len(expected) - 8) // 68
    if got == expected:
        print(f"identical: {n:,} positions"); print("PASS"); return
    m = (len(got) - 8) // 68
    first = next((i for i in range(min(n, m)) if got[8 + 68 * i:8 + 68 * (i + 1)] != expected[8 + 68 * i:8 + 68 * (i + 1)]),
                 min(n, m))
    print(f"DIFFERENT: reference {n:,} positions, lichess_convert {m:,}; first difference at record {first:,}")
    sys.exit(1)


if __name__ == "__main__":
    main()
