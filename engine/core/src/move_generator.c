#include <stdio.h>
#include "move_generator.h"


static inline_always bitboard knight_attack_wrapper(square s, bitboard ) {
    return knight_attack(s);
}

static inline_always bitboard king_attack_wrapper(square s, bitboard ) {
    return king_attack(s);
}

void generate_white_pawn_moves(board_t* board, move_buffer_t* move_buffer) {

}

void generate_black_pawn_moves(board_t* board, move_buffer_t* move_buffer) {

}

void generate_white_castling_moves(board_t* board, move_buffer_t* move_buffer) {

}

void generate_black_castling_moves(board_t* board, move_buffer_t* move_buffer) {

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

