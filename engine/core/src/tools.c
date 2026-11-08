#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/core/tiki.h"
#include "datagen.h"
#include "board.h"
#include "uci.h"
#include "nnue/nnue.h"


/* Whether 'path' ends in '.bin': the trainer's binary format, otherwise text. */
static bool is_binary_path(const char* path) {
    const size_t n = strlen(path);
    return n >= 4 && strcmp(path + n - 4, ".bin") == 0;
}

int64_t datagen_to_file(const char* path, int games, int depth, uint32_t seed) {
    const bool binary = is_binary_path(path);
    FILE* out = fopen(path, binary ? "ab" : "a");
    if (out == NULL) return -1;

    if (binary) {
        /* A new file gets the header; positions are only appended to a file that has it. */
        fseek(out, 0, SEEK_END);
        if (ftell(out) == 0) {
            fwrite(DATAGEN_BINARY_MAGIC, 1, DATAGEN_MAGIC_SIZE, out);
        } else {
            FILE* in = fopen(path, "rb");
            char magic[DATAGEN_MAGIC_SIZE] = {0};
            const bool ok = in != NULL && fread(magic, 1, DATAGEN_MAGIC_SIZE, in) == DATAGEN_MAGIC_SIZE &&
                            memcmp(magic, DATAGEN_BINARY_MAGIC, DATAGEN_MAGIC_SIZE) == 0;
            if (in != NULL) fclose(in);
            if (!ok) {
                fclose(out);
                return -2;
            }
        }
    }

    const datagen_options_t options = {
            .games = games,
            .depth = depth,
            .random_plies = 8,
            .max_plies = 400,
            .seed = seed,
            .format = binary ? datagen_binary : datagen_text,
    };
    uint64_t written = datagen(out, &options);
    fclose(out);
    return (int64_t) written;
}

bool load_startup_network(void) {
    const char* path = getenv(TIKI_EVALFILE_ENV);
    if (path == NULL || *path == '\0') {
        if (nnue_load_embedded()) return true;
        fprintf(stderr, "No network: set %s to a .nnue file, or build with -DTIKI_EMBED_NETWORK=ON "
                        "(see the README).\n", TIKI_EVALFILE_ENV);
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
