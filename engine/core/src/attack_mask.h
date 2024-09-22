#ifndef TIKI_ATTACK_MASK_H
#define TIKI_ATTACK_MASK_H

#include <stdlib.h>
#include "types.h"


/**
 * This function needs to be called before we can invoke any of the 'attack' functions,
 * it initializes all the tables used when calculating attacks (Pext etc.).
 */
void init_attack_table();


bitboard rook_attack(square s, bitboard occupancy);
bitboard bishop_attack(square s, bitboard occupancy);
bitboard queen_attack(square s, bitboard occupancy);
bitboard king_attack(square s);
bitboard knight_attack(square s);
bitboard pawn_attack(square s, colour c);

#endif
