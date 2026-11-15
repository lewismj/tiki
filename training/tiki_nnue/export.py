"""Quantisation and the network file format (engine/core/src/nnue/nnue.h), version 7:

    char magic[8] = "TIKINNUE", uint32 version, uint32 inputs (TOTAL_INPUTS), uint32 hidden,
    int16 ft_weights[INPUTS][hidden] (the piece features), int8 pair_weights[PAIRS][hidden] (the pawn pairs,
    clipped to PAIR_CLIP, 127 at QA), int16 ft_bias[hidden],
    int8  l1_weights[OUTPUT_BUCKETS][L1][hidden], int32 l1_bias[OUTPUT_BUCKETS][L1],
    int8  l2_weights[OUTPUT_BUCKETS][L2][2 * L1], int32 l2_bias[OUTPUT_BUCKETS][L2],
    int16 out_weights[OUTPUT_BUCKETS][OUT_INPUTS], int32 out_bias[OUTPUT_BUCKETS]

all little endian. Scales: the accumulator QA, the hidden layers' weights QW and their sums QH (1.0), their
activations 0..127 (1.0 = 127: CReLU is sum >> 6, SqrCReLU (sum * sum) >> 19 of the sum clamped to 0..QH),
the output layer's weights QO and its bias 127 * QO. The output is out_sum / (127 * QO) plus the skip
(l1 sums 14 - 15) / QH, computed as (out_sum * 64 + skip * QO) / (QH * QO), times SCALE.

Version 7 added PawnPairs (features.py) and the second hidden layer. Earlier versions (6: one hidden layer,
512 -> 16 -> 1; 5: the same at a coarser output scale; 1-4 older still) are a different network and aren't
read. quantised_eval() is a reference for what the engine computes, integer arithmetic included, so the
export can be checked against tiki (tests/test_cross_check.py).
"""

import struct

import numpy as np

from .features import INPUTS, OUTPUT_BUCKETS, PAIRS, TOTAL_INPUTS, all_features, parse_fen, position_bucket
from .model import L1, L2, OUT_INPUTS, QA, QH, QO, QW, SCALE, SKIP

MAGIC = b"TIKINNUE"
VERSION = 7
QU = 127                    # Hidden layer activations, 0..127 (u8 in the engine): 1.0 is 127.


def quantise(model):
    """Returns the integer network as numpy arrays, the shared rows and layers folded in."""
    def to_int(tensor, scale, dtype):
        values = np.rint(tensor.detach().cpu().double().numpy() * scale)
        info = np.iinfo(dtype)
        if values.min() < info.min or values.max() > info.max:
            raise ValueError(f"weights out of range for {dtype} after quantisation, clip them in training")
        return values.astype(dtype)

    weights = model.feature_weights()
    return {
        "ft_weights": to_int(weights[:INPUTS], QA, np.int16),            # [INPUTS, hidden]
        "pair_weights": to_int(weights[INPUTS:], QA, np.int8),           # [PAIRS, hidden]
        "ft_bias": to_int(model.ft_bias, QA, np.int16),                   # [hidden]
        "l1_weights": to_int(model.l1_weights(), QW, np.int8),            # [OUTPUT_BUCKETS, L1, hidden]
        "l1_bias": to_int(model.l1_biases(), QH, np.int32),               # [OUTPUT_BUCKETS, L1]
        "l2_weights": to_int(model.l2_weight, QW, np.int8),               # [OUTPUT_BUCKETS, L2, 2 * L1]
        "l2_bias": to_int(model.l2_bias, QH, np.int32),                   # [OUTPUT_BUCKETS, L2]
        "out_weights": to_int(model.out_weight, QO, np.int16),            # [OUTPUT_BUCKETS, OUT_INPUTS]
        "out_bias": to_int(model.out_bias, QU * QO, np.int32),            # [OUTPUT_BUCKETS]
    }


# The arrays of a network in file order: name, shape (hidden for the accumulator width), file dtype.
def _layout(hidden):
    return [("ft_weights", (INPUTS, hidden), "<i2"), ("pair_weights", (PAIRS, hidden), "i1"), ("ft_bias", (hidden,), "<i2"),
            ("l1_weights", (OUTPUT_BUCKETS, L1, hidden), "i1"), ("l1_bias", (OUTPUT_BUCKETS, L1), "<i4"),
            ("l2_weights", (OUTPUT_BUCKETS, L2, 2 * L1), "i1"), ("l2_bias", (OUTPUT_BUCKETS, L2), "<i4"),
            ("out_weights", (OUTPUT_BUCKETS, OUT_INPUTS), "<i2"), ("out_bias", (OUTPUT_BUCKETS,), "<i4")]


def write_network(network, path):
    hidden = network["ft_bias"].shape[0]
    with open(path, "wb") as file:
        file.write(MAGIC)
        file.write(struct.pack("<III", VERSION, TOTAL_INPUTS, hidden))
        for name, shape, dtype in _layout(hidden):
            if network[name].shape != shape:
                raise ValueError(f"{name} is {network[name].shape}, the format has {shape}")
            file.write(network[name].astype(dtype).tobytes())


