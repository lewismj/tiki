import os
import struct
import sys
import tempfile
import unittest
from unittest import mock

import numpy as np
import torch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import tiki_nnue.model as model_module
from tiki_nnue.data import collate, parse_line
from tiki_nnue.export import dequantise, export, quantise, quantised_eval, read_network, write_network
from tiki_nnue.features import (OUTPUT_BUCKETS, bucket_of_features, output_bucket, parse_fen, piece_bucket,
                                position_bucket)
from tiki_nnue.model import FT_CLIP, HIDDEN, L1, L1_CLIP, OUT_CLIP, QA, QH, QI, QW, SCALE, TikiNNUE, loss_fn

FENS = [
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
    "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/8 b - - 3 54",
    "4k3/8/8/8/8/8/8/3QK3 w - - 0 1",                      # Only the side to move has a queen.
    "3qk3/8/8/8/8/8/8/4K3 w - - 0 1",                      # Only the other side has one.
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


def fen_bucket(fen):
    return position_bucket(*parse_fen(fen))


def randomise(model, seed=5):
    """Larger, varied weights everywhere, so the accumulators reach the SCReLU clamp, the hidden units
    spread across CReLU's range (some clamped at 0 or 1) and every bucket differs."""
    torch.manual_seed(seed)
    with torch.no_grad():
        model.ft.weight.normal_(0.0, 0.1)
        model.ft_bias.uniform_(-0.2, 0.8)
        if model.factor is not None:
            model.factor.weight.normal_(0.0, 0.1)
        model.l1.weight.normal_(0.0, 0.05)
        model.l1.bias.normal_(0.0, 0.3)
        model.l1_shared.weight.normal_(0.0, 0.05)
        model.l1_shared.bias.fill_(0.3)
        model.out.weight.normal_(0.0, 0.5)
        model.out.bias.normal_(0.0, 0.3)
    model.clip_weights()
    return model


def quantised_pairwise(acc):
    """pairwise as the engine computes it on a dequantised accumulator (an exact multiple of 1 / QA):
    (x * y) >> 9 of the clamped halves, as a fraction of QI."""
    clamped = torch.round(torch.clamp(acc, 0.0, 1.0) * QA)
    half = acc.shape[1] // 2
    return torch.floor((clamped[:, :half] * clamped[:, half:]) / 512) / QI


class ModelTests(unittest.TestCase):

    def setUp(self):
        torch.manual_seed(1)
        self.model = TikiNNUE()
        self.model.clip_weights()

    def test_colour_symmetric(self):
        model = randomise(self.model)
        for a, b in zip(evaluate(model, FENS), evaluate(model, FLIPPED)):
            self.assertAlmostEqual(a, b, places=3)

    def test_side_to_move_conversion(self):
        """A white-relative score / result becomes the side to move's."""
        _, _, score, result = parse_line(f"{FENS[3]} | 120 | 1.0")
        self.assertEqual((-120.0, 0.0), (score, result))
        _, _, score, result = parse_line(f"{FENS[0]} | 120 | 1.0")
        self.assertEqual((120.0, 1.0), (score, result))

    def test_output_bucket(self):
        """Piece count part as before ((n - 2) / 4), times 4, plus 2 for the side to move's queen and 1
        for the other side's: 32 buckets."""
        self.assertEqual([0, 0, 0, 0, 1, 1, 1, 1, 2], [piece_bucket(n) for n in range(2, 11)])
        self.assertEqual(7, piece_bucket(30))
        self.assertEqual(7, piece_bucket(32))
        self.assertEqual(32, OUTPUT_BUCKETS)
        self.assertEqual(0, output_bucket(2, 0, 0))
        self.assertEqual(31, output_bucket(32, 1, 1))
        self.assertEqual(4 * 2 + 2, output_bucket(10, 1, 0))
        self.assertEqual(4 * 2 + 1, output_bucket(10, 0, 1))
        self.assertEqual(31, fen_bucket(FENS[0]))
        self.assertEqual(31, fen_bucket(FLIPPED[0]))
        self.assertEqual(4 * 2, fen_bucket(FENS[2]))            # 10 pieces, no queens.
        self.assertEqual(2, fen_bucket(FENS[5]))                # 3 pieces, the side to move's queen.
        self.assertEqual(1, fen_bucket(FENS[6]))                # The other side's.
        self.assertEqual(2, fen_bucket(flip_fen(FENS[5])))      # Colours swapped: still the side to move's.
        self.assertEqual(1, fen_bucket("4k3/8/8/8/8/8/8/3QK3 b - - 0 1"))     # Black to move, white's queen.
        # The trainer's batches agree with the position (bucket_of_features, from the stm features).
        for fen in FENS + FLIPPED:
            self.assertEqual(fen_bucket(fen), bucket_of_features(parse_line(f"{fen} | 0 | 0.5")[0]), fen)

    def test_each_bucket_has_its_own_stack(self):
        """Changing one bucket's hidden layer or output layer changes only the positions in that bucket."""
        model = randomise(self.model)
        before = evaluate(model, FENS)
        changed = fen_bucket(FENS[2])           # No other position shares it.
        self.assertEqual(1, [fen_bucket(fen) for fen in FENS].count(changed))
        with torch.no_grad():
            model.out.bias[changed] += 0.5
        for fen, a, b in zip(FENS, before, evaluate(model, FENS)):
            if fen_bucket(fen) == changed:
                self.assertAlmostEqual(a + 0.5 * SCALE, b, places=2)
            else:
                self.assertAlmostEqual(a, b, places=4)
        middle = evaluate(model, FENS)
        with torch.no_grad():
            model.l1.weight.view(OUTPUT_BUCKETS, L1, -1)[changed] += 0.05
        for fen, a, b in zip(FENS, middle, evaluate(model, FENS)):
            if fen_bucket(fen) == changed:
                self.assertNotAlmostEqual(a, b, places=2)
            else:
                self.assertAlmostEqual(a, b, places=4)

    def test_buckets_start_the_same(self):
        """A new net: the buckets' own hidden layers are zero and the output layers equal, so every
        bucket evaluates a position the same."""
        self.assertEqual(0.0, self.model.l1.weight.abs().max().item())
        self.assertEqual(0.0, self.model.l1.bias.abs().max().item())
        for b in range(OUTPUT_BUCKETS):
            torch.testing.assert_close(self.model.out.weight[b], self.model.out.weight[0])
        batch = collate([parse_line(f"{FENS[1]} | 0 | 0.5")])
        with torch.no_grad():
            outputs = [self.model(*batch[:4], torch.tensor([b])).item() for b in range(OUTPUT_BUCKETS)]
        for value in outputs:
            self.assertAlmostEqual(outputs[0], value, places=6)

    def test_l1_adds_the_shared_layer(self):
        """Bucket b's hidden layer is its own rows plus the shared layer, and the forward pass uses
        exactly that."""
        model = randomise(self.model)
        weights, biases = model.l1_weights(), model.l1_biases()
        self.assertEqual((OUTPUT_BUCKETS, L1, HIDDEN), tuple(weights.shape))
        for b in (0, 13, 31):
            torch.testing.assert_close(weights[b], model.l1.weight[b * L1:(b + 1) * L1] + model.l1_shared.weight)
            torch.testing.assert_close(biases[b], model.l1.bias[b * L1:(b + 1) * L1] + model.l1_shared.bias)
        # The forward pass, by hand for one position.
        batch = collate([parse_line(f"{FENS[3]} | 0 | 0.5")])
        us, them = model.accumulators(*batch[:4])
        # Pairwise by hand: each side's first half times its second half, clamped.
        def pairs(acc):
            return torch.clamp(acc[0, :HIDDEN // 2], 0, 1) * torch.clamp(acc[0, HIDDEN // 2:], 0, 1)
        a = torch.cat([pairs(us), pairs(them)])
        b = int(batch[4][0])
        h = torch.clamp(weights[b] @ a + biases[b], 0.0, 1.0)
        expected = (h @ model.out.weight[b] + model.out.bias[b]).item()
        with torch.no_grad():
            self.assertAlmostEqual(expected, model(*batch[:5]).item(), places=5)

    def test_clip_limits_the_sums(self):
        """Clipping keeps each bucket's own + shared hidden weights within L1_CLIP (what's quantised,
        int8), the output weights within OUT_CLIP, and leaves weights already in range alone."""
        with torch.no_grad():
            self.model.l1.weight.normal_(0.0, 2.0)
            self.model.l1_shared.weight.normal_(0.0, 2.0)
            self.model.l1.weight[5, 7] = 0.25
            self.model.l1_shared.weight[5, 7] = 0.5
            self.model.out.weight.normal_(0.0, 5.0)
        self.model.clip_weights()
        self.assertLessEqual(self.model.l1_weights().abs().max().item(), L1_CLIP + 1e-6)
        self.assertLessEqual(self.model.l1_shared.weight.abs().max().item(), L1_CLIP + 1e-6)
        self.assertLessEqual(self.model.out.weight.abs().max().item(), OUT_CLIP + 1e-6)
        self.assertAlmostEqual(0.75, self.model.l1_weights()[0, 5, 7].item(), places=6)
        quantise(self.model)        # In range for int8 / int16.

    def test_file_round_trip(self):
        network = quantise(randomise(self.model))
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "net.nnue")
            write_network(network, path)
            loaded = read_network(path)
        for key in ("ft_weights", "ft_bias", "l1_weights", "l1_bias", "out_weights", "out_bias"):
            self.assertEqual(network[key].dtype, loaded[key].dtype, key)
            np.testing.assert_array_equal(network[key], loaded[key])

    def test_older_versions_are_rejected(self):
        """Versions 1-4 are a different network: read_network refuses them rather than misreading."""
        network = quantise(self.model)
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "net.nnue")
            write_network(network, path)
            with open(path, "r+b") as file:
                file.seek(8)
                file.write(struct.pack("<I", 2))
            with self.assertRaisesRegex(ValueError, "version 2"):
                read_network(path)

    def test_quantised_matches_float(self):
        """The integer evaluation is the float model's on the quantised weights: exactly the same
        computation once the hidden layer inputs are rounded as the engine rounds them (only the final
        division truncates, under 1cp), and close to the float model without that rounding. Catches
        QA / QI / QW / QH / SCALE mistakes."""
        for factoriser in (False, True):
            network = quantise(randomise(TikiNNUE(factoriser=factoriser)))
            float_model = dequantise(network)
            with mock.patch.object(model_module, "pairwise", quantised_pairwise):
                exact = evaluate(float_model, FENS + FLIPPED)
            plain = evaluate(float_model, FENS + FLIPPED)
            for fen, a, b in zip(FENS + FLIPPED, exact, plain):
                integer = quantised_eval(network, fen)
                self.assertLess(abs(integer - a), 1.01, fen)
                self.assertLess(abs(integer - b), 15, fen)

    def test_quantised_close_to_trained_model(self):
        """Quantising (weights to 1/64, the pairwise inputs to 1/127) keeps the evaluation in the same
        place: a check for gross mistakes only, as these random hidden weights (std 0.05) are small
        next to the 1/64 step (test_quantised_matches_float is the exact check)."""
        model = randomise(TikiNNUE(factoriser=True))
        network = quantise(model)
        for fen, expected in zip(FENS, evaluate(model, FENS)):
            self.assertLess(abs(quantised_eval(network, fen) - expected), 60, fen)

    def test_hidden_inputs_fit_u8_and_int16_pairs(self):
        """The engine's inputs: pairwise products of the clamped halves, 0..127 (u8), 0 when either value
        is; weights int8, and a pair of products fits int16 (_mm256_maddubs_epi16 saturates)."""
        from tiki_nnue.export import hidden_inputs
        values = np.arange(-300, 300)
        inputs = hidden_inputs(np.concatenate([values, values[::-1]]))
        self.assertEqual(600, len(inputs))
        self.assertEqual(0, inputs.min())
        np.testing.assert_array_equal([127, 0, 0, 63, 32, 0], hidden_inputs(np.array([QA, 0, -5, 255, 128, 3,
                                                                                  QA, 200, QA, 128, 128, 1])))
        self.assertEqual(127, int(hidden_inputs(np.array([400, 400]))[0]))
        self.assertLessEqual(2 * 127 * 128, 32767)
        self.assertLessEqual(QA << 7, 32767)             # The engine's mulhi(x << 7, y): x << 7 in int16.
        self.assertEqual(QH, QI * QW)

    def test_training_reduces_loss(self):
        """Fit a handful of positions, the loss must fall well below where it started."""
        lines = [f"{fen} | {score} | {result}" for fen, score, result in
                 zip(FENS, (30, -250, 400, 80, -600, 900, -900), (0.5, 0.0, 1.0, 0.5, 0.0, 1.0, 0.0))]
        batch = collate([parse_line(line) for line in lines])
        optimiser = torch.optim.Adam(self.model.parameters(), lr=1e-3)

        def loss():
            return loss_fn(self.model(*batch[:5]), batch[5], batch[6], wdl=0.3)

        start = loss().item()
        own = self.model.l1.weight.detach().clone()
        for _ in range(300):
            optimiser.zero_grad()
            value = loss()
            value.backward()
            optimiser.step()
            self.model.clip_weights()
        self.assertLess(loss().item(), start * 0.2)
        # The buckets' own hidden layers learnt too (they start at zero).
        self.assertGreater((self.model.l1.weight - own).abs().max().item(), 1e-3)

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
        """A net of another width than the default (--hidden 256) writes its width to the file and reads
        back as the same net, evaluating as before."""
        model = randomise(TikiNNUE(hidden=256))
        network = quantise(model)
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "net.nnue")
            write_network(network, path)
            self.assertEqual(file_size(256), os.path.getsize(path))
            self.assertNotEqual(256, HIDDEN)
            loaded = read_network(path)
        self.assertEqual((22528, 256), loaded["ft_weights"].shape)
        self.assertEqual((OUTPUT_BUCKETS, L1, 256), loaded["l1_weights"].shape)
        for fen in FENS:
            self.assertEqual(quantised_eval(network, fen), quantised_eval(loaded, fen))

    def test_export_writes_engine_format(self):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "net.nnue")
            export(self.model, path)
            with open(path, "rb") as file:
                self.assertEqual(b"TIKINNUE", file.read(8))
                self.assertEqual((5, 22528, HIDDEN), struct.unpack("<III", file.read(12)))
            self.assertEqual(file_size(HIDDEN), os.path.getsize(path))


