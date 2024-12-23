#ifndef TIKI_EVALUATION_MASK_H
#define TIKI_EVALUATION_MASK_H

#include <stdalign.h>
#include "../types.h"


typedef struct {
    const bitboard isolated_file_mask[64];
    const bitboard passed_pawn_mask[2][64];
} evaluation_mask_t;
extern const evaluation_mask_t evaluation_mask_instance;


#endif