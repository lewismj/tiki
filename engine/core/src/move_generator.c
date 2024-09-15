#include <stdio.h>
#include "move_generator.h"


static inline_always bitboard knight_attack_wrapper(square s, bitboard ) {
    return knight_attack(s);
}

static inline_always bitboard king_attack_wrapper(square s, bitboard ) {
    return king_attack(s);
}

void generate_white_pawn_moves(board_t* board, move_buffer_t* move_buffer) {
    /* pawn, non-capturing moves. */
    bitboard b = board->pieces[P];
    while (b) {
        square source = get_lsb_and_pop_bit(&b);
        int target_sq = source - 8;
        if (target_sq >= a8 && !is_bit_set(&board->occupancy[both], target_sq)) {
            if (source >= a7 && source <= h7) {
                /* pawn promotion with no capture. */
                move_buffer->moves[move_buffer->index++] = encode_pawn_promotion(source, target_sq, P, Q, 0);
                move_buffer->moves[move_buffer->index++] = encode_pawn_promotion(source, target_sq, P, R, 0);
                move_buffer->moves[move_buffer->index++] = encode_pawn_promotion(source, target_sq, P, B, 0);
                move_buffer->moves[move_buffer->index++] = encode_pawn_promotion(source, target_sq, P, N, 0);
            } else {
                /* No capture, single square move. */
                move_buffer->moves[move_buffer->index++] = encode_no_capture(source, target_sq, P);

                /* Check to see if we can add a double push. */
                int double_push_sq = source - 16;
                if (source >= a2 && source <= h2 && !is_bit_set(&board->occupancy[both], double_push_sq)) {
                    move_buffer->moves[move_buffer->index++] = encode_pawn_double_push(source, double_push_sq, P);
                }
            }
        }

        /* pawn captures. */
        bitboard attacks = pawn_attack(source, white) & board->occupancy[black];
        while (attacks) {
            int attack_sq = get_lsb_and_pop_bit(&attacks);
            if (source >= a7 && source <=h7) {
                /* capture with promotion. */
                move_buffer->moves[move_buffer->index++] = encode_pawn_promotion(source, target_sq, P, Q, 1);
                move_buffer->moves[move_buffer->index++] = encode_pawn_promotion(source, target_sq, P, R, 1);
                move_buffer->moves[move_buffer->index++] = encode_pawn_promotion(source, target_sq, P, B, 1);
                move_buffer->moves[move_buffer->index++] = encode_pawn_promotion(source, target_sq, P, N, 1);
            } else {
                /* capture. */
                move_buffer->moves[move_buffer->index++] = encode_capture(source, attack_sq, P);
            }
        }

        /* check for en passant. */
        if (board->en_passant == none_sq) continue;
        bitboard ep = pawn_attack(source, white) & (1ULL << board->en_passant);
        if (ep) {
            square index = trailing_zero_count(ep);
            move_buffer->moves[move_buffer->index++] =
                    encode_move(source, index, P, 0, 1, 0, 1, 0, 0);
        }
    }
}

void generate_black_pawn_moves(board_t* board, move_buffer_t* move_buffer) {
    /* pawn, non-capturing moves. */
    bitboard b = board->pieces[p];
    while (b) {
        square source = get_lsb_and_pop_bit(&b);
        int target_sq = source + 8;
        if (target_sq >= h2 && !is_bit_set(&board->occupancy[both], target_sq)) {
            if (source >= a2 && source <= h2) {
                /* pawn promotion with no capture. */
                move_buffer->moves[move_buffer->index++] = encode_pawn_promotion(source, target_sq, p, q, 0);
                move_buffer->moves[move_buffer->index++] = encode_pawn_promotion(source, target_sq, p, r, 0);
                move_buffer->moves[move_buffer->index++] = encode_pawn_promotion(source, target_sq, p, b, 0);
                move_buffer->moves[move_buffer->index++] = encode_pawn_promotion(source, target_sq, p, n, 0);
            } else {
                /* No capture, single square move. */
                move_buffer->moves[move_buffer->index++] = encode_no_capture(source, target_sq, p);

                /* Check to see if we can add a double push. */
                int double_push_sq = source + 16;
                if (source >= a7 && source <= h7 && !is_bit_set(&board->occupancy[both], double_push_sq)) {
                    move_buffer->moves[move_buffer->index++] = encode_pawn_double_push(source, double_push_sq, p);
                }
            }
        }

        /* pawn captures. */
        bitboard attacks = pawn_attack(source, black) & board->occupancy[white];
        while (attacks) {
            int attack_sq = get_lsb_and_pop_bit(&attacks);
            if (source >= a2 && source <=h2) {
                /* capture with promotion. */
                move_buffer->moves[move_buffer->index++] = encode_pawn_promotion(source, target_sq, p, q, 1);
                move_buffer->moves[move_buffer->index++] = encode_pawn_promotion(source, target_sq, p, r, 1);
                move_buffer->moves[move_buffer->index++] = encode_pawn_promotion(source, target_sq, p, b, 1);
                move_buffer->moves[move_buffer->index++] = encode_pawn_promotion(source, target_sq, p, n, 1);
            } else {
                /* capture. */
                move_buffer->moves[move_buffer->index++] = encode_capture(source, attack_sq, p);
            }
        }

        /* check for en passant. */
        if (board->en_passant == none_sq) continue;
        bitboard ep = pawn_attack(source, black) & (1ULL << board->en_passant);
        if (ep) {
            square index = trailing_zero_count(ep);
            move_buffer->moves[move_buffer->index++] =
                    encode_move(source, index, p, 0, 1, 0, 1, 0, 0);
        }
    }
}

