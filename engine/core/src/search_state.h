#ifndef TIKI_SEARCH_STATE_H
#define TIKI_SEARCH_STATE_H

#include <unistd.h>
#include <string.h>
#include <stdatomic.h>
#include "search_constants.h"
#include "nnue/nnue.h"

static_assert(NNUE_STACK_SIZE > MAX_PLY + 1, "the NNUE stack must hold the deepest search path");

#ifdef TIKI_SEARCH_STATS
/* Move ordering statistics, printed by bench: beta cutoffs in negamax, how many on the first move
 * searched, how many by a quiet move and the quiet moves tried before each of those. */
typedef struct {
    uint64_t cutoffs;
    uint64_t first_move_cutoffs;
    uint64_t quiet_cutoffs;
    uint64_t quiets_before_cutoff;
} search_stats_t;
#endif


typedef struct align {
    /*
     * pv_table, use to store the principal variation, we use 64.64
     * array rather than a vector (triangular matrix), the indexing
     * is simplified using a matrix.
     */
    uint64_t repetition_check[MAX_MOVES];
    uint32_t pv_table[MAX_PLY][MAX_PLY];
    int32_t history_moves[12][64];
    uint32_t killer_moves[2][MAX_PLY];
    uint8_t pv_length[MAX_PLY];
    uint64_t nodes;
    uint64_t pv_nodes;         /* Nodes that ordered the previous iteration's PV move first (tests). */
#ifdef TIKI_SEARCH_STATS
    search_stats_t stats;
#endif
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


/**
 * The tables one search fills (PV, history, killers), emptied before each search. Keeping history
 * from move to move lost Elo (SPRT -16 +- 9).
 */
static inline_always void clear_search_tables(search_state_t* search_state) {
    memset(search_state->pv_table, 0ULL, sizeof(search_state->pv_table));
    memset(search_state->pv_length, 0ULL, sizeof(search_state->pv_length));
    memset(search_state->history_moves, 0, sizeof (search_state->history_moves));
    memset(search_state->killer_moves, 0, sizeof (search_state->killer_moves));
}

static inline_always void init_search_state(search_state_t* search_state) {
    search_state->nodes = 0;
    search_state->pv_nodes = 0;
#ifdef TIKI_SEARCH_STATS
    memset(&search_state->stats, 0, sizeof(search_state->stats));
#endif
    search_state->thread_id = 0;
    atomic_init(&search_state->shared_nodes, 0);
    search_state->ply = 0;
    search_state->repetition_index = 0;
    search_state->null_move_index = 0;
    search_state->score = 0;
    search_state->score_pv = false;
    search_state->follow_pv = true;
    clear_search_tables(search_state);
    memset(search_state->repetition_check, 0ULL, sizeof(search_state->repetition_check));
    nnue_stack_init(&search_state->nnue);
}

/* Before each search ('go'). The game history from 'position' is kept for the repetition checks. */
static inline_always void clear_pv_table(search_state_t* search_state) {
    search_state->nodes = 0;
    search_state->pv_nodes = 0;
    atomic_init(&search_state->shared_nodes, 0);
    search_state->ply = 0;
    search_state->null_move_index = 0;
    search_state->score = 0;
    search_state->score_pv = false;
    search_state->follow_pv = true;
    clear_search_tables(search_state);
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
