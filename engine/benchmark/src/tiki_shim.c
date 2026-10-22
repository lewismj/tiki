#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "tiki_shim.h"

#include "../../core/src/attack_mask.h"
#include "../../core/src/zobrist_key.h"
#include "../../core/src/board.h"
#include "../../core/src/move_generator.h"
#include "../../core/src/evaluation.h"
#include "../../core/src/search_state.h"
#include "../../core/src/search.h"
#include "../../core/src/transposition.h"
#include "../../core/src/limits.h"
#include "../../core/src/uci.h"


struct tiki_position {
    board_t board;
    search_state_t search_state;
    char fen[128];
};


void tiki_init(unsigned short hash_mb) {
    init_attack_table();
    init_zobrist_key();
    init_lmr_table();
    init_transposition_table(hash_mb);
}

tiki_position* tiki_position_new(const char* fen) {
    tiki_position* position = malloc(sizeof(tiki_position));
    snprintf(position->fen, sizeof(position->fen), "%s", fen);
    parse_fen(fen, &position->board);
    return position;
}

void tiki_position_free(tiki_position* position) {
    free(position);
}

int tiki_evaluate(const tiki_position* position) {
    return evaluation(&position->board);
}

int tiki_generate_moves(tiki_position* position) {
    move_buffer_t buffer;
    generate_moves(&position->board, &buffer);
    return buffer.index;
}

int tiki_make_undo_all(tiki_position* position) {
    move_buffer_t buffer;
    generate_moves(&position->board, &buffer);
    int legal = 0;
    for (int i = 0; i < buffer.index; i++) {
        legal += make_move(&position->board, buffer.moves[i]);
        pop_move(&position->board);
    }
    return legal;
}

static uint64_t perft(board_t* board, int depth) {
    if (depth == 0) return 1;
    uint64_t nodes = 0;
    move_buffer_t buffer;
    generate_moves(board, &buffer);
    for (int i = 0; i < buffer.index; i++) {
        if (make_move(board, buffer.moves[i])) nodes += perft(board, depth - 1);
        pop_move(board);
    }
    return nodes;
}

uint64_t tiki_perft(tiki_position* position, int depth) {
    return perft(&position->board, depth);
}

uint64_t tiki_search(tiki_position* position, int depth) {
    char command[160];
    limits_t limits;

    snprintf(command, sizeof(command), "position fen %s", position->fen);
    tt_clear();
    init_search_state(&position->search_state);
    parse_position(command, &position->board, &position->search_state);
    reset_time_control(&limits);
    limits.depth = depth;

    /* find_best_move prints 'info' lines, keep them out of the benchmark output. */
    static int dev_null = -1;
    if (dev_null == -1) dev_null = open("/dev/null", O_WRONLY);
    fflush(stdout);
    int saved = dup(STDOUT_FILENO);
    dup2(dev_null, STDOUT_FILENO);
    find_best_move(&position->board, &position->search_state, &limits);
    fflush(stdout);
    dup2(saved, STDOUT_FILENO);
    close(saved);
    return position->search_state.nodes;
}
