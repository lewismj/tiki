#ifndef TIKI_SEARCH_CONSTANTS_H
#define TIKI_SEARCH_CONSTANTS_H

#include "types.h"

/* Move ordering of captures by [attacker][victim]: most valuable victim, least valuable attacker
 * (search_constants.c), within MVV_LVA_MIN..MVV_LVA_MAX. */
extern const int mvv_lva[12][12];

/**
 * Late move reductions by [depth][move number] (1-based, the move being searched), in 1/PLY_GRAIN ply:
 * quiet moves PLY_GRAIN * (LMR_QUIET_BIAS + LMR_QUIET_SCALE * ln(depth) * ln(move)) / 1000, captures and
 * promotions with the LMR_CAPTURE_ pair. Filled by init_lmr_table(), at startup (on_startup) and when a
 * tuning run sets one of the four.
 */
extern int16_t lmr_quiet_table[64][64];
extern int16_t lmr_capture_table[64][64];
void init_lmr_table();

#define INF 900000
#define MATE_VALUE 890000
#define MATE_SCORE 880000
#define STOPPED_SEARCH  (-900000)
/* A tablebase win (syzygy.h), less the ply it's found at, so nearer wins score higher: below every
 * mate, above every evaluation. Like mates, these scores are stored in the TT relative to the node
 * (transposition.h) and pruning leaves them alone (is_mate_score). */
#define TB_WIN_SCORE (MATE_SCORE - 1000)
#define TB_WIN_MIN (TB_WIN_SCORE - MAX_PLY)   /* The lowest tablebase win, and so the lowest decisive score. */

#define MAX_MOVES 1024           /* Maximum number of moves in the game.                                     */
#define MAX_PLY 64               /* Maximum ply we can search.                                               */
#define MAX_SEARCH_DEPTH 32      /* Iterative deepening limit when no depth is given (e.g. 'go infinite').   */
#define NMP_MIN_DEPTH 2          /* Null move pruning from this depth, searched at depth - R (at least 0,    */
#define NMP_R_BASE 3072          /* qsearch), R = base + depth * depth_rate + min((eval - beta) / eval_div   */
#define NMP_R_DEPTH_RATE 342     /* whole plies * PLY_GRAIN, eval_max), in 1/PLY_GRAIN ply, rounded down     */
#define NMP_R_EVAL_DIV 200       /* once (342: the depth term is depth / 3 rounded down, below depth 512)    */
#define NMP_R_EVAL_MAX 3072      /* (null_move_depth): 3 plies, a third per ply of depth, a ply per full 200 */
                                 /* cp above beta (max 3).                                                   */
#define NMP_VERIFY_DEPTH 14      /* From this depth a null move cutoff is verified by a normal search.      */
#define LMR_TABLE_SIZE 64
#define PLY_GRAIN 1024           /* Depth adjustments that sum several terms (LMR, null move R) are worked   */
                                 /* out in 1/this ply and rounded once.                                      */
/*
 * Unit A (tiki-analysis/port-spec-history-2026-11-25.md, port-spec-lmr-razoring-2026-11-25.md): the
 * reference engine's quiet history, ordering, late move reductions and move loop pruning. Constants
 * on the history are in history units (quiet_history.h) and are the reference's values; [p] marks
 * 1/PLY_GRAIN ply; eval-unit constants are the reference's x 0.60 (tiki cp per reference unit,
 * measured on raw static evals of 3,000 positions, 25 Nov, net 11-21), the original in brackets.
 */
