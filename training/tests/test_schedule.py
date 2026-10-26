import argparse
import os
import sys
import unittest

import torch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from train import add_schedule_arguments, load_scheduler, make_scheduler


def parse(*argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--epochs", type=int, default=20)
    add_schedule_arguments(parser)
    return parser, parser.parse_args(list(argv))


def learning_rates(args):
    """The learning rate of each epoch, stepping the schedule after each as the trainers do."""
    optimiser = torch.optim.Adam([torch.zeros(1, requires_grad=True)], lr=args.lr)
    scheduler = make_scheduler(optimiser, args)
    rates = []
    for _ in range(args.epochs):
        rates.append(optimiser.param_groups[0]["lr"])
        optimiser.step()
        scheduler.step()
    return rates, optimiser, scheduler


class ScheduleTest(unittest.TestCase):
    def test_step_is_the_default(self):
        _, args = parse("--epochs", "9", "--lr-drop", "6")
        rates, _, _ = learning_rates(args)
        for rate, expected in zip(rates, [1e-3] * 6 + [1e-4] * 3):
            self.assertAlmostEqual(rate, expected)

    def test_cosine_falls_from_lr_to_lr_min(self):
        _, args = parse("--epochs", "12", "--lr-schedule", "cosine", "--lr-min", "1e-5")
        rates, _, _ = learning_rates(args)
        self.assertAlmostEqual(rates[0], 1e-3)
        self.assertAlmostEqual(rates[-1], 1e-5)
        self.assertTrue(all(a > b for a, b in zip(rates, rates[1:])))
        # Halfway between the first and last epoch it's halfway between the two rates.
        _, args = parse("--epochs", "13", "--lr-schedule", "cosine", "--lr-min", "1e-5")
        rates, _, _ = learning_rates(args)
        self.assertAlmostEqual(rates[6], (1e-3 + 1e-5) / 2)

    def test_resume_continues_the_schedule(self):
        parser, args = parse("--epochs", "12", "--lr-schedule", "cosine")
        rates, _, _ = learning_rates(args)
        # A checkpoint after 5 of 12 epochs carries on where the full run would be.
        full_optimiser = torch.optim.Adam([torch.zeros(1, requires_grad=True)], lr=args.lr)
        full = make_scheduler(full_optimiser, args)
        for _ in range(5):
            full_optimiser.step()
            full.step()
        resumed_optimiser = torch.optim.Adam([torch.zeros(1, requires_grad=True)], lr=args.lr)
        resumed = make_scheduler(resumed_optimiser, args)
        load_scheduler(resumed, full.state_dict(), args, parser)
        resumed_optimiser.load_state_dict(full_optimiser.state_dict())
        self.assertAlmostEqual(resumed_optimiser.param_groups[0]["lr"], rates[5])

    def test_resume_with_the_other_schedule_is_an_error(self):
        parser, step = parse()
        _, _, step_scheduler = learning_rates(step)
        _, cosine = parse("--lr-schedule", "cosine")
        optimiser = torch.optim.Adam([torch.zeros(1, requires_grad=True)], lr=cosine.lr)
        with self.assertRaises(SystemExit):
            load_scheduler(make_scheduler(optimiser, cosine), step_scheduler.state_dict(), cosine, parser)


if __name__ == "__main__":
    unittest.main()
