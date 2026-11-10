import importlib.util
import os
import sys
import tempfile
import unittest

import torch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from tiki_nnue.export import export  # noqa: E402
from tiki_nnue.model import TikiNNUE  # noqa: E402

SCRIPT = os.path.join(os.path.dirname(__file__), "..", "..", "scripts", "nnue_average.py")
spec = importlib.util.spec_from_file_location("nnue_average", SCRIPT)
nnue_average = importlib.util.module_from_spec(spec)
spec.loader.exec_module(nnue_average)

HIDDEN = 32


def random_model(seed, factoriser=True):
    torch.manual_seed(seed)
    model = TikiNNUE(hidden=HIDDEN, factoriser=factoriser)
    with torch.no_grad():
        for parameter in model.parameters():
            parameter.normal_(0.0, 0.1)
    return model


class AverageTest(unittest.TestCase):

    def test_average_is_weighted_mean_of_every_tensor(self):
        a, b = random_model(1).state_dict(), random_model(2).state_dict()
        result = nnue_average.average([a, b], [0.25, 0.75])
        self.assertEqual(list(result), list(a))
        for key in a:
            torch.testing.assert_close(result[key], 0.25 * a[key] + 0.75 * b[key])

    def test_identical_checkpoints_export_the_same_network(self):
        model = random_model(3)
        state = nnue_average.average([model.state_dict()] * 4, [0.25] * 4)
        with tempfile.TemporaryDirectory() as tmp:
            direct, averaged = os.path.join(tmp, "direct.nnue"), os.path.join(tmp, "averaged.nnue")
            export(model, direct)
            export(nnue_average.model_from_state(state), averaged)
            with open(direct, "rb") as f, open(averaged, "rb") as g:
                self.assertEqual(f.read(), g.read())

    def test_model_from_state_takes_width_and_factoriser_from_weights(self):
        for factoriser in (False, True):
            model = nnue_average.model_from_state(random_model(4, factoriser).state_dict())
            self.assertEqual(model.hidden, HIDDEN)
            self.assertEqual(model.factorised, factoriser)

    def test_mismatched_architectures_are_rejected(self):
        with self.assertRaises(ValueError):
            nnue_average.average([random_model(5, True).state_dict(), random_model(6, False).state_dict()],
                                 [0.5, 0.5])

    def test_reads_trainer_and_own_checkpoints(self):
        state = random_model(7).state_dict()
        with tempfile.TemporaryDirectory() as tmp:
            trainer, own = os.path.join(tmp, "tiki-1.pt"), os.path.join(tmp, "avg.pt")
            torch.save({"model": state, "optimiser": {}, "scheduler": {}, "epoch": 1}, trainer)
            torch.save({"model": state}, own)
            for path in (trainer, own):
                read = nnue_average.read_state(path)
                for key in state:
                    torch.testing.assert_close(read[key], state[key])


if __name__ == "__main__":
    unittest.main()
