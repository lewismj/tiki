"""Training data from the Lichess database (https://database.lichess.org, CC0).

Downloads monthly dumps of rated standard games and converts games analysed by
Lichess (those with Stockfish '[%eval ...]' annotations) to the trainer's format:

    <fen> | <score> | <result>

Score is in centipawns and result is 1.0 / 0.5 / 0.0, both from White's
point of view, matching 'tiki_app datagen'.

The scores are Stockfish's, so a network trained on them learns Stockfish's
evaluation (see the README).

Example:

    python lichess.py --download-dir /mnt/f/lichess \\
        --out-dir /mnt/f/lichess/data \\
        --months 2024-01:2024-03

A dump is 30 GB or more and is read without unpacking it. Downloads resume
if interrupted, and a month already converted is skipped, so the command can
be re-run.
"""

import argparse
import hashlib
import io
import multiprocessing
import os
import random
import re
import sys
import time
import urllib.request

import chess
import chess.pgn
import zstandard

from rich.console import Console
from rich.progress import (
    BarColumn,
    DownloadColumn,
    Progress,
    SpinnerColumn,
    TextColumn,
    TimeElapsedColumn,
    TimeRemainingColumn,
    TransferSpeedColumn,
)

from rich.table import Table


URL = (
    "https://database.lichess.org/standard/"
    "lichess_db_standard_rated_{month}.pgn.zst"
)

EVAL = "[%eval "

RESULTS = {
    "1-0": "1.0",
    "0-1": "0.0",
    "1/2-1/2": "0.5",
}

CONTENT_RANGE_RE = re.compile(r"bytes (\d+)-(\d+)/(\d+|\*)")

console = Console()


class CountingReader:
    """Wraps a binary reader and counts compressed bytes read."""

    def __init__(self, raw):
        self.raw = raw
        self.bytes_read = 0

    def read(self, size=-1):
        data = self.raw.read(size)
        self.bytes_read += len(data)
        return data

    def readable(self):
        return True


def months(specs):
    """'2024-01', or a range '2023-11:2024-02', to a list of 'YYYY-MM'."""
    out = []

    for spec in specs:
        first, separator, last = spec.partition(":")

        year, month = map(int, first.split("-"))
        end = tuple(map(int, (last or first).split("-")))

        if not 1 <= month <= 12:
            raise ValueError(f"Invalid month: {first}")

        if separator:
            end_year, end_month = end
            if not 1 <= end_month <= 12:
                raise ValueError(f"Invalid month: {last}")

            if (year, month) > (end_year, end_month):
                raise ValueError(f"Invalid month range: {spec}")

        while (year, month) <= end:
            out.append(f"{year:04d}-{month:02d}")

            if month == 12:
                year, month = year + 1, 1
            else:
                month += 1

    # Avoid processing the same month twice.
    return list(dict.fromkeys(out))


def download(month, directory):
    """Downloads or resumes a month's dump, returns its path."""
    path = os.path.join(directory, f"lichess_db_standard_rated_{month}.pgn.zst",)

    if os.path.exists(path):
        return path

    partial = path + ".part"
    done = os.path.getsize(partial) if os.path.exists(partial) else 0

    request = urllib.request.Request(
        URL.format(month=month),
        headers={"Range": f"bytes={done}-"} if done else {},
    )

    with urllib.request.urlopen(request) as response:
        status = response.status

        # Resume only if the server actually honoured the range request.
        if done:
            if status != 206:
                console.print(
                    f"[yellow]Server did not honour resume request "
                    f"for {month}; restarting download.[/yellow]"
                )
                done = 0
            else:
                content_range = response.headers.get("Content-Range", "")
                match = CONTENT_RANGE_RE.fullmatch(content_range)

                if not match or int(match.group(1)) != done:
                    raise RuntimeError(
                        f"Invalid Content-Range when resuming {month}: "
                        f"{content_range!r}"
                    )

        content_range = response.headers.get("Content-Range", "")
        match = CONTENT_RANGE_RE.fullmatch(content_range)

        if match and match.group(3) != "*":
            total = int(match.group(3))
        else:
            content_length = response.headers.get("Content-Length")
            total = (
                done + int(content_length)
                if content_length is not None
                else None
            )

        # If the server ignored Range, truncate rather than append.
        mode = "ab" if done else "wb"

        with open(partial, mode) as file:
            with Progress(
                TextColumn("{task.description}", markup=False),
                BarColumn(),
                DownloadColumn(),
                TransferSpeedColumn(),
                TimeRemainingColumn(),
                console=console,
            ) as progress:
                task = progress.add_task(
                    f"Downloading {month}",
                    total=total,
                    completed=done,
                )

                while chunk := response.read(1 << 20):
                    file.write(chunk)
                    progress.update(task, advance=len(chunk))

    os.replace(partial, path)
    return path


