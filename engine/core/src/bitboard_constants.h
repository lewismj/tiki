#ifndef TIKI_BITBOARD_CONSTANTS_H
#define TIKI_BITBOARD_CONSTANTS_H

#include "types.h"

typedef struct {
    const bitboard square_mask[64];
    const unsigned int bishop_relevant_bits[64];
    const unsigned int rook_relevant_bits[64];
    const bitboard not_a_file;
    const bitboard not_h_file;
    const bitboard not_ab_file;
    const bitboard not_hg_file;
    const bitboard rook_magic_numbers[64];
    const bitboard bishop_magic_numbers[64];
} tiki_bitboard_const_t;

extern const tiki_bitboard_const_t tiki_bitboard_const;

#endif
