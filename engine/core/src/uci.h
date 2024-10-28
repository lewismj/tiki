#ifndef TIKI_UCI_H
#define TIKI_UCI_H

#include "board.h"

/**
 * Require these for Perft and other tests.
 */
void on_startup();
void parse_fen(const char* fen, board_t* board);

/**
 * Main UCI loop.
 */
void uci_main();

#endif