def file_size(hidden):
    """header + ft weights and bias (int16) + per bucket: l1 weights (int8) and biases (int32), output
    weights (int16) and bias (int32)."""
    return (20 + 2 * (22528 * hidden + hidden) +
            OUTPUT_BUCKETS * (L1 * hidden + 4 * L1 + 2 * L1 + 4))


class FactoriserTests(unittest.TestCase):

    def setUp(self):
        self.model = randomise(TikiNNUE(factoriser=True), seed=3)

    def folded(self):
        """An unfactorised model with the factorised one's weights as the engine sees them."""
        plain = TikiNNUE()
        with torch.no_grad():
            plain.ft.weight.copy_(self.model.feature_weights())
            plain.ft_bias.copy_(self.model.ft_bias)
            plain.l1.weight.copy_(self.model.l1.weight)
            plain.l1.bias.copy_(self.model.l1.bias)
            plain.l1_shared.weight.copy_(self.model.l1_shared.weight)
            plain.l1_shared.bias.copy_(self.model.l1_shared.bias)
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
        """The exported net is an ordinary one, the same as exporting the folded model."""
        network = quantise(self.model)
        folded = quantise(self.folded())
        for key in ("ft_weights", "ft_bias", "l1_weights", "l1_bias", "out_weights", "out_bias"):
            np.testing.assert_array_equal(folded[key], network[key])

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
        torch.manual_seed(6)
        model = TikiNNUE(factoriser=True)
        optimiser = torch.optim.Adam(model.parameters(), lr=1e-3)
        shared = model.factor.weight.detach().clone()

        def loss():
            return loss_fn(model(*batch[:5]), batch[5], batch[6], wdl=0.3)

        start = loss().item()
        for _ in range(300):
            optimiser.zero_grad()
            value = loss()
            value.backward()
            optimiser.step()
            model.clip_weights()
        self.assertLess(loss().item(), start * 0.2)
        self.assertGreater((model.factor.weight - shared).abs().max().item(), 1e-3)

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
        # A checkpoint from before the hidden layer: a clear error, not load_state_dict's.
        old = {key: value for key, value in plain.items() if not key.startswith("l1")}
        with self.assertRaisesRegex(ValueError, "before the hidden layer"):
            load_model(TikiNNUE(), old, parser.parse_args([]), parser)
        # One whose hidden layer has another number of inputs (format 4's SCReLU inputs): also clear.
        wider = dict(plain)
        wider["l1.weight"] = torch.zeros(OUTPUT_BUCKETS * L1, 2 * HIDDEN)
        with self.assertRaisesRegex(ValueError, "hidden layer has 1024 inputs"):
            load_model(TikiNNUE(), wider, parser.parse_args([]), parser)


if __name__ == "__main__":
    unittest.main()
