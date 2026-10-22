#ifndef TIKI_H
#define TIKI_H

#include <stdint.h>

/**
 * Public interface of the engine library. The internals (board, search, etc.) are in
 * core/src and aren't part of it.
 */

/* Initialise the tables and the transposition table, call once before anything else. */
void on_startup(void);

/* Run the UCI loop on stdin until 'quit' or end of input. */
void uci_main(void);

/* Search the fixed bench positions to 'depth' (0 for the default) and print nodes and nodes/second. */
uint64_t bench(int depth);

#endif
