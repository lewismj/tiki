"""The float network, trained here and quantised by export.py into what engine/core/src/nnue/nnue.c runs.

    accumulator = ft_bias + sum of ft rows for the active features (one per perspective): the pieces'
                  and the pawn pairs' (features.py)
    a = pairwise(acc_stm) ++ pairwise(acc_nstm)                                     (hidden)
        pairwise(acc)[i] = clamp(acc[i], 0, 1) * clamp(acc[i + hidden / 2], 0, 1)    (hidden / 2 each)
    h0 = l1[bucket](a)                                                              (L1 = 16)
    a0 = act(h0) = CReLU(h0) ++ SqrCReLU(h0)                                        (32)
         CReLU(x) = clamp(x, 0, 1), SqrCReLU(x) = clamp(x, 0, 1)^2 * 127 / 128
    a1 = act(l2[bucket](a0))                                                        (L2 = 32, so 64)
    y = out[bucket](a0 ++ a1) + h0[14] - h0[15]                                     (96 inputs, plus the skip)
    eval (centipawns) = y * SCALE

The layers after l1 are Stockfish's (SFNNv20: both activations of each layer, the output reading every
layer's, and a skip from two of the first layer's sums), with tiki's 16-wide first layer. SqrCReLU's
127 / 128 is what the engine's integer square gives ((x * x) >> 19 of x at 8128 is 126 at most).

The pairwise product (as Stockfish's) is a smooth, square-like activation like SCReLU, but halves
the hidden layer's inputs, and a product is 0 when either value is, which makes the inputs sparse
enough for the engine to skip the zeros.

There is a layer stack (l1 and out) for each of the 32 output buckets (features.output_bucket: the piece
count, and which sides have a queen), so each phase of the game and queen balance gets its own
reading of the same accumulator.

l1 is trained as the sum of each bucket's own layer and one shared by every bucket (as Stockfish's
trainer does), so buckets that are rarely seen start from what the common ones learnt and only learn
their difference. The own layers start at zero, so every bucket starts as the shared layer, and the
output layers all start the same. l1_weights() / l1_biases() add the two, and that is all the engine
sees.

With factoriser=True, each feature's row is the sum of its own row and a row shared by the same piece
on the same square in every king bucket (704 shared rows), the same idea for the feature transformer.
feature_weights() adds the two.
"""

import torch
import torch.nn.functional as F
from torch import nn

from .features import INPUTS, KING_BUCKETS, OUTPUT_BUCKETS, PIECE_SQUARES, TOTAL_INPUTS

HIDDEN = 512    # Feature transformer width per perspective.
L1 = 16         # First hidden layer width, per bucket.
L2 = 32         # Second hidden layer width, per bucket (its inputs are L1's two activations, 2 * L1).
OUT_INPUTS = 2 * L1 + 2 * L2    # 96: both layers' activations.
SKIP = (14, 15)                 # The first layer's sums whose difference is added to the output.
SQR_SCALE = 127 / 128           # SqrCReLU's top, as the engine's integer square gives it.
QA = 255        # Accumulator scale (an activation of 1.0 is QA).
QI = 127        # Hidden layer input scale: (x * y) >> 9 of two clamped (0..QA) values is 0..127.
QW = 64         # Hidden layer weight scale.
QH = QI * QW    # Hidden layer sum (and bias) scale, 8128: an activation of 1.0 is QH.
QO = 2048       # Output layer weight scale (int16; the output bias is at QH * QO).
SCALE = 400     # Network output to centipawns, also the scale of the score -> win probability sigmoid.

# Feature transformer weights are clipped so the int16 accumulator can't overflow: 32 piece features
# plus the bias at FT_CLIP (505 at QA), and the at most 120 pawn pairs (C(16, 2)) at PAIR_CLIP (127 at QA,
# int8 range, as Stockfish's pair weights are): 33 * 505 + 120 * 127 = 31905 < 32767.
FT_CLIP = 1.98
PAIR_CLIP = 127 / QA
# Both hidden layers' weights fit int8 (u8 input x int8 weight pairs sum in int16 in the engine's SIMD).
# The output weights are int16 in the file, so they could be larger; they're clipped to the same range on
# purpose, as Stockfish's trainer clips its output layer, which keeps the output a moderate sum of its 96
# inputs. At QO they use +-4064 of int16 (at QW = 64 only +-127, and their rounding was then the largest
# quantisation error, about 4 cp sd of eval).
L1_CLIP = 127 / QW
OUT_CLIP = 127 / 64


