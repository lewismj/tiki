#ifndef TIKI_SEARCH_STATE_H
#define TIKI_SEARCH_STATE_H

#include <unistd.h>
#include "search_constants.h"


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
    uint8_t ply;
    uint8_t repetition_index;
    bool follow_pv;
    bool score_pv;
} search_state_t;


static inline_always void init_search_state(search_state_t* search_state) {
    search_state->nodes = 0;
    search_state->ply = 0;
    search_state->repetition_index = 0;
    search_state->score_pv = false;
    search_state->follow_pv = true;
    memset(search_state->pv_table, 0ULL, sizeof(search_state->pv_table));
    memset(search_state->pv_length, 0ULL, sizeof(search_state->pv_length));
    memset(search_state->history_moves, 0, sizeof (search_state->history_moves));
    memset(search_state->killer_moves, 0, sizeof (search_state->killer_moves));
    memset(search_state->repetition_check, 0ULL, sizeof(search_state->repetition_check));
}

/**
 * Used by UCI loop, where we parse a fen_position each ply, so the repetition state needs to be
 * preserved.
 */
static inline_always void clear_pv_table(search_state_t* search_state) {
    search_state->nodes = 0;
    search_state->ply = 0;
    search_state->score_pv = false;
    search_state->follow_pv = true;
    memset(search_state->pv_table, 0ULL, sizeof(search_state->pv_table));
    memset(search_state->pv_length, 0ULL, sizeof(search_state->pv_length));
    memset(search_state->history_moves, 0, sizeof (search_state->history_moves));
    memset(search_state->killer_moves, 0, sizeof (search_state->killer_moves));
}

static inline_always bool contains_repetition(search_state_t* search_state, const uint64_t position_hash) {
    for (int i=0; i<search_state->repetition_index; i++) {
        if (search_state->repetition_check[i] == position_hash) return true;
    }
    return false;
}

#endif
