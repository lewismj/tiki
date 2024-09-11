#include <stdio.h>

#include "bitboard_constants.h"
#include "bitboard_ops.h"

#include "mask_generator.h"



bitboard create_rook_blocker_mask(square s) {
    bitboard mask = 0ULL;

    int rank = s / 8;
    int file = s % 8;

    for (int r = rank + 1; r <= 6; r++) mask |= 1ULL << (r * 8 + file);
    for (int r = rank - 1; r >= 1; r--) mask |= 1ULL << (r * 8 + file);
    for (int f = file + 1; f <= 6; f++) mask |= 1ULL << (rank * 8 + f);
    for (int f = file - 1; f >= 1; f--) mask |= 1ULL << (rank * 8 + f);

    return mask;
}

bitboard create_bishop_blocker_mask(square s) {
    bitboard mask = 0ULL;

    int rank = s / 8;
    int file = s % 8;

    for (int r = rank + 1, f = file + 1; r <= 6 && f <= 6; r++, f++) mask |= 1ULL << (r * 8 + f);
    for (int r = rank - 1, f = file + 1; r >= 1 && f <= 6; r--, f++) mask |= 1ULL << (r * 8 + f);
    for (int r = rank + 1, f = file - 1; r <= 6 && f >= 1; r++, f--) mask |= 1ULL << (r * 8 + f);
    for (int r = rank - 1, f = file - 1; r >= 1 && f >= 1; r--, f--) mask |= 1ULL << (r * 8 + f);

    return mask;
}

bitboard create_rook_attack_mask(square s, bitboard blockers) {
    bitboard mask = 0ULL;

    int rank = s / 8;
    int file = s % 8;

    for (int r = rank + 1; r <= 7; r++) {
        bitboard tmp = 1ULL << (r * 8 + file);
        mask |= tmp;
        if (tmp & blockers) break;
    }

    for (int r = rank - 1; r >= 0; r--) {
        bitboard tmp = 1ULL << (r * 8 + file);
        mask |= tmp;
        if (tmp & blockers) break;
    }

    for (int f = file + 1; f <= 7; f++) {
        bitboard tmp = 1ULL << (rank * 8 + f);
        mask |= tmp;
        if (tmp & blockers) break;
    }

    for (int f = file - 1; f >= 0; f--) {
        bitboard tmp = 1ULL << (rank * 8 + f);
        mask |= tmp;
        if (tmp & blockers) break;
    }

    return mask;
}

bitboard create_bishop_attack_mask(square s, bitboard blockers) {
    bitboard mask = 0ULL;

    int rank = s / 8;
    int file = s % 8;

    for (int r = rank + 1, f = file + 1; r <= 7 && f <= 7; r++, f++) {
        bitboard tmp = 1ULL << (r * 8 + f);
        mask |= tmp;
        if (tmp & blockers) break;
    }

    for (int r = rank - 1, f = file + 1; r >= 0 && f <= 7; r--, f++) {
        bitboard tmp = 1ULL << (r * 8 + f);
        mask |= tmp;
        if (tmp & blockers) break;
    }

    for (int r = rank + 1, f = file - 1; r <= 7 && f >= 0; r++, f--) {
        bitboard tmp = 1ULL << (r * 8 + f);
        mask |= tmp;
        if (tmp & blockers) break;
    }

    for (int r = rank - 1, f = file - 1; r >= 0 && f >= 0; r--, f--) {
        bitboard tmp = 1ULL << (r * 8 + f);
        mask |= tmp;
        if (tmp & blockers) break;
    }

    return mask;
}

bitboard create_king_attack_mask(square s) {
    bitboard mask = 0ULL;
    bitboard loc = bitboard_const.square_mask[s];

    if (loc >> 8)  mask |= loc >> 8;                                    /* n.   */
    if (loc >> 9 & bitboard_const.not_h_file) mask |= loc >> 9;    /* nw.  */
    if (loc >> 7 & bitboard_const.not_a_file) mask |= loc >> 7;    /* ne.  */
    if (loc >> 1 & bitboard_const.not_h_file) mask |= loc >> 1;    /* w.   */

    if (loc << 8) mask |= loc << 8;                                     /* s.   */
    if (loc << 9 & bitboard_const.not_a_file) mask |= loc << 9;    /* sw.  */
    if (loc << 7 & bitboard_const.not_h_file) mask |= loc << 7;    /* se.  */
    if (loc << 1 & bitboard_const.not_a_file) mask |= loc << 1;    /* e.   */

    return mask;
}

bitboard create_knight_attack_mask(square s) {
    bitboard mask = 0ULL;
    bitboard loc = bitboard_const.square_mask[s];

    if (loc >> 17 & bitboard_const.not_h_file) mask |= loc >> 17;  /* nnw. */
    if (loc >> 15 & bitboard_const.not_a_file) mask |= loc >> 15;  /* nne. */
    if (loc >> 10 & bitboard_const.not_hg_file) mask |= loc >> 10; /* nw.  */
    if (loc >> 6 & bitboard_const.not_ab_file) mask |= loc >> 6;   /* ne.  */

    if (loc << 17 & bitboard_const.not_a_file) mask |= loc << 17; /* sse. */
    if (loc << 15 & bitboard_const.not_h_file) mask |= loc << 15;  /* ssw. */
    if (loc << 10 & bitboard_const.not_ab_file) mask |= loc << 10; /* se.  */
    if (loc << 6 & bitboard_const.not_hg_file) mask |= loc << 6;   /* sw.  */

    return mask;
}

bitboard create_pawn_attack_mask(square s, colour c) {
    bitboard mask = 0ULL;
    bitboard loc = bitboard_const.square_mask[s];

    if (c == white) {
        if (loc >> 7 & bitboard_const.not_a_file) mask |= loc >> 7;    /* ne.  */
        if (loc >> 9 & bitboard_const.not_h_file) mask |= loc >> 9;    /* nw.  */
    } else {
        if (loc << 7 & bitboard_const.not_h_file) mask |= loc << 7;    /* sw.  */
        if (loc << 9 & bitboard_const.not_a_file) mask |= loc << 9;    /* se.  */
    }

    return mask;
}


bitboard set_occupancy_variation(unsigned int index, unsigned int num_relevant_bits, bitboard attack_mask) {
    bitboard variation = 0ULL;

    for (int i=0; i < num_relevant_bits; i++) {
        square sq = get_lsb_and_pop_bit(&attack_mask);
        if (index & bitboard_const.square_mask[i]) variation |= bitboard_const.square_mask[sq];
    }

    return variation;
}