def pairwise(acc):
    """[batch, hidden] accumulators to [batch, hidden / 2] hidden layer inputs: the first half times the
    second, each clamped to 0..1."""
    half = acc.shape[1] // 2
    return torch.clamp(acc[:, :half], 0.0, 1.0) * torch.clamp(acc[:, half:], 0.0, 1.0)


def crelu(x):
    return torch.clamp(x, 0.0, 1.0)


def activations(x):
    """CReLU(x) ++ SqrCReLU(x) along the last dimension."""
    c = crelu(x)
    return torch.cat([c, c * c * SQR_SCALE], dim=-1)


class TikiNNUE(nn.Module):

    def __init__(self, hidden=HIDDEN, factoriser=False):
        super().__init__()
        self.hidden = hidden
        self.ft = nn.EmbeddingBag(TOTAL_INPUTS, hidden, mode="sum")
        self.ft_bias = nn.Parameter(torch.zeros(hidden))
        nn.init.normal_(self.ft.weight, mean=0.0, std=0.05)
        with torch.no_grad():
            self.ft.weight[INPUTS:].zero_()     # Pawn pairs start at nothing: they learn what they add.

        # Hidden layer: rows b * L1 .. b * L1 + L1 - 1 are bucket b's, plus the shared layer.
        self.l1 = nn.Linear(hidden, OUTPUT_BUCKETS * L1)
        self.l1_shared = nn.Linear(hidden, L1)
        nn.init.zeros_(self.l1.weight)
        nn.init.zeros_(self.l1.bias)
        # The pairwise inputs are mostly 0 or small, a wider start than the default (1 / sqrt(fan in))
        # so the hidden units start inside CReLU's range with some spread.
        nn.init.normal_(self.l1_shared.weight, mean=0.0, std=2.0 / hidden ** 0.5)
        nn.init.constant_(self.l1_shared.bias, 0.25)

        # Second hidden layer and output layer, per bucket, every bucket starting the same.
        self.l2_weight = nn.Parameter(torch.empty(OUTPUT_BUCKETS, L2, 2 * L1))
        self.l2_bias = nn.Parameter(torch.empty(OUTPUT_BUCKETS, L2))
        self.out_weight = nn.Parameter(torch.empty(OUTPUT_BUCKETS, OUT_INPUTS))
        self.out_bias = nn.Parameter(torch.zeros(OUTPUT_BUCKETS))
        l2_start = torch.empty(L2, 2 * L1).normal_(0.0, 1.0 / (2 * L1) ** 0.5)
        out_start = torch.empty(OUT_INPUTS).normal_(0.0, 1.0 / OUT_INPUTS ** 0.5)
        with torch.no_grad():
            self.l2_weight.copy_(l2_start.expand(OUTPUT_BUCKETS, L2, 2 * L1))
            self.l2_bias.fill_(0.25)
            self.out_weight.copy_(out_start.expand(OUTPUT_BUCKETS, OUT_INPUTS))

        # The shared rows start at zero, so a factorised net starts out as the unfactorised one (made
        # last, so the other layers draw the same random numbers either way).
        self.factor = nn.EmbeddingBag(PIECE_SQUARES, hidden, mode="sum") if factoriser else None
        if self.factor is not None:
            nn.init.zeros_(self.factor.weight)

    @property
    def factorised(self):
        return self.factor is not None

    def accumulators(self, stm_indices, stm_offsets, nstm_indices, nstm_offsets):
        if self.factor is None:
            us = self.ft(stm_indices, stm_offsets) + self.ft_bias
            them = self.ft(nstm_indices, nstm_offsets) + self.ft_bias
        else:
            # One lookup in the summed rows: looking up the 704 shared rows separately is much slower
            # (each is hit thousands of times a batch, which serialises its backward pass).
            weights = self.feature_weights()
            us = F.embedding_bag(stm_indices, weights, stm_offsets, mode="sum") + self.ft_bias
            them = F.embedding_bag(nstm_indices, weights, nstm_offsets, mode="sum") + self.ft_bias
        return us, them

    def forward(self, stm_indices, stm_offsets, nstm_indices, nstm_offsets, bucket):
        """Inputs as for nn.EmbeddingBag (flat indices, start offset per sample) and each sample's
        output bucket. Returns eval / SCALE."""
        us, them = self.accumulators(stm_indices, stm_offsets, nstm_indices, nstm_offsets)
        a = torch.cat([pairwise(us), pairwise(them)], dim=1)                # [batch, hidden]
        bucket = bucket.long()
        # Every bucket's hidden layer at once (one matrix product), then each sample's own.
        rows = (bucket * L1).unsqueeze(1) + torch.arange(L1, device=a.device)   # [batch, L1]
        h0 = self.l1(a).gather(1, rows) + self.l1_shared(a)                 # [batch, L1]
        a0 = activations(h0)                                                # [batch, 2 * L1]
        h1 = torch.bmm(self.l2_weight[bucket], a0.unsqueeze(2)).squeeze(2) + self.l2_bias[bucket]
        a1 = activations(h1)                                                # [batch, 2 * L2]
        y = (torch.cat([a0, a1], dim=1) * self.out_weight[bucket]).sum(dim=1) + self.out_bias[bucket]
        return y + h0[:, SKIP[0]] - h0[:, SKIP[1]]

    def feature_weights(self):
        """Each feature's row as the engine uses it, [TOTAL_INPUTS, hidden]: with the factoriser, a piece
        feature's own row plus its shared row (the pawn pair rows have no shared rows)."""
        if self.factor is None:
            return self.ft.weight
        own = self.ft.weight[:INPUTS].view(KING_BUCKETS, PIECE_SQUARES, self.hidden)
        pieces = (own + self.factor.weight).view(INPUTS, self.hidden)
        return torch.cat([pieces, self.ft.weight[INPUTS:]], dim=0)

    def l1_weights(self):
        """Each bucket's hidden layer weights as the engine uses them, [OUTPUT_BUCKETS, L1, hidden]:
        its own plus the shared layer."""
        own = self.l1.weight.view(OUTPUT_BUCKETS, L1, self.hidden)
        return own + self.l1_shared.weight

    def l1_biases(self):
        """[OUTPUT_BUCKETS, L1], own plus shared."""
        return self.l1.bias.view(OUTPUT_BUCKETS, L1) + self.l1_shared.bias

    @torch.no_grad()
    def clip_weights(self):
        """Clips what is quantised. Where a layer is a sum (factoriser, the shared hidden layer), it's
        the sum that is clipped: the shared part first, then each own part is moved to bring its sum
        into range."""
        if self.factor is None:
            self.ft.weight[:INPUTS].clamp_(-FT_CLIP, FT_CLIP)
        else:
            shared = self.factor.weight.clamp_(-FT_CLIP, FT_CLIP)
            own = self.ft.weight[:INPUTS].view(KING_BUCKETS, PIECE_SQUARES, self.hidden)
            own.copy_((own + shared).clamp_(-FT_CLIP, FT_CLIP) - shared)
        self.ft.weight[INPUTS:].clamp_(-PAIR_CLIP, PAIR_CLIP)
        self.ft_bias.clamp_(-FT_CLIP, FT_CLIP)
        shared = self.l1_shared.weight.clamp_(-L1_CLIP, L1_CLIP)
        own = self.l1.weight.view(OUTPUT_BUCKETS, L1, self.hidden)
        own.copy_((own + shared).clamp_(-L1_CLIP, L1_CLIP) - shared)
        self.l2_weight.clamp_(-L1_CLIP, L1_CLIP)
        self.out_weight.clamp_(-OUT_CLIP, OUT_CLIP)


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
