#ifndef TIKI_ATTACK_MASK_H
#define TIKI_ATTACK_MASK_H

#include <stdlib.h>
#include "types.h"


void init_attack_table();

bitboard rook_attack(square s, bitboard occupancy);

bitboard bishop_attack(square s, bitboard occupancy);

bitboard king_attack(square s);

bitboard knight_attack(square s);

bitboard pawn_attack(square s, colour c);

#endif
