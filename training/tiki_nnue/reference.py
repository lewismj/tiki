"""The network against a fixed set of positions scored by a strong engine, after each epoch: a check that
doesn't depend on the training target, so a run whose target is wrong (its validation loss can't show
that) is seen early.

The reference file has lines 'fen | score | set': the reference engine's score in centipawns from
white's point of view, and a name for the position's source (e.g. lichess, fishtest). Lines starting
with '#' are comments. Each epoch reports, by the reference score's size (|score| bands, BANDS):

    scale   median |net| / |score| (positions with |score| >= MIN_SCALE_SCORE)
    rank    Spearman correlation of the net's scores with the reference's

and, by the size of the net's own score (the same bands, |net| >= MIN_SCALE_SCORE):

    calib   median score / net (signed): what the reference says when the net says that much

Grouped by the reference's score, any net with errors looks too big near 0 and too small at large
scores (regression to the mean: positions the reference scores near 0 include ones the net misjudges
by more than that, and large reference scores include ones whose reason the net can't see). calib
groups by the net's score instead, so it is 1.0 in every band for a net whose scale is right on
average; below 1 the net overstates scores of that size, above 1 it understates them. It is signed
(score / net, not |score| / |net|): near 0 the reference's sign is often the opposite of the net's,
and the absolute ratio would make that look like scale.

and the share of positions with |score| >= 100 where the net has the same sign. With a comparison
network (an exported .nnue, e.g. the current best) its figures are computed once, with the engine's
own integer arithmetic (export.quantised_eval), and shown once at the start; each epoch then adds the
median |net| / |comparison| per set. The run's network is evaluated in floating point, which is
within a few centipawns of its exported version. It takes a fraction of a second per epoch.
"""

import numpy as np
import torch

from . import binary, export
from .data import batch_tensors
from .features import BLACK
from .model import SCALE

BANDS = [(0, 50), (50, 150), (150, 300), (300, 1000)]
MIN_SCALE_SCORE = 20


def read_reference(path):
    """(fens, scores, sets) from a reference file, scores from white's point of view."""
    fens, scores, sets = [], [], []
    with open(path) as file:
        for line in file:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            fields = [field.strip() for field in line.split("|")]
            if len(fields) != 3:
                raise ValueError(f"{path}: expected 'fen | score | set', got '{line}'")
            fens.append(fields[0])
            scores.append(float(fields[1]))
            sets.append(fields[2])
    if not fens:
        raise ValueError(f"{path}: no positions")
    return fens, np.array(scores), np.array(sets)


def _ranks(values):
    ranks = np.empty(len(values))
    ranks[np.argsort(values, kind="stable")] = np.arange(len(values))
    return ranks


def spearman(a, b):
    """Spearman rank correlation (ties ranked in order, which is close enough for scores in cp)."""
    if len(a) < 3:
        return float("nan")
    ra, rb = _ranks(np.asarray(a, float)), _ranks(np.asarray(b, float))
    if ra.std() == 0 or rb.std() == 0:
        return float("nan")
    return float(np.corrcoef(ra, rb)[0, 1])


def measures(net, reference):
    """{'scale': [per band], 'rank': [per band], 'calib': [per band], 'sign': float} of 'net' scores
    against 'reference' scores (both white's point of view). scale and rank are by |reference|,
    calib by |net|."""
    scale, rank, calib = [], [], []
    for low, high in BANDS:
        band = (np.abs(reference) >= low) & (np.abs(reference) < high)
        scaled = band & (np.abs(reference) >= MIN_SCALE_SCORE)
        scale.append(float(np.median(np.abs(net[scaled]) / np.abs(reference[scaled]))) if scaled.any()
                     else float("nan"))
        rank.append(spearman(net[band], reference[band]))
        own = (np.abs(net) >= max(low, MIN_SCALE_SCORE)) & (np.abs(net) < high)
        calib.append(float(np.median(reference[own] / net[own])) if own.any()
                     else float("nan"))
    clear = np.abs(reference) >= 100
    sign = float(np.mean((net[clear] > 0) == (reference[clear] > 0))) if clear.any() else float("nan")
    return {"scale": scale, "rank": rank, "calib": calib, "sign": sign}


class Reference:
    """A reference file's positions, ready to evaluate a model on, and an optional comparison network's
    scores on them."""

    def __init__(self, path, compare=None, device="cpu", batch_size=16384):
        self.path = path
        self.fens, self.scores, self.sets = read_reference(path)
        # binary.encode wants 'fen | score | result'; the score and result aren't used here.
        self.records = binary.encode([f"{fen} | 0 | 0.5" for fen in self.fens])
        self.white = np.where(self.records["side"] == BLACK, -1.0, 1.0)    # side to move -> white's view
        self.device, self.batch_size = device, batch_size
        self.compare_name = compare
        self.compare = None
        if compare:
            network = export.read_network(compare)
            self.compare = np.array([export.quantised_eval(network, fen) for fen in self.fens]) * self.white

    def evaluate(self, model):
        """The model's scores in centipawns, white's point of view."""
        was_training = model.training
        model.eval()
        out = []
        with torch.no_grad():
            for start in range(0, len(self.records), self.batch_size):
                tensors = batch_tensors(self.records[start:start + self.batch_size], self.device)
                out.append((model(*tensors[:5]) * SCALE).float().cpu().numpy())
        model.train(was_training)
        return np.concatenate(out) * self.white

    def _groups(self):
        return [("all", np.ones(len(self.fens), dtype=bool))] + [
            (name, self.sets == name) for name in dict.fromkeys(self.sets)]

    def _header(self, title):
        bands = " ".join(f"{f'{low}-{high}':>9}" for low, high in BANDS)
        return f"  {title}, {len(self.fens):,} positions, by |score|: {bands}"

    @staticmethod
    def _rows(label, m, extra=""):
        return [f"  {label:<18} scale " + " ".join(f"{v:9.2f}" for v in m["scale"]) + f"   sign {m['sign']:.3f}",
                f"  {'':<18} rank  " + " ".join(f"{v:9.3f}" for v in m["rank"]) + extra,
                f"  {'':<18} calib " + " ".join(f"{v:9.2f}" for v in m["calib"])]

    def compare_report(self):
        """Lines for the comparison network (they don't change, so they're shown once), or none."""
        if self.compare is None:
            return []
        lines = [self._header(f"reference: compare {self.compare_name}")]
        for name, chosen in self._groups():
            lines += self._rows(f"{name} compare", measures(self.compare[chosen], self.scores[chosen]))
        return lines

    def report(self, model):
        """Lines comparing the model with the reference scores, overall and for each set, with the
        median |model| / |comparison network| if there is one."""
        net = self.evaluate(model)
        lines = [self._header("reference")]
        for name, chosen in self._groups():
            extra = ""
            if self.compare is not None:
                big = np.abs(self.compare[chosen]) >= MIN_SCALE_SCORE
                ratio = np.abs(net[chosen][big]) / np.abs(self.compare[chosen][big])
                extra = f"   |net|/|compare| {np.median(ratio):.2f}" if big.any() else ""
            lines += self._rows(f"{name} net", measures(net[chosen], self.scores[chosen]), extra)
        return lines
