#ifndef TIKI_HCE_EVALUATION_H
#define TIKI_HCE_EVALUATION_H

#include "../types.h"
#include "../board.h"
#include "../bitboard_ops.h"
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

/** For passed pawns, look up rank of square. */
static const int rank_of_square[64] = {
        7, 7, 7, 7, 7, 7, 7, 7,
        6, 6, 6, 6, 6, 6, 6, 6,
        5, 5, 5, 5, 5, 5, 5, 5,
        4, 4, 4, 4, 4, 4, 4, 4,
        3, 3, 3, 3, 3, 3, 3, 3,
        2, 2, 2, 2, 2, 2, 2, 2,
        1, 1, 1, 1, 1, 1, 1, 1,
        0, 0, 0, 0, 0, 0, 0, 0
};

static const weight_type semi_open_file_bonus = 10;
static const weight_type open_file_bonus = 15;

/* Mobility units from Fruit. */
static const weight_type bishop_unit = 4;
static const weight_type queen_unit = 9;
static const weight_type bishop_mobility_opening = 5;
static const weight_type bishop_mobility_endgame = 5;
static const weight_type queen_mobility_opening = 1;
static const weight_type queen_mobility_endgame = 2;

static const weight_type king_shield_bonus = 5;

/*
 * eval_items[]
 *  0 = game phase score
 *  1 = score, if opening phase,
 *  2 = score, if endgame phase.
 */

static inline_always void eval_pawn_mobility(const board_t* const board,
                                             pawn_type pt,
                                             weight_type multiplier,
                                             weight_type* eval_items) {

    /* Check, doubled up pawns, rook open files etc. Some engines seem to
     * register rook open file as rook semi open and rook open etc..
     * Probably skews the eval. very marginally, which likely makes no difference.
     * The whole handcrafted eval. should be thrown out in favour of NNUE style
     * approach.
     */
    for (int file = 0; file < 8; file++) {
        bitboard pawns_on_file = board->pieces[pt] & bitboard_const.file_masks[file];
        if ( pawns_on_file ) {
            int num_pawns_on_file = pop_count(pawns_on_file);
            eval_items[1] += multiplier * num_pawns_on_file * double_or_isolated_pawn_penalty[0];
            eval_items[2] += multiplier * num_pawns_on_file * double_or_isolated_pawn_penalty[1];
        }
    }

}


