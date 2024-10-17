#ifndef TIKI_UCI_H
#define TIKI_UCI_H

#include "board.h"


void on_startup();
void on_shutdown();


void parse_fen(const char* fen, board_t* board);
void uci_main();

#endif
