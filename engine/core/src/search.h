#ifndef TIKI_SEARCH_H
#define TIKI_SEARCH_H

#include "types.h"
#include "board.h"
#include "move_generator.h"
#include "search_constants.h"
#include "search_state.h"
#include "transposition.h"
#include "limits.h"


#define MAX_SEARCH_THREADS 256

/**
 * Iterative deepening search to limits->depth (or until stopped), printing 'info' lines.
 * Returns the best move from the last completed depth, the first legal move if none completed,
 * or 0 when there are no legal moves.
 *
 * With limits->threads > 1 the search is Lazy SMP: helper threads search the same position on
 * their own board and search state, sharing only the TT. The main thread (this one) checks the
 * clock, prints 'info' with the total nodes and returns the move. When it finishes the helpers
 * are stopped by setting stop_search_flag, except with 'go infinite' where they run until 'stop'.
 */
move_t find_best_move(board_t* board, search_state_t* search_state, limits_t* limits);

/**
 * The factor on the soft time limit after a completed depth (see limits.h): from how many depths
 * in a row the best move has been unchanged ('stability', 0 when it just changed) and the share of
 * the root's nodes ('root_nodes', that depth's) its search took ('best_move_nodes').
 */
double tm_scale(int stability, uint64_t best_move_nodes, uint64_t root_nodes);

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
/* Scores all of a node's moves, then clears the PV flag so it only ever applies to this node. */
void score_moves(const move_buffer_t* buffer, int* scores, move_t tt_move, board_t* board, search_state_t* search_state);

/* Selection sort one step: swap the best scored move from i onwards into i and return it. */
move_t pick_next_move(move_buffer_t* buffer, int* scores, int i);

/* Quiet move raised alpha, min(16 * depth^2, 1600) bonus with gravity, bounded by HISTORY_MAX. */
void update_history(search_state_t* search_state, move_t mv, int depth);
void penalise_history(search_state_t* search_state, move_t mv, int depth);
/* Non-capture caused a cutoff, the first killer at this ply (the old first becomes the second). */
void store_killer(search_state_t* search_state, move_t mv);

/* Fifty-move rule (unless the last move mated), insufficient material or repetition. */
bool is_draw(board_t* board, search_state_t* search_state);

/* KvK, KNvK and KBvK (either side). */
bool is_insufficient_material(const board_t* board);

/* Pruning and reduction decisions, see search.c. */
bool can_null_move(const board_t* board, const search_state_t* search_state, int depth, int beta, int static_eval,
                   bool in_check, bool is_pv_node);
bool can_reverse_futility_prune(int depth, int static_eval, int beta, bool in_check, bool is_pv_node, int ply);
bool is_futile(int depth, int static_eval, int alpha, bool in_check, bool is_pv_node);
bool can_late_move_prune(int depth, int moves_searched, int alpha, bool in_check, bool is_pv_node);
bool can_see_prune(int depth, int moves_searched, int alpha, bool in_check, bool is_pv_node);
int see_prune_threshold(int depth, bool is_quiet);
bool can_reduce(int depth, int moves_searched, bool is_quiet, bool gives_check, bool in_check, bool is_killer);
int lmr_reduction(int depth, int moves_searched, bool is_pv_node, int history);
int iir_reduction(int depth, move_t tt_move);
bool can_singular_extend(int depth, int ply, int root_depth, move_t tt_move, int tt_depth, tt_entry_type tt_type,
                         int tt_score);
int singular_beta(int tt_score, int depth);
typedef enum { singular_none, singular_extend, singular_reduce, singular_multi_cut } singular_result_t;
singular_result_t singular_result(int score, int s_beta, int tt_score, int beta, bool is_pv_node);

#endif
