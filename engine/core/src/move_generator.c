#include <stdio.h>

#include "move_generator.h"


static bitboard knight_attack_wrapper(square s, bitboard ) {
    return knight_attack(s);
}

static bitboard king_attack_wrapper(square s, bitboard ) {
    return king_attack(s);
}

void generate_white_pawn_moves(board_t* board, move_buffer_t* move_buffer) {
    bitboard white_pawns = board->pieces[P];
    /*
     * pawn, non-capturing moves.
     * board is A8=0
     *           ...  H8,
     * white moves 'up' the board.
     */

    /*
     * promotions.
     * down = promotion, no capture.
     * down_left, down_right = promotion with capture.
     */
    bitboard pawns_on_rank7 = white_pawns & bitboard_const.rank_masks[6];
    bitboard up = (pawns_on_rank7 >> 8) & ~board->occupancy[both];
    while (up) {
        square target = get_lsb_and_pop_bit(&up);
        square source = target + 8;
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, P, Q, 0, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, P, R, 0, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, P, B, 0, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, P, N, 0, 0, 0, 0, 0);
    }

    bitboard up_left = (pawns_on_rank7 >> 9 & bitboard_const.not_h_file) & board->occupancy[black];
    while (up_left) {
        square target = get_lsb_and_pop_bit(&up_left);
        square source =  target + 9;
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, P, Q, 1, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, P, R, 1, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, P, B, 1, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, P, N, 1, 0, 0, 0, 0);
    }
    bitboard up_right = (pawns_on_rank7 >> 7 & bitboard_const.not_a_file) & board->occupancy[black];
    while (up_right) {
        square target = get_lsb_and_pop_bit(&up_right);
        square source = target + 7;
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, P, Q, 1, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, P, R, 1, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, P, B, 1, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, P, N, 1, 0, 0, 0, 0);
    }

    /*
     * single, double push & regular attacks.
     */
    bitboard pawns_not_on_rank7 = white_pawns & ~bitboard_const.rank_masks[6];
    /* non captures, n.b. ranks have array index 0 ... 7 */
    up = (pawns_not_on_rank7 >> 8) & ~board->occupancy[both];
    bitboard double_push = ((up & bitboard_const.rank_masks[2]) >> 8) & ~board->occupancy[both];
    while (up) {
        square target = get_lsb_and_pop_bit(&up);
        square source = target + 8;
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, P, 0, 0, 0, 0, 0, 0);
    }
    while (double_push) {
        square target = get_lsb_and_pop_bit(&double_push);
        square source = target + 16;
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, P, 0, 0, 1, 0, 0, 0);
    }
    up_left = (pawns_not_on_rank7 >> 9 & bitboard_const.not_h_file) & board->occupancy[black];
    while (up_left) {
        square target = get_lsb_and_pop_bit(&up_left);
        square source = target + 9;
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, P, 0, 1, 0, 0, 0, 0);
    }
    up_right = (pawns_not_on_rank7 >> 7 & bitboard_const.not_a_file) & board->occupancy[black];
    while (up_right) {
        square target = get_lsb_and_pop_bit(&up_right);
        square source = target + 7;
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, P, 0, 1, 0, 0, 0, 0);
    }

    /* en passant. */
    if (board->en_passant != none_sq) {
        bitboard en_passant_capture = pawns_not_on_rank7 & pawn_attack(board->en_passant, black);
        while (en_passant_capture) {
            square source = get_lsb_and_pop_bit(&en_passant_capture);
            move_buffer->moves[move_buffer->index++] = encode_move(source, board->en_passant, P, 0, 1, 0, 1, 0, 0);
        }
    }
}