def evaluated_games(path, progress=None, task=None):
    """Yields PGN text for games containing evaluation annotations.

    Conversion progress is based on compressed bytes consumed. Zstandard
    buffers input, so this is an estimate rather than an exact game count.
    """
    total_bytes = os.path.getsize(path)
    last_update = time.monotonic()

    with open(path, "rb") as compressed:
        counter = CountingReader(compressed)
        reader = io.TextIOWrapper(
            zstandard.ZstdDecompressor().stream_reader(
                counter,
                read_size=1 << 20,
            ),
            encoding="utf-8",
            errors="replace",
        )

        game = []

        for line in reader:
            # A game starts at its first header line, after the previous game.
            if line.startswith("[Event ") and game:
                text = "".join(game)

                if EVAL in text:
                    yield text

                game = []

            game.append(line)

            # Refresh the progress bar periodically while scanning the dump.
            now = time.monotonic()
            if progress is not None and task is not None:
                if now - last_update >= 0.25:
                    progress.update(
                        task,
                        completed=min(counter.bytes_read, total_bytes),
                    )
                    last_update = now

        if game:
            text = "".join(game)

            if EVAL in text:
                yield text

        if progress is not None and task is not None:
            progress.update(task, completed=total_bytes)


class Converter:
    """Converts one game's PGN text to training data."""

    def __init__(
        self,
        skip_plies,
        sample,
        min_elo,
        max_score,
        seed,
    ):
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

        # Optional rating filter.
        if self.min_elo:
            try:
                white_elo = int(game.headers["WhiteElo"])
                black_elo = int(game.headers["BlackElo"])

                if min(white_elo, black_elo) < self.min_elo:
                    return []

            except (KeyError, ValueError):
                return []

        # Stable sampling across runs and worker counts.
        game_hash = hashlib.blake2b(
            text.encode("utf-8"),
            digest_size=16,
        ).hexdigest()

        rng = random.Random(f"{self.seed}:{game_hash}")

        lines = []
        node = game
        ply = 0

        while node.variations:
            node = node.variations[0]
            ply += 1

            score = node.eval()

            if score is None:
                continue

            if ply <= self.skip_plies:
                continue

            # Require a following move so we can filter captures/promotions.
            if not node.variations:
                continue

            # Convert to White's perspective.
            # Mate scores return None and are skipped.
            cp = score.white().score()

            if cp is None or abs(cp) > self.max_score:
                continue

            board = node.board()
            following = node.variations[0].move

            # Quiet positions only, as in datagen.
            if board.is_check():
                continue

            if board.is_capture(following) or following.promotion:
                continue

            if rng.random() >= self.sample:
                continue

            lines.append(f"{board.fen()} | {cp} | {result}\n")

        return lines


def convert(path, out_path, converter, workers, max_positions):
    """Converts a dump to training data, using a .part file until complete."""
    partial = out_path + ".part"

    games = 0
    positions = 0
    start = time.time()
    total_bytes = os.path.getsize(path)

    with open(partial, "w", encoding="utf-8") as out:
        with multiprocessing.Pool(workers) as pool:
            with Progress(
                SpinnerColumn(),
                TextColumn("{task.description}", markup=False),
                BarColumn(),
                TextColumn("{task.percentage:>3.0f}%"),
                TimeElapsedColumn(),
                TimeRemainingColumn(),
                console=console,
            ) as progress:
                task = progress.add_task(f"Converting {os.path.basename(path)}",total=total_bytes,)

                try:
                    # The main process owns the progress bar and compressed
                    # reader. Workers convert individual games in parallel.
                    for lines in pool.imap(converter, evaluated_games(path, progress, task), chunksize=64,):
                        games += 1
                        out.writelines(lines)
                        positions += len(lines)

                        elapsed = max(time.time() - start, 1.0)
                        rate = positions / elapsed

                        # Update the description periodically to limit
                        # terminal overhead.
                        if games % 2000 == 0:
                            progress.update(
                                task,
                                description=(
                                    f"Converting {os.path.basename(path)} "
                                    f"· {games:,} games "
                                    f"· {positions:,} positions "
                                    f"· {rate:,.0f} positions/s"
                                ),
                            )

                        if max_positions and positions >= max_positions:
                            pool.terminate()
                            break

                    else:
                        pool.close()

                except BaseException:
                    pool.terminate()
                    raise

                finally:
                    pool.join()

                progress.update(
                    task,
                    description=(
                        f"Converted {os.path.basename(path)} "
                        f"· {games:,} games "
                        f"· {positions:,} positions"
                    ),
                )

    os.replace(partial, out_path)
    return games, positions


