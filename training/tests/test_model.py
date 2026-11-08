import os
import struct
import sys
import tempfile
import unittest

import numpy as np
import torch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from tiki_nnue.data import collate, parse_line
from tiki_nnue.export import dequantise, export, quantise, quantised_eval, read_network, write_network
from tiki_nnue.features import OUTPUT_BUCKETS, output_bucket
from tiki_nnue.model import FT_CLIP, HIDDEN, QA, QB, SCALE, TikiNNUE, loss_fn

FENS = [
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
    "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/8 b - - 3 54",
]

def flip_fen(fen):
    """The same position with colours swapped: ranks reversed, piece case swapped, other side to move."""
    board, side = fen.split()[:2]
    return "/".join(reversed(board.swapcase().split("/"))) + (" b" if side == "w" else " w") + " - - 0 1"


# The same positions with colours swapped (and the board flipped), for the other side to move.
FLIPPED = [flip_fen(fen) for fen in FENS]


def evaluate(model, fens):
    """Float eval in centipawns (side to move)."""
    batch = collate([parse_line(f"{fen} | 0 | 0.5") for fen in fens])
    with torch.no_grad():
        return (model(*batch[:5]) * SCALE).tolist()


class ModelTests(unittest.TestCase):

    def setUp(self):
        torch.manual_seed(1)
        self.model = TikiNNUE()
        self.model.clip_weights()

    def test_colour_symmetric(self):
        for a, b in zip(evaluate(self.model, FENS), evaluate(self.model, FLIPPED)):
            self.assertAlmostEqual(a, b, places=3)

    def test_side_to_move_conversion(self):
        """A white-relative score / result becomes the side to move's."""
        _, _, score, result = parse_line(f"{FENS[3]} | 120 | 1.0")
        self.assertEqual((-120.0, 0.0), (score, result))
        _, _, score, result = parse_line(f"{FENS[0]} | 120 | 1.0")
        self.assertEqual((120.0, 1.0), (score, result))

    def test_file_round_trip(self):
        network = quantise(self.model)
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "net.nnue")
            write_network(network, path)
            loaded = read_network(path)
        for key in ("ft_weights", "ft_bias", "out_weights", "out_bias"):
            np.testing.assert_array_equal(network[key], loaded[key])

    def test_single_output_network_loads_into_every_bucket(self):
        """A version 1 file (one output layer) reads as that layer in every bucket."""
        network = quantise(self.model)
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "net.nnue")
            with open(path, "wb") as file:
                file.write(b"TIKINNUE" + struct.pack("<III", 1, 22528, HIDDEN))
                for key in ("ft_weights", "ft_bias"):
                    file.write(network[key].astype("<i2").tobytes())
                file.write(network["out_weights"][3].astype("<i2").tobytes())
                file.write(struct.pack("<i", int(network["out_bias"][3])))
            loaded = read_network(path)
        np.testing.assert_array_equal(network["ft_weights"], loaded["ft_weights"])
        self.assertEqual((OUTPUT_BUCKETS, 2 * HIDDEN), loaded["out_weights"].shape)
        for bucket in range(OUTPUT_BUCKETS):
            np.testing.assert_array_equal(network["out_weights"][3], loaded["out_weights"][bucket])
            self.assertEqual(network["out_bias"][3], loaded["out_bias"][bucket])

    def test_output_bucket_by_piece_count(self):
        self.assertEqual([0, 0, 0, 0, 1, 1, 1, 1, 2], [output_bucket(n) for n in range(2, 11)])
        self.assertEqual(7, output_bucket(30))
        self.assertEqual(7, output_bucket(32))

    def test_each_bucket_has_its_own_output(self):
        """Changing one bucket's output layer changes only the positions in that bucket."""
        def bucket(fen):
            return output_bucket(sum(ch.isalpha() for ch in fen.split()[0]))

        before = evaluate(self.model, FENS)
        changed = bucket(FENS[2])           # 10 pieces, bucket 2: no other position shares it.
        with torch.no_grad():
            self.model.out.bias[changed] += 0.5
        for fen, a, b in zip(FENS, before, evaluate(self.model, FENS)):
            if bucket(fen) == changed:
                self.assertAlmostEqual(a + 0.5 * SCALE, b, places=2)
            else:
                self.assertAlmostEqual(a, b, places=4)

    def test_quantised_matches_dequantised_float(self):
        """The integer evaluation is the float formula on the quantised weights, only the integer
        divisions truncate (well under 2cp). Catches QA / QB / SCALE mistakes, unlike a comparison
        with the unquantised model, which is dominated by rounding the output weights to 1/QB."""
        with torch.no_grad():
            self.model.ft.weight.normal_(0.0, 0.1)       # Large enough to reach the SCReLU clamp.
            self.model.ft_bias.uniform_(-0.2, 0.8)
            self.model.out.weight.normal_(0.0, 0.3)
        self.model.clip_weights()
        network = quantise(self.model)
        for fen, expected in zip(FENS, evaluate(dequantise(network), FENS)):
            self.assertLess(abs(quantised_eval(network, fen) - expected), 2, fen)

    def test_training_reduces_loss(self):
        """Fit a handful of positions, the loss must fall well below where it started."""
        lines = [f"{fen} | {score} | {result}" for fen, score, result in
                 zip(FENS, (30, -250, 400, 80, -600), (0.5, 0.0, 1.0, 0.5, 0.0))]
        batch = collate([parse_line(line) for line in lines])
        optimiser = torch.optim.Adam(self.model.parameters(), lr=1e-2)

        def loss():
            return loss_fn(self.model(*batch[:5]), batch[5], batch[6], wdl=0.3)

        start = loss().item()
        for _ in range(200):
            optimiser.zero_grad()
            value = loss()
            value.backward()
            optimiser.step()
            self.model.clip_weights()
        self.assertLess(loss().item(), start * 0.2)

    def test_loss_power(self):
        """--loss-power: 2 (the default) is the MSE, other powers are mean |error| ** power, with a finite
        gradient where the error is 0; the trainers reject a power below 1."""
        output = torch.tensor([0.5, -1.0, 0.0, 2.0], requires_grad=True)
        score = torch.tensor([100.0, -300.0, 0.0, 500.0])
        result = torch.tensor([1.0, 0.0, 0.5, 1.0])
        target = 0.7 * torch.sigmoid(score / SCALE) + 0.3 * result
        error = torch.sigmoid(output) - target
        self.assertTrue(torch.equal(loss_fn(output, score, result, 0.3), (error ** 2).mean()))
        self.assertTrue(torch.equal(loss_fn(output, score, result, 0.3, 2.0), (error ** 2).mean()))
        for power in (2.25, 2.5, 3.0):
            self.assertTrue(torch.allclose(loss_fn(output, score, result, 0.3, power),
                                           (error.abs() ** power).mean()))
        # Errors below 1, so a higher power gives a smaller loss.
        self.assertLess(loss_fn(output, score, result, 0.3, 2.5).item(), loss_fn(output, score, result, 0.3).item())

        exact = torch.zeros(1, requires_grad=True)
        loss_fn(exact, torch.zeros(1), torch.full((1,), 0.5), 0.3, 2.5).backward()
        self.assertEqual(0.0, exact.grad.item())

        import argparse
        from train import add_loss_arguments, check_loss_arguments
        parser = argparse.ArgumentParser()
        add_loss_arguments(parser)
        self.assertEqual(2.0, parser.parse_args([]).loss_power)
        self.assertEqual(2.5, parser.parse_args(["--loss-power", "2.5"]).loss_power)
        with self.assertRaises(SystemExit):
            check_loss_arguments(parser, parser.parse_args(["--loss-power", "0.5"]))

    def test_run_epoch_reports_the_mse(self):
        """run_epoch returns (loss, MSE): the same at power 2, the MSE alongside the power's loss otherwise,
        so validation losses of runs with different powers can be compared."""
        from train import epoch_result, run_epoch
        lines = [f"{fen} | {score} | {result}" for fen, score, result in
                 zip(FENS, (30, -250, 400, 80, -600), (0.5, 0.0, 1.0, 0.5, 0.0))]
        batches = [collate([parse_line(line) for line in lines])]
        loss, mse = run_epoch(self.model, batches, "cpu", 0.3)
        self.assertEqual(loss, mse)
        with torch.no_grad():
            output = self.model(*batches[0][:5])
        expected = loss_fn(output, batches[0][5], batches[0][6], 0.3, 2.5).item()
        expected_mse = loss_fn(output, batches[0][5], batches[0][6], 0.3).item()
        loss, mse = run_epoch(self.model, batches, "cpu", 0.3, power=2.5)
        self.assertAlmostEqual(expected, loss, places=6)
        self.assertAlmostEqual(expected_mse, mse, places=6)
        self.assertEqual("epoch 3: train 0.020000, validation 0.021000, lr 0.001, 5s -> n/tiki-3.nnue",
                         epoch_result(3, 0.02, (0.021, 0.021), 2.0, 0.001, 5, "n/tiki-3"))
        self.assertEqual("epoch 3: train 0.010000, validation 0.011000 (MSE 0.021000), lr 0.001, 5s -> n/tiki-3.nnue",
                         epoch_result(3, 0.01, (0.011, 0.021), 2.5, 0.001, 5, "n/tiki-3"))

    def test_other_width_round_trip(self):
        """A net of another width than the default (--hidden 256) writes its width to the file and reads back as the same net,
        evaluating as before."""
        torch.manual_seed(2)
        model = TikiNNUE(hidden=256)
        model.clip_weights()
        network = quantise(model)
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "net.nnue")
            write_network(network, path)
            self.assertEqual(20 + 2 * (22528 * 256 + 256 + 8 * 512) + 8 * 4, os.path.getsize(path))
            self.assertNotEqual(256, HIDDEN)
            loaded = read_network(path)
        self.assertEqual((22528, 256), loaded["ft_weights"].shape)
        for fen in FENS:
            self.assertEqual(quantised_eval(network, fen), quantised_eval(loaded, fen))
            self.assertLess(abs(quantised_eval(loaded, fen) - evaluate(dequantise(loaded), [fen])[0]), 2, fen)

    def test_export_writes_engine_format(self):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "net.nnue")
            export(self.model, path)
            with open(path, "rb") as file:
                self.assertEqual(b"TIKINNUE", file.read(8))
            # header + ft weights + ft bias + out weights (int16) + out biases (int32), per bucket
            self.assertEqual(20 + 2 * (22528 * HIDDEN + HIDDEN + 8 * 2 * HIDDEN) + 8 * 4, os.path.getsize(path))


