#include <stdio.h>
#include <stdlib.h>

#include "transposition.h"


transposition_node_t* t_table;
size_t tt_size;
uint8_t tt_generation;


void free_transposition_table() {
    if (t_table != NULL) free(t_table);
    t_table = NULL;
}

static size_t tt_entries_for(size_t mb) {
    /* Round down to a power of two, so the index is a mask rather than a division. */
    const size_t entries = ((size_t) 0x100000 * mb) / sizeof(transposition_node_t);
    size_t size = 1;
    while (size * 2 <= entries) size *= 2;
    return size;
}

bool init_transposition_table(size_t mb) {
    if (mb < 1) mb = 1;
    if (mb > TT_MAX_MB) mb = TT_MAX_MB;

    /* Free first, so the old and new tables don't both need to fit in memory. */
    const size_t previous_size = tt_size;
    free_transposition_table();

    size_t size = tt_entries_for(mb);
    transposition_node_t* table = (transposition_node_t*) calloc(size, sizeof(transposition_node_t));
    bool requested = table != NULL;
    if (table == NULL && previous_size) {
        printf("info string Tiki could not allocate %zuMB for the transposition table, keeping %zuMB.\n",
               mb, (previous_size * sizeof(transposition_node_t)) >> 20);
        size = previous_size;
        table = (transposition_node_t*) calloc(size, sizeof(transposition_node_t));
    }
    if (table == NULL) {
        printf("info string Tiki error, could not allocate %zuMB for the transposition table.\n", mb);
        tt_size = 0;
        tt_generation = 0;
        return false;
    }

    t_table = table;
    tt_size = size;
    tt_generation = 0;
    return requested;
}
