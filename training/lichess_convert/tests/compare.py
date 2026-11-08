"""Checks lichess_convert against lichess.py (python-chess) on the same input, byte for byte.

    python3 tests/compare.py <lichess_convert> [--games N] [--seed S] [--pgn file.pgn ...]

Without --pgn it generates random games with the features a reader can get wrong: comments (several, multi-line,
evals valid and not, mates), NAGs and annotations, variations (nested, empty, with illegal moves), illegal and
ambiguous moves, null moves, FEN starts, castling, promotions, en passant, missing or '*' results, ratings that
aren't numbers, ';' and '%' lines. Each input is zstd-compressed, converted by both, and the outputs compared.
"""
import argparse
import os
import random
import subprocess
import sys
import tempfile

import chess
import chess.pgn
import zstandard

TRAINING = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
sys.path.insert(0, TRAINING)
import lichess  # noqa: E402

RECORD = 68


def eval_text(rng):
    kind = rng.random()
    if kind < 0.70:
        cp = rng.choice([rng.randint(-400, 400), rng.randint(-4000, 4000), 0])
        forms = [f"{cp / 100:.2f}", f"{cp / 100:.1f}", f"{cp // 100}", f"{cp / 100:+.2f}"]
        value = rng.choice(forms)
        if rng.random() < 0.05:
            value = value.replace("0.", ".", 1) if value.startswith(("0.", "-0.")) else value
        if rng.random() < 0.03:
            value = value + "."
    elif kind < 0.85:
        value = f"#{rng.choice([-1, 1]) * rng.randint(0, 12)}"
        if rng.random() < 0.3:
            value = value.replace("#", "#+") if not value.startswith("#-") else value
    else:
        value = rng.choice(["1.234", "abc", "", "12345678901", "1,", "0.5x", "--1", "#", "+"])
    depth = f",{rng.randint(1, 40)}" if rng.random() < 0.1 else ""
    space = rng.choice([" "] * 20 + ["  ", "\t", "\n"])
    return f"[%eval{space}{value}{depth}]"


def comment(rng):
    parts = []
    if rng.random() < 0.9:
        parts.append(eval_text(rng))
    if rng.random() < 0.5:
        parts.append(f"[%clk 0:0{rng.randint(0, 9)}:{rng.randint(10, 59)}]")
    if rng.random() < 0.1:
        parts.append(rng.choice(["Inaccuracy. Nf3 was best.", "Mistake.", "(x)", "1-0 ?", "$4"]))
    rng.shuffle(parts)
    body = " ".join(parts)
    if rng.random() < 0.05:
        body = body.replace(" ", "\n", 1)
    return "{ " + body + " }" if rng.random() < 0.9 else "{" + body + "}"


def random_move(rng, board):
    moves = list(board.legal_moves)
    if not moves:
        return None
    promotions = [m for m in moves if m.promotion]
    captures = [m for m in moves if board.is_capture(m)]
    r = rng.random()
    if promotions and r < 0.7:
        return rng.choice(promotions)
    if captures and r < 0.4:
        return rng.choice(captures)
    return rng.choice(moves)


def san_token(rng, board, move):
    san = board.san(move)
    r = rng.random()
    if r < 0.05:
        san = san.rstrip("+#")
    elif r < 0.07:
        san = move.uci()                      # fully specified
    elif r < 0.08 and san.startswith("O-O"):
        san = san.replace("O", "0")
    return san


def variation(rng, board, depth):
    """Alternative moves to the last move played on 'board'."""
    b = board.copy()
    b.pop()
    out = ["("]
    for i in range(rng.randint(0, 4)):
        move = random_move(rng, b)
        if move is None:
            break
        if b.turn == chess.WHITE or i == 0:
            out.append(f"{b.fullmove_number}{'.' if b.turn == chess.WHITE else '...'}")
        # No illegal moves here: python-chess then parses the mainline on the variation's board, and lichess.py
        # fails (an assertion in Converter), so a dump that converted can't have them.
        out.append(san_token(rng, b, move))
        b.push(move)
        if rng.random() < 0.5:
            out.append(comment(rng))
        if depth < 2 and rng.random() < 0.1 and b.move_stack:
            out.append(variation(rng, b, depth + 1))
    out.append(")")
    return " ".join(out)


