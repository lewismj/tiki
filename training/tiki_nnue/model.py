"""The float network, trained here and quantised by export.py into what engine/core/src/nnue/nnue.c runs.

    accumulator = ft_bias + sum of ft rows for the active features (one per perspective)
    y = out[bucket](SCReLU(acc_stm) ++ SCReLU(acc_nstm)), SCReLU(x) = clamp(x, 0, 1)^2
    eval (centipawns) = y * SCALE

with one output layer per bucket of piece counts (features.output_bucket), so the opening, middlegame
and endgame each get their own weighting of the same accumulator.

With factoriser=True, each feature's row is the sum of its own row and a row shared by the same piece
on the same square in every king bucket (704 shared rows). The shared rows learn from every position,
so king buckets that are rarely seen start from what the common ones learnt and only learn their
difference. feature_weights() adds the two, and that is all the engine sees: the exported network has
the same size and format either way.

With psqt=True, each feature also has a value per output bucket (PSQT), summed per perspective like
the accumulator, and half the difference between the side to move's sum and the other side's is added
to the output: y += (psqt_stm[bucket] - psqt_nstm[bucket]) / 2. It starts as material values (a piece
is worth the same on every square), so the net starts from material and learns the rest. With the
factoriser too, the PSQT has shared rows the same way.
"""

import torch
import torch.nn.functional as F
from torch import nn

from .features import INPUTS, KING_BUCKETS, OUTPUT_BUCKETS, PIECE_SQUARES

HIDDEN = 512
QA = 255        # Accumulator scale (an activation of 1.0 is QA).
QB = 64         # Output weight scale.
SCALE = 400     # Network output to centipawns, also the scale of the score -> win probability sigmoid.

# Feature transformer weights are clipped so QA * w fits easily in int16 and 32 features plus the
# bias can't overflow the int16 accumulator (33 * 1.98 * 255 < 32767).
FT_CLIP = 1.98
OUT_CLIP = 127 / QB  # Quantised output weights fit in int8 range, so SCReLU(v) * w can use int16 (madd).

# Starting PSQT values in centipawns, by piece type (P N B R Q K), for a piece of the perspective's side
# (the other side's pieces are the negative). Kings have none (feature kind 10 is either king).
PSQT_MATERIAL = (100, 300, 320, 500, 900, 0)


