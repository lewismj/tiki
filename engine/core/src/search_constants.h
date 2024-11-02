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

#define INF 900000
#define MATE_VALUE 890000
#define MATE_SCORE 880000
#define STOPPED_SEARCH  (-900000)

#define MAX_MOVES 1024           /* Maximum number of moves in the game.                                     */
#define MAX_PLY 64               /* Maximum ply we can search.                                               */
#define NULL_MOVE_DEPTH_BOUND 4  /* Lower bound for depth to enable null move pruning.                       */
#define LMR_DEPTH_BOUND 4        /* Lower bound for depth to enable late move reduction.                     */
#define ASPIRATION_WINDOW 300    /* Offsets to use to narrow search window with iterative deepening.         */
#endif
