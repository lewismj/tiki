"""Rescales the scores of training data so every file is on the trainer's scale (s = 400).

    python3 rescale.py /mnt/f/lichess2/data-2000-s100-tb /mnt/f/lichess2/data-2000-s100-tb-s400
    python3 rescale.py --fit-only /mnt/f/lichess2/data-2000-s100-tb

The trainer turns a score into a win probability as sigmoid(score / 400) (model.SCALE). How scores
relate to results varies between sources and over time: Lichess's evaluations (Elo 2000+, relabelled)
fit s = 331-338 for most months of 2024-08 to 2026-07, 305-316 in 2024-03 to 2024-07 and 395-429 from
2026-08 (score_scale.py, rescale.py --fit-only). So for each file, the s that best fits
result ~ sigmoid(score / s) is found from a sample of its positions (as score_scale.py does), and
every score multiplied by --scale-to / s, rounded and kept within int16. The pieces, results and side
to move are copied unchanged.

--scale-to 400 makes the scores match the trainer's sigmoid exactly, but it also changes the network's
eval scale, which the search's margins are tuned to. Rescaling to the scale most of the data already
has (e.g. --scale-to 335 for Lichess) removes the differences between months and keeps the eval scale.

IN and OUT are files, or directories (every .bin in IN; .part files of a conversion still running
are left out). Each output is written via a .part file. Outputs that exist are skipped unless
--force, and IN and OUT may not be the same file. A table shows each file's fitted s, the factor,
and s fitted again on the output (400 if it went right).
"""

import argparse
import os
import time

import numpy as np
from rich.console import Console
from rich.table import Table

from score_scale import fit_scale, sample
from tiki_nnue import binary
from tiki_nnue.model import SCALE

CHUNK = 1 << 22   # Records per write, about 285 MB.


def fitted_scale(path, positions, seed):
    """s fitted on a sample of 'path' (the same sample for the same seed and file size)."""
    records = sample([path], positions, np.random.default_rng(seed))
    if len(records) == 0:
        raise ValueError(f"{path}: no records")
    return fit_scale(records["score"].astype(np.int32), records["result"] / 2.0)


def rescale_scores(scores, factor):
    """int16 scores multiplied by 'factor', rounded to the nearest and clamped as binary.encode does."""
    return np.clip(np.rint(scores.astype(np.float64) * factor), -32767, 32767).astype("<i2")


def rescale_file(source, target, factor):
    """Writes 'source' to 'target' (via .part) with every score multiplied by 'factor'. Returns the
    number of records."""
    count = binary.count(source)
    records = np.memmap(source, dtype=binary.RECORD, mode="r", offset=len(binary.MAGIC), shape=(count,))
    partial = target + ".part"
    with open(partial, "wb") as out:
        out.write(binary.MAGIC)
        for start in range(0, count, CHUNK):
            chunk = np.array(records[start:start + CHUNK])
            chunk["score"] = rescale_scores(chunk["score"], factor)
            out.write(chunk.tobytes())
    del records
    os.replace(partial, target)
    return count


def bin_files(path):
    if os.path.isdir(path):
        return sorted(os.path.join(path, name) for name in os.listdir(path) if name.endswith(".bin"))
    return [path]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", help="a .bin file or a directory of them")
    parser.add_argument("target", nargs="?", help="the output file or directory (not needed with --fit-only)")
    parser.add_argument("--scale-to", type=float, default=SCALE, help="the s the output should fit")
    parser.add_argument("--positions", type=int, default=1_000_000, help="positions sampled to fit each file's s")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--fit-only", action="store_true", help="show each file's fitted s and factor, write nothing")
    parser.add_argument("--force", action="store_true", help="rewrite outputs that already exist")
    args = parser.parse_args()
    if args.positions < 1000:
        parser.error("--positions must be at least 1000")
    if not args.fit_only and args.target is None:
        parser.error("a target is needed (or --fit-only)")

    sources = bin_files(args.source)
    if not sources:
        parser.error(f"no .bin files in {args.source}")
    if os.path.isdir(args.source) and args.target is not None:
        os.makedirs(args.target, exist_ok=True)
    elif args.target is not None and os.path.isdir(args.target):
        parser.error("a single file's target must be a file")

    console = Console()
    table = Table(title=f"Scores rescaled to s = {args.scale_to:g}")
    for column in ("File", "Records", "Fitted s", "Factor", "s after", "Time"):
        table.add_column(column, justify="left" if column == "File" else "right")

    for source in sources:
        name = os.path.basename(source)
        s = fitted_scale(source, args.positions, args.seed)
        factor = args.scale_to / s
        if args.fit_only:
            table.add_row(name, f"{binary.count(source):,}", f"{s:.0f}", f"{factor:.3f}", "-", "-")
            continue

        target = os.path.join(args.target, name) if os.path.isdir(args.target) else args.target
        if os.path.exists(target) and os.path.realpath(target) == os.path.realpath(source):
            parser.error(f"{source}: the target is the same file")
        if os.path.exists(target) and not args.force:
            console.print(f"{name}: {target} exists, skipped (--force rewrites it)")
            table.add_row(name, f"{binary.count(source):,}", f"{s:.0f}", f"{factor:.3f}", "skipped", "-")
            continue

        start = time.time()
        count = rescale_file(source, target, factor)
        after = fitted_scale(target, args.positions, args.seed)
        seconds = time.time() - start
        console.print(f"{name}: {count:,} records, s {s:.0f} x {factor:.3f} -> {after:.0f}, {seconds:.0f}s")
        table.add_row(name, f"{count:,}", f"{s:.0f}", f"{factor:.3f}", f"{after:.0f}", f"{seconds:.0f}s")

    console.print(table)


if __name__ == "__main__":
    main()
