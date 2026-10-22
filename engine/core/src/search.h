#ifndef TIKI_SEARCH_H
#define TIKI_SEARCH_H

#include "types.h"
#include "board.h"
#include "move_generator.h"
#include "search_constants.h"
#include "search_state.h"
#include "limits.h"


/**
 * Iterative deepening search to limits->depth (or until stopped), printing 'info' lines.
 * Returns the best move from the last completed depth, the first legal move if none completed,
 * or 0 when there are no legal moves.
 */
move_t find_best_move(board_t* board, search_state_t* search_state, limits_t* limits);

/* Prints 'bestmove', 'bestmove 0000' for no move. */
void print_bestmove(move_t mv);


/*
 * The rest is the search's internals, declared here so they can be unit tested.
 */

int negamax(int alpha, int beta, int depth, board_t* board, search_state_t* search_state, limits_t* limits);
int quiescence(int alpha, int beta, board_t* board, search_state_t* search_state, limits_t* limits);

/* Root search in a window around the previous depth's score, widened on a fail. */
int aspiration_search(board_t* board, search_state_t* search_state, limits_t* limits, int depth, int previous_score);

/* Used when no iteration completed (stopped very early), 0 if there are no legal moves. */
move_t first_legal_move(board_t* board);

/* Move ordering score: TT move, PV move, winning captures, killers, history, losing captures. */
int score_move(move_t move, move_t tt_move, board_t* board, search_state_t* search_state);

/* Selection sort one step: swap the best scored move from i onwards into i and return it. */
move_t pick_next_move(move_buffer_t* buffer, int* scores, int i);

/* Quiet move raised alpha, depth^2 bonus with gravity, bounded by HISTORY_MAX. */
void update_history(search_state_t* search_state, move_t mv, int depth);

/* Fifty-move rule, insufficient material or repetition. */
bool is_draw(const board_t* board, search_state_t* search_state);

/* KvK, KNvK and KBvK (either side). */
bool is_insufficient_material(const board_t* board);

/* Pruning and reduction decisions, see search.c. */
bool can_null_move(const board_t* board, const search_state_t* search_state, int depth, int beta, int static_eval,
                   bool in_check, bool is_pv_node);
bool can_reverse_futility_prune(int depth, int static_eval, int beta, bool in_check, bool is_pv_node, int ply);
bool is_futile(int depth, int static_eval, int alpha, bool in_check, bool is_pv_node);
int lmr_reduction(int depth, int moves_searched, bool is_pv_node);
int iir_reduction(int depth, move_t tt_move);

#endif
