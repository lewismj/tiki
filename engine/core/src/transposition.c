#include <stdio.h>
#include <stdlib.h>

#include "transposition.h"


transposition_node_t* t_table;
size_t tt_size;


void free_transposition_table() {
    if (t_table != NULL) free(t_table);
}

void init_transposition_table(unsigned short mb) {
    if (t_table != NULL) free(t_table);

    size_t hash_size = (size_t) 0x100000 * mb;
    tt_size = hash_size / sizeof(transposition_node_t);

    t_table = (transposition_node_t*) calloc(tt_size, sizeof(transposition_node_t));
    if ( t_table == NULL) {
        printf("Tiki error, could not allocate %dMB for transposition table.\n", mb);
    }
}
