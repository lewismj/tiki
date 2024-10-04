#ifndef TIKI_SEARCH_CONSTANTS_H
#define TIKI_SEARCH_CONSTANTS_H

#include "types.h"

/**
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

#define infinity    50000
#define mate_value  49000
#define mate_score  48000

#endif
