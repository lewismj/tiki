#ifndef TIKI_SYZYGY_H
#define TIKI_SYZYGY_H

#include <stdbool.h>
#include "board.h"
#include "types.h"

/**
 * Syzygy tablebases, probed with Fathom (engine/third_party/fathom).
 *
 * - In the search (negamax_node), a position with at most syzygy_search_pieces pieces, no castling
 *   rights and a fifty-move counter of 0 (just after a capture or pawn move) is looked up in the WDL
 *   tables. A win or loss is scored TB_WIN_SCORE less the ply, a draw 0.
 * - At the root (syzygy_rank_root, before each search), the moves are ranked with the DTZ tables,
 *   or the WDL tables if a DTZ table is missing, and only the best ranked moves are searched. With
 *   DTZ the ranking keeps the win within the fifty-move rule, so the search then probes nothing and
 *   chooses among those moves by itself. With WDL only, it keeps probing when winning.
 *
 * Fathom treats wins and losses that the fifty-move rule turns into draws ("cursed" and "blessed")
 * as draws here. Without tables (the default) nothing is probed and the search is unchanged.
 */

#define SYZYGY_PROBE_DEPTH_DEFAULT 1
#define SYZYGY_PROBE_DEPTH_MAX 100

/**
 * Loads the tables in 'path' (directories separated by ';' on Windows, ':' elsewhere), replacing
 * any loaded before. An empty path or "<empty>" unloads them. Returns the most pieces the tables
 * cover, 0 if none were found.
 */
int syzygy_init(const char* path);

/* The most pieces the loaded tables cover, 0 without tables. */
int syzygy_largest(void);

/* 'SyzygyProbeDepth': positions with as many pieces as the largest tables are probed from this
 * depth, those with fewer at any depth. */
extern int syzygy_probe_depth;

/* Pieces up to which the current search probes (0: none), set by syzygy_rank_root. */
extern int syzygy_search_pieces;

/**
 * The WDL result of 'board' for the side to move: 1 a win, 0 a draw, -1 a loss. False if it can't
 * be probed (too many pieces, castling rights, a fifty-move counter above 0, or a missing table).
 */
bool syzygy_probe_wdl(const board_t* board, int* wdl);

/**
 * Before a search: if the root is in the tables, restricts the root moves to the best ranked ones
 * (syzygy_root_allows) and sets syzygy_search_pieces for the search. Returns whether the root is
 * in the tables. Not thread safe: only the main thread calls it, before the helpers start.
 */
bool syzygy_rank_root(board_t* board);

/* Whether the search may play 'mv' at the root (always, unless syzygy_rank_root restricted it). */
bool syzygy_root_allows(move_t mv);

/* The root's tablebase result after syzygy_rank_root (1 win, 0 draw, -1 loss), if it's in the tables. */
bool syzygy_root_wdl(int* wdl);

/* After a search: no root restriction, probing as the tables allow. */
void syzygy_end_search(void);

/* Fathom's bitboards and squares count from a1, tiki's from a8: one is the other mirrored vertically. */
static inline bitboard syzygy_bitboard(bitboard b) {
    return __builtin_bswap64(b);
}

#endif
