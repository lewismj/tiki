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
void parse_position(const char* position, board_t* board, search_state_t* search_state);
void parse_go(const char* position, board_t* board, search_state_t* search_state, limits_t* limits);


/**
 * Main UCI loop.
 */
void uci_main();

#endif
