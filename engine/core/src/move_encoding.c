#include <stdio.h>

#include "move_encoding.h"

void print_move(move_t m, showable options) {
    if (options & show) {
        printf("move: %d, ", m);
        printf("source: %s, ", square_to_str[get_source_square(m)]);
        printf("target: %s, ", square_to_str[get_target_square(m)]);
        printf("moved: %c, ", piece_to_char[get_move_piece(m)]);
        printf("promoted: %c, ", get_promoted_piece(m) == 0 ? '-' : piece_to_char[get_promoted_piece(m)]);
        printf("en-passant: %c, ", get_enpassant_flag(m) != 0 ? 't' : 'f');
        printf("capture: %c, ", get_capture_flag(m) != 0 ? 't' : 'f');
        printf("double push: %c, \n", get_double_push_flag(m) != 0 ? 't' : 'f');
        printf("king side castle?: %c ", get_king_side_castle_flag(m) != 0 ? 't' : 'f');
        printf("queen side castle?: %c\n", get_queen_side_castle_flag(m) != 0 ? 't' : 'f');
    }
    if (options & min) {
        printf("%s%s:", square_to_str[get_source_square(m)],square_to_str[get_target_square(m)]);
        printf("%c:", piece_to_char[get_move_piece(m)]);
        printf("%c:", get_promoted_piece(m) == 0 ? '-' : piece_to_char[get_promoted_piece(m)]);
        printf("%c:", get_enpassant_flag(m) != 0 ? 't' : 'f');
        printf("%c:", get_capture_flag(m) != 0 ? 't' : 'f');
        printf("%c:", get_double_push_flag(m) != 0 ? 't' : 'f');
        printf("%c:", get_king_side_castle_flag(m) != 0 ? 't' : 'f');
        printf("%c\n", get_queen_side_castle_flag(m) != 0 ? 't' : 'f');
    }
}