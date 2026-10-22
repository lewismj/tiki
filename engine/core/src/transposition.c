
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

void init_transposition_table(unsigned short mb) {
    if (t_table != NULL) free(t_table);

    /* Round down to a power of two, so the index is a mask rather than a division. */
    size_t entries = ((size_t) 0x100000 * mb) / sizeof(transposition_node_t);
    tt_size = 1;
    while (tt_size * 2 <= entries) tt_size *= 2;

    t_table = (transposition_node_t*) calloc(tt_size, sizeof(transposition_node_t));
    if ( t_table == NULL) {
        printf("Tiki error, could not allocate %dMB for transposition table.\n", mb);
    }
    tt_generation = 0;
}
