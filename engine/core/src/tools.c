#include <stdio.h>
#include <stdlib.h>

#include "../include/core/tiki.h"
#include "datagen.h"
#include "board.h"
#include "uci.h"
#include "nnue/nnue.h"


int64_t datagen_to_file(const char* path, int games, int depth, uint32_t seed) {
    FILE* out = fopen(path, "a");
    if (out == NULL) return -1;

    const datagen_options_t options = {
            .games = games,
            .depth = depth,
            .random_plies = 8,
            .max_plies = 400,
            .seed = seed,
    };
    uint64_t written = datagen(out, &options);
    fclose(out);
    return (int64_t) written;
}

bool load_startup_network(void) {
    const char* path = getenv(TIKI_EVALFILE_ENV);
    if (path == NULL || *path == '\0') {
        fprintf(stderr, "No network: set %s to a .nnue file (see training/README.md).\n", TIKI_EVALFILE_ENV);
        return false;
    }
    if (!nnue_load(path)) {
        fprintf(stderr, "Could not load the network '%s' (%s).\n", path, TIKI_EVALFILE_ENV);
        return false;
    }
    return true;
}

int nnue_eval_fen(const char* net_path, const char* fen) {
    if (!nnue_load(net_path)) {
        fprintf(stderr, "Could not load network '%s'.\n", net_path);
        return 1;
    }
    board_t board;
    parse_fen(fen, &board);
    printf("%d\n", nnue_evaluate(&board));
    nnue_free();
    return 0;
}
