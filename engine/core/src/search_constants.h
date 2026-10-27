#ifndef TIKI_SEARCH_CONSTANTS_H
#define TIKI_SEARCH_CONSTANTS_H

#include "types.h"

typedef struct align {
    int mvv_lva[12][12];
} search_const_t ;

/**
 * Singleton instance of the constants.
 */
extern const search_const_t search_const;

/**
 * Late move reductions by [depth][moves searched], 0.75 + log(depth) * log(moves) / 2.25.
 * Filled by init_lmr_table(), called at startup (on_startup).
 */
extern uint8_t lmr_table[64][64];
void init_lmr_table();

#define INF 900000
#define MATE_VALUE 890000
#define MATE_SCORE 880000
#define STOPPED_SEARCH  (-900000)

#define MAX_MOVES 1024           /* Maximum number of moves in the game.                                     */
#define MAX_PLY 64               /* Maximum ply we can search.                                               */
#define MAX_SEARCH_DEPTH 32      /* Iterative deepening limit when no depth is given (e.g. 'go infinite').   */
#define NULL_MOVE_DEPTH_BOUND 4  /* Lower bound for depth to enable null move pruning.                       */
#define LMR_MIN_DEPTH 3          /* Late move reduction from this depth...                                   */
#define LMR_MIN_MOVES 3          /* ...after this many moves have been searched at the node.                 */
#define LMR_TABLE_SIZE 64
#define IIR_MIN_DEPTH 4          /* Internal iterative reduction (no TT move) from this depth.               */
#define ASPIRATION_WINDOW 25     /* Initial half-width of the root window around the previous score...       */
#define ASPIRATION_MAX_DELTA 1000 /* ...doubled on each fail, full window once it's wider than this.         */
#define ASPIRATION_MIN_DEPTH 4   /* Shallower depths use the full window, their scores are less stable.      */
#define HISTORY_MAX 7000         /* History scores stay within +-this, under the killers (8000, 9000).       */
#define MAX_QUIETS_PENALISED 64  /* Quiet moves remembered per node for the history malus.                   */
#define BAD_CAPTURE_SCORE (-20000) /* Ordering offset for losing captures (SEE < 0), below every quiet move.  */
#define DELTA_MARGIN 200         /* Qsearch delta pruning, skip a capture if eval + victim + margin <= alpha. */
#define RFP_MAX_DEPTH 6          /* Reverse futility pruning up to this depth...                             */
#define RFP_MARGIN 100           /* ...when static eval - margin * depth >= beta.                            */
#define FUTILITY_MAX_DEPTH 3     /* Futility pruning of quiet moves up to this depth...                      */
#define FUTILITY_MARGIN 150      /* ...when static eval + margin * depth <= alpha.                           */
#define LMP_MAX_DEPTH 6          /* Late move pruning of quiet moves up to this depth...                     */
#define LMP_BASE 3               /* ...after base + depth^2 moves have been searched at the node.            */
#define SEE_PRUNE_MAX_DEPTH 8    /* SEE pruning in the main search up to this depth, skipping...             */
#define SEE_QUIET_MARGIN 60      /* ...quiet moves with SEE < -margin * depth...                             */
#define SEE_CAPTURE_MARGIN 20    /* ...and captures with SEE < -margin * depth^2.                            */
#define SE_MIN_DEPTH 6           /* Singular extensions from this depth, with a TT entry at least...         */
#define SE_TT_DEPTH_MARGIN 3     /* ...this much shallower than the node, if every other move scores below...*/
#define SE_MARGIN 2              /* ...the TT score - margin * depth (cp) in a search of half the depth.      */

/* Move ordering (score_move), highest first. Each category is an offset, the bands mustn't overlap. */
#define TT_MOVE_SCORE 30000      /* The TT move...                                                           */
#define PV_MOVE_SCORE 20000      /* ...the PV move while following the PV...                                 */
#define CAPTURE_SCORE 10000      /* ...winning or equal captures, plus mvv_lva...                            */
#define KILLER_1_SCORE 9000      /* ...the killers...                                                        */
#define KILLER_2_SCORE 8000
                                 /* ...quiet moves by history, within +-HISTORY_MAX, then losing captures.   */
#define MVV_LVA_MIN 100          /* The range of search_const.mvv_lva (search_constants.c).                  */
#define MVV_LVA_MAX 605

static_assert(CAPTURE_SCORE + MVV_LVA_MAX < PV_MOVE_SCORE && PV_MOVE_SCORE < TT_MOVE_SCORE, "captures under the PV move");
static_assert(KILLER_1_SCORE < CAPTURE_SCORE + MVV_LVA_MIN && KILLER_2_SCORE < KILLER_1_SCORE, "killers under captures");
static_assert(HISTORY_MAX < KILLER_2_SCORE, "quiet moves under the killers");
static_assert(BAD_CAPTURE_SCORE + MVV_LVA_MAX < -HISTORY_MAX, "losing captures under every quiet move");

/* Built with TIKI_TUNE, the tunable constants above become variables set by UCI options (tune.h). */
#include "tune.h"
#endif