void generate_white_castling_moves(board_t* board, move_buffer_t* move_buffer) {
    if (board->castle_flag & white_king_side) {
        if (!is_bit_set(&board->occupancy[both], f1) &&
            !is_bit_set(&board->occupancy[both], g1) &&
            !is_bit_set(&board->occupancy[black], e1) &&
            !is_bit_set(&board->occupancy[black], f1)) {

            move_buffer->moves[move_buffer->index++] =
                    encode_move(e1, g1, K, 0, 0, 0, 0, 1, 0);
        }
    }
    if (board->castle_flag & white_queen_side) {
        if (!is_bit_set(&board->occupancy[both], d1) &&
            !is_bit_set(&board->occupancy[both], c1) &&
            !is_bit_set(&board->occupancy[both], b1) &&
            !is_bit_set(&board->occupancy[black], e1) &&
            !is_bit_set(&board->occupancy[black], d1)) {

            move_buffer->moves[move_buffer->index++] =
                    encode_move(e1, c1, K, 0, 0, 0, 0, 0, 1);
        }
    }
}

void generate_black_castling_moves(board_t* board, move_buffer_t* move_buffer) {
    if (board->castle_flag & black_king_side) {
        if (!is_bit_set(&board->occupancy[both], f8) &&
            !is_bit_set(&board->occupancy[both], g8)  &&
            !is_bit_set(&board->occupancy[white], e8) &&
            !is_bit_set(&board->occupancy[white], f8))
        {
            move_buffer->moves[move_buffer->index++] =
                    encode_move(e8, g8, k, 0, 0, 0, 0, 1, 0);
        }
    }

    if (board->castle_flag & black_queen_side) {
        if (!is_bit_set(&board->occupancy[both], d8) &&
            !is_bit_set(&board->occupancy[both], c8) &&
            !is_bit_set(&board->occupancy[both], b8) &&
            !is_bit_set(&board->occupancy[white], e8) &&
            !is_bit_set(&board->occupancy[white], d8)) {

            move_buffer->moves[move_buffer->index++] =
                    encode_move(e8, c8, k, 0, 0, 0, 0, 0, 1);
        }
    }
}

void generate_moves(board_t* board, move_buffer_t* move_buffer) {
    bitboard all = board->occupancy[both];

    if (board->side == white) {
        bitboard not_self = ~board->occupancy[white];
        bitboard opponent = board->occupancy[black];

        generate_white_pawn_moves(board, move_buffer);
        generate_white_castling_moves(board, move_buffer);
        generate_piece_move(N, board->pieces[N], &not_self, &opponent, &all, knight_attack_wrapper, move_buffer);
        generate_piece_move(B, board->pieces[B], &not_self, &opponent, &all, bishop_attack, move_buffer);
        generate_piece_move(R, board->pieces[R], &not_self, &opponent, &all, rook_attack, move_buffer);
        generate_piece_move(Q, board->pieces[Q], &not_self, &opponent, &all, queen_attack, move_buffer);
        generate_piece_move(K, board->pieces[K], &not_self, &opponent, &all, king_attack_wrapper, move_buffer);


    } else {
        bitboard not_self = ~board->occupancy[black];
        bitboard opponent = board->occupancy[white];

        generate_black_pawn_moves(board, move_buffer);
        generate_black_castling_moves(board, move_buffer);
        generate_piece_move(n, board->pieces[n], &not_self, &opponent, &all, knight_attack_wrapper, move_buffer);
        generate_piece_move(b, board->pieces[b], &not_self, &opponent, &all, bishop_attack, move_buffer);
        generate_piece_move(r, board->pieces[r], &not_self, &opponent, &all, rook_attack, move_buffer);
        generate_piece_move(q, board->pieces[q], &not_self, &opponent, &all, queen_attack, move_buffer);
        generate_piece_move(k, board->pieces[k], &not_self, &opponent, &all, king_attack_wrapper, move_buffer);
    }

}
