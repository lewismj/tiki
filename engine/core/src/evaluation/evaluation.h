#ifndef TIKI_EVALUATION_H
#define TIKI_EVALUATION_H

#include "../types.h"
#include "../board.h"
#include "evaluation_weights.h"
#include "evaluation_mask.h"



static const int piece_types[12] = { 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5 };

static const int opening_offsets[12] = {
    0 * 64, 1 * 64, 2 * 64, 3 * 64, 4 * 64, 5 * 64,
    0 * 64, 1 * 64, 2 * 64, 3 * 64, 4 * 64, 5 * 64
};

static const int endgame_offsets[12] = {
    6 * 64 + 0 * 64, 6 * 64 + 1 * 64, 6 * 64 + 2 * 64,
    6 * 64 + 3 * 64, 6 * 64 + 4 * 64, 6 * 64 + 5 * 64,
    6 * 64 + 0 * 64, 6 * 64 + 1 * 64, 6 * 64 + 2 * 64,
    6 * 64 + 3 * 64 + 0 * 64, 6 * 64 + 4 * 64, 6 * 64 + 5 * 64
};

static inline_always void material_and_position_score(board_t* board, weight_type* phase_score) {
    for (int i = 0; i < 12; i++) {
        bitboard pieces = board->pieces[i];

        int n = pop_count(pieces);
        phase_score[0] += n * material_weights[i];
        phase_score[1] += n * material_weights[12 + i];

        int opening_offset = opening_offsets[i];
        int endgame_offset = endgame_offsets[i];

        while (pieces) {
            square sq = get_lsb_and_pop_bit(&pieces);
            if (i < 6) sq = square_map[sq];
            phase_score[0] += positional_weights[opening_offset + sq];
            phase_score[1] += positional_weights[endgame_offset + sq];
        }
    }
}

static inline_always weight_type evaluate(board_t* board) {
    weight_type scores[2] = {0, 0};
   material_and_position_score(board,scores);
   return scores[0];
}

#endif
