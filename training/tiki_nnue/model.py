"""The float network, trained here and quantised by export.py into what engine/core/src/nnue/nnue.c runs.

    accumulator = ft_bias + sum of ft rows for the active features (one per perspective)
    y = out[bucket](SCReLU(acc_stm) ++ SCReLU(acc_nstm)), SCReLU(x) = clamp(x, 0, 1)^2
    eval (centipawns) = y * SCALE

with one output layer per bucket of piece counts (features.output_bucket), so the opening, middlegame
and endgame each get their own weighting of the same accumulator.
"""

import torch
from torch import nn

from .features import INPUTS, OUTPUT_BUCKETS

HIDDEN = 256
QA = 255        # Accumulator scale (an activation of 1.0 is QA).
QB = 64         # Output weight scale.
SCALE = 400     # Network output to centipawns, also the scale of the score -> win probability sigmoid.

# Feature transformer weights are clipped so QA * w fits easily in int16 and 32 features plus the
# bias can't overflow the int16 accumulator (33 * 1.98 * 255 < 32767).
FT_CLIP = 1.98
OUT_CLIP = 32767 / QB / 2


def screlu(x):
    return torch.clamp(x, 0.0, 1.0) ** 2


class TikiNNUE(nn.Module):

    def __init__(self, hidden=HIDDEN):
        super().__init__()
        self.hidden = hidden
        self.ft = nn.EmbeddingBag(INPUTS, hidden, mode="sum")
        self.ft_bias = nn.Parameter(torch.zeros(hidden))
        self.out = nn.Linear(2 * hidden, OUTPUT_BUCKETS)     # Row b is bucket b's output layer.
        nn.init.normal_(self.ft.weight, mean=0.0, std=0.05)
        nn.init.normal_(self.out.weight, mean=0.0, std=0.05)
        nn.init.zeros_(self.out.bias)

    def forward(self, stm_indices, stm_offsets, nstm_indices, nstm_offsets, bucket):
        """Inputs as for nn.EmbeddingBag (flat indices, start offset per sample) and each sample's
        output bucket. Returns eval / SCALE."""
        us = self.ft(stm_indices, stm_offsets) + self.ft_bias
        them = self.ft(nstm_indices, nstm_offsets) + self.ft_bias
        outputs = self.out(torch.cat([screlu(us), screlu(them)], dim=1))    # [batch, OUTPUT_BUCKETS]
        return outputs.gather(1, bucket.long().unsqueeze(1)).squeeze(1)

    @torch.no_grad()
    def clip_weights(self):
        self.ft.weight.clamp_(-FT_CLIP, FT_CLIP)
        self.ft_bias.clamp_(-FT_CLIP, FT_CLIP)
        self.out.weight.clamp_(-OUT_CLIP, OUT_CLIP)


def loss_fn(output, score, result, wdl):
    """MSE between win probabilities: the net's, and a blend of the search score's and the game result.

    output is the net's eval / SCALE, score centipawns and result 1 / 0.5 / 0, both for the side to move.
    wdl is the weight of the game result (0 trains on the search score only).
    """
    target = (1.0 - wdl) * torch.sigmoid(score / SCALE) + wdl * result
    return ((torch.sigmoid(output) - target) ** 2).mean()
