#ifndef TIKI_ATTACK_MASK_H
#define TIKI_ATTACK_MASK_H

#include <stdlib.h>
#include "types.h"
#include "bitboard_constants.h"

/**
 * Attack tables, here we use the magic bitboards & pext cache to provide the
 * functions used by the move generator.
 */
typedef struct {
    bitboard rook_blocker_masks[64];
    bitboard rook_attack_table[64][4096];
    bitboard bishop_blocker_masks[64];
    bitboard bishop_attack_table[64][512];
    bitboard king_attacks[64];
    bitboard knight_attacks[64];
    bitboard pawn_attack_table[2][64];
#ifdef USE_PEXT
    bitboard pext_attacks[107648];
    bitboard pext_rook_base[64];
    bitboard pext_bishop_base[64];
#endif
} attack_mask_t;
extern attack_mask_t am_instance;

/**
 * This function needs to be called before we can invoke any of the 'attack' functions,
 * it initializes all the tables used when calculating attacks (Pext etc.).
 */
void init_attack_table();

static inline_always bitboard magic_rook_attack(square s, bitboard occupancy) {
    bitboard tmp = occupancy & am_instance.rook_blocker_masks[s];
    tmp *= bitboard_const.rook_magic_numbers[s];
    tmp >>= 64 - bitboard_const.rook_relevant_bits[s];
    return am_instance.rook_attack_table[s][(int)tmp];
}

static inline_always bitboard magic_bishop_attack(square s, bitboard occupancy) {
    bitboard tmp = occupancy & am_instance.bishop_blocker_masks[s];
    tmp *= bitboard_const.bishop_magic_numbers[s];
    tmp >>= 64 - bitboard_const.bishop_relevant_bits[s];
    return am_instance.bishop_attack_table[s][(int)tmp];
}

static inline_always bitboard king_attack(square s) {
    return am_instance.king_attacks[s];
}

static inline_always bitboard knight_attack(square s) {
    return am_instance.knight_attacks[s];
}

static inline_always bitboard pawn_attack(square s, colour c) {
    return am_instance.pawn_attack_table[c][s];
}

static inline_always bitboard rook_attack(square s, bitboard occupancy) {
#ifdef USE_PEXT
    return am_instance.pext_attacks[am_instance.pext_rook_base[s] +
                                         __builtin_ia32_pext_di (occupancy, am_instance.rook_blocker_masks[s])];
#else
    return magic_rook_attack(s,occupancy);
#endif
}

static inline_always bitboard bishop_attack(square s, bitboard occupancy) {
#ifdef USE_PEXT
    return am_instance.pext_attacks[am_instance.pext_bishop_base[s] +
                                     __builtin_ia32_pext_di (occupancy, am_instance.bishop_blocker_masks[s])];
#else
    return magic_bishop_attack(s,occupancy);
#endif
}

static inline_always bitboard queen_attack(square s, bitboard occupancy) {
    return rook_attack(s, occupancy) | bishop_attack(s, occupancy);
}

#endif