void generate_black_pawn_moves(board_t* board, move_buffer_t* move_buffer) {
    /*
     * pawn, non-capturing moves.
     * board is A8
     *           ...  H0,
     * black moves 'down' the board.
     */
    bitboard black_pawns = board->pieces[p];
    /*
     * promotions.
     * down = promotion, no capture.
     * down_left, down_right = promotion with capture.
     */
    bitboard pawns_on_rank2 = black_pawns & bitboard_const.rank_masks[1];
    bitboard down = (pawns_on_rank2 << 8) & ~board->occupancy[both];
    while (down) {
        square target = get_lsb_and_pop_bit(&down);
        square source = target - 8;
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, p, q, 0, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, p, r, 0, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, p, b, 0, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, p, n, 0, 0, 0, 0, 0);
    }

    bitboard down_left = (pawns_on_rank2 << 9 & bitboard_const.not_a_file) & board->occupancy[white];
    while (down_left) {
        square target = get_lsb_and_pop_bit(&down_left);
        square source =  target - 9;
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, p, q, 1, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, p, r, 1, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, p, b, 1, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, p, n, 1, 0, 0, 0, 0);
    }
    bitboard down_right = (pawns_on_rank2 << 7 & bitboard_const.not_h_file) & board->occupancy[white];
    while (down_right) {
        square target = get_lsb_and_pop_bit(&down_right);
        square source = target - 7;
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, p, q, 1, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, p, r, 1, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, p, b, 1, 0, 0, 0, 0);
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, p, n, 1, 0, 0, 0, 0);
    }

    /*
     * single, double push & regular attacks.
     */
    bitboard pawns_not_on_rank2 = black_pawns & ~bitboard_const.rank_masks[1];
    /* non captures, n.b. ranks have array index 0 ... 7 */
    down = (pawns_not_on_rank2 << 8) & ~board->occupancy[both];
    bitboard double_push = ((down & bitboard_const.rank_masks[5]) << 8) & ~board->occupancy[both];
    while (down) {
        square target = get_lsb_and_pop_bit(&down);
        square source = target - 8;
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, p, 0, 0, 0, 0, 0, 0);
    }
    while (double_push) {
        square target = get_lsb_and_pop_bit(&double_push);
        square source = target - 16;
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, p, 0, 0, 1, 0, 0, 0);
    }
    down_left = (pawns_not_on_rank2 << 9 & bitboard_const.not_a_file) & board->occupancy[white];
    while (down_left) {
        square target = get_lsb_and_pop_bit(&down_left);
        square source = target - 9;
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, p, 0, 1, 0, 0, 0, 0);
    }
    down_right = (pawns_not_on_rank2 << 7 & bitboard_const.not_h_file) & board->occupancy[white];
    while (down_right) {
        square target = get_lsb_and_pop_bit(&down_right);
        square source = target - 7;
        move_buffer->moves[move_buffer->index++] = encode_move(source, target, p, 0, 1, 0, 0, 0, 0);
    }

    /* en passant. */
    if (board->en_passant != none_sq) {
        bitboard en_passant_capture = pawns_not_on_rank2 & pawn_attack(board->en_passant, white);
        while (en_passant_capture) {
            square source = get_lsb_and_pop_bit(&en_passant_capture);
            move_buffer->moves[move_buffer->index++] = encode_move(source, board->en_passant, p, 0, 1, 0, 1, 0, 0);
        }
    }
}

void generate_white_castling_moves(board_t* board, move_buffer_t* move_buffer) {
    if (board->castle_flag & white_king_side) {
        /* g1 & f1= 0x6000000000000000ULL  */
        if (!(board->occupancy[both] & 0x6000000000000000ULL) &&
            !is_square_attacked_by_black(board, e1) &&
            !is_square_attacked_by_black(board, g1) &&
            !is_square_attacked_by_black(board, f1)) {

            move_buffer->moves[move_buffer->index++] =
                    encode_move(e1, g1, K, 0, 0, 0, 0, 1, 0);
        }
    }
    if (board->castle_flag & white_queen_side) {
        /* d1, c1, b1 = 0xe00000000000000 */
        if (!(board->occupancy[both] & 0xe00000000000000ULL) &&
            !is_square_attacked_by_black(board, e1) &&
            !is_square_attacked_by_black(board, c1) &&
            !is_square_attacked_by_black(board, d1)) {

            move_buffer->moves[move_buffer->index++] =
                    encode_move(e1, c1, K, 0, 0, 0, 0, 0, 1);
        }
    }
}

void generate_black_castling_moves(board_t* board, move_buffer_t* move_buffer) {
    if (board->castle_flag & black_king_side) {
        /* f8, g8 = 0x60 */
        if (!(board->occupancy[both] & 0x60ULL) &&
            !is_square_attacked_by_white(board, e8) &&
            !is_square_attacked_by_white(board, g8) &&
            !is_square_attacked_by_white(board, f8))
        {
            move_buffer->moves[move_buffer->index++] =
                    encode_move(e8, g8, k, 0, 0, 0, 0, 1, 0);
        }
    }

    if (board->castle_flag & black_queen_side) {
        /* d8, c8, b8 = 0xe */
        if (!(board->occupancy[both] & 0xeULL) &&
            !is_square_attacked_by_white(board, e8) &&
            !is_square_attacked_by_white(board, c8) &&
            !is_square_attacked_by_white(board, d8)) {

            move_buffer->moves[move_buffer->index++] =
                    encode_move(e8, c8, k, 0, 0, 0, 0, 0, 1);
        }
    }
}

void generate_moves(board_t* board, move_buffer_t* move_buffer) {
    move_buffer->index =0;
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

