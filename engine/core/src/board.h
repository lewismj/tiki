#ifndef TIKI_BOARD_H
#define TIKI_BOARD_H

#include <inttypes.h>
#include "types.h"

/**
 * Define the common board data structure.
 */

typedef struct {
    bitboard** pieces;
    bitboard** occupancy;
    int castle_rights;
    square en_passant;
    colour side;
    int half_move;
    int full_move;
    uint64_t hash;
} board_t;



#endif
