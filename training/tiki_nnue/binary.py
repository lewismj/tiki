"""Binary training data: the 'fen | score | result' text, parsed once into fixed-size records.

A file is MAGIC followed by records of RECORD (68 bytes, little-endian):

    pieces  uint16[32]  piece * 64 + square (tiki's order and index, see features.py), PAD after the last
    score   int16       centipawns for the side to move, clamped to the int16 range
    result  uint8       2 / 1 / 0 for a win / draw / loss for the side to move
    side    uint8       side to move (WHITE / BLACK)

Training computes the features of a whole batch at once from these (data.batch_tensors), so it
never parses FENs. This module needs only numpy, so lichess.py's workers can write records directly.
"""

import multiprocessing
import os

import numpy as np

from .features import BLACK, MAX_ACTIVE, parse_fen

MAGIC = b"TIKIBIN1"
PAD = 0xFFFF
RECORD = np.dtype([("pieces", "<u2", (MAX_ACTIVE,)), ("score", "<i2"), ("result", "u1"), ("side", "u1")])
LINES_PER_CHUNK = 1 << 16


def encode(lines):
    """Records for 'fen | score | result' lines (white's point of view, as datagen writes them).
    Blank lines are skipped."""
    lines = [line for line in lines if line.strip()]
    records = np.zeros(len(lines), dtype=RECORD)
    records["pieces"] = PAD
    for i, line in enumerate(lines):
        fen, score, result = (field.strip() for field in line.split("|"))
        pieces, side = parse_fen(fen)
        score, result = float(score), float(result)
        if side == BLACK:
            score, result = -score, 1.0 - result
        records["pieces"][i, :len(pieces)] = [piece * 64 + sq for piece, sq in pieces]
        records["score"][i] = max(-32767, min(32767, round(score)))
        records["result"][i] = round(result * 2)
        records["side"][i] = side
    return records


def _encode_chunk(lines):
    """encode() for a chunk of raw lines, also returning their size in bytes (for progress)."""
    return encode([line.decode() for line in lines]), sum(len(line) for line in lines)


def _chunks(path):
    with open(path, "rb") as file:
        while lines := file.readlines(LINES_PER_CHUNK * 80):
            yield lines


def convert_text(text_path, out_path, workers=1, progress=None):
    """Converts a text file to a binary one, via a .part file. 'progress', if given, is called with
    the number of bytes converted since the last call. Returns the number of positions."""
    partial = out_path + ".part"
    positions = 0
    with open(partial, "wb") as out:
        out.write(MAGIC)
        with multiprocessing.Pool(workers) if workers > 1 else _Serial() as pool:
            for records, size in pool.imap(_encode_chunk, _chunks(text_path)):
                out.write(records.tobytes())
                positions += len(records)
                if progress is not None:
                    progress(size)
    os.replace(partial, out_path)
    return positions


class _Serial:
    """Stands in for a Pool when there's one worker."""

    def __enter__(self):
        return self

    def __exit__(self, *_):
        return False

    def imap(self, function, items):
        return map(function, items)


def count(path):
    """Number of records in a binary file, checking its header and size."""
    with open(path, "rb") as file:
        if file.read(len(MAGIC)) != MAGIC:
            raise ValueError(f"{path}: not a tiki binary data file")
    size = os.path.getsize(path) - len(MAGIC)
    if size % RECORD.itemsize:
        raise ValueError(f"{path}: truncated ({size} bytes of records)")
    return size // RECORD.itemsize


def load(paths, progress=None):
    """All the records of the binary files, in one array. 'progress' as for convert_text."""
    records = np.empty(sum(count(path) for path in paths), dtype=RECORD)
    view = memoryview(records.view(np.uint8))
    filled = 0
    for path in paths:
        with open(path, "rb") as file:
            file.seek(len(MAGIC))
            while read := file.readinto(view[filled:filled + (1 << 24)]):
                filled += read
                if progress is not None:
                    progress(read)
    return records