/* History (quiet_history.h). */
#define HIST_CLEAR_MAIN 802      /* Counters after 'ucinewgame': main...                                     */
#define HIST_CLEAR_CONT 762      /* ...continuation...                                                       */
#define HIST_CLEAR_PAWN 0        /* ...pawn structure.                                                       */
#define HIST_BONUS_OFFSET (-113) /* Quiet update at a fail high (quiet_history_update), main and pawn:       */
#define HIST_BONUS_LINEAR 164    /* bonus min(offset + linear * depth + diff * score_diff / 64, limit)...   */
#define HIST_BONUS_SCORE_DIFF 247 /* (per cp; reference 148 per unit)                                        */
#define HIST_BONUS_LIMIT 2178
#define HIST_MALUS_OFFSET (-51)  /* ...malus -min(...), the same shape...                                    */
#define HIST_MALUS_LINEAR 160
#define HIST_MALUS_SCORE_DIFF 258 /* (155)                                                                   */
#define HIST_MALUS_LIMIT 1844
#define CONT_BONUS_OFFSET (-105) /* ...and the continuation history's own pair.                              */
#define CONT_BONUS_LINEAR 166
#define CONT_BONUS_SCORE_DIFF 270 /* (162)                                                                   */
#define CONT_BONUS_LIMIT 2065
#define CONT_MALUS_OFFSET (-50)
#define CONT_MALUS_LINEAR 162
#define CONT_MALUS_SCORE_DIFF 290 /* (174)                                                                   */
#define CONT_MALUS_LIMIT 2065
#define HIST_SCORE_DIFF_MAX 154  /* score_diff = min(score - beta, this) cp (256).                           */
#define CONT_UPDATE_WEIGHT_2 1014 /* Continuation updates 2, 3, 4 and 6 plies back x this / 1024 (1 back:   */
#define CONT_UPDATE_WEIGHT_3 300  /* x 1; 5 back: not updated).                                              */
#define CONT_UPDATE_WEIGHT_4 978
#define CONT_UPDATE_WEIGHT_6 978
#define PRIOR_CMH_CAP 1200       /* A node that fails low: the quiet move that led to it gains               */
#define PRIOR_CMH_SCALE 120      /* min(cap, scale * depth - bias) in the parent's continuation history.    */
#define PRIOR_CMH_BIAS 100
/* Quiet ordering (quiet_order_score). */
#define PAWN_HIST_WEIGHT 1024    /* Pawn history x this / 1024; continuation 2, 4 and 6 plies back x the    */
#define CONT_ORDER_WEIGHT_2 1019 /* next three / 1024 (1 back: x 1).                                         */
#define CONT_ORDER_WEIGHT_4 555
#define CONT_ORDER_WEIGHT_6 582
#define THREAT_ESCAPE_MINOR 4000 /* A knight or bishop leaving / entering a square the opponent's pawns      */
#define THREAT_ENTER_MINOR 4000  /* attack, a rook one their minors (or pawns) attack, a queen one their      */
#define THREAT_ESCAPE_ROOK 8000  /* rooks (or minors, pawns) attack.                                          */
#define THREAT_ENTER_ROOK 8000
#define THREAT_ESCAPE_QUEEN 12000
#define THREAT_ENTER_QUEEN 12000
#define NODE_CACHE_BONUS 4096    /* Near the root: x the move's share of the position's searched nodes.      */
/* Late move reductions (lmr_reduction), [p]. */
#define LMR_MIN_DEPTH 1          /* From this depth, every move after the first (captures not at PV nodes).  */
#define LMR_QUIET_SCALE 430      /* Tables, in 1/1000 (init_lmr_table).                                      */
#define LMR_QUIET_BIAS 553
#define LMR_CAPTURE_SCALE 420
#define LMR_CAPTURE_BIAS 673
#define LMR_QUIET_NON_PV 240     /* Quiet: more at a non-PV node...                                          */
#define LMR_QUIET_TT_CAPTURE 1168 /* ...more when the TT move is a capture or promotion...                   */
#define LMR_QUIET_REFUTATION 2688 /* ...less for the killer and the counter move...                          */
#define LMR_QUIET_CUT_NODE 2928  /* ...more at an expected cut node...                                       */
#define LMR_QUIET_NOT_IMPROVING 608 /* ...more when not improving...                                         */
#define LMR_QUIET_CHECK 1136     /* ...less when it gives check...                                           */
#define LMR_STAT_OFFSET 6877     /* ...and -(stat + offset) / div (stat: quiet_stat).                        */
#define LMR_STAT_DIV 15
#define LMR_CAPTURE_WINNING 1008 /* Capture (or promotion): less when it wins (attacker below victim)...     */
#define LMR_CAPTURE_BAD (-192)   /* ...plus this when it loses (reference SEE)...                            */
#define LMR_CAPTURE_CUT_NODE 1296 /* ...more at an expected cut node...                                      */
#define LMR_CAPTURE_NOT_IMPROVING (-288) /* ...plus this when not improving...                               */
#define LMR_CAPTURE_CHECK (-64)  /* ...less this when it gives check.                                        */
#define LMR_TT_DEPTH 208         /* Both: less when the TT entry is at least as deep as the node.            */
#define LMR_DEEPER_MARGIN 51     /* A reduced move that beats alpha is re-searched a ply deeper when it      */
                                 /* also beats the best score by more than this (cp; 85)...                  */
#define LMR_SHALLOWER_PER_PLY 6  /* ...a ply shallower when it's below best + new depth * this / 10 cp (1 per */
                                 /* ply), and at full depth only if that's deeper than the reduced search.   */
