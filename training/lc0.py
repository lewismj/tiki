"""Training data from Leela Chess Zero's self-play games (https://storage.lczero.org/files/training_data,
Open Database License, see the README).

Each run (test80, ...) publishes a .tar of games about every hour, 1-2 GB each. This streams the
tars and converts their positions to the trainer's binary format (tiki_nnue/binary.py), one file per
tar, lc0-<run>-<YYYYMMDD>-<HHMM>.bin, so nothing needs to be stored but the output (--download-dir
keeps the tars too, and reads them from there on a re-run). A tar already converted is skipped, so
the command can be re-run, and an interrupted one is started again.

    python3 lc0.py --out-dir /mnt/f/lc0/data --run test80 --dates 2024-04-01:2024-04-30 --per-day 2

Positions are lc0 training records (V6, one .gz file of them per game), from the side to move's
point of view: that side's pieces are written as White's, with White to move, which is the same
position for tiki's colour-symmetric network. As in datagen and lichess.py only quiet positions are
kept (not in check, lc0's best move not a capture or promotion). The score is lc0's search value of
its best move (best_q, or --q root / played), converted from Q to centipawns on the trainer's scale
(q_to_centipawns), and the result is the game's.
"""

import argparse
import gzip
import hashlib
import multiprocessing
import os
import re
import sys
import tarfile
import time
import urllib.error
import urllib.request
from datetime import date, timedelta

import chess
import numpy as np
from rich.console import Console
from rich.progress import (BarColumn, DownloadColumn, Progress, SpinnerColumn, TextColumn, TimeElapsedColumn,
                           TimeRemainingColumn, TransferSpeedColumn)
from rich.table import Table

from tiki_nnue import binary

BASE_URL = "https://storage.lczero.org/files/training_data"
# The server (behind Cloudflare) refuses Python's default User-Agent.
USER_AGENT = "tiki-training/1.0 (+https://github.com/lewismj/tiki)"
CONTENT_RANGE_RE = re.compile(r"bytes (\d+)-(\d+)/(\d+|\*)")
TAR_RE = re.compile(r'href="(training-run\d+-([\w.-]+?)-(\d{8})-(\d{4})\.tar)"')

# lc0's V6TrainingData (src/trainingdata/trainingdata_v6.h), packed, little-endian.
V6 = np.dtype([
    ("version", "<u4"), ("input_format", "<u4"), ("probabilities", "<f4", (1858,)), ("planes", "<u8", (104,)),
    ("castling", "u1", (4,)), ("side_to_move_or_enpassant", "u1"), ("rule50_count", "u1"),
    ("invariance_info", "u1"), ("dummy", "u1"),
    ("root_q", "<f4"), ("best_q", "<f4"), ("root_d", "<f4"), ("best_d", "<f4"), ("root_m", "<f4"),
    ("best_m", "<f4"), ("plies_left", "<f4"), ("result_q", "<f4"), ("result_d", "<f4"),
    ("played_q", "<f4"), ("played_d", "<f4"), ("played_m", "<f4"),
    ("orig_q", "<f4"), ("orig_d", "<f4"), ("orig_m", "<f4"),
    ("visits", "<u4"), ("played_idx", "<u2"), ("best_idx", "<u2"), ("policy_kld", "<f4"), ("q_st", "<f4"),
])
assert V6.itemsize == 8356
DELETED = 1 << 6            # invariance_info: marked for deletion by the rescorer.

# Each byte of a plane is stored bit reversed (lc0 writes ReverseBitsInBytes(mask)).
REVERSE_BITS = np.array([int(f"{i:08b}"[::-1], 2) for i in range(256)], dtype=np.uint8)
RANKS_1_AND_8 = 0xFF000000000000FF


