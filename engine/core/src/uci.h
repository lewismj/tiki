#ifndef TIKI_UCI_H
#define TIKI_UCI_H

#include "board.h"
#include "search_state.h"
#include "limits.h"

/**
 * Require these for Perft and other tests.
 */
void on_startup();
void parse_fen(const char* fen, board_t* board);
move_t parse_move(const char* input, board_t* board); /* 0 if not a pseudo-legal move here. */
/* Writes the position as FEN. The board doesn't track the full move number, so it's passed in. */
void board_to_fen(const board_t* board, int fullmove, char* fen, size_t size);
void parse_position(const char* position, board_t* board, search_state_t* search_state);
void parse_go(const char* command, board_t* board, search_state_t* search_state, limits_t* limits);
void parse_go_limits(const char* command, const board_t* board, limits_t* limits);
void new_game(board_t* board, search_state_t* search_state);


typedef enum {
    uci_cmd_unknown,
    uci_cmd_uci,
    uci_cmd_ucinewgame,
    uci_cmd_isready,
    uci_cmd_position,
    uci_cmd_go,
    uci_cmd_stop,
    uci_cmd_quit,
    uci_cmd_setoption,
    uci_cmd_bench      /* Not UCI, 'bench [depth]' runs the fixed benchmark (see bench.h). */
} uci_command_t;

/**
 * Identify a UCI command by its first token.
 */
uci_command_t parse_uci_command(const char* command);

/**
 * 'setoption name <name> value <value>'. The options are 'Hash' (MB, the TT is resized and
 * cleared) and 'Threads' (Lazy SMP, used from the next 'go').
 */
void parse_setoption(const char* command);
int get_search_threads();

/**
 * Handle a single UCI command, returns false once the engine should exit.
 */
bool handle_uci_command(const char* command, board_t* board, search_state_t* search_state, limits_t* limits);


/**
 * UCI loop reading commands from 'in' (via the input thread), until 'quit' or end of input.
 */
void uci_loop(FILE* in, board_t* board, search_state_t* search_state, limits_t* limits);

/**
 * Main UCI loop, on stdin.
 */
void uci_main();

#endif
