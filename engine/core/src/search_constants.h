#ifndef TIKI_SEARCH_CONSTANTS_H
#define TIKI_SEARCH_CONSTANTS_H

#include "types.h"

/**
 * TODO - these values need to be scaled with average evaluation scores.
 *
 * Most valuable victim, least valuable attacker.
 * Heuristic used to sort captures.
 *     [Victims]    Pawn Knight Bishop   Rook  Queen   King
 *     [Attackers]
 *           Pawn   105    205    305    405    505    605
 *         Knight   104    204    304    404    504    604
 *         Bishop   103    203    303    403    503    603
 *           Rook   102    202    302    402    502    602
 *          Queen   101    201    301    401    501    601
 *           King   100    200    300    400    500    600
 */

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
#endif