def policy_moves():
    """lc0's 1858 policy outputs as (from, to) squares (a1 = 0), from the side to move's point of view:
    every queen or knight move by from and then to square, then the pawn promotions to the last rank
    as q, r, b for each (a promotion without a letter is a knight's)."""
    moves = []
    for frm in range(64):
        for to in range(64):
            df, dr = abs((to & 7) - (frm & 7)), abs((to >> 3) - (frm >> 3))
            if frm != to and (df == 0 or dr == 0 or df == dr or {df, dr} == {1, 2}):
                moves.append((frm, to))
    for frm in range(48, 56):
        for to in range(56, 64):
            if abs((to & 7) - (frm & 7)) <= 1:
                moves.extend([(frm, to)] * 3)
    return moves


MOVES = policy_moves()
assert len(MOVES) == 1858


SCALE = 400     # tiki_nnue/model.py's SCALE (not imported, it would load torch in every worker).


def q_to_centipawns(q):
    """A Q value (-1..1, wins - losses for the side to move) as centipawns on the trainer's scale:
    the score whose sigmoid(score / SCALE) is the expected result (1 + q) / 2, so the trainer reads
    it back as the win probability lc0's search gave. lc0's own display conversion
    (111.7 * tan(1.562 * q)) gives about a third of that, which trained nets towards evals far too
    small for what the positions are worth (see score_scale.py)."""
    q = np.clip(q, -0.999, 0.999)
    return SCALE * np.log((1 + q) / (1 - q))


def squares(mask):
    while mask:
        low = mask & -mask
        yield low.bit_length() - 1
        mask ^= low


