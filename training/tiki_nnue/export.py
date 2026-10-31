"""Quantisation and the network file format (engine/core/src/nnue/nnue.h), version 2:

    char magic[8] = "TIKINNUE", uint32 version, uint32 inputs, uint32 hidden,
    int16 ft_weights[inputs][hidden], int16 ft_bias[hidden],
    int16 out_weights[OUTPUT_BUCKETS][2 * hidden], int32 out_bias[OUTPUT_BUCKETS]

all little endian. Version 1 (before output buckets) has a single out_weights[2 * hidden] and out_bias;
read_network loads it as the same output layer in every bucket, which evaluates exactly as it did.
quantised_eval() is a reference for what the engine computes, integer arithmetic included, so the
export can be checked against tiki (tests/test_cross_check.py).
"""

import struct

import numpy as np

from .features import INPUTS, OUTPUT_BUCKETS, active_features, output_bucket, parse_fen
from .model import QA, QB, SCALE

MAGIC = b"TIKINNUE"
VERSION = 2
VERSION_SINGLE_OUTPUT = 1     # Before output buckets, still read.


def quantise(model):
    """Returns the integer network as numpy arrays."""
    def to_int(tensor, scale, dtype):
        values = np.rint(tensor.detach().cpu().double().numpy() * scale)
        info = np.iinfo(dtype)
        if values.min() < info.min or values.max() > info.max:
            raise ValueError(f"weights out of range for {dtype} after quantisation, clip them in training")
        return values.astype(dtype)

    return {
        "ft_weights": to_int(model.ft.weight, QA, np.int16),        # [INPUTS, HIDDEN]
        "ft_bias": to_int(model.ft_bias, QA, np.int16),
        "out_weights": to_int(model.out.weight, QB, np.int16),       # [OUTPUT_BUCKETS, 2 * HIDDEN]
        "out_bias": to_int(model.out.bias, QA * QB, np.int32),       # [OUTPUT_BUCKETS]
    }


def write_network(network, path):
    hidden = network["ft_bias"].shape[0]
    with open(path, "wb") as file:
        file.write(MAGIC)
        file.write(struct.pack("<III", VERSION, INPUTS, hidden))
        file.write(network["ft_weights"].astype("<i2").tobytes())
        file.write(network["ft_bias"].astype("<i2").tobytes())
        file.write(network["out_weights"].astype("<i2").tobytes())
        file.write(network["out_bias"].astype("<i4").tobytes())


def read_network(path):
    with open(path, "rb") as file:
        data = file.read()
    if data[:8] != MAGIC:
        raise ValueError("not a tiki network")
    version, inputs, hidden = struct.unpack_from("<III", data, 8)
    if version not in (VERSION, VERSION_SINGLE_OUTPUT) or inputs != INPUTS:
        raise ValueError(f"unsupported network: version {version}, {inputs} inputs")
    buckets = OUTPUT_BUCKETS if version == VERSION else 1
    offset = 20
    def take(count, dtype):
        nonlocal offset
        values = np.frombuffer(data, dtype=dtype, count=count, offset=offset)
        offset += count * np.dtype(dtype).itemsize
        return values
    network = {
        "ft_weights": take(inputs * hidden, "<i2").reshape(inputs, hidden).astype(np.int16),
        "ft_bias": take(hidden, "<i2").astype(np.int16),
        "out_weights": take(buckets * 2 * hidden, "<i2").reshape(buckets, 2 * hidden).astype(np.int16),
        "out_bias": take(buckets, "<i4").astype(np.int32),
    }
    if offset != len(data):
        raise ValueError("trailing data after the network")
    if buckets == 1:
        network["out_weights"] = np.repeat(network["out_weights"], OUTPUT_BUCKETS, axis=0)
        network["out_bias"] = np.repeat(network["out_bias"], OUTPUT_BUCKETS)
    return network


def dequantise(network):
    """A float model with exactly the quantised weights (e.g. to fine-tune an exported network)."""
    import torch
    from .model import TikiNNUE

    model = TikiNNUE(hidden=network["ft_bias"].shape[0])
    with torch.no_grad():
        model.ft.weight.copy_(torch.from_numpy(network["ft_weights"].astype(np.float64) / QA))
        model.ft_bias.copy_(torch.from_numpy(network["ft_bias"].astype(np.float64) / QA))
        model.out.weight.copy_(torch.from_numpy(network["out_weights"].astype(np.float64) / QB))
        model.out.bias.copy_(torch.from_numpy(network["out_bias"].astype(np.float64) / (QA * QB)))
    return model


def _trunc_div(a, b):
    """C integer division (truncates towards zero), Python's // floors."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def quantised_eval(network, fen):
    """The engine's evaluation of 'fen' (centipawns, side to move), same integer arithmetic as nnue.c."""
    pieces, side = parse_fen(fen)
    hidden = network["ft_bias"].shape[0]

    def accumulator(perspective):
        acc = network["ft_bias"].astype(np.int64).copy()
        for feature in active_features(pieces, perspective):
            acc += network["ft_weights"][feature]
        if acc.min() < -32768 or acc.max() > 32767:
            raise ValueError("accumulator overflows int16")
        return acc

    def screlu(acc):
        return np.clip(acc, 0, QA) ** 2

    bucket = output_bucket(len(pieces))
    out = network["out_weights"][bucket].astype(np.int64)
    total = int(np.dot(screlu(accumulator(side)), out[:hidden]) + np.dot(screlu(accumulator(side ^ 1)), out[hidden:]))
    output = _trunc_div(total, QA) + int(network["out_bias"][bucket])
    return _trunc_div(output * SCALE, QA * QB)


def export(model, path):
    write_network(quantise(model), path)
