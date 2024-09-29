#ifndef TIKI_EVALUATION_H
#define TIKI_EVALUATION_H

#include "../types.h"
#include "../board.h"
#include "evaluation_weights.h"
#include "evaluation_mask.h"

/**
 * Piece types:
 *  P, N, B, R, Q, K, p, n, b, r, q, k
 */
static const int piece_types[12] = { 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5 };

/**
 * Offsets into piece square tables, num phases * pieces 6 (we score white/black the same).
 */
static const int opening_offsets[2*6] = {
    0 * 64, 1 * 64, 2 * 64, 3 * 64, 4 * 64, 5 * 64,
    0 * 64, 1 * 64, 2 * 64, 3 * 64, 4 * 64, 5 * 64
};

static const int endgame_offsets[2*6] = {
    6 * 64 + 0 * 64, 6 * 64 + 1 * 64, 6 * 64 + 2 * 64,
    6 * 64 + 3 * 64, 6 * 64 + 4 * 64, 6 * 64 + 5 * 64,
    6 * 64 + 0 * 64, 6 * 64 + 1 * 64, 6 * 64 + 2 * 64,
    6 * 64 + 3 * 64 + 0 * 64, 6 * 64 + 4 * 64, 6 * 64 + 5 * 64
};

static const weight_type include_in_game_phase_scoring[12] =
        {0, 1, 1, 1, 1, 0, 0, -1, -1, -1, -1, 0 };

static const weight_type positional_multiplier[12] =
        { 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1};

typedef enum { white_pawns=P, black_pawns=p } pawn_type;

/** Double or Isolated pawn penalty per phase. */
static const weight_type double_or_isolated_pawn_penalty[2] = {-5, -10};

/** Passed pawn bonus per rank  */
static const weight_type passed_pawn_bonus[8] = { 0, 10, 30, 50, 75, 100, 150, 200 };

/*
 * eval_items[]
 *  0 = game phase score
 *  1 = score, if opening phase,
 *  2 = score, if endgame phase.
 */

static inline_always void pawn_mobility_structure(const board_t* const board, pawn_type pt, weight_type* eval_items) {
    bitboard pawns = board->pieces[pt];
    for (int file = 0; file < 8; file++) {
        bitboard pawns_on_file = pawns & evaluation_mask_instance.file_masks[file];
        int n = pop_count(pawns_on_file);

        if (n > 1) { /* If we have doubled pawns. */
            eval_items[1] += n * double_or_isolated_pawn_penalty[0];
            eval_items[2] += n * double_or_isolated_pawn_penalty[1];
        }

        if (n > 0) { /* We have pawns, so check if isolated. */
            int m = pop_count(pawns_on_file & evaluation_mask_instance.inv_isolated_file_mask[file]);
            if (m) {
                eval_items[1] += m * double_or_isolated_pawn_penalty[0];
                eval_items[2] += m * double_or_isolated_pawn_penalty[1];
            }
        }
    }
}

static inline_always void piece_mobility(const board_t* const board,  weight_type* eval_items) {

}

static inline_always void material_and_position_score(const board_t* const board, weight_type* eval_items) {
    for (int i = 0; i < 12; i++) {
        bitboard pieces = board->pieces[i];
        int n = pop_count(pieces);
        if (include_in_game_phase_scoring[i] != 0)
            eval_items[0] += n * include_in_game_phase_scoring[i] * material_weights[i];

        eval_items[1] += n * material_weights[i];
        eval_items[2] += n * material_weights[12 + i];

        int opening_offset = opening_offsets[i];
        int endgame_offset = endgame_offsets[i];

        while (pieces) {
            square sq = get_lsb_and_pop_bit(&pieces);
            if (i > 5) sq = square_map[sq];
            eval_items[1] += positional_multiplier[i]*positional_weights[opening_offset + sq];
            eval_items[2] += positional_multiplier[i]*positional_weights[endgame_offset + sq];
        }
    }
}

static inline_always weight_type evaluate(const board_t* const board) {
    weight_type score[3] = {0, 0, 0};

    material_and_position_score(board,score);
    pawn_mobility_structure(board, white_pawns, score);
    pawn_mobility_structure(board, black_pawns, score);
    piece_mobility(board, score);

    printf("game phase: %d, open: %d, end: %d\n",score[0], score[1], score[2]);

    return score[0];
}

#endif
