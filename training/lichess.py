"""Training data from the Lichess database (https://database.lichess.org, CC0).

Downloads the monthly dumps of rated standard games and converts the games that Lichess analysed
(they have a Stockfish '[%eval ...]' after every move) to the trainer's format:

    <fen> | <score> | <result>

with the score in centipawns and the result 1.0 / 0.5 / 0.0, both from white's point of view, the
same as 'tiki_app datagen'. The scores are Stockfish's, so a network trained on them learns
Stockfish's evaluation (see the README).

    python3 lichess.py --download-dir /mnt/f/lichess --out-dir /mnt/f/lichess/data --months 2024-01:2024-03

A dump is 30 GB or more and is read without unpacking it. Downloads resume if interrupted, and a
month already converted is skipped, so the command can be re-run.
"""

import argparse
import io
import multiprocessing
import os
import random
import sys
import time
import urllib.request

import chess
import chess.pgn
import zstandard

URL = "https://database.lichess.org/standard/lichess_db_standard_rated_{month}.pgn.zst"
EVAL = "[%eval "
RESULTS = {"1-0": "1.0", "0-1": "0.0", "1/2-1/2": "0.5"}


def months(specs):
    """'2024-01', or a range '2023-11:2024-02', to a list of 'YYYY-MM'."""
    out = []
    for spec in specs:
        first, _, last = spec.partition(":")
        year, month = map(int, first.split("-"))
        end = tuple(map(int, (last or first).split("-")))
        while (year, month) <= end:
            out.append(f"{year:04d}-{month:02d}")
            year, month = (year + 1, 1) if month == 12 else (year, month + 1)
    return out


def download(month, directory):
    """Downloads (or resumes) a month's dump, returns its path."""
    path = os.path.join(directory, f"lichess_db_standard_rated_{month}.pgn.zst")
    if os.path.exists(path):
        return path
    partial = path + ".part"
    done = os.path.getsize(partial) if os.path.exists(partial) else 0

    request = urllib.request.Request(URL.format(month=month), headers={"Range": f"bytes={done}-"} if done else {})
    with urllib.request.urlopen(request) as response:
        if done and response.status != 206:     # The server ignored the range, start again.
            done = 0
        total = done + int(response.headers.get("Content-Length", 0))
        with open(partial, "ab" if done else "wb") as file:
            start, last = time.time(), 0.0
            while chunk := response.read(1 << 20):
                file.write(chunk)
                done += len(chunk)
                if time.time() - last > 10:
                    last = time.time()
                    rate = (done / (1 << 20)) / max(last - start, 1)
                    print(f"  {month}: {done >> 30} / {total >> 30} GB, {rate:.0f} MB/s", flush=True)
    os.replace(partial, path)
    return path


def evaluated_games(path):
    """Yields the PGN text of each game that has evals (most games don't, they're skipped unparsed)."""
    with open(path, "rb") as compressed:
        reader = io.TextIOWrapper(zstandard.ZstdDecompressor().stream_reader(compressed), encoding="utf-8",
                                  errors="replace")
        game = []
        for line in reader:
            # A game starts at its first header line, after the previous game's moves.
            if line.startswith("[Event ") and game:
                text = "".join(game)
                if EVAL in text:
                    yield text
                game = []
            game.append(line)
        if game and EVAL in "".join(game):
            yield "".join(game)


class Converter:
    """Converts one game's PGN text to data lines. A class so the settings reach the worker processes."""

    def __init__(self, skip_plies, sample, min_elo, max_score, seed):
        self.skip_plies = skip_plies
        self.sample = sample
        self.min_elo = min_elo
        self.max_score = max_score
        self.seed = seed

    def __call__(self, text):
        game = chess.pgn.read_game(io.StringIO(text))
        if game is None:
            return []
        result = RESULTS.get(game.headers.get("Result"))
        if result is None:
            return []
        if self.min_elo:
            try:
                if min(int(game.headers["WhiteElo"]), int(game.headers["BlackElo"])) < self.min_elo:
                    return []
            except (KeyError, ValueError):
                return []

        # Per-game generator, so the sample doesn't depend on which worker gets the game.
        rng = random.Random(f"{self.seed}:{game.headers.get('Site', '')}")
        lines = []
        node = game
        ply = 0
        # The eval on a node is for the position after its move, the next move is the one played from it.
        while node.variations:
            node = node.variations[0]
            ply += 1
            score = node.eval()
            if score is None or ply <= self.skip_plies or not node.variations:
                continue
            cp = score.white().score()              # None for a mate score, those are skipped.
            if cp is None or abs(cp) > self.max_score:
                continue
            board = node.board()
            following = node.variations[0].move
            # Quiet positions only, as datagen: not in check, the move played isn't a capture or promotion.
            if board.is_check() or board.is_capture(following) or following.promotion:
                continue
            if rng.random() >= self.sample:
                continue
            lines.append(f"{board.fen()} | {cp} | {result}\n")
        return lines


def convert(path, out_path, converter, workers, max_positions):
    """Converts a dump to 'out_path' (written to a .part file, renamed when done). Returns the counts."""
    partial = out_path + ".part"
    games = positions = 0
    start = time.time()
    with open(partial, "w") as out, multiprocessing.Pool(workers) as pool:
        for lines in pool.imap(converter, evaluated_games(path), chunksize=64):
            games += 1
            out.writelines(lines)
            positions += len(lines)
            if games % 20000 == 0:
                print(f"  {games} games, {positions} positions, {positions / (time.time() - start):.0f}/s",
                      flush=True)
            if max_positions and positions >= max_positions:
                pool.terminate()
                break
    os.replace(partial, out_path)
    return games, positions


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--download-dir", required=True, help="where the .pgn.zst dumps go (30+ GB each)")
    parser.add_argument("--out-dir", required=True, help="where the converted data goes")
    parser.add_argument("--months", nargs="+", required=True, help="YYYY-MM, or ranges YYYY-MM:YYYY-MM")
    parser.add_argument("--skip-plies", type=int, default=16, help="opening plies to skip (book moves)")
    parser.add_argument("--sample", type=float, default=0.5,
                        help="fraction of the remaining positions to keep (neighbouring positions are similar)")
    parser.add_argument("--min-elo", type=int, default=0, help="skip games where either player is rated lower")
    parser.add_argument("--max-score", type=int, default=3000, help="skip positions scored above this (centipawns)")
    parser.add_argument("--max-positions", type=int, default=0, help="stop each month after this many (0: all)")
    parser.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    parser.add_argument("--delete-dumps", action="store_true", help="delete each dump after converting it")
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()

    os.makedirs(args.download_dir, exist_ok=True)
    os.makedirs(args.out_dir, exist_ok=True)
    converter = Converter(args.skip_plies, args.sample, args.min_elo, args.max_score, args.seed)

    for month in months(args.months):
        out_path = os.path.join(args.out_dir, f"lichess-{month}.txt")
        if os.path.exists(out_path):
            print(f"{month}: already converted ({out_path})")
            continue
        print(f"{month}: downloading to {args.download_dir}", flush=True)
        dump = download(month, args.download_dir)
        print(f"{month}: converting", flush=True)
        games, positions = convert(dump, out_path, converter, args.workers, args.max_positions)
        print(f"{month}: {games} evaluated games, {positions} positions -> {out_path}", flush=True)
        if args.delete_dumps:
            os.remove(dump)


if __name__ == "__main__":
    sys.exit(main())
