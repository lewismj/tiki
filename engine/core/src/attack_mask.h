#ifndef TIKI_ATTACK_MASK_H
#define TIKI_ATTACK_MASK_H

#include "types.h"

#define USE_PEXT

typedef struct {
    bitboard* rook_blocker_masks;
    bitboard** rook_attack_table;
    bitboard* bishop_blocker_masks;
    bitboard** bishop_attack_table;
    bitboard* king_attacks;
    bitboard* knight_attacks;
    bitboard** pawn_attack_table;
#ifdef USE_PEXT
    bitboard* pext_attacks;
    bitboard* pext_rook_base;
    bitboard* pext_bishop_base;
#endif
} tiki_attack_mask_t;

static tiki_attack_mask_t* instance = NULL;
tiki_attack_mask_t* create_attack_mask();

bitboard rook_attack(square s, bitboard occupancy);
bitboard bishop_attack(square s, bitboard occupancy);
bitboard king_attack(square s);
bitboard knight_attack(square s);
bitboard pawn_attack(square s, colour c);

#endif
