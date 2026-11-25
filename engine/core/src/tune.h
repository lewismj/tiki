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
    RFP_IMPROVING_DEFAULT = RFP_IMPROVING,
    RFP_OPP_WORSENING_DEFAULT = RFP_OPP_WORSENING,
    RFP_OPP_WORSENING_MARGIN_DEFAULT = RFP_OPP_WORSENING_MARGIN,
    RFP_NO_TT_DEFAULT = RFP_NO_TT,
    RFP_MIN_MARGIN_DEFAULT = RFP_MIN_MARGIN,
    RFP_BETA_BLEND_DEFAULT = RFP_BETA_BLEND,
    HIST_CLEAR_MAIN_DEFAULT = HIST_CLEAR_MAIN,
    HIST_CLEAR_CONT_DEFAULT = HIST_CLEAR_CONT,
    HIST_CLEAR_PAWN_DEFAULT = HIST_CLEAR_PAWN,
    HIST_BONUS_OFFSET_DEFAULT = HIST_BONUS_OFFSET,
    HIST_BONUS_LINEAR_DEFAULT = HIST_BONUS_LINEAR,
    HIST_BONUS_SCORE_DIFF_DEFAULT = HIST_BONUS_SCORE_DIFF,
    HIST_BONUS_LIMIT_DEFAULT = HIST_BONUS_LIMIT,
    HIST_MALUS_OFFSET_DEFAULT = HIST_MALUS_OFFSET,
    HIST_MALUS_LINEAR_DEFAULT = HIST_MALUS_LINEAR,
    HIST_MALUS_SCORE_DIFF_DEFAULT = HIST_MALUS_SCORE_DIFF,
    HIST_MALUS_LIMIT_DEFAULT = HIST_MALUS_LIMIT,
    CONT_BONUS_OFFSET_DEFAULT = CONT_BONUS_OFFSET,
    CONT_BONUS_LINEAR_DEFAULT = CONT_BONUS_LINEAR,
    CONT_BONUS_SCORE_DIFF_DEFAULT = CONT_BONUS_SCORE_DIFF,
    CONT_BONUS_LIMIT_DEFAULT = CONT_BONUS_LIMIT,
    CONT_MALUS_OFFSET_DEFAULT = CONT_MALUS_OFFSET,
    CONT_MALUS_LINEAR_DEFAULT = CONT_MALUS_LINEAR,
    CONT_MALUS_SCORE_DIFF_DEFAULT = CONT_MALUS_SCORE_DIFF,
    CONT_MALUS_LIMIT_DEFAULT = CONT_MALUS_LIMIT,
    HIST_SCORE_DIFF_MAX_DEFAULT = HIST_SCORE_DIFF_MAX,
    CONT_UPDATE_WEIGHT_2_DEFAULT = CONT_UPDATE_WEIGHT_2,
    CONT_UPDATE_WEIGHT_3_DEFAULT = CONT_UPDATE_WEIGHT_3,
    CONT_UPDATE_WEIGHT_4_DEFAULT = CONT_UPDATE_WEIGHT_4,
    CONT_UPDATE_WEIGHT_6_DEFAULT = CONT_UPDATE_WEIGHT_6,
    PRIOR_CMH_CAP_DEFAULT = PRIOR_CMH_CAP,
    PRIOR_CMH_SCALE_DEFAULT = PRIOR_CMH_SCALE,
    PRIOR_CMH_BIAS_DEFAULT = PRIOR_CMH_BIAS,
    PAWN_HIST_WEIGHT_DEFAULT = PAWN_HIST_WEIGHT,
    CONT_ORDER_WEIGHT_2_DEFAULT = CONT_ORDER_WEIGHT_2,
    CONT_ORDER_WEIGHT_4_DEFAULT = CONT_ORDER_WEIGHT_4,
    CONT_ORDER_WEIGHT_6_DEFAULT = CONT_ORDER_WEIGHT_6,
    THREAT_ESCAPE_MINOR_DEFAULT = THREAT_ESCAPE_MINOR,
    THREAT_ENTER_MINOR_DEFAULT = THREAT_ENTER_MINOR,
    THREAT_ESCAPE_ROOK_DEFAULT = THREAT_ESCAPE_ROOK,
    THREAT_ENTER_ROOK_DEFAULT = THREAT_ENTER_ROOK,
    THREAT_ESCAPE_QUEEN_DEFAULT = THREAT_ESCAPE_QUEEN,
    THREAT_ENTER_QUEEN_DEFAULT = THREAT_ENTER_QUEEN,
    NODE_CACHE_BONUS_DEFAULT = NODE_CACHE_BONUS,
    LMR_MIN_DEPTH_DEFAULT = LMR_MIN_DEPTH,
    LMR_QUIET_SCALE_DEFAULT = LMR_QUIET_SCALE,
    LMR_QUIET_BIAS_DEFAULT = LMR_QUIET_BIAS,
    LMR_CAPTURE_SCALE_DEFAULT = LMR_CAPTURE_SCALE,
    LMR_CAPTURE_BIAS_DEFAULT = LMR_CAPTURE_BIAS,
    LMR_QUIET_NON_PV_DEFAULT = LMR_QUIET_NON_PV,
    LMR_QUIET_TT_CAPTURE_DEFAULT = LMR_QUIET_TT_CAPTURE,
    LMR_QUIET_REFUTATION_DEFAULT = LMR_QUIET_REFUTATION,
    LMR_QUIET_CUT_NODE_DEFAULT = LMR_QUIET_CUT_NODE,
    LMR_QUIET_NOT_IMPROVING_DEFAULT = LMR_QUIET_NOT_IMPROVING,
    LMR_QUIET_CHECK_DEFAULT = LMR_QUIET_CHECK,
    LMR_STAT_OFFSET_DEFAULT = LMR_STAT_OFFSET,
    LMR_STAT_DIV_DEFAULT = LMR_STAT_DIV,
    LMR_CAPTURE_WINNING_DEFAULT = LMR_CAPTURE_WINNING,
    LMR_CAPTURE_BAD_DEFAULT = LMR_CAPTURE_BAD,
    LMR_CAPTURE_CUT_NODE_DEFAULT = LMR_CAPTURE_CUT_NODE,
    LMR_CAPTURE_NOT_IMPROVING_DEFAULT = LMR_CAPTURE_NOT_IMPROVING,
    LMR_CAPTURE_CHECK_DEFAULT = LMR_CAPTURE_CHECK,
    LMR_TT_DEPTH_DEFAULT = LMR_TT_DEPTH,
    LMR_DEEPER_MARGIN_DEFAULT = LMR_DEEPER_MARGIN,
    PV_TT_MOVE_MIN_ROOT_DEPTH_DEFAULT = PV_TT_MOVE_MIN_ROOT_DEPTH,
    ALPHA_IMPROVEMENT_MIN_DEPTH_DEFAULT = ALPHA_IMPROVEMENT_MIN_DEPTH,
    LMP_BASE_DEFAULT = LMP_BASE,
    LMP_PV_SCALE_DEFAULT = LMP_PV_SCALE,
    HIST_PRUNE_MAX_DEPTH_DEFAULT = HIST_PRUNE_MAX_DEPTH,
    HIST_PRUNE_LINEAR_DEFAULT = HIST_PRUNE_LINEAR,
    HIST_PRUNE_QUAD_DEFAULT = HIST_PRUNE_QUAD,
    FUTILITY_LMR_SQ_DEFAULT = FUTILITY_LMR_SQ,
    FUTILITY_STAT_DIV_DEFAULT = FUTILITY_STAT_DIV,
    SEE_CAPTURE_DEPTH_DEFAULT = SEE_CAPTURE_DEPTH,
    SEE_CAPTURE_MULT_DEFAULT = SEE_CAPTURE_MULT,
    SEE_QUIET_DEPTH_DEFAULT = SEE_QUIET_DEPTH,
    SEE_QUIET_MULT_DEFAULT = SEE_QUIET_MULT,
    SEE_QUIET_STAT_DIV_DEFAULT = SEE_QUIET_STAT_DIV,
    NMP_MIN_DEPTH_DEFAULT = NMP_MIN_DEPTH,
    NMP_R_BASE_DEFAULT = NMP_R_BASE,
    NMP_R_DEPTH_RATE_DEFAULT = NMP_R_DEPTH_RATE,
    NMP_R_EVAL_DIV_DEFAULT = NMP_R_EVAL_DIV,
    NMP_R_EVAL_MAX_DEFAULT = NMP_R_EVAL_MAX,
    NMP_VERIFY_DEPTH_DEFAULT = NMP_VERIFY_DEPTH,
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
    QS_FUTILITY_MARGIN_DEFAULT = QS_FUTILITY_MARGIN,
    QS_MOVE_COUNT_DEFAULT = QS_MOVE_COUNT,
    QS_STAND_PAT_BLEND_DEFAULT = QS_STAND_PAT_BLEND,
    QS_FAIL_HIGH_BLEND_DEFAULT = QS_FAIL_HIGH_BLEND,
    QS_TT_HALFMOVE_LIMIT_DEFAULT = QS_TT_HALFMOVE_LIMIT,
    SE_EXACT_MUL_DEFAULT = SE_EXACT_MUL,
    SE_DEPTH_MUL_DEFAULT = SE_DEPTH_MUL,
    SE_DEPTH_SUB_DEFAULT = SE_DEPTH_SUB,
    SE_DOUBLE_MARGIN_DEFAULT = SE_DOUBLE_MARGIN,
    SE_TRIPLE_MARGIN_DEFAULT = SE_TRIPLE_MARGIN,
    SE_PV_BONUS_DEFAULT = SE_PV_BONUS,
    SE_FAIL_HIGH_NEG_DEFAULT = SE_FAIL_HIGH_NEG,
    SE_CUT_NODE_NEG_DEFAULT = SE_CUT_NODE_NEG,
    SE_TT_ALPHA_NEG_DEFAULT = SE_TT_ALPHA_NEG,
    IIR_MIN_DEPTH_DEFAULT = IIR_MIN_DEPTH,
    ASPIRATION_WINDOW_DEFAULT = ASPIRATION_WINDOW,
    CAPTURE_HISTORY_DIVISOR_DEFAULT = CAPTURE_HISTORY_DIVISOR,
    PROBCUT_MIN_DEPTH_DEFAULT = PROBCUT_MIN_DEPTH,
    PROBCUT_MARGIN_DEFAULT = PROBCUT_MARGIN,
    PROBCUT_IMPROVING_DEFAULT = PROBCUT_IMPROVING
};