def read_network(path):
    """The network at 'path' as numpy arrays (version 7 only)."""
    with open(path, "rb") as file:
        data = file.read()
    if data[:8] != MAGIC:
        raise ValueError("not a tiki network")
    version, inputs, hidden = struct.unpack_from("<III", data, 8)
    if version != VERSION or inputs != TOTAL_INPUTS:
        raise ValueError(f"unsupported network: version {version}, {inputs} inputs (this trainer reads version "
                         f"{VERSION}, {TOTAL_INPUTS} inputs)")
    offset = 20

    def take(shape, dtype):
        nonlocal offset
        count = int(np.prod(shape))
        values = np.frombuffer(data, dtype=dtype, count=count, offset=offset)
        offset += count * np.dtype(dtype).itemsize
        return values.reshape(shape)

    network = {name: take(shape, dtype).astype(np.dtype(dtype).newbyteorder("="))
               for name, shape, dtype in _layout(hidden)}
    if offset != len(data):
        raise ValueError("trailing data after the network")
    return network


def dequantise(network):
    """A float model with exactly the quantised weights (e.g. to fine-tune an exported network): the
    first hidden layer as the buckets' own layers, the shared one zero."""
    import torch
    from .model import TikiNNUE

    hidden = network["ft_bias"].shape[0]
    model = TikiNNUE(hidden=hidden)

    def f64(array, scale):
        return torch.from_numpy(array.astype(np.float64) / scale)

    with torch.no_grad():
        model.ft.weight[:INPUTS].copy_(f64(network["ft_weights"], QA))
        model.ft.weight[INPUTS:].copy_(f64(network["pair_weights"], QA))
        model.ft_bias.copy_(f64(network["ft_bias"], QA))
        model.l1.weight.copy_(f64(network["l1_weights"], QW).reshape(OUTPUT_BUCKETS * L1, hidden))
        model.l1.bias.copy_(f64(network["l1_bias"], QH).reshape(OUTPUT_BUCKETS * L1))
        model.l1_shared.weight.zero_()
        model.l1_shared.bias.zero_()
        model.l2_weight.copy_(f64(network["l2_weights"], QW))
        model.l2_bias.copy_(f64(network["l2_bias"], QH))
        model.out_weight.copy_(f64(network["out_weights"], QO))
        model.out_bias.copy_(f64(network["out_bias"], QU * QO))
    return model


def _trunc_div(a, b):
    """C integer division (truncates towards zero), Python's // floors."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def hidden_inputs(accumulator):
    """The engine's hidden layer inputs from one perspective's int accumulator (hidden values, hidden / 2
    inputs): the pairwise product of the clamped halves, (x * y) >> 9 with x, y in 0..QA, rounded down,
    0..127 (u8 in the engine, which computes it as mulhi(x << 7, y))."""
    clamped = np.clip(accumulator.astype(np.int64), 0, QA)
    half = len(clamped) // 2
    return (clamped[:half] * clamped[half:]) >> 9


def activations(sums):
    """A hidden layer's int sums (scale QH) to its u8 activations, CReLU then SqrCReLU, as the engine: the
    sum clamped to 0..QH, then >> 6 (0..127) and (x * x) >> 19 (0..126)."""
    x = np.clip(sums.astype(np.int64), 0, QH)
    return np.concatenate([x >> 6, (x * x) >> 19])


def stack_output(network, a, bucket):
    """The layer stack of 'bucket' on the hidden layer inputs 'a' (0..127), in centipawns."""
    h0 = network["l1_weights"][bucket].astype(np.int64) @ a + network["l1_bias"][bucket].astype(np.int64)
    a0 = activations(h0)
    h1 = network["l2_weights"][bucket].astype(np.int64) @ a0 + network["l2_bias"][bucket].astype(np.int64)
    a1 = activations(h1)
    out = int(np.concatenate([a0, a1]) @ network["out_weights"][bucket].astype(np.int64)) + int(network["out_bias"][bucket])
    skip = int(h0[SKIP[0]] - h0[SKIP[1]])
    return _trunc_div((out * (QH // QU) + skip * QO) * SCALE, QH * QO)


def quantised_eval(network, fen):
    """The engine's evaluation of 'fen' (centipawns, side to move), same integer arithmetic as nnue.c."""
    pieces, side = parse_fen(fen)

    def accumulator(perspective):
        acc = network["ft_bias"].astype(np.int64).copy()
        for feature in all_features(pieces, perspective):
            acc += network["ft_weights"][feature] if feature < INPUTS else network["pair_weights"][feature - INPUTS]
        if acc.min() < -32768 or acc.max() > 32767:
            raise ValueError("accumulator overflows int16")
        return acc

    bucket = position_bucket(pieces, side)
    a = np.concatenate([hidden_inputs(accumulator(side)), hidden_inputs(accumulator(side ^ 1))])
    return stack_output(network, a, bucket)


def export(model, path):
    write_network(quantise(model), path)