#define PV_TT_MOVE_MIN_ROOT_DEPTH 8 /* PV: the TT move isn't searched in qsearch past this root depth.       */
#define ALPHA_IMPROVEMENT_MIN_DEPTH 2 /* After alpha is raised, the node's depth less 1 when above this.     */
/* Move loop pruning, before the move is made (negamax). lmr_depth: depth less the quiet table's reduction. */
#define LMP_BASE 4               /* Late move pruning: quiets after base + d^2 (improving) or base + d^2/2,  */
#define LMP_PV_SCALE 2           /* d = depth + this at PV nodes.                                            */
#define HIST_PRUNE_MAX_DEPTH 9   /* History pruning below this depth: stat < -linear * lmr_depth...          */
#define HIST_PRUNE_LINEAR 234
#define HIST_PRUNE_QUAD 148      /* ...- quad * lmr_depth^2.                                                 */
#define FUTILITY_LMR_SQ 192      /* Futility: corrected eval + this / 10 cp * lmr_depth^2 (32 per unit)...   */
#define FUTILITY_STAT_DIV 638    /* ...+ stat / this (383 per unit) < alpha.                                 */
#define SEE_CAPTURE_DEPTH 5      /* SEE pruning, target square attacked: losing captures to this depth,      */
#define SEE_CAPTURE_MULT 120     /* SEE < -mult * depth (reference SEE values)...                            */
#define SEE_QUIET_DEPTH 9        /* ...other moves to this depth, SEE < -mult * lmr_depth - stat / div.     */
#define SEE_QUIET_MULT 49
#define SEE_QUIET_STAT_DIV 134
/* Correction history (search_state.h, corr_history_t; search.c, eval_correction). */
#define CORR_GRAIN 32            /* Entries in 1/this cp...                                                  */
#define CORR_ENTRY_MAX (128 * CORR_GRAIN) /* ...each within +-128 cp (gravity, as the history).               */
#define CORR_PAWN_WEIGHT 100     /* The correction: (pawn entry * this + the two other entries * the next)   */
#define CORR_NONPAWN_WEIGHT 50   /* / 100, in cp...                                                          */
#define CORR_MAX 64              /* ...within +-this (cp).                                                   */
#define CORR_ERROR_MAX 256       /* An update's error (score - corrected eval) within +-this (cp)...         */
#define CORR_DEPTH_MAX 8         /* ...weighted by min(depth, this)...                                       */
#define CORR_RATE 32             /* ...the correction moving by 2 * error * weight / this per update.        */
#define IIR_MIN_DEPTH 4          /* Internal iterative reduction (no TT move) from this depth.               */
#define ASPIRATION_WINDOW 25     /* Initial half-width of the root window around the previous score...       */
#define ASPIRATION_MAX_DELTA 1000 /* ...doubled on each fail, full window once it's wider than this.         */
#define ASPIRATION_MIN_DEPTH 4   /* Shallower depths use the full window, their scores are less stable.      */
#define MAX_CAPTURES_PENALISED 32 /* Captures remembered per node for the capture history malus.             */
#define CAPTURE_HISTORY_MAX 7000 /* Capture history stays within +-this...                                   */
#define CAPTURE_HIST_BONUS_SCALE 16 /* ...updated by min(scale * depth^2, max) (capture_history_bonus).    */
#define CAPTURE_HIST_BONUS_MAX 1600
#define CAPTURE_HISTORY_DIVISOR 32 /* ...and moves a capture's ordering score by history / this (+-218, about */
                                   /* two victims in MVV-LVA, whose victims are 100 apart).                  */
#define DELTA_MARGIN 200         /* Qsearch delta pruning, skip a capture if eval + victim + margin <= alpha. */
#define RFP_MAX_DEPTH 6          /* Reverse futility pruning up to this depth, when static eval - margin     */
#define RFP_MARGIN 50            /* >= beta (rfp_margin): margin * depth, less RFP_IMPROVING when the        */
#define RFP_IMPROVING 87         /* position is improving and the opponent can't win material, less          */
#define RFP_OPP_WORSENING 19     /* RFP_OPP_WORSENING when the opponent's last move cost them (the two       */
#define RFP_OPP_WORSENING_MARGIN 45 /* sides' network evals sum to more than this), less RFP_NO_TT * depth   */
#define RFP_NO_TT 16             /* without a TT entry, at least RFP_MIN_MARGIN (all cp). The pruned node    */
#define RFP_MIN_MARGIN 10        /* returns the static eval moved RFP_BETA_BLEND / 1024 of the way to beta.  */
#define RFP_BETA_BLEND 525       /* Sizes: reference values 83, 145, 32, 75, 26, 16 (reference's internal    */
                                 /* static-eval units) x 0.60 = tiki cp per reference unit, measured on raw  */
                                 /* static evals of 3,000 reference positions (25 Nov, net 11-21; least      */
                                 /* squares 0.60-0.61 by band, medians 0.55-0.63). Re-measure for a new net. */
