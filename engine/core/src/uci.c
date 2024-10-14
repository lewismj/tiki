#include <stdio.h>

#include "uci.h"
#include "attack_mask.h"
#include "zobrist_key.h"
#include "transposition.h"
#include "parallel.h"

void on_shutdown() {
    printf("Shutting down search threads.\n");
    free_search_threads();
    printf("Free transposition table.\n");
    free_transposition_table();
}

void on_startup(int max_threads) {
    init_search_threads(max_threads);
    init_zobrist_key();
    init_attack_table();
    init_transposition_table(256);
    atexit(on_shutdown);
}

