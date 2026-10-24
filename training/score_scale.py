"""Compares how the scores of different training data sets relate to their game results, to check
that they're on the same scale before training on them together.

    python3 score_scale.py /mnt/f/lichess/data-s015 /mnt/f/lc0/data

Each argument is a data set: a .bin file or a directory of them. For each, a random sample of
positions is read and:
- the scale s that best fits result ~ sigmoid(score / s) is found. The trainer's target assumes
  s = 400 (model.SCALE), so a data set whose s is far from 400 trains the net towards evals that
  are too large (s > 400) or too small (s < 400) for what the positions are worth;
- the share of each result and the spread of the scores are shown;
- the average result by score band is shown, side by side, for a direct comparison.

Two data sets with similar s can be trained on together; if they differ by more than 10-20%, one's
scores should be rescaled first (multiplied by 400 / s).
"""

import argparse
import os

import numpy as np
from rich.console import Console
from rich.table import Table

from tiki_nnue import binary
from tiki_nnue.model import SCALE
from tiki_nnue.stream import data_files

BANDS = [-3000, -800, -400, -200, -100, -50, -20, 20, 50, 100, 200, 400, 800, 3001]


def sample(paths, positions, rng):
    """About 'positions' random records spread over the .bin files in 'paths', in proportion to
    their sizes."""
    files = [path for path in data_files(paths) if path.endswith(".bin")]
    counts = np.array([binary.count(path) for path in files])
    total = int(counts.sum())
    if total == 0:
        return np.zeros(0, dtype=binary.RECORD)
    parts = []
    for path, count in zip(files, counts):
        k = min(int(count), int(round(positions * count / total)))
        if k == 0:
            continue
        records = np.memmap(path, dtype=binary.RECORD, mode="r", offset=len(binary.MAGIC), shape=(int(count),))
        parts.append(np.array(records[np.sort(rng.choice(int(count), k, replace=False))]))
    return np.concatenate(parts) if parts else np.zeros(0, dtype=binary.RECORD)


def fit_scale(score, result):
    """The s minimising the cross entropy of result (0 / 0.5 / 1) against sigmoid(score / s)."""
    score = score.astype(np.float64)
    result = result.astype(np.float64)

    def loss(log_s):
        p = 1.0 / (1.0 + np.exp(-score / np.exp(log_s)))
        p = np.clip(p, 1e-9, 1 - 1e-9)
        return -np.mean(result * np.log(p) + (1 - result) * np.log(1 - p))

    # Golden section search on log(s), the loss is unimodal in s.
    a, b = np.log(20.0), np.log(5000.0)
    g = (np.sqrt(5) - 1) / 2
    c, d = b - g * (b - a), a + g * (b - a)
    fc, fd = loss(c), loss(d)
    for _ in range(80):
        if fc < fd:
            b, d, fd = d, c, fc
            c = b - g * (b - a)
            fc = loss(c)
        else:
            a, c, fc = c, d, fd
            d = a + g * (b - a)
            fd = loss(d)
    return float(np.exp((a + b) / 2))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("data", nargs="+", help="data sets: .bin files or directories of them")
    parser.add_argument("--positions", type=int, default=1_000_000, help="positions sampled from each data set")
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()

    console = Console()
    rng = np.random.default_rng(args.seed)
    names = [os.path.basename(os.path.normpath(path)) or path for path in args.data]
    samples = []
    for name, path in zip(names, args.data):
        records = sample([path], args.positions, rng)
        if len(records) == 0:
            parser.error(f"no .bin data in {path}")
        samples.append((records["score"].astype(np.int32), records["result"] / 2.0))
        console.print(f"{name}: {len(records):,} positions sampled")

    summary = Table(title=f"Score scale (the trainer assumes s = {SCALE})")
    for column in ("Data", "Fitted s", "s / 400", "Wins", "Draws", "Losses", "Mean |score|", "|score| 90%"):
        summary.add_column(column, justify="left" if column == "Data" else "right")
    for name, (score, result) in zip(names, samples):
        s = fit_scale(score, result)
        summary.add_row(name, f"{s:.0f}", f"{s / SCALE:.2f}",
                        f"{np.mean(result == 1):.1%}", f"{np.mean(result == 0.5):.1%}", f"{np.mean(result == 0):.1%}",
                        f"{np.mean(np.abs(score)):.0f}", f"{np.percentile(np.abs(score), 90):.0f}")
    console.print(summary)

    bands = Table(title="Average result for the side to move, by score (positions in the band)")
    bands.add_column("Score", justify="right")
    for name in names:
        bands.add_column(name, justify="right")
    bands.add_column(f"sigmoid(score/{SCALE})", justify="right")
    for low, high in zip(BANDS, BANDS[1:]):
        row = [f"{low} .. {high - 1}"]
        for score, result in samples:
            mask = (score >= low) & (score < high)
            row.append(f"{np.mean(result[mask]):.3f} ({np.mean(mask):.1%})" if mask.any() else "-")
        middle = (low + high - 1) / 2
        row.append(f"{1 / (1 + np.exp(-middle / SCALE)):.3f}")
        bands.add_row(*row)
    console.print(bands)


if __name__ == "__main__":
    main()
