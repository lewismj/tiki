#ifndef TIKI_EVALUATION_H
#define TIKI_EVALUATION_H

#include "types.h"
#include "board.h"

#include "nnue/nnue.h"

/**
 * The network's evaluation (centipawns, side to move). A network must be loaded (see
 * load_startup_network in tiki.h). 'nnue' is the search's accumulator stack, its top must be 'board'.
 */
static inline_always int evaluation(const board_t* const board, nnue_stack_t* nnue) {
    const int eval = nnue_evaluate_incremental(nnue, board);
    /* Debug builds check the incremental accumulators against a full refresh (as make_move does the hash). */
    assert(eval == nnue_evaluate(board));
    return eval;
}

#endif