def decode(data, name, skip_plies, sample, max_score, q_field, seed):
    """One game's V6 records (the .gz file's bytes, uncompressed) as binary.RECORDs, with the counts
    (records, kept, rejected as not a valid position). 'name' seeds the sampling with 'seed'."""
    records = np.frombuffer(data, dtype=V6, count=len(data) // V6.itemsize)
    count = len(records)
    rng = np.random.default_rng([seed, int.from_bytes(hashlib.blake2b(name.encode(), digest_size=8).digest(), "little")])
    q = records[q_field].astype(np.float64)
    cp = q_to_centipawns(q)
    candidate = ((np.arange(count) >= skip_plies) & (records["version"] == 6) & np.isfinite(q)
                 & (np.abs(cp) <= max_score) & ((records["invariance_info"] & DELETED) == 0)
                 & np.isfinite(records["result_q"]) & (rng.random(count) < sample))
    planes = REVERSE_BITS[np.ascontiguousarray(records["planes"][:, :12]).view(np.uint8)].view("<u8").reshape(count, 12)

    out = np.zeros(int(candidate.sum()), dtype=binary.RECORD)
    out["pieces"] = binary.PAD
    kept = rejected = 0
    board = chess.Board(None)
    for i in np.flatnonzero(candidate):
        masks = [int(m) for m in planes[i]]          # Ours P N B R Q K, then theirs, a1 = 0.
        ours, theirs = 0, 0
        for m in masks[:6]:
            ours |= m
        for m in masks[6:]:
            theirs |= m
        if (masks[5].bit_count() != 1 or masks[11].bit_count() != 1 or (masks[0] | masks[6]) & RANKS_1_AND_8
                or ours & theirs or ours.bit_count() > 16 or theirs.bit_count() > 16):
            rejected += 1
            continue

        # Quiet: lc0's best move isn't a capture (en passant is a pawn moving diagonally to an empty
        # square) or promotion. Castling is the king moving to its own rook's square.
        frm, to = MOVES[records["best_idx"][i]]
        target = 1 << to
        if not (ours >> frm) & 1:
            rejected += 1
            continue
        if target & theirs:
            continue
        if (masks[0] >> frm) & 1 and ((frm & 7) != (to & 7) or to >= 56):
            continue

        # Not in check.
        board.pawns, board.knights, board.bishops = masks[0] | masks[6], masks[1] | masks[7], masks[2] | masks[8]
        board.rooks, board.queens, board.kings = masks[3] | masks[9], masks[4] | masks[10], masks[5] | masks[11]
        board.occupied_co[chess.WHITE], board.occupied_co[chess.BLACK] = ours, theirs
        board.occupied = ours | theirs
        board.turn = chess.WHITE
        if board.is_check():
            continue

        # Ours are White's pieces (tiki's 0-5), theirs Black's (6-11). Tiki's squares are a8 = 0.
        pieces = [piece * 64 + (sq ^ 56) for piece in range(12) for sq in squares(masks[piece])]
        out["pieces"][kept, :len(pieces)] = pieces
        out["score"][kept] = int(round(float(cp[i])))
        out["result"][kept] = int(np.clip(np.rint(records["result_q"][i]), -1, 1)) + 1
        out["side"][kept] = 0
        kept += 1
    return out[:kept], (count, kept, rejected)


class Converter:
    """decode() for a pool: (name, gzip bytes) to (records, counts)."""

    def __init__(self, skip_plies, sample, max_score, q_field, seed):
        self.skip_plies, self.sample, self.max_score, self.q_field, self.seed = skip_plies, sample, max_score, q_field, seed

    def __call__(self, item):
        name, compressed = item
        try:
            data = gzip.decompress(compressed)
        except (OSError, EOFError):
            return np.zeros(0, dtype=binary.RECORD), (0, 0, 1)
        if len(data) % V6.itemsize:
            return np.zeros(0, dtype=binary.RECORD), (0, 0, 1)    # Not V6 (another version's record size).
        return decode(data, name, self.skip_plies, self.sample, self.max_score, self.q_field, self.seed)


class CountingReader:
    """A file-like reader that counts the bytes read (for progress)."""

    def __init__(self, raw):
        self.raw, self.bytes_read = raw, 0

    def read(self, size=-1):
        data = self.raw.read(size)
        self.bytes_read += len(data)
        return data


def games(fileobj, prefix=""):
    """(name, bytes) of each game (.gz member) of a tar, read as a stream. The name is prefixed with
    'prefix' (the tar's), as the member names repeat from tar to tar."""
    with tarfile.open(fileobj=fileobj, mode="r|") as tar:
        for member in tar:
            if member.isfile() and member.name.endswith(".gz"):
                yield prefix + member.name, tar.extractfile(member).read()


def fetch(url, headers=None):
    """An open HTTP response for 'url'."""
    return urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": USER_AGENT, **(headers or {})}))


def list_tars(run):
    """(file name, date, time) of the run's tars, from the server's directory listing."""
    with fetch(f"{BASE_URL}/{run}/") as response:
        listing = response.read().decode("utf-8", errors="replace")
    tars = {match.group(1): (match.group(3), match.group(4)) for match in TAR_RE.finditer(listing)}
    return sorted((name, day, hour) for name, (day, hour) in tars.items())


def days(spec):
    """'2024-04-01', or a range '2024-04-01:2024-04-30', as a set of 'YYYYMMDD'."""
    first, _, last = spec.partition(":")
    start, end = date.fromisoformat(first), date.fromisoformat(last or first)
    if start > end:
        raise ValueError(f"Invalid date range: {spec}")
    return {(start + timedelta(days=i)).strftime("%Y%m%d") for i in range((end - start).days + 1)}


def choose(tars, wanted_days, per_day):
    """The tars on 'wanted_days', at most 'per_day' of each day spread evenly over it (0: all)."""
    chosen = []
    for day in sorted(wanted_days):
        today = [tar for tar in tars if tar[1] == day]
        if per_day and len(today) > per_day:
            today = [today[round(i * (len(today) - 1) / max(1, per_day - 1))] for i in range(per_day)] \
                if per_day > 1 else [today[len(today) // 2]]
        chosen.extend(today)
    return chosen


def download(url, path, label):
    """Downloads 'url' to 'path' via a .part file, resuming a partial download, with a progress bar
    (removed when it's done, the conversion's stays)."""
    partial = path + ".part"
    done = os.path.getsize(partial) if os.path.exists(partial) else 0
    try:
        response = fetch(url, {"Range": f"bytes={done}-"} if done else {})
    except urllib.error.HTTPError as error:
        # Nothing left to download: the .part file is the whole tar.
        if error.code == 416 and done:
            os.replace(partial, path)
            return
        raise

    with response:
        content_range = CONTENT_RANGE_RE.fullmatch(response.headers.get("Content-Range", ""))
        # Resume only if the server honoured the range request, otherwise start again.
        if done:
            if response.status != 206:
                console.print(f"[yellow]The server didn't resume {label}, downloading it again.[/yellow]")
                done = 0
            elif not content_range or int(content_range.group(1)) != done:
                raise RuntimeError(f"Invalid Content-Range resuming {label}: {response.headers.get('Content-Range')!r}")

        if content_range and content_range.group(3) != "*":
            total = int(content_range.group(3))
        else:
            length = response.headers.get("Content-Length")
            total = done + int(length) if length is not None else None

        with open(partial, "ab" if done else "wb") as file, Progress(
                TextColumn("{task.description}", markup=False), BarColumn(), DownloadColumn(),
                TransferSpeedColumn(), TimeRemainingColumn(), console=console, transient=True) as progress:
            task = progress.add_task(f"Downloading {label}", total=total, completed=done)
            while chunk := response.read(1 << 20):
                file.write(chunk)
                progress.update(task, advance=len(chunk))
    os.replace(partial, path)


def open_tar(run, name, download_dir, label):
    """A stream of the tar: from --download-dir if it's there (downloading it first, resuming a
    partial download), otherwise straight from the server. Returns (file, size)."""
    url = f"{BASE_URL}/{run}/{name}"
    if not download_dir:
        response = fetch(url)
        return response, int(response.headers.get("Content-Length") or 0)
    path = os.path.join(download_dir, name)
    if not os.path.exists(path):
        download(url, path, label)
    return open(path, "rb"), os.path.getsize(path)


def downloaded(tars, download_dir):
    """The tars (as from choose()) already downloaded in full to 'download_dir' (a .part file, a
    download still to finish, doesn't count)."""
    return [tar for tar in tars if os.path.isfile(os.path.join(download_dir, tar[0]))]


def convert(fileobj, size, out_path, converter, workers, label):
    """Converts one tar to a binary data file (via a .part file). Returns (games, records, kept, rejected)."""
    partial = out_path + ".part"
    totals = [0, 0, 0, 0]
    reader = CountingReader(fileobj)
    with open(partial, "wb") as out, multiprocessing.Pool(workers) as pool, Progress(
            SpinnerColumn(), TextColumn("{task.description}", markup=False), BarColumn(), DownloadColumn(),
            TransferSpeedColumn(), TimeElapsedColumn(), console=console) as progress:
        out.write(binary.MAGIC)
        task = progress.add_task(label, total=size or None)
        for records, (count, kept, rejected) in pool.imap(converter, games(reader, label + "/"), chunksize=16):
            out.write(records.tobytes())
            totals[0] += 1
            totals[1] += count
            totals[2] += kept
            totals[3] += rejected
            if totals[0] % 256 == 0:
                progress.update(task, completed=reader.bytes_read,
                                description=f"{label} · {totals[0]:,} games · {totals[2]:,} positions")
        progress.update(task, completed=reader.bytes_read)
    os.replace(partial, out_path)
    return tuple(totals)


console = Console()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out-dir", required=True, help="where the converted data goes")
    parser.add_argument("--run", default="test80", help="training run (a directory of the server's training_data)")
    parser.add_argument("--dates", required=True, nargs="+", help="YYYY-MM-DD, or ranges YYYY-MM-DD:YYYY-MM-DD")
    parser.add_argument("--per-day", type=int, default=2, help="tars per day, spread over it (0: all, about 24)")
    parser.add_argument("--download-dir", help="keep the tars here (1-2 GB each) rather than only streaming them")
    parser.add_argument("--skip-missing", action="store_true",
                        help="only convert tars already in --download-dir, download nothing")
    parser.add_argument("--skip-plies", type=int, default=16, help="opening plies to skip (temperature moves)")
    parser.add_argument("--sample", type=float, default=0.25, help="fraction of positions to keep")
    parser.add_argument("--max-score", type=int, default=3000, help="skip positions scored above this (centipawns)")
    parser.add_argument("--q", choices=("best", "root", "played"), default="best",
                        help="lc0's value for the score: its best move's, the root's or the played move's")
    parser.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    parser.add_argument("--seed", type=int, default=1, help="deterministic sampling seed")
    args = parser.parse_args()
    if args.skip_plies < 0 or args.max_score < 0 or args.per_day < 0 or args.workers < 1:
        parser.error("--skip-plies, --max-score and --per-day must be >= 0, --workers >= 1")
    if not 0.0 < args.sample <= 1.0:
        parser.error("--sample must be in (0, 1]")
    if args.skip_missing and not args.download_dir:
        parser.error("--skip-missing needs --download-dir")
    try:
        wanted = set().union(*(days(spec) for spec in args.dates))
    except ValueError as error:
        parser.error(str(error))

    os.makedirs(args.out_dir, exist_ok=True)
    if args.download_dir:
        os.makedirs(args.download_dir, exist_ok=True)
    console.print("[bold]Tiki · lc0 training data[/bold]")
    tars = choose(list_tars(args.run), wanted, args.per_day)
    console.print(f"Run {args.run}: {len(tars)} tar(s) on {len(wanted)} day(s), sampling {args.sample:.0%}, "
                  f"score from {args.q}_q, {args.workers} workers")
    if args.skip_missing:
        local = downloaded(tars, args.download_dir)
        console.print(f"{len(local)} of them downloaded to {args.download_dir}, "
                      f"skipping the other {len(tars) - len(local)}")
        tars = local
    if not tars:
        console.print("[yellow]No tars on those dates.[/yellow]")
        return 1

    converter = Converter(args.skip_plies, args.sample, args.max_score, f"{args.q}_q", args.seed)
    table = Table(title="lc0 training data")
    for column in ("Tar", "Status", "Games", "Records", "Positions", "Rejected"):
        table.add_column(column, justify="left" if column in ("Tar", "Status") else "right")
    status = 0
    for name, day, hour in tars:
        out_path = os.path.join(args.out_dir, f"lc0-{args.run}-{day}-{hour}.bin")
        if os.path.exists(out_path):
            table.add_row(name, "Skipped", "-", "-", f"{binary.count(out_path):,}", "-")
            continue
        try:
            start = time.time()
            fileobj, size = open_tar(args.run, name, args.download_dir, f"{day}-{hour}")
            with fileobj:
                game_count, records, kept, rejected = convert(fileobj, size, out_path, converter, args.workers,
                                                              f"{day}-{hour}")
            console.print(f"[green]{name}: {game_count:,} games, {kept:,} of {records:,} positions kept "
                          f"({rejected:,} rejected), {time.time() - start:.0f}s[/green]")
            table.add_row(name, "Complete", f"{game_count:,}", f"{records:,}", f"{kept:,}", f"{rejected:,}")
            # Many rejected records means the records aren't being read correctly: stop rather than
            # write bad data.
            if rejected > 0.01 * max(records, 1):
                console.print(f"[red]{name}: {rejected:,} records rejected, is this run's data V6?[/red]")
                status = 1
                break
        except KeyboardInterrupt:
            console.print("\n[yellow]Interrupted, the current tar will be converted again next time.[/yellow]")
            status = 130
            break
        except Exception as error:  # noqa: BLE001 - reported, then on to the summary
            console.print(f"[red]{name}: failed: {error}[/red]")
            table.add_row(name, "Failed", "-", "-", "-", "-")
            status = 1
            break
    console.print(table)
    return status


if __name__ == "__main__":
    multiprocessing.freeze_support()
    sys.exit(main())
