"""Quantisation and the network file format (engine/core/src/nnue/nnue.h), version 5:

    char magic[8] = "TIKINNUE", uint32 version, uint32 inputs, uint32 hidden,
    int16 ft_weights[inputs][hidden], int16 ft_bias[hidden],
    int8  l1_weights[OUTPUT_BUCKETS][L1][hidden], int32 l1_bias[OUTPUT_BUCKETS][L1],
    int16 out_weights[OUTPUT_BUCKETS][L1], int32 out_bias[OUTPUT_BUCKETS]

all little endian. Earlier versions (1, 2: an output layer straight from the accumulator; 3: PSQT;
4: a hidden layer on SCReLU inputs, never released) are a different network and aren't read. quantised_eval() is a reference for what the engine
computes, integer arithmetic included, so the export can be checked against tiki
(tests/test_cross_check.py).
"""

import struct

import numpy as np

from .features import INPUTS, OUTPUT_BUCKETS, active_features, parse_fen, position_bucket
from .model import L1, QA, QH, QW, SCALE

MAGIC = b"TIKINNUE"
VERSION = 5


def quantise(model):
    """Returns the integer network as numpy arrays, the shared rows and layers folded in."""
    def to_int(tensor, scale, dtype):
        values = np.rint(tensor.detach().cpu().double().numpy() * scale)
        info = np.iinfo(dtype)
        if values.min() < info.min or values.max() > info.max:
            raise ValueError(f"weights out of range for {dtype} after quantisation, clip them in training")
        return values.astype(dtype)

    return {
        "ft_weights": to_int(model.feature_weights(), QA, np.int16),     # [INPUTS, hidden]
        "ft_bias": to_int(model.ft_bias, QA, np.int16),                   # [hidden]
        "l1_weights": to_int(model.l1_weights(), QW, np.int8),            # [OUTPUT_BUCKETS, L1, hidden]
        "l1_bias": to_int(model.l1_biases(), QH, np.int32),               # [OUTPUT_BUCKETS, L1]
        "out_weights": to_int(model.out.weight, QW, np.int16),            # [OUTPUT_BUCKETS, L1]
        "out_bias": to_int(model.out.bias, QH * QW, np.int32),            # [OUTPUT_BUCKETS]
    }


def write_network(network, path):
    hidden = network["ft_bias"].shape[0]
    with open(path, "wb") as file:
        file.write(MAGIC)
        file.write(struct.pack("<III", VERSION, INPUTS, hidden))
        file.write(network["ft_weights"].astype("<i2").tobytes())
        file.write(network["ft_bias"].astype("<i2").tobytes())
        file.write(network["l1_weights"].astype("i1").tobytes())
        file.write(network["l1_bias"].astype("<i4").tobytes())
        file.write(network["out_weights"].astype("<i2").tobytes())
        file.write(network["out_bias"].astype("<i4").tobytes())


def read_network(path):
    with open(path, "rb") as file:
        data = file.read()
    if data[:8] != MAGIC:
        raise ValueError("not a tiki network")
    version, inputs, hidden = struct.unpack_from("<III", data, 8)
    if version != VERSION or inputs != INPUTS:
        raise ValueError(f"unsupported network: version {version}, {inputs} inputs (this trainer reads version {VERSION})")
    offset = 20

    def take(shape, dtype):
        nonlocal offset
        count = int(np.prod(shape))
        values = np.frombuffer(data, dtype=dtype, count=count, offset=offset)
        offset += count * np.dtype(dtype).itemsize
        return values.reshape(shape)

    network = {
        "ft_weights": take((inputs, hidden), "<i2").astype(np.int16),
        "ft_bias": take((hidden,), "<i2").astype(np.int16),
        "l1_weights": take((OUTPUT_BUCKETS, L1, hidden), "i1").astype(np.int8),
        "l1_bias": take((OUTPUT_BUCKETS, L1), "<i4").astype(np.int32),
        "out_weights": take((OUTPUT_BUCKETS, L1), "<i2").astype(np.int16),
        "out_bias": take((OUTPUT_BUCKETS,), "<i4").astype(np.int32),
    }
    if offset != len(data):
        raise ValueError("trailing data after the network")
    return network


def dequantise(network):
    """A float model with exactly the quantised weights (e.g. to fine-tune an exported network): the
    hidden layers as the buckets' own layers, the shared one zero."""
    import torch
    from .model import TikiNNUE

    hidden = network["ft_bias"].shape[0]
    model = TikiNNUE(hidden=hidden)

    def f64(array, scale):
        return torch.from_numpy(array.astype(np.float64) / scale)

    with torch.no_grad():
        model.ft.weight.copy_(f64(network["ft_weights"], QA))
        model.ft_bias.copy_(f64(network["ft_bias"], QA))
        model.l1.weight.copy_(f64(network["l1_weights"], QW).reshape(OUTPUT_BUCKETS * L1, hidden))
        model.l1.bias.copy_(f64(network["l1_bias"], QH).reshape(OUTPUT_BUCKETS * L1))
        model.l1_shared.weight.zero_()
        model.l1_shared.bias.zero_()
        model.out.weight.copy_(f64(network["out_weights"], QW))
        model.out.bias.copy_(f64(network["out_bias"], QH * QW))
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


def quantised_eval(network, fen):
    """The engine's evaluation of 'fen' (centipawns, side to move), same integer arithmetic as nnue.c."""
    pieces, side = parse_fen(fen)

    def accumulator(perspective):
        acc = network["ft_bias"].astype(np.int64).copy()
        for feature in active_features(pieces, perspective):
            acc += network["ft_weights"][feature]
        if acc.min() < -32768 or acc.max() > 32767:
            raise ValueError("accumulator overflows int16")
        return acc

    bucket = position_bucket(pieces, side)
    a = np.concatenate([hidden_inputs(accumulator(side)), hidden_inputs(accumulator(side ^ 1))])
    h = network["l1_weights"][bucket].astype(np.int64) @ a + network["l1_bias"][bucket].astype(np.int64)
    h = np.clip(h, 0, QH)
    output = int(h @ network["out_weights"][bucket].astype(np.int64)) + int(network["out_bias"][bucket])
    return _trunc_div(output * SCALE, QH * QW)


def export(model, path):
    write_network(quantise(model), path)