#define PROBCUT_MIN_DEPTH 5      /* ProbCut from this depth: a capture that still beats beta + margin...     */
#define PROBCUT_MARGIN 200       /* ...(this much less when the position is improving)...                    */
#define PROBCUT_IMPROVING 50
#define PROBCUT_REDUCTION 4      /* ...in a search this much shallower cuts the node off (can_probcut).      */
/* The depth a ProbCut cutoff is worth in the TT: the reduced search plus the move made before it. */
#define PROBCUT_TT_DEPTH(depth) ((depth) - PROBCUT_REDUCTION + 1)
#define SE_MIN_DEPTH 6           /* Singular extensions from this depth, with a TT entry at least...         */
#define SE_TT_DEPTH_MARGIN 3     /* ...this much shallower than the node, if every other move scores below...*/
#define SE_REDUCTION 1           /* A TT move that isn't singular but beat beta is searched this much        */
                                 /* shallower (singular_reduce).                                             */
#define SE_MARGIN 32             /* ...the TT score - margin * depth / SE_MARGIN_GRAIN (cp) in a search of   */
#define SE_MARGIN_GRAIN 16       /* half the depth: SE_MARGIN in 1/16 cp per ply.                            */

/* Move ordering (score_move), highest first. Each category is an offset, the bands mustn't overlap. */
#define TT_MOVE_SCORE 1000000000 /* The TT move...                                                           */
#define PV_MOVE_SCORE 900000000  /* ...the PV move while following the PV...                                 */
#define CAPTURE_SCORE 500000000  /* ...winning or equal captures, plus mvv_lva...                            */
#define QUEEN_PROMOTION_SCORE (CAPTURE_SCORE + 505) /* ...queen promotions without a capture, as a pawn  */
                                                   /* taking a queen (captures that promote are captures). */
#define KILLER_SCORE 100000000   /* ...the killer, the counter move...                                       */
#define COUNTER_SCORE (KILLER_SCORE - 1)
#define QUIET_SCORE_SPAN 1000000 /* ...quiet moves (quiet_order_score, within +-this)...                     */
#define BAD_CAPTURE_SCORE (-20000000) /* ...losing captures (SEE < 0)...                                     */
#define UNDERPROMOTION_KNIGHT (-30000000) /* ...then underpromotions: their quiet score plus these.          */
#define UNDERPROMOTION_OTHER (-40000000)
#define MVV_LVA_MIN 100          /* The range of mvv_lva (search_constants.c).                  */
#define MVV_LVA_MAX 605

#define CAPTURE_HISTORY_SPAN (CAPTURE_HISTORY_MAX / CAPTURE_HISTORY_DIVISOR)
/* The asserts hold for every divisor a tuning run can set (tune.c, at least this). */
#define CAPTURE_HISTORY_DIVISOR_MIN 8
#define CAPTURE_HISTORY_SPAN_MAX (CAPTURE_HISTORY_MAX / CAPTURE_HISTORY_DIVISOR_MIN)
static_assert(CAPTURE_SCORE + MVV_LVA_MAX + CAPTURE_HISTORY_SPAN_MAX < PV_MOVE_SCORE && PV_MOVE_SCORE < TT_MOVE_SCORE,
              "captures under the PV move");
static_assert(KILLER_SCORE < CAPTURE_SCORE + MVV_LVA_MIN - CAPTURE_HISTORY_SPAN_MAX, "killers under captures");
static_assert(KILLER_SCORE < QUEEN_PROMOTION_SCORE && QUEEN_PROMOTION_SCORE < PV_MOVE_SCORE,
              "queen promotions over the killers");
static_assert(QUIET_SCORE_SPAN < COUNTER_SCORE, "quiet moves under the counter move");
static_assert(BAD_CAPTURE_SCORE + MVV_LVA_MAX + CAPTURE_HISTORY_SPAN_MAX < -QUIET_SCORE_SPAN,
              "losing captures under every quiet move");
static_assert(UNDERPROMOTION_KNIGHT + QUIET_SCORE_SPAN < BAD_CAPTURE_SCORE - CAPTURE_HISTORY_SPAN_MAX,
              "underpromotions last");

/* Built with TIKI_TUNE, the tunable constants above become variables set by UCI options (tune.h). */
#include "tune.h"
#endif
