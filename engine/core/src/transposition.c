#include <stdlib.h>
#include "transposition.h"


align transposition_node_t* t_table;

void free_transposition_table() {
    /* No need to iterate over elements, struct. contains primitives only. */
    if (t_table != NULL) free(t_table);
}


void init_transposition_table(unsigned short mb) {


    if (t_table !=NULL) atexit(free_transposition_table);
}