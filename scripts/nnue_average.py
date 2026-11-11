#!/usr/bin/env python3
"""Averages training checkpoints (.pt) weight by weight and exports the result as a tiki network (.nnue).

    python3 scripts/nnue_average.py a.pt b.pt [...] -o out.nnue [--weights 2 1 ...] [--checkpoint out.pt]

The weights are averaged in float, before quantisation, so the net is rounded once (as the trainer's own export
does) rather than averaging already rounded and clipped integers. --checkpoint also saves the averaged weights as a
checkpoint (model weights only, no optimiser state) for later averaging or exporting.

Only checkpoints from the same training lineage average well: epochs of one run, or nets fine-tuned from the same
parent. Independently trained nets learn their hidden units in different orders, and their average plays badly.
The checkpoints must all have the same architecture (--hidden and --factoriser); the shared factoriser rows are
averaged like any other weight and folded in on export, which is the same as folding first (it's linear).
Needs the trainer's environment (PyTorch, numpy).
"""

import argparse
import os
import sys

import torch

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "training"))

from tiki_nnue.export import export  # noqa: E402
from tiki_nnue.model import TikiNNUE  # noqa: E402


def read_state(path):
    """A checkpoint's model weights (a trainer checkpoint, or one this script saved)."""
    checkpoint = torch.load(path, map_location="cpu", weights_only=True)
    state = checkpoint.get("model", checkpoint) if isinstance(checkpoint, dict) else None
    if not isinstance(state, dict) or "ft.weight" not in state or "l1.weight" not in state:
        sys.exit(f"{path}: not a checkpoint of this trainer's network (format version 5)")
    return state


def average(states, weights):
    """The weighted average of state dicts with the same keys and shapes, in float64, returned as float32."""
    keys = list(states[0])
    for i, state in enumerate(states[1:], 1):
        if list(state) != keys:
            raise ValueError(f"checkpoint {i + 1} has other layers than checkpoint 1 (--factoriser differs?)")
        for key in keys:
            if state[key].shape != states[0][key].shape:
                raise ValueError(f"checkpoint {i + 1}'s {key} is {tuple(state[key].shape)}, "
                                 f"checkpoint 1's {tuple(states[0][key].shape)} (--hidden differs?)")
    return {key: sum(w * state[key].double() for w, state in zip(weights, states)).float() for key in keys}


def model_from_state(state):
    """A TikiNNUE holding 'state', its width and factoriser taken from the weights."""
    model = TikiNNUE(hidden=state["ft.weight"].shape[1], factoriser="factor.weight" in state)
    model.load_state_dict(state)
    return model


def main():
    parser = argparse.ArgumentParser(description="Average training checkpoints (.pt) into a tiki network.")
    parser.add_argument("checkpoints", nargs="+", help="the checkpoints (.pt) to average")
    parser.add_argument("-o", "--output", required=True, help="the network (.nnue) to write")
    parser.add_argument("--weights", nargs="+", type=float,
                        help="a weight per checkpoint (default equal), normalised to sum to 1")
    parser.add_argument("--checkpoint", help="also save the averaged weights to this .pt")
    args = parser.parse_args()

    for path in args.checkpoints:
        if not path.endswith(".pt"):
            parser.error(f"{path}: expected a training checkpoint (.pt); the .nnue beside it is already quantised")
    weights = args.weights or [1.0] * len(args.checkpoints)
    if len(weights) != len(args.checkpoints):
        parser.error(f"{len(weights)} weights for {len(args.checkpoints)} checkpoints")
    if any(w <= 0 for w in weights):
        parser.error("weights must be positive")
    weights = [w / sum(weights) for w in weights]

    try:
        state = average([read_state(path) for path in args.checkpoints], weights)
    except ValueError as error:
        sys.exit(str(error))
    model = model_from_state(state)
    export(model, args.output)
    if args.checkpoint:
        torch.save({"model": state}, args.checkpoint)

    names = ", ".join(f"{path} ({w:.3f})" for path, w in zip(args.checkpoints, weights))
    print(f"Wrote {args.output} from {names}")


if __name__ == "__main__":
    main()
