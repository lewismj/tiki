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
 * their own board and search state, sharing the TT and the correction history. The main thread
 * (this one) checks the clock and prints 'info' with the total nodes. When it finishes the helpers
 * are stopped by setting stop_search_flag, except with 'go infinite' where they run until 'stop',
 * and the move is chosen by a vote of every thread's last completed depth (vote_best_thread).
 */
move_t find_best_move(board_t* board, search_state_t* search_state, limits_t* limits);

/* A thread's last completed depth, for the Lazy SMP vote: its best move (0 if no depth completed),
 * score and depth. */
typedef struct {
    move_t move;
    int score;
    int depth;
} thread_result_t;

/* Added to each thread's score above the lowest in the vote (cp), so the lowest still counts. */
#define VOTE_SCORE_OFFSET 5

/**
 * Lazy SMP: which thread's move to play, from each thread's last completed depth ('results', the
 * main thread first). Returns its index, or -1 if no thread completed a depth.
 *
 * Each thread votes for its move with weight (score - lowest score + VOTE_SCORE_OFFSET) * depth,
 * so deeper and better scored results count for more, and the move with the most votes wins (on
 * a tie, the thread with the higher weight). Decisive scores come first: a thread that has found
 * a win is chosen over any that hasn't, the fastest win among them; a thread that sees itself lost
 * is chosen only if every thread does, the slowest loss among them. Starts from the main thread,
 * which keeps its move unless another thread is strictly better by those rules.
 */
int vote_best_thread(const thread_result_t* results, int count);

/**
 * The factor on the soft time limit after a completed depth (see limits.h): from how many depths
 * in a row the best move has been unchanged ('stability', 0 when it just changed) and the share of
 * the root's nodes ('root_nodes', that depth's) its search took ('best_move_nodes').
 */
double tm_scale(int stability, uint64_t best_move_nodes, uint64_t root_nodes);

/**
 * A further factor on the soft time limit, for a falling score ('score', the depth just completed): 1 plus
 * TM_FALLING_PREV % per 10 cp it is below the previous search's score ('previous', if 'has_previous': the
 * search for this side's last move) and TM_FALLING_ITER % per 10 cp below the score TM_FALLING_DEPTHS depths
 * before ('earlier', if 'has_earlier'), at most TM_FALLING_MAX %. Never below 1 (a rising score takes the
 * usual time), and 1 with mate or tablebase scores, which are no estimate.
 */
double tm_falling_scale(int score, bool has_previous, int previous, bool has_earlier, int earlier);

/**
 * A further factor on the soft time limit, for an unstable best move: (TM_INSTABILITY_BASE +
 * TM_INSTABILITY_PER_CHANGE * 'changes') / 100, 'changes' the times another move took the lead at the root during
 * a depth, the total halved after each depth (so recent depths count most).
 */
double tm_instability_scale(double changes);

/**
 * A further factor on the soft time limit, for a complex position: (TM_COMPLEXITY_BASE + TM_COMPLEXITY_SCALE *
 * complexity / TM_COMPLEXITY_CAP) / 100, the complexity |'score' - 'root_eval'| * ln('depth') (cp), at most
 * TM_COMPLEXITY_CAP. A search that has moved far from the static eval has found something the eval
 * doesn't see, and gets more time; one that agrees with it gets less. 1 without a root eval
 * ('has_eval' false: the root is in check) and with mate or tablebase scores, which are no estimate.
 */
double tm_complexity_scale(int score, bool has_eval, int root_eval, int depth);

/* The static eval refined by the position's TT entry (NULL for none): the TT score when its bound
 * says the true score is on that side of the eval, otherwise the eval. Mate scores are ignored. */
int adjusted_eval(int eval, const tt_info_t* tt);

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

/* The only legal move, 0 if there are none or more than one (find_best_move plays it at once on the clock). */
move_t only_legal_move(board_t* board);

