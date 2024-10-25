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

static inline_always bool contains_repetition(search_state_t* search_state, const uint64_t position_hash) {
    for (int i=search_state->repetition_index-1; i>0; i--) {
        if (search_state->repetition_check[i] == position_hash) return true;
    }
    return false;
}

#endif