def random_game(rng, index):
    board = chess.Board()
    headers = {"Event": f"Rated Blitz game {index}", "Site": "https://lichess.org/x"}
    if rng.random() < 0.05:
        for _ in range(rng.randint(5, 40)):
            move = random_move(rng, board)
            if move is None:
                break
            board.push(move)
        if board.is_game_over():
            board = chess.Board()
        else:
            headers["FEN"] = board.fen()
            headers["SetUp"] = "1"
            headers["Variant"] = "From Position"
            board = chess.Board(board.fen())
    for colour in ("WhiteElo", "BlackElo"):
        r = rng.random()
        if r < 0.9:
            headers[colour] = str(rng.randint(1500, 2700))
        elif r < 0.95:
            headers[colour] = rng.choice(["?", " 2100", "2_100", "+2200", "", "abc"])
    result = rng.choice(["1-0", "0-1", "1/2-1/2", "*"])
    if rng.random() > 0.03:
        headers["Result"] = result

    tokens = []
    if rng.random() < 0.05:
        tokens.append(comment(rng))
    length = rng.choice([rng.randint(0, 30), rng.randint(20, 120), rng.randint(100, 300)])
    for ply in range(length):
        move = random_move(rng, board)
        if move is None:
            break
        if board.turn == chess.WHITE or ply == 0 or tokens and tokens[-1].endswith(("}", ")")):
            tokens.append(f"{board.fullmove_number}{'.' if board.turn == chess.WHITE else '...'}")
        r = rng.random()
        if r < 0.002:
            tokens.append(rng.choice(["Qh8", "Ke7", "Nb5", "exd5", "O-O-O"]))   # often illegal here
        elif r < 0.003 and not board.is_check():      # (a null move in check lets the king be taken)
            tokens.append("--")
            board.push(chess.Move.null())
            continue
        tokens.append(san_token(rng, board, move))
        board.push(move)
        r = rng.random()
        if r < 0.03:
            tokens.append(rng.choice(["?", "??", "!", "!!", "!?", "?!"]))
        elif r < 0.05:
            tokens.append(f"${rng.randint(1, 20)}")
        if rng.random() < 0.85:
            tokens.append(comment(rng))
            if rng.random() < 0.05:
                tokens.append(comment(rng))
        if rng.random() < 0.03:
            tokens.append(variation(rng, board, 0) if rng.random() < 0.9 else "( )")
            if rng.random() < 0.5:
                tokens.append(comment(rng))
    tokens.append(rng.choice([result, result, result, "*", "1-0"]))

    lines = [f'[{k} "{v}"]' for k, v in headers.items()]
    if rng.random() < 0.02:
        lines.append("% escaped line")
    lines.append("")
    if rng.random() < 0.01:
        lines.append("")                      # two blank lines: python-chess sees no moves
    text = " ".join(tokens)
    if rng.random() < 0.2:                    # wrapped movetext
        words, cur, wrapped = text.split(" "), "", []
        for word in words:
            if len(cur) + len(word) > 79:
                wrapped.append(cur)
                cur = word
            else:
                cur = f"{cur} {word}" if cur else word
        wrapped.append(cur)
        text = "\n".join(wrapped)
    if rng.random() < 0.02:
        text += " ; a comment to the end of the line"
    lines.append(text)
    lines.append("")
    return "\n".join(lines) + "\n"


def python_convert(zst, out, args):
    converter = lichess.Converter(args.skip_plies, 1.0, args.min_elo, args.max_score, 1, True)
    return lichess.convert(zst, out, converter, args.workers, 0)


def compare(c_tool, pgn_text, args, label):
    with tempfile.TemporaryDirectory() as tmp:
        zst = os.path.join(tmp, "in.pgn.zst")
        with open(zst, "wb") as f:
            f.write(zstandard.ZstdCompressor(level=3).compress(pgn_text.encode()))
        py_out, c_out = os.path.join(tmp, "py.bin"), os.path.join(tmp, "c.bin")
        games, positions = python_convert(zst, py_out, args)
        result = subprocess.run([c_tool, "--min-elo", str(args.min_elo), "--skip-plies", str(args.skip_plies),
                                 "--max-score", str(args.max_score), zst, c_out],
                                capture_output=True, text=True)
        if result.returncode != 0:
            print(f"{label}: lichess_convert failed: {result.stderr}")
            return False
        a, b = open(py_out, "rb").read(), open(c_out, "rb").read()
        if a == b:
            print(f"{label}: identical, {games} games, {positions} positions ({result.stdout.strip()})")
            return True
        print(f"{label}: DIFFERENT: python {len(a)} bytes, C {len(b)} bytes ({result.stdout.strip()})")
        for i in range(8, min(len(a), len(b)), RECORD):
            if a[i:i + RECORD] != b[i:i + RECORD]:
                print(f"  first difference at record {(i - 8) // RECORD}")
                break
        if args.keep:
            for name in ("in.pgn.zst", "py.bin", "c.bin"):
                os.replace(os.path.join(tmp, name), os.path.join(args.keep, f"{label}-{name}"))
        return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("c_tool")
    parser.add_argument("--games", type=int, default=3000)
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--pgn", nargs="*", default=[])
    parser.add_argument("--skip-plies", type=int, default=4)
    parser.add_argument("--min-elo", type=int, default=2000)
    parser.add_argument("--max-score", type=int, default=3000)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--keep", default="")
    args = parser.parse_args()

    ok = True
    for path in args.pgn:
        with open(path, encoding="utf-8", errors="replace") as f:
            ok &= compare(args.c_tool, f.read(), args, os.path.basename(path))
    if not args.pgn:
        for r in range(args.rounds):
            rng = random.Random(args.seed * 1000 + r)
            text = "".join(random_game(rng, i) for i in range(args.games))
            for skip, elo in ((args.skip_plies, args.min_elo), (0, 0)):
                args.skip_plies, args.min_elo = skip, elo
                ok &= compare(args.c_tool, text, args, f"random-{args.seed}-{r}-skip{skip}-elo{elo}")
            args.skip_plies, args.min_elo = parser.get_default("skip_plies"), parser.get_default("min_elo")
    print("PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