static inline_always void evaluate0(const board_t* const board, weight_type* eval_items) {
    for (int i = 0; i < 12; i++) {
        bitboard pieces = board->pieces[i];
        int num_bits = pop_count(pieces);
        if (include_in_game_phase_scoring[i] != 0)
            eval_items[0] += num_bits * include_in_game_phase_scoring[i] * material_weights[i];

        eval_items[1] += num_bits * material_weights[i];
        eval_items[2] += num_bits * material_weights[12 + i];

        int opening_offset = opening_offsets[i];
        int endgame_offset = endgame_offsets[i];


        while (pieces) {
            square sq = get_lsb_and_pop_bit(&pieces);
            square mapped_sq = i > 5 ? square_map[sq] : sq;

            eval_items[1] += positional_multiplier[i] * positional_weights[opening_offset + mapped_sq];
            eval_items[2] += positional_multiplier[i] * positional_weights[endgame_offset + mapped_sq];

            switch (i) {
                case P: {
                    if (!(board->pieces[P] & evaluation_mask_instance.isolated_file_mask[sq])) {
                        eval_items[1] += double_or_isolated_pawn_penalty[0];
                        eval_items[2] += double_or_isolated_pawn_penalty[1];
                    }
                    if (!(board->pieces[p] & evaluation_mask_instance.passed_pawn_mask[white][sq])) {
                        eval_items[1] += passed_pawn_bonus[rank_of_square[sq]];
                        eval_items[2] += passed_pawn_bonus[rank_of_square[sq]];
                    }
                }
                    break;
                case p: {
                    if (!(board->pieces[p] & evaluation_mask_instance.isolated_file_mask[sq])) {
                        eval_items[1] -= double_or_isolated_pawn_penalty[0];
                        eval_items[2] -= double_or_isolated_pawn_penalty[1];
                    }
                    if (!(board->pieces[P] & evaluation_mask_instance.passed_pawn_mask[black][sq])) {
                        eval_items[1] -= passed_pawn_bonus[rank_of_square[mapped_sq]];
                        eval_items[2] -= passed_pawn_bonus[rank_of_square[mapped_sq]];
                    }
                }
                    break;
                case B: {
                    int bits = pop_count(bishop_attack(sq, board->occupancy[both]));
                    eval_items[1] += (bits - bishop_unit) * bishop_mobility_opening;
                    eval_items[2] += (bits - bishop_unit) * bishop_mobility_endgame;
                }
                    break;
                case b: {
                    int bits = pop_count(bishop_attack(sq, board->occupancy[both]));
                    eval_items[1] -= (bits - bishop_unit) * bishop_mobility_opening;
                    eval_items[2] -= (bits - bishop_unit) * bishop_mobility_endgame;
                }
                    break;
                case R: {
                    /* Check for semi-open files, could be done as part of pawn mobility, but there
                     * are usually fewer rooks than files on the board, so we can perform corresponding
                     * fewer number of bitboard comparisons.
                     */
                    if (!(board->pieces[P] & bitboard_const.file_masks[rank_of_square[sq]])) {
                        if (!(board->pieces[p] & bitboard_const.file_masks[rank_of_square[sq]])) {
                            /* Open file bonus. */
                            eval_items[0] += open_file_bonus;
                            eval_items[1] += open_file_bonus;
                        } else {
                            /* Semi open file bonus. */
                            eval_items[0] += semi_open_file_bonus;
                            eval_items[1] += semi_open_file_bonus;
                        }
                    }
                }
                    break;
                case r: {
                    if (!(board->pieces[p] & bitboard_const.file_masks[rank_of_square[sq]])) {
                        if (!(board->pieces[P] & bitboard_const.file_masks[rank_of_square[sq]])) {
                            /* Open file bonus. */
                            eval_items[0] += open_file_bonus;
                            eval_items[1] += open_file_bonus;
                        } else {
                            /* Semi open file bonus. */
                            eval_items[0] += semi_open_file_bonus;
                            eval_items[1] += semi_open_file_bonus;
                        }
                    }
                }
                    break;
                case Q: {
                    int bits = pop_count(queen_attack(sq, board->occupancy[both]));
                    eval_items[1] += (bits - queen_unit) * queen_mobility_opening;
                    eval_items[2] += (bits - queen_unit) * queen_mobility_endgame;
                }
                    break;
                case q: {
                    int bits = pop_count(queen_attack(sq, board->occupancy[both]));
                    eval_items[1] -= (bits - queen_unit) * queen_mobility_opening;
                    eval_items[2] -= (bits - queen_unit) * queen_mobility_endgame;
                }
                    break;
                case K: {
                    /* If King 'attacks' own pieces, then it has a shield. */
                    int bits = pop_count(king_attack(sq) & board->occupancy[white]);
                    eval_items[1] += bits * king_shield_bonus;
                    eval_items[2] += bits * king_shield_bonus;
                }
                    break;
                case k: {
                    int bits = pop_count(king_attack(sq) & board->occupancy[black]);
                    eval_items[1] -= bits * king_shield_bonus;
                    eval_items[2] -= bits * king_shield_bonus;
                }
                    break;
                default:
                    break;
            }
        }
    }
}


static inline_always weight_type eval_hce(const board_t* const board) {
    weight_type phases[2] = {6192, 518};
    weight_type score[3] = {0, 0, 0};
    evaluate0(board, score);
    eval_pawn_mobility(board, white_pawns, 1, score);
    eval_pawn_mobility(board, black_pawns, -1, score);

    /* this can be generalized, but not worth it, the handcrafted eval approach is
     * nowadays only useful for testing search, not for actual gameplay. */
    int res;
    if (score[0] > phases[0]) res = score[1];
    else if (score[0] < phases[1]) res = score[2];
    else res = score[1] + (score[0] - phases[0]) * (score[2] - score[1]) / (phases[1] - phases[0]);

    return board->side == white ? res : -res;
}

#endif