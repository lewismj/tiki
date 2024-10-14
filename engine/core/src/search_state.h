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

    uint32_t pv_table[MAX_PLY][MAX_PLY];
    int32_t history_moves[12][64];
    uint32_t killer_moves[2][MAX_PLY];
    uint8_t pv_length[MAX_PLY];
    uint64_t nodes_visited;
    uint8_t ply;
    bool follow_pv;
    bool score_pv;
} search_state_t;


#endif
