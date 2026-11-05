#ifndef TIKI_TUNE_H
#define TIKI_TUNE_H

#include <stdbool.h>
#include <stddef.h>

#include "search_constants.h"
#include "limits.h"

/**
 * Search parameters for SPSA tuning (OpenBench). Normally they're the constants in
 * search_constants.h. Built with TIKI_TUNE they're variables instead, each with a UCI option of
 * the constant's name, so a tuning run sets them per game without rebuilding. Their defaults are
 * the constants' values either way.
 */
typedef struct {
    const char* name;   /* UCI option name, the constant's name. */
    int* value;
    int min;
    int max;
    double c_end;       /* SPSA's final step size and learning rate, for OpenBench's input. */
    double r_end;
} tunable_t;

/* The tunable parameters, none unless built with TIKI_TUNE. */
extern const tunable_t tunables[];
extern const int tunable_count;

/* 'uci': an 'option name ... type spin' line per parameter. */
void print_tunable_options(void);

/* 'setoption': sets the parameter called 'name' ('length' characters) to 'value', clamped to its
 * range. False if there's no such parameter. */
bool set_tunable(const char* name, size_t length, int value);

/* 'tiki tune': OpenBench's SPSA input, a line per parameter (name, int, value, min, max, c_end, r_end). */
void print_spsa_input(void);

#ifdef TIKI_TUNE
/* The constants become variables, their values kept as the defaults. */
enum {
    RFP_MARGIN_DEFAULT = RFP_MARGIN,
    RFP_MAX_DEPTH_DEFAULT = RFP_MAX_DEPTH,
    FUTILITY_MARGIN_DEFAULT = FUTILITY_MARGIN,
    FUTILITY_MAX_DEPTH_DEFAULT = FUTILITY_MAX_DEPTH,
    LMP_BASE_DEFAULT = LMP_BASE,
    SEE_QUIET_MARGIN_DEFAULT = SEE_QUIET_MARGIN,
    SEE_CAPTURE_MARGIN_DEFAULT = SEE_CAPTURE_MARGIN,
    SE_MARGIN_DEFAULT = SE_MARGIN,
    RFP_IMPROVING_DEFAULT = RFP_IMPROVING,
    FUTILITY_BASE_DEFAULT = FUTILITY_BASE,
    LMP_NOT_IMPROVING_PCT_DEFAULT = LMP_NOT_IMPROVING_PCT,
    NMP_MIN_DEPTH_DEFAULT = NMP_MIN_DEPTH,
    NMP_R_BASE_DEFAULT = NMP_R_BASE,
    NMP_R_DEPTH_DIV_DEFAULT = NMP_R_DEPTH_DIV,
    NMP_R_EVAL_DIV_DEFAULT = NMP_R_EVAL_DIV,
    NMP_R_EVAL_MAX_DEFAULT = NMP_R_EVAL_MAX,
    NMP_VERIFY_DEPTH_DEFAULT = NMP_VERIFY_DEPTH,
    LMR_HISTORY_DIVISOR_DEFAULT = LMR_HISTORY_DIVISOR,
    HIST_PRUNE_MAX_DEPTH_DEFAULT = HIST_PRUNE_MAX_DEPTH,
    HIST_PRUNE_MARGIN_DEFAULT = HIST_PRUNE_MARGIN,
    LMR_CUT_NODE_DEFAULT = LMR_CUT_NODE,
    LMR_NOT_IMPROVING_DEFAULT = LMR_NOT_IMPROVING,
    TM_MOVES_LEFT_DEFAULT = TM_MOVES_LEFT,
    TM_SOFT_PERCENT_DEFAULT = TM_SOFT_PERCENT,
    TM_HARD_PERCENT_DEFAULT = TM_HARD_PERCENT,
    TM_UNSTABLE_PERCENT_DEFAULT = TM_UNSTABLE_PERCENT,
    TM_NODES_PERCENT_DEFAULT = TM_NODES_PERCENT,
    CORR_PAWN_WEIGHT_DEFAULT = CORR_PAWN_WEIGHT,
    CORR_NONPAWN_WEIGHT_DEFAULT = CORR_NONPAWN_WEIGHT,
    CORR_MAX_DEFAULT = CORR_MAX,
    CORR_RATE_DEFAULT = CORR_RATE,
    SE_MIN_DEPTH_DEFAULT = SE_MIN_DEPTH,
    SE_TT_DEPTH_MARGIN_DEFAULT = SE_TT_DEPTH_MARGIN,
    SE_REDUCTION_DEFAULT = SE_REDUCTION,
    LMR_KILLER_MAX_DEFAULT = LMR_KILLER_MAX,
    LMR_BASE_DEFAULT = LMR_BASE,
    LMR_DIVISOR_DEFAULT = LMR_DIVISOR,
    LMR_MIN_DEPTH_DEFAULT = LMR_MIN_DEPTH,
    LMR_MIN_MOVES_DEFAULT = LMR_MIN_MOVES,
    HIST_BONUS_SCALE_DEFAULT = HIST_BONUS_SCALE,
    HIST_BONUS_MAX_DEFAULT = HIST_BONUS_MAX,
    LMP_MAX_DEPTH_DEFAULT = LMP_MAX_DEPTH,
    SEE_PRUNE_MAX_DEPTH_DEFAULT = SEE_PRUNE_MAX_DEPTH,
    IIR_MIN_DEPTH_DEFAULT = IIR_MIN_DEPTH,
    ASPIRATION_WINDOW_DEFAULT = ASPIRATION_WINDOW,
    DELTA_MARGIN_DEFAULT = DELTA_MARGIN,
    CAPTURE_HISTORY_DIVISOR_DEFAULT = CAPTURE_HISTORY_DIVISOR
};