extern int tune_rfp_margin;
extern int tune_rfp_max_depth;
extern int tune_rfp_improving;
extern int tune_rfp_opp_worsening;
extern int tune_rfp_opp_worsening_margin;
extern int tune_rfp_no_tt;
extern int tune_rfp_min_margin;
extern int tune_rfp_beta_blend;
extern int tune_hist_clear_main;
extern int tune_hist_clear_cont;
extern int tune_hist_clear_pawn;
extern int tune_hist_bonus_offset;
extern int tune_hist_bonus_linear;
extern int tune_hist_bonus_score_diff;
extern int tune_hist_bonus_limit;
extern int tune_hist_malus_offset;
extern int tune_hist_malus_linear;
extern int tune_hist_malus_score_diff;
extern int tune_hist_malus_limit;
extern int tune_cont_bonus_offset;
extern int tune_cont_bonus_linear;
extern int tune_cont_bonus_score_diff;
extern int tune_cont_bonus_limit;
extern int tune_cont_malus_offset;
extern int tune_cont_malus_linear;
extern int tune_cont_malus_score_diff;
extern int tune_cont_malus_limit;
extern int tune_hist_score_diff_max;
extern int tune_cont_update_weight_2;
extern int tune_cont_update_weight_3;
extern int tune_cont_update_weight_4;
extern int tune_cont_update_weight_6;
extern int tune_prior_cmh_cap;
extern int tune_prior_cmh_scale;
extern int tune_prior_cmh_bias;
extern int tune_pawn_hist_weight;
extern int tune_cont_order_weight_2;
extern int tune_cont_order_weight_4;
extern int tune_cont_order_weight_6;
extern int tune_threat_escape_minor;
extern int tune_threat_enter_minor;
extern int tune_threat_escape_rook;
extern int tune_threat_enter_rook;
extern int tune_threat_escape_queen;
extern int tune_threat_enter_queen;
extern int tune_node_cache_bonus;
extern int tune_lmr_min_depth;
extern int tune_lmr_quiet_scale;
extern int tune_lmr_quiet_bias;
extern int tune_lmr_capture_scale;
extern int tune_lmr_capture_bias;
extern int tune_lmr_quiet_non_pv;
extern int tune_lmr_quiet_tt_capture;
extern int tune_lmr_quiet_refutation;
extern int tune_lmr_quiet_cut_node;
extern int tune_lmr_quiet_not_improving;
extern int tune_lmr_quiet_check;
extern int tune_lmr_stat_offset;
extern int tune_lmr_stat_div;
extern int tune_lmr_capture_winning;
extern int tune_lmr_capture_bad;
extern int tune_lmr_capture_cut_node;
extern int tune_lmr_capture_not_improving;
extern int tune_lmr_capture_check;
extern int tune_lmr_tt_depth;
extern int tune_lmr_deeper_margin;
extern int tune_pv_tt_move_min_root_depth;
extern int tune_alpha_improvement_min_depth;
extern int tune_lmp_base;
extern int tune_lmp_pv_scale;
extern int tune_hist_prune_max_depth;
extern int tune_hist_prune_linear;
extern int tune_hist_prune_quad;
extern int tune_futility_lmr_sq;
extern int tune_futility_stat_div;
extern int tune_see_capture_depth;
extern int tune_see_capture_mult;
extern int tune_see_quiet_depth;
extern int tune_see_quiet_mult;
extern int tune_see_quiet_stat_div;
extern int tune_nmp_min_depth;
extern int tune_nmp_r_base;
extern int tune_nmp_r_depth_rate;
extern int tune_nmp_r_eval_div;
extern int tune_nmp_r_eval_max;
extern int tune_nmp_verify_depth;
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
extern int tune_qs_futility_margin;
extern int tune_qs_move_count;
extern int tune_qs_stand_pat_blend;
extern int tune_qs_fail_high_blend;
extern int tune_qs_tt_halfmove_limit;
extern int tune_se_exact_mul;
extern int tune_se_depth_mul;
extern int tune_se_depth_sub;
extern int tune_se_double_margin;
extern int tune_se_triple_margin;
extern int tune_se_pv_bonus;
extern int tune_se_fail_high_neg;
extern int tune_se_cut_node_neg;
extern int tune_se_tt_alpha_neg;
extern int tune_iir_min_depth;
extern int tune_aspiration_window;
extern int tune_capture_history_divisor;
extern int tune_probcut_min_depth;
extern int tune_probcut_margin;
extern int tune_probcut_improving;