def material_psqt():
    """[PIECE_SQUARES, OUTPUT_BUCKETS] starting PSQT values (eval / SCALE), by feature kind and square."""
    values = torch.zeros(PIECE_SQUARES, OUTPUT_BUCKETS)
    for kind in range(10):
        value = PSQT_MATERIAL[kind // 2] / SCALE
        values[kind * 64:(kind + 1) * 64] = value if kind % 2 == 0 else -value
    return values


def screlu(x):
    return torch.clamp(x, 0.0, 1.0) ** 2


class TikiNNUE(nn.Module):

    def __init__(self, hidden=HIDDEN, factoriser=False, psqt=False):
        super().__init__()
        self.hidden = hidden
        self.ft = nn.EmbeddingBag(INPUTS, hidden, mode="sum")
        self.ft_bias = nn.Parameter(torch.zeros(hidden))
        self.out = nn.Linear(2 * hidden, OUTPUT_BUCKETS)     # Row b is bucket b's output layer.
        nn.init.normal_(self.ft.weight, mean=0.0, std=0.05)
        nn.init.normal_(self.out.weight, mean=0.0, std=0.05)
        nn.init.zeros_(self.out.bias)
        # The shared rows start at zero, so a factorised net starts out as the unfactorised one.
        self.factor = nn.EmbeddingBag(PIECE_SQUARES, hidden, mode="sum") if factoriser else None
        if self.factor is not None:
            nn.init.zeros_(self.factor.weight)
        # PSQT, material to start with: in the shared rows with the factoriser, else in every king bucket.
        self.psqt = nn.Embedding(INPUTS, OUTPUT_BUCKETS) if psqt else None
        self.psqt_factor = nn.Embedding(PIECE_SQUARES, OUTPUT_BUCKETS) if psqt and factoriser else None
        with torch.no_grad():
            if self.psqt_factor is not None:
                nn.init.zeros_(self.psqt.weight)
                self.psqt_factor.weight.copy_(material_psqt())
            elif self.psqt is not None:
                self.psqt.weight.copy_(material_psqt().repeat(KING_BUCKETS, 1))

    @property
    def factorised(self):
        return self.factor is not None

    @property
    def has_psqt(self):
        return self.psqt is not None

    def forward(self, stm_indices, stm_offsets, nstm_indices, nstm_offsets, bucket):
        """Inputs as for nn.EmbeddingBag (flat indices, start offset per sample) and each sample's
        output bucket. Returns eval / SCALE."""
        if self.factor is None:
            us = self.ft(stm_indices, stm_offsets) + self.ft_bias
            them = self.ft(nstm_indices, nstm_offsets) + self.ft_bias
        else:
            # One lookup in the summed rows: looking up the 704 shared rows separately is much slower
            # (each is hit thousands of times a batch, which serialises its backward pass).
            weights = self.feature_weights()
            us = F.embedding_bag(stm_indices, weights, stm_offsets, mode="sum") + self.ft_bias
            them = F.embedding_bag(nstm_indices, weights, nstm_offsets, mode="sum") + self.ft_bias
        outputs = self.out(torch.cat([screlu(us), screlu(them)], dim=1))    # [batch, OUTPUT_BUCKETS]
        if self.psqt is not None:
            weights = self.psqt_weights()
            psqt_us = F.embedding_bag(stm_indices, weights, stm_offsets, mode="sum")
            psqt_them = F.embedding_bag(nstm_indices, weights, nstm_offsets, mode="sum")
            outputs = outputs + (psqt_us - psqt_them) / 2
        return outputs.gather(1, bucket.long().unsqueeze(1)).squeeze(1)

    def feature_weights(self):
        """Each feature's row as the engine uses it, [INPUTS, hidden]: with the factoriser, its own
        row plus its shared row."""
        if self.factor is None:
            return self.ft.weight
        own = self.ft.weight.view(KING_BUCKETS, PIECE_SQUARES, self.hidden)
        return (own + self.factor.weight).view(INPUTS, self.hidden)

    def psqt_weights(self):
        """Each feature's PSQT values as the engine uses them, [INPUTS, OUTPUT_BUCKETS] (None without
        PSQT): with the factoriser, its own plus its shared row."""
        if self.psqt is None:
            return None
        if self.psqt_factor is None:
            return self.psqt.weight
        own = self.psqt.weight.view(KING_BUCKETS, PIECE_SQUARES, OUTPUT_BUCKETS)
        return (own + self.psqt_factor.weight).view(INPUTS, OUTPUT_BUCKETS)

    @torch.no_grad()
    def clip_weights(self):
        """Clips what is quantised. With the factoriser that's the sums, so each own row is moved to
        bring its sum with the shared row into range."""
        if self.factor is None:
            self.ft.weight.clamp_(-FT_CLIP, FT_CLIP)
        else:
            shared = self.factor.weight.clamp_(-FT_CLIP, FT_CLIP)
            own = self.ft.weight.view(KING_BUCKETS, PIECE_SQUARES, self.hidden)
            own.copy_((own + shared).clamp_(-FT_CLIP, FT_CLIP) - shared)
        self.ft_bias.clamp_(-FT_CLIP, FT_CLIP)
        self.out.weight.clamp_(-OUT_CLIP, OUT_CLIP)


def loss_fn(output, score, result, wdl, power=2.0):
    """Mean |error| ** power between win probabilities: the net's, and a blend of the search score's and
    the game result. power 2 is the MSE; above 2 weights large errors more (Stockfish's trainer uses 2.5).

    output is the net's eval / SCALE, score centipawns and result 1 / 0.5 / 0, both for the side to move.
    wdl is the weight of the game result (0 trains on the search score only).
    """
    target = (1.0 - wdl) * torch.sigmoid(score / SCALE) + wdl * result
    error = torch.sigmoid(output) - target
    if power == 2.0:
        return (error ** 2).mean()
    return (error.abs() ** power).mean()