class FactoriserTests(unittest.TestCase):

    def setUp(self):
        torch.manual_seed(3)
        self.model = TikiNNUE(factoriser=True)
        with torch.no_grad():
            self.model.factor.weight.normal_(0.0, 0.1)
        self.model.clip_weights()

    def folded(self):
        """An unfactorised model with the factorised one's weights as the engine sees them."""
        plain = TikiNNUE()
        with torch.no_grad():
            plain.ft.weight.copy_(self.model.feature_weights())
            plain.ft_bias.copy_(self.model.ft_bias)
            plain.out.weight.copy_(self.model.out.weight)
            plain.out.bias.copy_(self.model.out.bias)
        return plain

    def test_starts_as_unfactorised(self):
        """The shared rows start at zero, so a new factorised net evaluates as the plain one."""
        torch.manual_seed(4)
        plain = TikiNNUE()
        torch.manual_seed(4)
        factorised = TikiNNUE(factoriser=True)
        self.assertEqual(0.0, factorised.factor.weight.abs().max().item())
        for a, b in zip(evaluate(plain, FENS), evaluate(factorised, FENS)):
            self.assertAlmostEqual(a, b, places=4)

    def test_each_feature_adds_its_shared_row(self):
        """Feature f's row is its own plus shared row f % 704, in every king bucket."""
        weights = self.model.feature_weights()
        for feature in (0, 703, 704, 5 * 704 + 321, 22527):
            expected = self.model.ft.weight[feature] + self.model.factor.weight[feature % 704]
            torch.testing.assert_close(weights[feature], expected)

    def test_forward_matches_folded_weights(self):
        """Evaluating with the shared rows is the same as evaluating with them added into each bucket."""
        for a, b in zip(evaluate(self.model, FENS + FLIPPED), evaluate(self.folded(), FENS + FLIPPED)):
            self.assertAlmostEqual(a, b, places=3)

    def test_export_folds_the_shared_rows(self):
        """The exported net is an ordinary one, the same as exporting the folded model, and evaluates
        like the float model."""
        network = quantise(self.model)
        folded = quantise(self.folded())
        for key in ("ft_weights", "ft_bias", "out_weights", "out_bias"):
            np.testing.assert_array_equal(folded[key], network[key])
        for fen, expected in zip(FENS, evaluate(dequantise(network), FENS)):
            self.assertLess(abs(quantised_eval(network, fen) - expected), 2, fen)

    def test_clip_limits_the_sums(self):
        """Clipping keeps own + shared within FT_CLIP (what's quantised), and leaves rows already
        in range alone."""
        with torch.no_grad():
            self.model.ft.weight.normal_(0.0, 1.0)
            self.model.factor.weight.normal_(0.0, 1.5)
            self.model.ft.weight[7].fill_(0.25)
            self.model.factor.weight[7].fill_(0.5)
        self.model.clip_weights()
        weights = self.model.feature_weights()
        self.assertLessEqual(weights.abs().max().item(), FT_CLIP + 1e-6)
        self.assertLessEqual(self.model.factor.weight.abs().max().item(), FT_CLIP + 1e-6)
        torch.testing.assert_close(weights[7], torch.full((HIDDEN,), 0.75))
        quantise(self.model)        # In range for int16 and the accumulator.

    def test_training_trains_the_shared_rows(self):
        lines = [f"{fen} | {score} | {result}" for fen, score, result in
                 zip(FENS, (30, -250, 400, 80, -600), (0.5, 0.0, 1.0, 0.5, 0.0))]
        batch = collate([parse_line(line) for line in lines])
        optimiser = torch.optim.Adam(self.model.parameters(), lr=1e-2)
        shared = self.model.factor.weight.detach().clone()

        def loss():
            return loss_fn(self.model(*batch[:5]), batch[5], batch[6], wdl=0.3)

        start = loss().item()
        for _ in range(200):
            optimiser.zero_grad()
            value = loss()
            value.backward()
            optimiser.step()
            self.model.clip_weights()
        self.assertLess(loss().item(), start * 0.2)
        self.assertGreater((self.model.factor.weight - shared).abs().max().item(), 0.01)

    def test_resume_needs_the_same_setting(self):
        """A checkpoint resumes only with the --factoriser setting it was trained with."""
        from train import add_model_arguments, load_model
        import argparse

        parser = argparse.ArgumentParser()
        add_model_arguments(parser)
        parser.error = lambda message: (_ for _ in ()).throw(ValueError(message))
        factorised = self.model.state_dict()
        plain = TikiNNUE().state_dict()
        load_model(TikiNNUE(factoriser=True), factorised, parser.parse_args(["--factoriser"]), parser)
        load_model(TikiNNUE(), plain, parser.parse_args([]), parser)
        with self.assertRaisesRegex(ValueError, "with --factoriser"):
            load_model(TikiNNUE(), factorised, parser.parse_args([]), parser)
        with self.assertRaisesRegex(ValueError, "without --factoriser"):
            load_model(TikiNNUE(factoriser=True), plain, parser.parse_args(["--factoriser"]), parser)


if __name__ == "__main__":
    unittest.main()
