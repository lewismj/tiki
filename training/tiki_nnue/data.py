"""Training data written by 'tiki_app datagen' (engine/core/src/datagen.h): one position per line,

    <fen> | <score> | <result>

with the score in centipawns and the result (1.0 / 0.5 / 0.0) both from white's point of view.
Samples are converted to the side to move's point of view, which is what the network evaluates.
"""

import torch
from torch.utils.data import Dataset

from .features import BLACK, active_features, parse_fen


def parse_line(line):
    """Returns (stm features, nstm features, score, result), score and result for the side to move."""
    fen, score, result = (field.strip() for field in line.split("|"))
    pieces, side = parse_fen(fen)
    score, result = float(score), float(result)
    if side == BLACK:
        score, result = -score, 1.0 - result
    return active_features(pieces, side), active_features(pieces, side ^ 1), score, result


class TextDataset(Dataset):
    """Loads the lines into memory and parses them as they're used. Fine for a few tens of millions
    of positions; beyond that a binary pre-processed format would be the next step."""

    def __init__(self, paths):
        self.lines = []
        for path in paths:
            with open(path) as file:
                self.lines.extend(line for line in file if line.strip())

    def __len__(self):
        return len(self.lines)

    def __getitem__(self, i):
        return parse_line(self.lines[i])


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
