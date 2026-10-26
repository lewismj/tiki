"""Training data written by 'tiki datagen' (engine/core/src/datagen.h): one position per line,

    <fen> | <score> | <result>

with the score in centipawns and the result (1.0 / 0.5 / 0.0) both from white's point of view.
Samples are converted to the side to move's point of view, which is what the network evaluates.

Training reads the binary form of this (binary.py) and computes a batch's features on the training
device (batch_tensors). parse_line and collate are the straightforward version, which the tests
check batch_tensors against.
"""

import numpy as np
import torch

from .binary import PAD
from .features import BLACK, KING, PIECE_SQUARES, WHITE, active_features, parse_fen


def parse_line(line):
    """Returns (stm features, nstm features, score, result), score and result for the side to move."""
    fen, score, result = (field.strip() for field in line.split("|"))
    pieces, side = parse_fen(fen)
    score, result = float(score), float(result)
    if side == BLACK:
        score, result = -score, 1.0 - result
    return active_features(pieces, side), active_features(pieces, side ^ 1), score, result


def collate(samples):
    """Batches samples into EmbeddingBag inputs: flat index tensors plus a start offset per sample."""
    def bag(lists):
        offsets, flat = [], []
        for features in lists:
            offsets.append(len(flat))
            flat.extend(features)
        return torch.tensor(flat, dtype=torch.long), torch.tensor(offsets, dtype=torch.long)

    stm_indices, stm_offsets = bag(s[0] for s in samples)
    nstm_indices, nstm_offsets = bag(s[1] for s in samples)
    score = torch.tensor([s[2] for s in samples], dtype=torch.float32)
    result = torch.tensor([s[3] for s in samples], dtype=torch.float32)
    return stm_indices, stm_offsets, nstm_indices, nstm_offsets, score, result


def perspective_features(pieces, perspective):
    """The features of 'pieces' (int32 [n, 32], binary.RECORD's pieces) for each row's 'perspective'
    (int32 [n]), as EmbeddingBag inputs. features.feature_index for a whole batch, as tensor ops."""
    valid = pieces != PAD
    piece, sq = pieces >> 6, pieces & 63

    king_piece = torch.where(perspective == BLACK, KING[BLACK], KING[WHITE])
    is_king = valid & (piece == king_piece[:, None])
    king = sq.gather(1, is_king.to(torch.uint8).argmax(dim=1, keepdim=True)).squeeze(1)

    flip = perspective * 56
    oriented_king = king ^ flip
    oriented_sq = sq ^ flip[:, None]
    mirror = ((oriented_king & 7) >= 4).int() * 7      # Horizontal mirror, king on files a-d.
    oriented_king = oriented_king ^ mirror
    oriented_sq = oriented_sq ^ mirror[:, None]
    bucket = (oriented_king >> 3) * 4 + (oriented_king & 7)

    kind_type = piece % 6
    theirs = ((piece >= 6).int() != perspective[:, None]).int()
    kind = torch.where(kind_type == 5, 10, kind_type * 2 + theirs)
    index = bucket[:, None] * PIECE_SQUARES + kind * 64 + oriented_sq

    counts = valid.sum(dim=1)
    return index[valid].long(), (torch.cumsum(counts, 0) - counts).long()


def batch_tensors(records, device="cpu"):
    """collate's tensors, on 'device', for a batch of binary.RECORD records."""
    def tensor(array):
        return torch.from_numpy(np.ascontiguousarray(array)).to(device)

    pieces = tensor(records["pieces"].view("<i2")).int() & 0xFFFF
    side = tensor(records["side"]).int()
    stm_indices, stm_offsets = perspective_features(pieces, side)
    nstm_indices, nstm_offsets = perspective_features(pieces, side ^ 1)
    score = tensor(records["score"]).float()
    result = tensor(records["result"]).float() / 2
    return stm_indices, stm_offsets, nstm_indices, nstm_offsets, score, result


VALIDATION_BLOCK = 4096


def validation_split(count, fraction, rng, block=VALIDATION_BLOCK):
    """Splits record indices 0..count-1 into (training, validation), holding out random blocks of
    consecutive records rather than single positions. Each game's positions are consecutive in the
    data files, so validation is (nearly) whole games the network never trains on: only the game cut
    at each block's edge is on both sides. Small data sets get smaller blocks, so there are enough
    to choose from."""
    block = max(1, min(block, count // 100))
    blocks = -(-count // block)
    held = min(max(1, round(blocks * fraction)), max(1, blocks - 1))
    chosen = np.zeros(blocks, dtype=bool)
    chosen[rng.choice(blocks, held, replace=False)] = True
    validating = np.repeat(chosen, block)[:count]
    return np.flatnonzero(~validating), np.flatnonzero(validating)
