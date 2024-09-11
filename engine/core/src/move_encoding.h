#ifndef TIKI_MOVE_ENCODING_H
#define TIKI_MOVE_ENCODING_H

#include "types.h"

/**
      binary move bits                                            hex

     0000 0000 0000 0000 0000 0011 1111   source square           0x3f
     0000 0000 0000 0000 1111 1100 0000   target square           0xfc0
     0000 0000 0000 1111 0000 0000 0000   piece                   0xf000
     0000 0000 1111 0000 0000 0000 0000   promoted piece          0xf0000
     0000 0001 0000 0000 0000 0000 0000   capture flag            0x100000
     0000 0010 0000 0000 0000 0000 0000   double push flag        0x200000
     0000 0100 0000 0000 0000 0000 0000   en passant              0x400000
     0000 1000 0000 0000 0000 0000 0000   king side castle flag   0x800000
     0001 0000 0000 0000 0000 0000 0000   queen side castle flag  0x1000000
**/

/**
 * Encode move information to move type (uint32_t).
 * @return the encoded move.
 */
static inline __attribute__((always_inline)) move encode_move(  square  source,
                                                                square target,
                                                                piece move_piece,
                                                                piece promoted,
                                                                int capture_flag,
                                                                int double_push_flag,
                                                                int en_passant_flag,
                                                                int king_side_castle_flag,
                                                                int queen_side_castle_flag) {
    return source |
           (target << 6) |
           (move_piece << 12) |
           (promoted << 16) |
           (capture_flag << 20) |
           (double_push_flag << 21) |
           (en_passant_flag << 22) |
           (king_side_castle_flag << 23) |
           (queen_side_castle_flag << 24);
}

static inline __attribute__((always_inline)) square get_source_square(move m) {
    return m & 0x3f;
}

static inline __attribute__((always_inline)) square get_target_square(move m) {
    return (m & 0xfc0) >> 6;
}

static inline __attribute__((always_inline)) piece get_move_piece(move m) {
    return (m & 0xf000) >> 12;
}

static inline __attribute__((always_inline)) piece get_promoted_piece(move m) {
    return (m & 0xf0000) >> 16;
}

static inline __attribute__((always_inline)) int get_capture_flag(move m) {
    return (int) m & 0x100000;
}

static inline __attribute__((always_inline)) int get_double_push_flag(move m) {
    return (int) m & 0x200000;
}

static inline __attribute__((always_inline)) int get_enpassant_flag(move m) {
    return (int) m & 0x200000;
}

static inline __attribute__((always_inline)) int get_king_side_castle_flag(move m) {
    return (int) m & 0x800000;
}

static inline __attribute__((always_inline)) int get_queen_side_castle_flag(move m) {
    return (int) m & 0x1000000;
}

void print_move(move m);

#endif
