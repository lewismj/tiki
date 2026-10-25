#ifndef TIKI_SEARCH_STATE_H
#define TIKI_SEARCH_STATE_H

#include <unistd.h>
#include <string.h>
#include <stdatomic.h>
#include "search_constants.h"
#include "nnue/nnue.h"

static_assert(NNUE_STACK_SIZE > MAX_PLY + 1, "the NNUE stack must hold the deepest search path");


typedef struct align {
    /*
     * pv_table, use to store the principal variation, we use 64.64
     * array rather than a vector (triangular matrix), the indexing
     * is simplified using a matrix.
     */
    uint64_t repetition_check[MAX_MOVES];
    uint32_t pv_table[MAX_PLY][MAX_PLY];
    int32_t history_moves[12][64];
    /* Continuation history, [previous piece][previous target][piece][target]: how good a quiet
     * move has been straight after the move 1 or 2 plies back (one table for both). */
    int16_t continuation_history[12][64][12][64];
    uint32_t move_stack[MAX_PLY];  /* The move made at each ply of the search path, 0 for a null move. */
    uint32_t killer_moves[2][MAX_PLY];
    uint8_t pv_length[MAX_PLY];
    uint64_t nodes;
    /* Move ordering statistics (bench): beta cutoffs in negamax, how many on the first move
     * searched, how many by a quiet move and the quiet moves tried before each of those. */
    uint64_t cutoffs;
    uint64_t first_move_cutoffs;
    uint64_t quiet_cutoffs;
    uint64_t quiets_before_cutoff;
    uint8_t ply;
    uint16_t repetition_index;
    uint16_t null_move_index;  /* repetition_check index of the position after the last null move (0 if none). */
    bool follow_pv;
    bool score_pv;
    int score;                 /* Score of the last completed iteration (side to move). */
    int thread_id;             /* 0 for the main thread, which checks the clock and reports. */
    _Atomic uint64_t shared_nodes; /* Helper threads publish 'nodes' here for the main thread. */
    nnue_stack_t nnue;         /* Accumulators along the search path (when a network is loaded). */
} search_state_t;


/* The tables tied to one search's path from the root (PV, move stack, killers), emptied. */
static inline_always void clear_search_path(search_state_t* search_state) {
    memset(search_state->pv_table, 0ULL, sizeof(search_state->pv_table));
    memset(search_state->pv_length, 0ULL, sizeof(search_state->pv_length));
    memset(search_state->move_stack, 0, sizeof (search_state->move_stack));
    memset(search_state->killer_moves, 0, sizeof (search_state->killer_moves));
}

/* History and continuation history, emptied. They're kept from move to move during a game. */
static inline_always void clear_history(search_state_t* search_state) {
    memset(search_state->history_moves, 0, sizeof (search_state->history_moves));
    memset(search_state->continuation_history, 0, sizeof (search_state->continuation_history));
}

static inline_always void init_search_state(search_state_t* search_state) {
    search_state->nodes = 0;
    search_state->cutoffs = search_state->first_move_cutoffs = 0;
    search_state->quiet_cutoffs = search_state->quiets_before_cutoff = 0;
    search_state->thread_id = 0;
    atomic_init(&search_state->shared_nodes, 0);
    search_state->ply = 0;
    search_state->repetition_index = 0;
    search_state->null_move_index = 0;
    search_state->score = 0;
    search_state->score_pv = false;
    search_state->follow_pv = true;
    clear_search_path(search_state);
    clear_history(search_state);
    memset(search_state->repetition_check, 0ULL, sizeof(search_state->repetition_check));
    nnue_stack_init(&search_state->nnue);
}

/**
 * Before each search ('go'). The game history from 'position' is kept for the repetition checks,
 * and history and continuation history are kept so what was learnt on earlier moves carries
 * over. Both are cleared by init_search_state ('ucinewgame').
 */
static inline_always void clear_pv_table(search_state_t* search_state) {
    search_state->nodes = 0;
    atomic_init(&search_state->shared_nodes, 0);
    search_state->ply = 0;
    search_state->null_move_index = 0;
    search_state->score = 0;
    search_state->score_pv = false;
    search_state->follow_pv = true;
    clear_search_path(search_state);
    nnue_reset(&search_state->nnue);
}

/**
 * repetition_check[repetition_index] holds the current position, entries below it are the
 * earlier positions (game history from 'position', then the search path).
 *
 * Only positions with the same side to move (every second entry) since the last irreversible
 * move ('fifty_move' plies back) and the last null move can repeat. The position two plies back
 * can't, as each side has only made one move, so the scan starts four plies back.
 */
static inline_always bool contains_repetition(search_state_t* search_state, const uint64_t position_hash, int fifty_move) {
    int oldest = search_state->repetition_index - fifty_move;
    if (oldest < search_state->null_move_index) oldest = search_state->null_move_index;

    for (int i = search_state->repetition_index - 4; i >= oldest; i -= 2) {
        if (search_state->repetition_check[i] == position_hash) return true;
    }
    return false;
}


#endif