extern int tune_rfp_margin;
extern int tune_rfp_max_depth;
extern int tune_futility_margin;
extern int tune_futility_max_depth;
extern int tune_lmp_base;
extern int tune_see_quiet_margin;
extern int tune_see_capture_margin;
extern int tune_se_margin;
extern int tune_rfp_improving;
extern int tune_futility_base;
extern int tune_lmp_not_improving_pct;
extern int tune_nmp_min_depth;
extern int tune_nmp_r_base;
extern int tune_nmp_r_depth_div;
extern int tune_nmp_r_eval_div;
extern int tune_nmp_r_eval_max;
extern int tune_nmp_verify_depth;
extern int tune_lmr_history_divisor;
extern int tune_hist_prune_max_depth;
extern int tune_hist_prune_margin;
extern int tune_lmr_cut_node;
extern int tune_lmr_not_improving;
extern int tune_tm_moves_left;
extern int tune_tm_soft_percent;
extern int tune_tm_hard_percent;
extern int tune_tm_unstable_percent;
extern int tune_tm_nodes_percent;
extern int tune_corr_pawn_weight;
extern int tune_corr_nonpawn_weight;
extern int tune_corr_max;
extern int tune_corr_rate;
extern int tune_se_min_depth;
extern int tune_se_tt_depth_margin;
extern int tune_se_reduction;
extern int tune_lmr_killer_max;
extern int tune_lmr_base;
extern int tune_lmr_divisor;
extern int tune_lmr_min_depth;
extern int tune_lmr_min_moves;
extern int tune_hist_bonus_scale;
extern int tune_hist_bonus_max;
extern int tune_lmp_max_depth;
extern int tune_see_prune_max_depth;
extern int tune_iir_min_depth;
extern int tune_aspiration_window;
extern int tune_delta_margin;
extern int tune_capture_history_divisor;

