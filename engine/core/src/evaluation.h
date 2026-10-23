#ifndef TIKI_EVALUATION_H
#define TIKI_EVALUATION_H

#include "types.h"
#include "board.h"

#include "hce/evaluation.h"

static inline_always int evaluation(const board_t* const board) {
    /* HCE until a trained network replaces it (see nnue/nnue.h and training/). */
    return eval_hce(board);
}

#endif