/* Move ordering score: TT move, PV move, winning captures, killers, history, losing captures. */
int score_move(move_t move, move_t tt_move, board_t* board, search_state_t* search_state);
/* Scores all of a node's moves, then clears the PV flag so it only ever applies to this node. */
void score_moves(const move_buffer_t* buffer, int* scores, move_t tt_move, board_t* board, search_state_t* search_state);

/* Selection sort one step: swap the best scored move from i onwards into i and return it. */
move_t pick_next_move(move_buffer_t* buffer, int* scores, int i);

/* Quiet move raised alpha, the history_bonus with gravity, bounded by HISTORY_MAX. */
void update_history(search_state_t* search_state, move_t mv, int depth);
void penalise_history(search_state_t* search_state, move_t mv, int depth);
/* Capture history: a capture caused a cutoff, or was searched before the move that did ('board' is
 * the position the capture is made from). The same bonus and gravity as quiet history. */
void update_capture_history(search_state_t* search_state, const board_t* board, move_t mv, int depth);
void penalise_capture_history(search_state_t* search_state, const board_t* board, move_t mv, int depth);
/* Non-capture caused a cutoff, the first killer at this ply (the old first becomes the second). */
void store_killer(search_state_t* search_state, move_t mv);

/* Fifty-move rule (unless the last move mated), insufficient material or repetition. */
bool is_draw(board_t* board, search_state_t* search_state);

/* KvK, KNvK and KBvK (either side). */
bool is_insufficient_material(const board_t* board);

/* Pruning and reduction decisions, see search.c. */
bool is_improving(int static_eval, int eval_2_plies_up, int eval_4_plies_up);
bool can_null_move(const board_t* board, const search_state_t* search_state, int depth, int beta, int static_eval,
                   bool in_check, bool is_pv_node);
int null_move_depth(int depth, int static_eval, int beta);
bool can_reverse_futility_prune(int depth, int static_eval, int beta, bool in_check, bool is_pv_node, int ply,
                                bool improving);
bool is_futile(int depth, int static_eval, int alpha, bool in_check, bool is_pv_node);
bool can_late_move_prune(int depth, int moves_searched, int alpha, bool in_check, bool is_pv_node, bool improving);
bool can_see_prune(int depth, int moves_searched, int alpha, bool in_check, bool is_pv_node);
int see_prune_threshold(int depth, bool is_quiet);
bool can_reduce(int depth, int moves_searched, bool is_quiet, bool gives_check, bool in_check, bool is_killer,
                bool is_bad_capture);
bool can_history_prune(int depth, int history, int moves_searched, int alpha, bool in_check, bool is_pv_node,
                       bool is_killer);
/* A quiet move's history at the node: its history plus its continuation history after the moves 1, 2
 * and 4 plies back (search_state->path_moves, those there are). */
int quiet_history(const search_state_t* search_state, move_t mv);
int lmr_reduction(int depth, int moves_searched, bool is_pv_node, int history, bool cut_node, bool improving);
/* Correction history (corr_history_t): the correction to the position's static eval, within
 * +-CORR_MAX, and an update moving it towards 'error' (score - corrected eval) at this depth. */
int eval_correction(const board_t* board, const search_state_t* search_state);
void update_correction(search_state_t* search_state, const board_t* board, int depth, int error);
int iir_reduction(int depth, move_t tt_move);
int probcut_beta(int beta, bool improving);
bool can_probcut(int depth, int ply, int beta, bool in_check, bool is_pv_node, bool tt_hit, int tt_depth,
                 tt_entry_type tt_type, int tt_score, int pc_beta);
bool can_singular_extend(int depth, int ply, int root_depth, move_t tt_move, int tt_depth, tt_entry_type tt_type,
                         int tt_score);
int singular_beta(int tt_score, int depth);
typedef enum { singular_none, singular_extend, singular_reduce, singular_multi_cut } singular_result_t;
singular_result_t singular_result(int score, int s_beta, int tt_score, int beta, bool is_pv_node);

#endif
