#ifndef TIKI_BITBOARD_OPS_H
#define TIKI_BITBOARD_OPS_H

#include "types.h"
#include "bitboard_constants.h"


/**
 * Print bitboard to standard output.
 *
 * @param b the bitboard.
 * @param options   the print options, select one or more flags.
 */
void print_bitboard(const bitboard* b, showable options);

/**
 * Return true if the bit represent square s is set on the bitboard.
 *
 * @param b the bitboard.
 * @param s the square to check.
 * @return true is the bit corresponding to 's' has be set, false otherwise.
 */
static inline_always bool is_bit_set(const bitboard* b, square s) {
    return (*b & bitboard_const.square_mask[s]) != 0;
}

/**
 * Set the bit on the bitboard corresponding to the given square s.
 *
 * @param b the bitboard to update.
 * @param s the square on the bitboard to set.
 */
static inline_always void set_bit(bitboard* b, square s) {
    *b |= bitboard_const.square_mask[s];
}

/**
 * Unset (to 0) the bit on the bitboard corresponding to the given square s.
 *
 * @param b the bitboard to update.
 * @param s the square on the bitboard to unset.
 */
static inline_always void pop_bit(bitboard* b, square s) {
    *b &= ~bitboard_const.square_mask[s];
}

/**
 * Return the number of trailing zeros in the bitboard.
 *
 * @param b the bitboard.
 * @return  the number of trailing zeros.
 */
static inline_always int trailing_zero_count(bitboard b) {
    if (b == 0ULL) return 64;
    return __builtin_ctzll(b);
}

/**
 * Return the square corresponding to LSB of board representation and pop the bit of the bitboard.
 * @param b pointer to the bitboard.
 * @return the square index.
 */
static inline_always int get_lsb_and_pop_bit(bitboard* b) {
    int index = trailing_zero_count(*b);
    pop_bit(b, index);
    return index;
}

/**
 * Return the number of bits set in the bitboard.
 *
 * @param b the bitboard.
 * @return the number of bits set.
 */
static inline_always int pop_count(bitboard b) {
    return __builtin_popcountll(b);
}


#endif