#undef RFP_MARGIN
#undef RFP_MAX_DEPTH
#undef FUTILITY_MARGIN
#undef FUTILITY_MAX_DEPTH
#undef LMP_BASE
#undef SEE_QUIET_MARGIN
#undef SEE_CAPTURE_MARGIN
#undef SE_MARGIN
#undef RFP_IMPROVING
#undef FUTILITY_BASE
#undef LMP_NOT_IMPROVING_PCT
#undef NMP_MIN_DEPTH
#undef NMP_R_BASE
#undef NMP_R_DEPTH_DIV
#undef NMP_R_EVAL_DIV
#undef NMP_R_EVAL_MAX
#undef NMP_VERIFY_DEPTH
#undef LMR_HISTORY_DIVISOR
#undef HIST_PRUNE_MAX_DEPTH
#undef HIST_PRUNE_MARGIN
#undef LMR_CUT_NODE
#undef LMR_NOT_IMPROVING
#undef TM_MOVES_LEFT
#undef TM_SOFT_PERCENT
#undef TM_HARD_PERCENT
#undef TM_UNSTABLE_PERCENT
#undef TM_NODES_PERCENT
#undef CORR_PAWN_WEIGHT
#undef CORR_NONPAWN_WEIGHT
#undef CORR_MAX
#undef CORR_RATE
#undef SE_MIN_DEPTH
#undef SE_TT_DEPTH_MARGIN
#undef SE_REDUCTION
#undef LMR_KILLER_MAX
#undef LMR_BASE
#undef LMR_DIVISOR
#undef LMR_MIN_DEPTH
#undef LMR_MIN_MOVES
#undef HIST_BONUS_SCALE
#undef HIST_BONUS_MAX
#undef LMP_MAX_DEPTH
#undef SEE_PRUNE_MAX_DEPTH
#undef IIR_MIN_DEPTH
#undef ASPIRATION_WINDOW
#undef DELTA_MARGIN
#undef CAPTURE_HISTORY_DIVISOR
#define RFP_MARGIN tune_rfp_margin
#define RFP_MAX_DEPTH tune_rfp_max_depth
#define FUTILITY_MARGIN tune_futility_margin
#define FUTILITY_MAX_DEPTH tune_futility_max_depth
#define LMP_BASE tune_lmp_base
#define SEE_QUIET_MARGIN tune_see_quiet_margin
#define SEE_CAPTURE_MARGIN tune_see_capture_margin
#define SE_MARGIN tune_se_margin
#define RFP_IMPROVING tune_rfp_improving
#define FUTILITY_BASE tune_futility_base
#define LMP_NOT_IMPROVING_PCT tune_lmp_not_improving_pct
#define NMP_MIN_DEPTH tune_nmp_min_depth
#define NMP_R_BASE tune_nmp_r_base
#define NMP_R_DEPTH_DIV tune_nmp_r_depth_div
#define NMP_R_EVAL_DIV tune_nmp_r_eval_div
#define NMP_R_EVAL_MAX tune_nmp_r_eval_max
#define NMP_VERIFY_DEPTH tune_nmp_verify_depth
#define LMR_HISTORY_DIVISOR tune_lmr_history_divisor
#define HIST_PRUNE_MAX_DEPTH tune_hist_prune_max_depth
#define HIST_PRUNE_MARGIN tune_hist_prune_margin
#define LMR_CUT_NODE tune_lmr_cut_node
#define LMR_NOT_IMPROVING tune_lmr_not_improving
#define TM_MOVES_LEFT tune_tm_moves_left
#define TM_SOFT_PERCENT tune_tm_soft_percent
#define TM_HARD_PERCENT tune_tm_hard_percent
#define TM_UNSTABLE_PERCENT tune_tm_unstable_percent
#define TM_NODES_PERCENT tune_tm_nodes_percent
#define CORR_PAWN_WEIGHT tune_corr_pawn_weight
#define CORR_NONPAWN_WEIGHT tune_corr_nonpawn_weight
#define CORR_MAX tune_corr_max
#define CORR_RATE tune_corr_rate
#define SE_MIN_DEPTH tune_se_min_depth
#define SE_TT_DEPTH_MARGIN tune_se_tt_depth_margin
#define SE_REDUCTION tune_se_reduction
#define LMR_KILLER_MAX tune_lmr_killer_max
#define LMR_BASE tune_lmr_base
#define LMR_DIVISOR tune_lmr_divisor
#define LMR_MIN_DEPTH tune_lmr_min_depth
#define LMR_MIN_MOVES tune_lmr_min_moves
#define HIST_BONUS_SCALE tune_hist_bonus_scale
#define HIST_BONUS_MAX tune_hist_bonus_max
#define LMP_MAX_DEPTH tune_lmp_max_depth
#define SEE_PRUNE_MAX_DEPTH tune_see_prune_max_depth
#define IIR_MIN_DEPTH tune_iir_min_depth
#define ASPIRATION_WINDOW tune_aspiration_window
#define DELTA_MARGIN tune_delta_margin
#define CAPTURE_HISTORY_DIVISOR tune_capture_history_divisor
#endif

#endif
