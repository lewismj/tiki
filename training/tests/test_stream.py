import os
import subprocess
import sys
import tempfile
import unittest

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.dirname(__file__))

from tiki_nnue import binary
from tiki_nnue.stream import Epoch, StreamData, TrainingStream, ValidationBatches, data_files

try:
    import chess
except ImportError:
    chess = None

HERE = os.path.dirname(os.path.abspath(__file__))


def write_ids(path, first, count):
    """A binary data file whose records are numbered first..first+count-1 (in pieces[0], so not
    real positions: for tests that don't compute features)."""
    records = np.zeros(count, dtype=binary.RECORD)
    records["pieces"] = binary.PAD
    records["pieces"][:, 0] = np.arange(first, first + count)
    with open(path, "wb") as file:
        file.write(binary.MAGIC)
        file.write(records.tobytes())


def ids(batches):
    return np.concatenate([records["pieces"][:, 0] for records in batches]).astype(np.int64)


class StreamTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.paths = []
        for i, count in enumerate((20000, 7000, 333)):
            path = os.path.join(self.directory.name, f"data-{i}.bin")
            write_ids(path, i * 20000, count)
            self.paths.append(path)
        self.total = 20000 + 7000 + 333

    def tearDown(self):
        self.directory.cleanup()

    def all_ids(self):
        return np.concatenate([np.arange(i * 20000, i * 20000 + count) for i, count in enumerate((20000, 7000, 333))])

    def test_data_files(self):
        open(os.path.join(self.directory.name, "data-9.bin.part"), "wb").close()
        self.assertEqual(self.paths, data_files([self.directory.name]))
        self.assertEqual(self.paths[:1], data_files([self.paths[0], self.paths[0]]))

    def test_split_covers_every_record_once(self):
        data = StreamData(self.paths, 0.05, seed=1, block=100, chunk=1000)
        self.assertEqual(self.total, data.training_count + data.validation_count)
        self.assertAlmostEqual(0.05, data.validation_count / self.total, delta=0.01)
        seen = np.concatenate([data.read(*r)["pieces"][:, 0] for r in data.training + data.validation])
        np.testing.assert_array_equal(np.sort(self.all_ids()), np.sort(seen.astype(np.int64)))
        self.assertTrue(all(end - start <= 1000 for _, start, end in data.training))

    def test_validation_is_stable_when_files_are_added(self):
        def held_out(paths, path):
            data = StreamData(paths, 0.05, seed=3, block=100)
            index = paths.index(path)
            return [(start, end) for file, start, end in data.validation if file == index]

        first = self.paths[0]
        self.assertEqual(held_out([first], first), held_out(self.paths, first))
        self.assertEqual(held_out(self.paths, first), held_out(list(reversed(self.paths)), first))
        self.assertNotEqual(held_out(self.paths, first), StreamData([first], 0.05, seed=4, block=100).validation)

    def test_a_pass_yields_every_training_record_once(self):
        data = StreamData(self.paths, 0.05, seed=1, block=100, chunk=1000)
        stream = TrainingStream(data, batch_size=256, device="cpu", buffer=3000, rng=np.random.default_rng(1))
        batches = stream.record_batches()
        first_pass = [next(batches) for _ in range(stream.passes())]
        self.assertTrue(all(len(b) == 256 for b in first_pass[:-1]))
        self.assertEqual(data.training_count, sum(len(b) for b in first_pass))

        training = np.concatenate([data.read(*r)["pieces"][:, 0] for r in data.training]).astype(np.int64)
        np.testing.assert_array_equal(np.sort(training), np.sort(ids(first_pass)))
        # Shuffled across files, not file by file.
        self.assertGreater(len(np.unique(ids(first_pass[:4]) // 20000)), 1)

        # The next pass covers the same records, in another order.
        second_pass = [next(batches) for _ in range(stream.passes())]
        np.testing.assert_array_equal(np.sort(training), np.sort(ids(second_pass)))
        self.assertFalse(np.array_equal(ids(first_pass), ids(second_pass)))

    def test_epochs_continue_the_stream(self):
        data = StreamData(self.paths, 0.0, seed=1, chunk=1000)
        stream = TrainingStream(data, batch_size=500, device="cpu", buffer=2000, rng=np.random.default_rng(2))
        batches = stream.record_batches()
        epoch = Epoch(batches, 10)
        self.assertEqual(10, len(epoch))
        self.assertEqual(10, sum(1 for _ in epoch))
        self.assertEqual(10, sum(1 for _ in Epoch(batches, 10)))

    def test_validation_batches(self):
        data = StreamData(self.paths, 0.1, seed=1, block=100)
        everything = ValidationBatches(data, batch_size=128, device="cpu")
        batches = list(everything.record_batches())
        self.assertEqual(len(everything), len(batches))
        self.assertEqual(data.validation_count, sum(len(b) for b in batches))

        limited = ValidationBatches(data, batch_size=128, device="cpu", limit=500, seed=1)
        self.assertEqual(500, limited.count)
        self.assertEqual(500, sum(len(b) for b in limited.record_batches()))
        again = ValidationBatches(data, batch_size=128, device="cpu", limit=500, seed=1)
        np.testing.assert_array_equal(ids(limited.record_batches()), ids(again.record_batches()))

    def test_rejects_bad_files(self):
        path = os.path.join(self.directory.name, "bad.bin")
        with open(path, "wb") as file:
            file.write(b"NOTTIKI!")
        with self.assertRaises(ValueError):
            StreamData([path], 0.02, seed=1)


@unittest.skipIf(chess is None, "needs python-chess")
class TrainStreamingTest(unittest.TestCase):
    def test_trains_and_exports(self):
        from test_data import random_lines
        with tempfile.TemporaryDirectory() as directory:
            data = os.path.join(directory, "data")
            os.makedirs(data)
            for i in range(2):
                with open(os.path.join(data, f"part-{i}.bin"), "wb") as file:
                    file.write(binary.MAGIC)
                    file.write(binary.encode(random_lines(600, seed=i)).tobytes())
            out = os.path.join(directory, "nets")
            subprocess.run([sys.executable, os.path.join(HERE, "..", "train_streaming.py"), "--data", data,
                            "--out", out, "--epochs", "2", "--batch-size", "64", "--buffer", "300",
                            "--epoch-positions", "500", "--device", "cpu"],
                           check=True, capture_output=True, cwd=os.path.join(HERE, ".."))
            for epoch in (1, 2):
                self.assertTrue(os.path.exists(os.path.join(out, f"tiki-{epoch}.nnue")))
                self.assertTrue(os.path.exists(os.path.join(out, f"tiki-{epoch}.pt")))
            with open(os.path.join(out, "train.log")) as log:
                self.assertIn("finished", log.read())


if __name__ == "__main__":
    unittest.main()
