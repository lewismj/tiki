#include <stdio.h>

#include "uci.h"
#include "attack_mask.h"
#include "zobrist_key.h"
#include "transposition.h"


void on_shutdown() {
    printf("Free transposition table.\n");
    free_transposition_table();
}

void on_startup() {
    init_zobrist_key();
    init_attack_table();
    init_transposition_table(256);
    atexit(on_shutdown);
}

