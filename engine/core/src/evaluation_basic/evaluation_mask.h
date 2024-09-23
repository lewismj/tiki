#ifndef TIKI_EVALUATION_MASK_H
#define TIKI_EVALUATION_MASK_H

#include <stdalign.h>
#include "../types.h"


typedef struct {
    bitboard rank_mask[64];
    bitboard file_mask[64];
    bitboard isolated_pawn_mask[64];
    bitboard passed_pawn_mask[2][64];
} evaluation_mask_t;


void init_evaluation_masks();


#endif