#undef RFP_MARGIN
#undef RFP_MAX_DEPTH
#undef RFP_IMPROVING
#undef RFP_OPP_WORSENING
#undef RFP_OPP_WORSENING_MARGIN
#undef RFP_NO_TT
#undef RFP_MIN_MARGIN
#undef RFP_BETA_BLEND
#undef HIST_CLEAR_MAIN
#undef HIST_CLEAR_CONT
#undef HIST_CLEAR_PAWN
#undef HIST_BONUS_OFFSET
#undef HIST_BONUS_LINEAR
#undef HIST_BONUS_SCORE_DIFF
#undef HIST_BONUS_LIMIT
#undef HIST_MALUS_OFFSET
#undef HIST_MALUS_LINEAR
#undef HIST_MALUS_SCORE_DIFF
#undef HIST_MALUS_LIMIT
#undef CONT_BONUS_OFFSET
#undef CONT_BONUS_LINEAR
#undef CONT_BONUS_SCORE_DIFF
#undef CONT_BONUS_LIMIT
#undef CONT_MALUS_OFFSET
#undef CONT_MALUS_LINEAR
#undef CONT_MALUS_SCORE_DIFF
#undef CONT_MALUS_LIMIT
#undef HIST_SCORE_DIFF_MAX
#undef CONT_UPDATE_WEIGHT_2
#undef CONT_UPDATE_WEIGHT_3
#undef CONT_UPDATE_WEIGHT_4
#undef CONT_UPDATE_WEIGHT_6
#undef PRIOR_CMH_CAP
#undef PRIOR_CMH_SCALE
#undef PRIOR_CMH_BIAS
#undef PAWN_HIST_WEIGHT
#undef CONT_ORDER_WEIGHT_2
#undef CONT_ORDER_WEIGHT_4
#undef CONT_ORDER_WEIGHT_6
#undef THREAT_ESCAPE_MINOR
#undef THREAT_ENTER_MINOR
#undef THREAT_ESCAPE_ROOK
#undef THREAT_ENTER_ROOK
#undef THREAT_ESCAPE_QUEEN
#undef THREAT_ENTER_QUEEN
#undef NODE_CACHE_BONUS
#undef LMR_MIN_DEPTH
#undef LMR_QUIET_SCALE
#undef LMR_QUIET_BIAS
#undef LMR_CAPTURE_SCALE
#undef LMR_CAPTURE_BIAS
#undef LMR_QUIET_NON_PV
#undef LMR_QUIET_TT_CAPTURE
#undef LMR_QUIET_REFUTATION
#undef LMR_QUIET_CUT_NODE
#undef LMR_QUIET_NOT_IMPROVING
#undef LMR_QUIET_CHECK
#undef LMR_STAT_OFFSET
#undef LMR_STAT_DIV
#undef LMR_CAPTURE_WINNING
#undef LMR_CAPTURE_BAD
#undef LMR_CAPTURE_CUT_NODE
#undef LMR_CAPTURE_NOT_IMPROVING
#undef LMR_CAPTURE_CHECK
#undef LMR_TT_DEPTH
#undef LMR_DEEPER_MARGIN
#undef PV_TT_MOVE_MIN_ROOT_DEPTH
#undef ALPHA_IMPROVEMENT_MIN_DEPTH
#undef LMP_BASE
#undef LMP_PV_SCALE
#undef HIST_PRUNE_MAX_DEPTH
#undef HIST_PRUNE_LINEAR
#undef HIST_PRUNE_QUAD
#undef FUTILITY_LMR_SQ
#undef FUTILITY_STAT_DIV
#undef SEE_CAPTURE_DEPTH
#undef SEE_CAPTURE_MULT
#undef SEE_QUIET_DEPTH
#undef SEE_QUIET_MULT
#undef SEE_QUIET_STAT_DIV
#undef NMP_MIN_DEPTH
#undef NMP_R_BASE
#undef NMP_R_DEPTH_RATE
#undef NMP_R_EVAL_DIV
#undef NMP_R_EVAL_MAX
#undef NMP_VERIFY_DEPTH
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
#undef QS_FUTILITY_MARGIN
#undef QS_MOVE_COUNT
#undef QS_STAND_PAT_BLEND
#undef QS_FAIL_HIGH_BLEND
#undef QS_TT_HALFMOVE_LIMIT
#undef SE_EXACT_MUL
#undef SE_DEPTH_MUL
#undef SE_DEPTH_SUB
#undef SE_DOUBLE_MARGIN
#undef SE_TRIPLE_MARGIN
#undef SE_PV_BONUS
#undef SE_FAIL_HIGH_NEG
#undef SE_CUT_NODE_NEG
#undef SE_TT_ALPHA_NEG
#undef IIR_MIN_DEPTH
#undef ASPIRATION_WINDOW
#undef CAPTURE_HISTORY_DIVISOR
#undef PROBCUT_MIN_DEPTH
#undef PROBCUT_MARGIN
#undef PROBCUT_IMPROVING
#define RFP_MARGIN tune_rfp_margin
#define RFP_MAX_DEPTH tune_rfp_max_depth
#define RFP_IMPROVING tune_rfp_improving
#define RFP_OPP_WORSENING tune_rfp_opp_worsening
#define RFP_OPP_WORSENING_MARGIN tune_rfp_opp_worsening_margin
#define RFP_NO_TT tune_rfp_no_tt
#define RFP_MIN_MARGIN tune_rfp_min_margin
#define RFP_BETA_BLEND tune_rfp_beta_blend
#define HIST_CLEAR_MAIN tune_hist_clear_main
#define HIST_CLEAR_CONT tune_hist_clear_cont
#define HIST_CLEAR_PAWN tune_hist_clear_pawn
#define HIST_BONUS_OFFSET tune_hist_bonus_offset
#define HIST_BONUS_LINEAR tune_hist_bonus_linear
#define HIST_BONUS_SCORE_DIFF tune_hist_bonus_score_diff
#define HIST_BONUS_LIMIT tune_hist_bonus_limit
#define HIST_MALUS_OFFSET tune_hist_malus_offset
#define HIST_MALUS_LINEAR tune_hist_malus_linear
#define HIST_MALUS_SCORE_DIFF tune_hist_malus_score_diff
#define HIST_MALUS_LIMIT tune_hist_malus_limit
#define CONT_BONUS_OFFSET tune_cont_bonus_offset
#define CONT_BONUS_LINEAR tune_cont_bonus_linear
#define CONT_BONUS_SCORE_DIFF tune_cont_bonus_score_diff
#define CONT_BONUS_LIMIT tune_cont_bonus_limit
#define CONT_MALUS_OFFSET tune_cont_malus_offset
#define CONT_MALUS_LINEAR tune_cont_malus_linear
#define CONT_MALUS_SCORE_DIFF tune_cont_malus_score_diff
#define CONT_MALUS_LIMIT tune_cont_malus_limit
#define HIST_SCORE_DIFF_MAX tune_hist_score_diff_max
#define CONT_UPDATE_WEIGHT_2 tune_cont_update_weight_2
#define CONT_UPDATE_WEIGHT_3 tune_cont_update_weight_3
#define CONT_UPDATE_WEIGHT_4 tune_cont_update_weight_4
#define CONT_UPDATE_WEIGHT_6 tune_cont_update_weight_6
#define PRIOR_CMH_CAP tune_prior_cmh_cap
#define PRIOR_CMH_SCALE tune_prior_cmh_scale
#define PRIOR_CMH_BIAS tune_prior_cmh_bias
#define PAWN_HIST_WEIGHT tune_pawn_hist_weight
#define CONT_ORDER_WEIGHT_2 tune_cont_order_weight_2
#define CONT_ORDER_WEIGHT_4 tune_cont_order_weight_4
#define CONT_ORDER_WEIGHT_6 tune_cont_order_weight_6
#define THREAT_ESCAPE_MINOR tune_threat_escape_minor
#define THREAT_ENTER_MINOR tune_threat_enter_minor
#define THREAT_ESCAPE_ROOK tune_threat_escape_rook
#define THREAT_ENTER_ROOK tune_threat_enter_rook
#define THREAT_ESCAPE_QUEEN tune_threat_escape_queen
#define THREAT_ENTER_QUEEN tune_threat_enter_queen
#define NODE_CACHE_BONUS tune_node_cache_bonus
#define LMR_MIN_DEPTH tune_lmr_min_depth
#define LMR_QUIET_SCALE tune_lmr_quiet_scale
#define LMR_QUIET_BIAS tune_lmr_quiet_bias
#define LMR_CAPTURE_SCALE tune_lmr_capture_scale
#define LMR_CAPTURE_BIAS tune_lmr_capture_bias
#define LMR_QUIET_NON_PV tune_lmr_quiet_non_pv
#define LMR_QUIET_TT_CAPTURE tune_lmr_quiet_tt_capture
#define LMR_QUIET_REFUTATION tune_lmr_quiet_refutation
#define LMR_QUIET_CUT_NODE tune_lmr_quiet_cut_node
#define LMR_QUIET_NOT_IMPROVING tune_lmr_quiet_not_improving
#define LMR_QUIET_CHECK tune_lmr_quiet_check
#define LMR_STAT_OFFSET tune_lmr_stat_offset
#define LMR_STAT_DIV tune_lmr_stat_div
#define LMR_CAPTURE_WINNING tune_lmr_capture_winning
#define LMR_CAPTURE_BAD tune_lmr_capture_bad
#define LMR_CAPTURE_CUT_NODE tune_lmr_capture_cut_node
#define LMR_CAPTURE_NOT_IMPROVING tune_lmr_capture_not_improving
#define LMR_CAPTURE_CHECK tune_lmr_capture_check
#define LMR_TT_DEPTH tune_lmr_tt_depth
#define LMR_DEEPER_MARGIN tune_lmr_deeper_margin
#define PV_TT_MOVE_MIN_ROOT_DEPTH tune_pv_tt_move_min_root_depth
#define ALPHA_IMPROVEMENT_MIN_DEPTH tune_alpha_improvement_min_depth
#define LMP_BASE tune_lmp_base
#define LMP_PV_SCALE tune_lmp_pv_scale
#define HIST_PRUNE_MAX_DEPTH tune_hist_prune_max_depth
#define HIST_PRUNE_LINEAR tune_hist_prune_linear
#define HIST_PRUNE_QUAD tune_hist_prune_quad
#define FUTILITY_LMR_SQ tune_futility_lmr_sq
#define FUTILITY_STAT_DIV tune_futility_stat_div
#define SEE_CAPTURE_DEPTH tune_see_capture_depth
#define SEE_CAPTURE_MULT tune_see_capture_mult
#define SEE_QUIET_DEPTH tune_see_quiet_depth
#define SEE_QUIET_MULT tune_see_quiet_mult
#define SEE_QUIET_STAT_DIV tune_see_quiet_stat_div
#define NMP_MIN_DEPTH tune_nmp_min_depth
#define NMP_R_BASE tune_nmp_r_base
#define NMP_R_DEPTH_RATE tune_nmp_r_depth_rate
#define NMP_R_EVAL_DIV tune_nmp_r_eval_div
#define NMP_R_EVAL_MAX tune_nmp_r_eval_max
#define NMP_VERIFY_DEPTH tune_nmp_verify_depth
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
#define QS_FUTILITY_MARGIN tune_qs_futility_margin
#define QS_MOVE_COUNT tune_qs_move_count
#define QS_STAND_PAT_BLEND tune_qs_stand_pat_blend
#define QS_FAIL_HIGH_BLEND tune_qs_fail_high_blend
#define QS_TT_HALFMOVE_LIMIT tune_qs_tt_halfmove_limit
#define SE_EXACT_MUL tune_se_exact_mul
#define SE_DEPTH_MUL tune_se_depth_mul
#define SE_DEPTH_SUB tune_se_depth_sub
#define SE_DOUBLE_MARGIN tune_se_double_margin
#define SE_TRIPLE_MARGIN tune_se_triple_margin
#define SE_PV_BONUS tune_se_pv_bonus
#define SE_FAIL_HIGH_NEG tune_se_fail_high_neg
#define SE_CUT_NODE_NEG tune_se_cut_node_neg
#define SE_TT_ALPHA_NEG tune_se_tt_alpha_neg
#define IIR_MIN_DEPTH tune_iir_min_depth
#define ASPIRATION_WINDOW tune_aspiration_window
#define CAPTURE_HISTORY_DIVISOR tune_capture_history_divisor
#define PROBCUT_MIN_DEPTH tune_probcut_min_depth
#define PROBCUT_MARGIN tune_probcut_margin
#define PROBCUT_IMPROVING tune_probcut_improving
#endif

#endif
