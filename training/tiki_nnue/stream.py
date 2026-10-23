"""Training data streamed from binary files (binary.py) in chunks, so memory use doesn't grow with the
amount of data: train_streaming.py's alternative to binary.load and validation_split.

Each file is split into blocks of consecutive records (whole games, as in validation_split), and some
blocks are held out for validation. Which ones depends only on the file's name, its size in blocks
and the seed, so adding files (another month) doesn't change what the others hold out.

A training pass reads runs of training blocks ('chunks') in a random order across all the files,
gathers them into a buffer of about 'buffer' positions, shuffles the buffer and splits it into
batches. The next buffer is read by a background thread while the current one is trained on. So
batches mix positions from many games, months and files, and the most held in memory is about two
buffers.
"""

import os
import queue
import threading
import zlib

import numpy as np

from . import binary
from .data import VALIDATION_BLOCK, batch_tensors

CHUNK = 1 << 18     # Records read at once: 17 MB of consecutive positions.


def data_files(paths):
    """The .bin and .txt files among 'paths', with directories expanded (sorted, .part files left out)."""
    files = []
    for path in paths:
        if os.path.isdir(path):
            files.extend(sorted(os.path.join(path, name) for name in os.listdir(path)
                                if name.endswith((".bin", ".txt"))))
        else:
            files.append(path)
    return list(dict.fromkeys(files))


class StreamData:
    """The training and validation ranges of a set of binary data files."""

    def __init__(self, paths, validation, seed, block=VALIDATION_BLOCK, chunk=CHUNK):
        self.paths = list(paths)
        self.counts = [binary.count(path) for path in self.paths]
        self.training, self.validation = [], []     # (file index, first record, end record)
        for index, (path, count) in enumerate(zip(self.paths, self.counts)):
            training, validating = self._split(path, count, validation, seed, block)
            self.training.extend(_ranges(index, training, chunk))
            self.validation.extend(_ranges(index, validating, chunk))
        self.training_count = sum(end - start for _, start, end in self.training)
        self.validation_count = sum(end - start for _, start, end in self.validation)

    @staticmethod
    def _split(path, count, fraction, seed, block):
        """Blocks of 'path' as (training, validation) lists of (first, end) record ranges. Small files
        get smaller blocks, as in data.validation_split."""
        if count == 0:
            return [], []
        block = max(1, min(block, count // 100))
        blocks = -(-count // block)
        held = min(max(1, round(blocks * fraction)), max(0, blocks - 1)) if fraction > 0 else 0
        rng = np.random.default_rng([seed, zlib.crc32(os.path.basename(path).encode())])
        chosen = np.zeros(blocks, dtype=bool)
        chosen[rng.choice(blocks, held, replace=False)] = True
        training, validation = [], []
        for i in range(blocks):
            (validation if chosen[i] else training).append((i * block, min((i + 1) * block, count)))
        return training, validation

    def read(self, file, start, end):
        """Records [start, end) of file number 'file'."""
        return np.fromfile(self.paths[file], dtype=binary.RECORD, count=end - start,
                           offset=len(binary.MAGIC) + start * binary.RECORD.itemsize)


def _ranges(file, blocks, chunk):
    """Consecutive (first, end) blocks merged into runs of at most 'chunk' records, as (file, first, end)."""
    ranges = []
    for start, end in blocks:
        if ranges and ranges[-1][2] == start and end - ranges[-1][1] <= chunk:
            ranges[-1] = (file, ranges[-1][1], end)
        else:
            ranges.append((file, start, end))
    return ranges


def _background(generator, depth=1):
    """Runs 'generator' in a thread, up to 'depth' items ahead of the consumer. Exceptions are
    re-raised in the consumer."""
    items = queue.Queue(maxsize=depth)
    done = object()
    stop = threading.Event()

    def produce():
        try:
            for item in generator:
                while not stop.is_set():
                    try:
                        items.put(item, timeout=0.1)
                        break
                    except queue.Full:
                        pass
                if stop.is_set():
                    return
            items.put(done)
        except BaseException as error:  # noqa: BLE001 - handed to the consumer
            items.put(error)

    thread = threading.Thread(target=produce, daemon=True)
    thread.start()
    try:
        while (item := items.get()) is not done:
            if isinstance(item, BaseException):
                raise item
            yield item
    finally:
        stop.set()


class TrainingStream:
    """Endless shuffled training batches (data.batch_tensors tuples), pass after pass over the data.
    Each pass yields ceil(training_count / batch_size) batches, the last one possibly short."""

    def __init__(self, data, batch_size, device, buffer, rng):
        self.data, self.batch_size, self.device, self.rng = data, batch_size, device, rng
        self.buffer = max(buffer, batch_size)

    def passes(self):
        return -(-self.data.training_count // self.batch_size)

    def _buffers(self):
        """Shuffled buffers of records, one pass after another, then None at the end of each pass."""
        while True:
            order = self.rng.permutation(len(self.data.training))
            parts, size = [], 0
            for i in order:
                parts.append(self.data.read(*self.data.training[i]))
                size += len(parts[-1])
                if size >= self.buffer:
                    yield self._shuffled(parts)
                    parts, size = [], 0
            if parts:
                yield self._shuffled(parts)
            yield None

    def _shuffled(self, parts):
        records = np.concatenate(parts)
        return records[self.rng.permutation(len(records))]

    def record_batches(self):
        """Batches of records, exactly batch_size except the last of each pass (records left over
        from one buffer go into the next)."""
        left = None
        for records in _background(self._buffers()):
            if records is None:
                if left is not None and len(left):
                    yield left
                left = None
                continue
            if left is not None and len(left):
                records = np.concatenate([left, records])
            whole = len(records) // self.batch_size * self.batch_size
            for start in range(0, whole, self.batch_size):
                yield records[start:start + self.batch_size]
            left = records[whole:]

    def __iter__(self):
        return (batch_tensors(records, self.device) for records in self.record_batches())


class Epoch:
    """'count' batches from a shared iterator, for train.run_epoch (which needs a length)."""

    def __init__(self, batches, count):
        self.batches, self.count = batches, count

    def __len__(self):
        return self.count

    def __iter__(self):
        for _ in range(self.count):
            yield next(self.batches)


class ValidationBatches:
    """The validation records, read in order, or the first 'limit' of them in a fixed random order of
    their ranges, as batches."""

    def __init__(self, data, batch_size, device, limit=0, seed=0):
        self.data, self.batch_size, self.device = data, batch_size, device
        ranges = list(data.validation)
        if limit and data.validation_count > limit:
            rng = np.random.default_rng(seed)
            ranges = [ranges[i] for i in rng.permutation(len(ranges))]
            kept, total = [], 0
            for file, start, end in ranges:
                if total >= limit:
                    break
                end = min(end, start + limit - total)
                kept.append((file, start, end))
                total += end - start
            ranges = kept
        self.ranges = ranges
        self.count = sum(end - start for _, start, end in ranges)

    def __len__(self):
        return -(-self.count // self.batch_size)

    def _buffers(self):
        for file, start, end in self.ranges:
            yield self.data.read(file, start, end)

    def record_batches(self):
        left = np.empty(0, dtype=binary.RECORD)
        for records in _background(self._buffers(), depth=2):
            records = np.concatenate([left, records]) if len(left) else records
            whole = len(records) // self.batch_size * self.batch_size
            for start in range(0, whole, self.batch_size):
                yield records[start:start + self.batch_size]
            left = records[whole:]
        if len(left):
            yield left

    def __iter__(self):
        return (batch_tensors(records, self.device) for records in self.record_batches())
