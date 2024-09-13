#ifndef TIKI_ATTACK_MASK_H
#define TIKI_ATTACK_MASK_H

#include <stdlib.h>
#include "types.h"


/**
 *
 */
void init_attack_table();

/**
 *
 * @param s
 * @param occupancy
 * @return
 */
bitboard rook_attack(square s, bitboard occupancy);

/**
 *
 * @param s
 * @param occupancy
 * @return
 */
bitboard bishop_attack(square s, bitboard occupancy);

/**
 *
 * @param s
 * @return
 */
bitboard king_attack(square s);

/**
 *
 * @param s
 * @return
 */
bitboard knight_attack(square s);

/**
 *
 * @param s
 * @param c
 * @return
 */
bitboard pawn_attack(square s, colour c);

#endif
