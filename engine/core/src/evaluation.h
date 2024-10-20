#ifndef TIKI_EVALUATION_H
#define TIKI_EVALUATION_H

#include "types.h"
#include "board.h"

#include "nnue/evaluation.h"

static inline_always int evaluation(const board_t* const board) {
    return eval_nnue(board);
}

#endif
