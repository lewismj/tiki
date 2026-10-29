#!/usr/bin/env python3
"""Averages tiki networks (.nnue) into one, weight by weight.

    python3 scripts/nnue_average.py a.nnue b.nnue [...] -o out.nnue [--weights 2 1 ...]

Only nets from the same training lineage average well: checkpoints of one run, or nets fine-tuned from the same
parent. Independently trained nets learn their hidden units in different orders, and their average plays badly.

The format (engine/core/src/nnue/nnue.c, little endian):
    char magic[8] = "TIKINNUE", uint32 version, uint32 inputs, uint32 hidden,
    int16 ft_weights[inputs * hidden], int16 ft_bias[hidden], int16 out_weights[2 * hidden], int32 out_bias.
"""

import argparse
import sys

import numpy as np

MAGIC = b"TIKINNUE"
VERSION = 1
HEADER = np.dtype([("magic", "S8"), ("version", "<u4"), ("inputs", "<u4"), ("hidden", "<u4")])


def read_net(path):
    """Returns ((version, inputs, hidden), [arrays in file order]), exiting on anything that isn't a tiki net."""
    data = open(path, "rb").read()
    if len(data) < HEADER.itemsize:
        sys.exit(f"{path}: too short to be a network")
    header = np.frombuffer(data, HEADER, count=1)[0]
    if header["magic"] != MAGIC or header["version"] != VERSION:
        sys.exit(f"{path}: not a version {VERSION} tiki network")
    inputs, hidden = int(header["inputs"]), int(header["hidden"])

    layout = [("<i2", inputs * hidden), ("<i2", hidden), ("<i2", 2 * hidden), ("<i4", 1)]
    expected = HEADER.itemsize + sum(np.dtype(dtype).itemsize * count for dtype, count in layout)
    if len(data) != expected:
        sys.exit(f"{path}: {len(data)} bytes, expected {expected} for {inputs} inputs and {hidden} hidden")

    arrays, at = [], HEADER.itemsize
    for dtype, count in layout:
        arrays.append(np.frombuffer(data, dtype, count=count, offset=at))
        at += np.dtype(dtype).itemsize * count
    return (VERSION, inputs, hidden), arrays


def main():
    parser = argparse.ArgumentParser(description="Average tiki networks (.nnue) weight by weight.")
    parser.add_argument("nets", nargs="+", help="the networks to average")
    parser.add_argument("-o", "--output", required=True, help="the network to write")
    parser.add_argument("--weights", nargs="+", type=float,
                        help="a weight per network (default equal), normalised to sum to 1")
    args = parser.parse_args()

    weights = args.weights or [1.0] * len(args.nets)
    if len(weights) != len(args.nets):
        sys.exit(f"{len(weights)} weights for {len(args.nets)} networks")
    if any(w <= 0 for w in weights):
        sys.exit("weights must be positive")
    weights = np.array(weights) / sum(weights)

    nets = [read_net(path) for path in args.nets]
    shape = nets[0][0]
    for path, (other, _) in zip(args.nets, nets):
        if other != shape:
            sys.exit(f"{path}: {other[1]} inputs and {other[2]} hidden, {args.nets[0]} has {shape[1]} and {shape[2]}")

    # Average in float64 (exact for these integers), round to nearest and clip to the stored type.
    with open(args.output, "wb") as out:
        np.array([(MAGIC, *shape)], HEADER).tofile(out)
        for i, first in enumerate(nets[0][1]):
            total = sum(w * net[1][i].astype(np.float64) for w, net in zip(weights, nets))
            limits = np.iinfo(first.dtype)
            np.clip(np.rint(total), limits.min, limits.max).astype(first.dtype).tofile(out)

    names = ", ".join(f"{path} ({w:.3f})" for path, w in zip(args.nets, weights))
    print(f"Wrote {args.output} from {names}")


if __name__ == "__main__":
    main()
