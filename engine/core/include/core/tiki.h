#ifndef TIKI_H
#define TIKI_H

#include <stdbool.h>
#include <stdint.h>

/* Environment variable naming the network file, read by load_startup_network. */
#define TIKI_EVALFILE_ENV "TIKI_EVALFILE"

/**
 * Public interface of the engine library. The internals (board, search, etc.) are in
 * core/src and aren't part of it.
 */

/* Initialise the tables and the transposition table, call once before anything else. */
void on_startup(void);

/**
 * Loads the network tiki evaluates with, required before searching (there's no other
 * evaluation): the file named by TIKI_EVALFILE if it's set, otherwise the network built into the
 * binary (TIKI_EMBED_NETWORK=ON in CMake). Prints the reason to stderr and returns false if
 * there's no network, or the TIKI_EVALFILE one can't be loaded.
 */
bool load_startup_network(void);

/* Run the UCI loop on stdin until 'quit' or end of input. */
void uci_main(void);

/* Search the fixed bench positions to 'depth' (0 for the default) and print nodes and nodes/second. */
uint64_t bench(int depth);

/* Self-play NNUE training data appended to 'path' (format in core/src/datagen.h). Returns the
 * number of positions written, -1 if the file can't be opened. */
int64_t datagen_to_file(const char* path, int games, int depth, uint32_t seed);

/* Loads the network at 'net_path' and prints its evaluation of 'fen' (centipawns, side to move).
 * Returns 0, or 1 if the network can't be loaded. Used to check the trainer's export. */
int nnue_eval_fen(const char* net_path, const char* fen);

/* OpenBench's SPSA input for the tunable search parameters, a line each (core/src/tune.h). Only a
 * build with TIKI_TUNE has them, otherwise it says so. */
void print_spsa_input(void);

#endif
