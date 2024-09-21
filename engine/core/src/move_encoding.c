#include <stdio.h>

#include "move_encoding.h"

void print_move(move_t m) {
    printf("move: %d, ", m);
    printf("source: %s, ", square_to_str[get_source_square(m)]);
    printf("target: %s, ", square_to_str[get_target_square(m)]);
    printf("moved: %s, ", piece_to_str[get_move_piece(m)]);
    printf("promoted: %s, ", get_promoted_piece(m) ==0 ? "-" : piece_to_str[get_promoted_piece(m)]);
    printf("en-passant: %s, ", get_enpassant_flag(m) != 0 ? "t" : "f");
    printf("capture: %s, ", get_capture_flag(m) != 0 ? "t" : "f");
    printf("double push: %s, \n", get_double_push_flag(m) !=0 ? "t" : "f");
    printf("king side castle?: %s ", get_king_side_castle_flag(m) !=0 ? "t": "f");
    printf("queen side castle?: %s\n", get_queen_side_castle_flag(m) !=0 ? "t" : "f");
}