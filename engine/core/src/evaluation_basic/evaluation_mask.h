#ifndef TIKI_EVALUATION_MASK_H
#define TIKI_EVALUATION_MASK_H

#include <stdalign.h>
#include "../types.h"


/**
 * Constants (masks) used in simple evaluation. See 'magic_generator'.
 */
typedef struct align {
    const bitboard rank_mask[64];
    const bitboard file_mask[64];
    const bitboard isolated_pawn_mask[64];
    const bitboard passed_pawn_mask[2][64];
} evaluation_mask_t;


#endif