def print_summary(results):
    """Displays a month-by-month summary."""
    table = Table(title="Lichess Training Data")

    table.add_column("Month")
    table.add_column("Status")
    table.add_column("Games", justify="right")
    table.add_column("Positions", justify="right")

    for month, status, games, positions in results:
        table.add_row(
            month,
            status,
            f"{games:,}" if games is not None else "-",
            f"{positions:,}" if positions is not None else "-",
        )

    console.print()
    console.print(table)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,)

    parser.add_argument("--download-dir", required=True, help="where the .pgn.zst dumps go (30+ GB each)", )
    parser.add_argument("--out-dir", required=True, help="where the converted data goes", )
    parser.add_argument("--months", nargs="+", required=True, help="YYYY-MM, or ranges YYYY-MM:YYYY-MM", )
    parser.add_argument("--skip-plies", type=int, default=16, help="opening plies to skip (book moves)", )
    parser.add_argument("--sample", type=float, default=0.5, help="fraction of remaining positions to keep", )
    parser.add_argument("--min-elo", type=int, default=0, help="skip games where either player is rated lower", )
    parser.add_argument("--max-score", type=int, default=3000, help="skip positions scored above this (centipawns)", )
    parser.add_argument("--max-positions", type=int, default=0, help="stop each month after this many (0: all)", )
    parser.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) - 1), help="number of conversion worker processes", )
    parser.add_argument("--delete-dumps", action="store_true", help="delete each dump after converting it", )
    parser.add_argument("--seed", type=int, default=1, help="deterministic sampling seed", )

    args = parser.parse_args()

    if args.skip_plies < 0:
        parser.error("--skip-plies must be >= 0")

    if not 0.0 <= args.sample <= 1.0:
        parser.error("--sample must be between 0.0 and 1.0")

    if args.min_elo < 0:
        parser.error("--min-elo must be >= 0")

    if args.max_score < 0:
        parser.error("--max-score must be >= 0")

    if args.max_positions < 0:
        parser.error("--max-positions must be >= 0")

    if args.workers < 1:
        parser.error("--workers must be >= 1")

    try:
        requested_months = months(args.months)
    except ValueError as error:
        parser.error(str(error))

    os.makedirs(args.download_dir, exist_ok=True)
    os.makedirs(args.out_dir, exist_ok=True)

    converter = Converter(args.skip_plies, args.sample, args.min_elo, args.max_score, args.seed,)

    results = []

    console.print()
    console.print("[bold]Tiki · Lichess training data[/bold]")
    console.print(f"Months: {', '.join(requested_months)}")
    console.print(f"Workers: {args.workers}")
    console.print(f"Sampling: {args.sample:.0%}")
    console.print()

    for month in requested_months:
        out_path = os.path.join(
            args.out_dir,
            f"lichess-{month}.txt",
        )

        if os.path.exists(out_path):
            console.print(f"[dim]{month}: already converted[/dim]")
            results.append((month, "Skipped", None, None))
            continue

        try:
            console.print(f"[bold cyan]{month}[/bold cyan]")

            dump = download(month, args.download_dir)

            console.print(f"[green]Download complete: {dump}[/green]")

            games, positions = convert(
                dump,
                out_path,
                converter,
                args.workers,
                args.max_positions,
            )

            console.print(f"[green]Conversion complete: " f"{games:,} games, {positions:,} positions[/green]")

            results.append((month, "Complete", games, positions))

            if args.delete_dumps:
                os.remove(dump)
                console.print(f"[dim]Deleted dump: {dump}[/dim]")

        except KeyboardInterrupt:
            console.print(
                "\n[yellow]Interrupted. Partial download/conversion retained."
                "[/yellow]"
            )
            results.append((month, "Interrupted", None, None))
            print_summary(results)
            return 130

        except Exception as error:
            console.print(f"[red]{month}: failed: {error}[/red]")
            results.append((month, "Failed", None, None))
            print_summary(results)
            return 1

    print_summary(results)


if __name__ == "__main__":
    multiprocessing.freeze_support()
    sys.exit(main())