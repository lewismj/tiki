#ifndef TIKI_EVALUATION_H
#define TIKI_EVALUATION_H

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

static const weight_type rook_semi_open_file_bonus = 10;
static const weight_type rook_open_file_bonus = 15;

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

static inline_always void pawn_and_rook_mobility(const board_t* const board,
                                                 pawn_type pt,
                                                 weight_type multiplier,
                                                 weight_type* eval_items) {
    int opponents_pawns = (pt == white_pawns) ? black_pawns : white_pawns;

    for (int file = 0; file < 8; file++) {

        /* Pawn and Rook Mobility. */
        bitboard pawns_on_file = board->pieces[pt] & evaluation_mask_instance.file_masks[file];
        int num_pawns_on_file = pop_count(pawns_on_file);

        if (num_pawns_on_file > 0) { /* We have pawns, so check if isolated. */
            int m = pop_count(pawns_on_file & evaluation_mask_instance.inv_isolated_file_mask[file]);
            if (m) {
                eval_items[1] += multiplier * m * double_or_isolated_pawn_penalty[0];
                eval_items[2] += multiplier * m * double_or_isolated_pawn_penalty[1];
            }
            /*
             * We can have only one passed pawn per file, so take the pt pawn and check.
             * i.e. if we have two pawns on a file, we consider just the front pawn for
             * passed pawn check.
             */
            square sq = trailing_zero_count(pawns_on_file);
            int mask_index = (pt == white_pawns) ? 0 : 1;
            if (!(evaluation_mask_instance.passed_pawn_mask[mask_index][sq] & board->pieces[opponents_pawns])) {
                int bonus = multiplier * passed_pawn_bonus[rank_of_square[sq]];
                eval_items[1] += bonus;
                eval_items[2] += bonus;
            }
            /* Check to see if the file is semi open to a rook. */
            bitboard rooks_on_file = board->pieces[pt+3] & evaluation_mask_instance.file_masks[file];
            if (rooks_on_file) {
                /* strictly speaking is it file open if the black pawn is advanced and the white rook is
                 * further up the board? */
                if (!(board->pieces[pt] & evaluation_mask_instance.file_masks[file])) {
                    /* semi open file for rook. */
                    eval_items[1] += multiplier * rook_semi_open_file_bonus;
                    eval_items[2] += multiplier * rook_semi_open_file_bonus;
                }

            }
        } else if (num_pawns_on_file == 0) {
            /* Check to see if our rook has an open file. */
            bitboard rooks_on_file = board->pieces[pt+3] & evaluation_mask_instance.file_masks[file];
            if (rooks_on_file && ! (board->pieces[opponents_pawns] & evaluation_mask_instance.file_masks[file])) {
                /* open file for rook. */
                printf("open file.\n");
                eval_items[1] += multiplier * rook_open_file_bonus;
                eval_items[2] += multiplier * rook_open_file_bonus;
            }
        }
        if (num_pawns_on_file > 1) { /* If we have doubled pawns. */
            eval_items[1] += multiplier * num_pawns_on_file * double_or_isolated_pawn_penalty[0];
            eval_items[2] += multiplier * num_pawns_on_file * double_or_isolated_pawn_penalty[1];
        }

    }
}

static inline_always void mobility_scores(const board_t* const board, weight_type* eval_items) {
    static int mobility[6] = {B, b, Q, q, K, k};
    for (int i=0; i<4; i++) {
        int p = mobility[i];
        bitboard pieces = board->pieces[p];
        while (pieces) {
            square sq = get_lsb_and_pop_bit(&pieces);
            switch (p) {
                case B: {
                    int bits = pop_count(bishop_attack(sq, board->occupancy[both]));
                    eval_items[1] += (bits-bishop_unit) * bishop_mobility_opening;
                    eval_items[2] += (bits-bishop_unit) * bishop_mobility_endgame;
                }
                break;
                case b: {
                    int bits = pop_count(bishop_attack(sq, board->occupancy[both]));
                    eval_items[1] -= (bits-bishop_unit) * bishop_mobility_opening;
                    eval_items[2] -= (bits-bishop_unit) * bishop_mobility_endgame;
                }
                break;
                case Q: {
                    int bits = pop_count(queen_attack(sq, board->occupancy[both]));
                    eval_items[1] += (bits-queen_unit) * queen_mobility_opening;
                    eval_items[2] += (bits-queen_unit) * queen_mobility_endgame;
                }
                break;
                case q: {
                    int bits = pop_count(queen_attack(sq, board->occupancy[both]));
                    eval_items[1] -= (bits-queen_unit) * queen_mobility_opening;
                    eval_items[2] -= (bits-queen_unit) * queen_mobility_endgame;
                }
                break;
                case K: {
                    /* If King 'attacks' own pieces, then it has a shield. */
                    int bits = pop_count(king_attack(sq) & board->occupancy[white]);
                    eval_items[1] += bits*king_shield_bonus;
                    eval_items[2] += bits*king_shield_bonus;
                }
                break;
                case k: {
                    int bits = pop_count(king_attack(sq) & board->occupancy[black]);
                    eval_items[1] -= bits*king_shield_bonus;
                    eval_items[2] -= bits*king_shield_bonus;
                }
                break;
                default:
                    break;
            }
        }
    }
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
    pawn_and_rook_mobility(board, white_pawns, 1, score);
    pawn_and_rook_mobility(board, black_pawns, -1, score);
    mobility_scores(board, score);

    printf("game phase: %d, open: %d, end: %d\n",score[0], score[1], score[2]);

    return score[0];
}

#endif
