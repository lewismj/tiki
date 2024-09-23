#ifndef TIKI_BITBOARD_CONSTANTS_H
#define TIKI_BITBOARD_CONSTANTS_H

#include <stdalign.h>
#include "types.h"

/**
 * BitBoard constants (e.g. magic numbers). See 'magic_generator'.
 */
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
} bitboard_const_t;

/**
 * Singleton instance of the constants.
 */
extern const bitboard_const_t bitboard_const;

#endif
