#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "tune.h"

#ifdef TIKI_TUNE
int tune_rfp_margin = RFP_MARGIN_DEFAULT;
int tune_rfp_max_depth = RFP_MAX_DEPTH_DEFAULT;
int tune_rfp_improving = RFP_IMPROVING_DEFAULT;
int tune_rfp_opp_worsening = RFP_OPP_WORSENING_DEFAULT;
int tune_rfp_opp_worsening_margin = RFP_OPP_WORSENING_MARGIN_DEFAULT;
int tune_rfp_no_tt = RFP_NO_TT_DEFAULT;
int tune_rfp_min_margin = RFP_MIN_MARGIN_DEFAULT;
int tune_rfp_beta_blend = RFP_BETA_BLEND_DEFAULT;
int tune_hist_clear_main = HIST_CLEAR_MAIN_DEFAULT;
int tune_hist_clear_cont = HIST_CLEAR_CONT_DEFAULT;
int tune_hist_clear_pawn = HIST_CLEAR_PAWN_DEFAULT;
int tune_hist_bonus_offset = HIST_BONUS_OFFSET_DEFAULT;
int tune_hist_bonus_linear = HIST_BONUS_LINEAR_DEFAULT;
int tune_hist_bonus_score_diff = HIST_BONUS_SCORE_DIFF_DEFAULT;
int tune_hist_bonus_limit = HIST_BONUS_LIMIT_DEFAULT;
int tune_hist_malus_offset = HIST_MALUS_OFFSET_DEFAULT;
int tune_hist_malus_linear = HIST_MALUS_LINEAR_DEFAULT;
int tune_hist_malus_score_diff = HIST_MALUS_SCORE_DIFF_DEFAULT;
int tune_hist_malus_limit = HIST_MALUS_LIMIT_DEFAULT;
int tune_cont_bonus_offset = CONT_BONUS_OFFSET_DEFAULT;
int tune_cont_bonus_linear = CONT_BONUS_LINEAR_DEFAULT;
int tune_cont_bonus_score_diff = CONT_BONUS_SCORE_DIFF_DEFAULT;
int tune_cont_bonus_limit = CONT_BONUS_LIMIT_DEFAULT;
int tune_cont_malus_offset = CONT_MALUS_OFFSET_DEFAULT;
int tune_cont_malus_linear = CONT_MALUS_LINEAR_DEFAULT;
int tune_cont_malus_score_diff = CONT_MALUS_SCORE_DIFF_DEFAULT;
int tune_cont_malus_limit = CONT_MALUS_LIMIT_DEFAULT;
int tune_hist_score_diff_max = HIST_SCORE_DIFF_MAX_DEFAULT;
int tune_cont_update_weight_2 = CONT_UPDATE_WEIGHT_2_DEFAULT;
int tune_cont_update_weight_3 = CONT_UPDATE_WEIGHT_3_DEFAULT;
int tune_cont_update_weight_4 = CONT_UPDATE_WEIGHT_4_DEFAULT;
int tune_cont_update_weight_6 = CONT_UPDATE_WEIGHT_6_DEFAULT;
int tune_prior_cmh_cap = PRIOR_CMH_CAP_DEFAULT;
int tune_prior_cmh_scale = PRIOR_CMH_SCALE_DEFAULT;
int tune_prior_cmh_bias = PRIOR_CMH_BIAS_DEFAULT;
int tune_pawn_hist_weight = PAWN_HIST_WEIGHT_DEFAULT;
int tune_cont_order_weight_2 = CONT_ORDER_WEIGHT_2_DEFAULT;
int tune_cont_order_weight_4 = CONT_ORDER_WEIGHT_4_DEFAULT;
int tune_cont_order_weight_6 = CONT_ORDER_WEIGHT_6_DEFAULT;
int tune_threat_escape_minor = THREAT_ESCAPE_MINOR_DEFAULT;
int tune_threat_enter_minor = THREAT_ENTER_MINOR_DEFAULT;
int tune_threat_escape_rook = THREAT_ESCAPE_ROOK_DEFAULT;
int tune_threat_enter_rook = THREAT_ENTER_ROOK_DEFAULT;
int tune_threat_escape_queen = THREAT_ESCAPE_QUEEN_DEFAULT;
int tune_threat_enter_queen = THREAT_ENTER_QUEEN_DEFAULT;
int tune_node_cache_bonus = NODE_CACHE_BONUS_DEFAULT;
int tune_lmr_min_depth = LMR_MIN_DEPTH_DEFAULT;
int tune_lmr_quiet_scale = LMR_QUIET_SCALE_DEFAULT;
int tune_lmr_quiet_bias = LMR_QUIET_BIAS_DEFAULT;
int tune_lmr_capture_scale = LMR_CAPTURE_SCALE_DEFAULT;
int tune_lmr_capture_bias = LMR_CAPTURE_BIAS_DEFAULT;
int tune_lmr_quiet_non_pv = LMR_QUIET_NON_PV_DEFAULT;
int tune_lmr_quiet_tt_capture = LMR_QUIET_TT_CAPTURE_DEFAULT;
int tune_lmr_quiet_refutation = LMR_QUIET_REFUTATION_DEFAULT;
int tune_lmr_quiet_cut_node = LMR_QUIET_CUT_NODE_DEFAULT;
int tune_lmr_quiet_not_improving = LMR_QUIET_NOT_IMPROVING_DEFAULT;
int tune_lmr_quiet_check = LMR_QUIET_CHECK_DEFAULT;
int tune_lmr_stat_offset = LMR_STAT_OFFSET_DEFAULT;
int tune_lmr_stat_div = LMR_STAT_DIV_DEFAULT;
int tune_lmr_capture_winning = LMR_CAPTURE_WINNING_DEFAULT;
int tune_lmr_capture_bad = LMR_CAPTURE_BAD_DEFAULT;
int tune_lmr_capture_cut_node = LMR_CAPTURE_CUT_NODE_DEFAULT;
int tune_lmr_capture_not_improving = LMR_CAPTURE_NOT_IMPROVING_DEFAULT;
int tune_lmr_capture_check = LMR_CAPTURE_CHECK_DEFAULT;
int tune_lmr_tt_depth = LMR_TT_DEPTH_DEFAULT;
int tune_lmr_deeper_margin = LMR_DEEPER_MARGIN_DEFAULT;
int tune_pv_tt_move_min_root_depth = PV_TT_MOVE_MIN_ROOT_DEPTH_DEFAULT;
int tune_alpha_improvement_min_depth = ALPHA_IMPROVEMENT_MIN_DEPTH_DEFAULT;
int tune_lmp_base = LMP_BASE_DEFAULT;
int tune_lmp_pv_scale = LMP_PV_SCALE_DEFAULT;
int tune_hist_prune_max_depth = HIST_PRUNE_MAX_DEPTH_DEFAULT;
int tune_hist_prune_linear = HIST_PRUNE_LINEAR_DEFAULT;
int tune_hist_prune_quad = HIST_PRUNE_QUAD_DEFAULT;
int tune_futility_lmr_sq = FUTILITY_LMR_SQ_DEFAULT;
int tune_futility_stat_div = FUTILITY_STAT_DIV_DEFAULT;
int tune_see_capture_depth = SEE_CAPTURE_DEPTH_DEFAULT;
int tune_see_capture_mult = SEE_CAPTURE_MULT_DEFAULT;
int tune_see_quiet_depth = SEE_QUIET_DEPTH_DEFAULT;
int tune_see_quiet_mult = SEE_QUIET_MULT_DEFAULT;
int tune_see_quiet_stat_div = SEE_QUIET_STAT_DIV_DEFAULT;
int tune_nmp_min_depth = NMP_MIN_DEPTH_DEFAULT;
int tune_nmp_r_base = NMP_R_BASE_DEFAULT;
int tune_nmp_r_depth_rate = NMP_R_DEPTH_RATE_DEFAULT;
int tune_nmp_r_eval_div = NMP_R_EVAL_DIV_DEFAULT;
int tune_nmp_r_eval_max = NMP_R_EVAL_MAX_DEFAULT;
int tune_nmp_verify_depth = NMP_VERIFY_DEPTH_DEFAULT;
int tune_tm_moves_left = TM_MOVES_LEFT_DEFAULT;
int tune_tm_soft_percent = TM_SOFT_PERCENT_DEFAULT;
int tune_tm_hard_percent = TM_HARD_PERCENT_DEFAULT;
int tune_tm_unstable_percent = TM_UNSTABLE_PERCENT_DEFAULT;
int tune_tm_nodes_percent = TM_NODES_PERCENT_DEFAULT;
int tune_corr_pawn_weight = CORR_PAWN_WEIGHT_DEFAULT;
int tune_corr_nonpawn_weight = CORR_NONPAWN_WEIGHT_DEFAULT;
int tune_corr_max = CORR_MAX_DEFAULT;
int tune_corr_rate = CORR_RATE_DEFAULT;
int tune_se_min_depth = SE_MIN_DEPTH_DEFAULT;
int tune_se_tt_depth_margin = SE_TT_DEPTH_MARGIN_DEFAULT;
int tune_qs_futility_margin = QS_FUTILITY_MARGIN_DEFAULT;
int tune_qs_move_count = QS_MOVE_COUNT_DEFAULT;
int tune_qs_stand_pat_blend = QS_STAND_PAT_BLEND_DEFAULT;
int tune_qs_fail_high_blend = QS_FAIL_HIGH_BLEND_DEFAULT;
int tune_qs_tt_halfmove_limit = QS_TT_HALFMOVE_LIMIT_DEFAULT;
int tune_se_exact_mul = SE_EXACT_MUL_DEFAULT;
int tune_se_depth_mul = SE_DEPTH_MUL_DEFAULT;
int tune_se_depth_sub = SE_DEPTH_SUB_DEFAULT;
int tune_se_double_margin = SE_DOUBLE_MARGIN_DEFAULT;
int tune_se_triple_margin = SE_TRIPLE_MARGIN_DEFAULT;
int tune_se_pv_bonus = SE_PV_BONUS_DEFAULT;
int tune_se_fail_high_neg = SE_FAIL_HIGH_NEG_DEFAULT;
int tune_se_cut_node_neg = SE_CUT_NODE_NEG_DEFAULT;
int tune_se_tt_alpha_neg = SE_TT_ALPHA_NEG_DEFAULT;
int tune_iir_min_depth = IIR_MIN_DEPTH_DEFAULT;
int tune_aspiration_window = ASPIRATION_WINDOW_DEFAULT;
int tune_capture_history_divisor = CAPTURE_HISTORY_DIVISOR_DEFAULT;
int tune_probcut_min_depth = PROBCUT_MIN_DEPTH_DEFAULT;
int tune_probcut_margin = PROBCUT_MARGIN_DEFAULT;
int tune_probcut_improving = PROBCUT_IMPROVING_DEFAULT;

/* name, variable, min, max, c_end, r_end. c_end is about a twentieth of the range, at least 0.5
 * (OpenBench's minimum for integers); r_end is OpenBench's usual 0.002. */
const tunable_t tunables[] = {
    { "RFP_MARGIN",         &tune_rfp_margin,         20, 150,  6.0, 0.002 },
    { "RFP_MAX_DEPTH",      &tune_rfp_max_depth,       3,  10,  0.5, 0.002 },
    { "RFP_IMPROVING",      &tune_rfp_improving,       0, 250, 12.0, 0.002 },
    { "RFP_OPP_WORSENING",  &tune_rfp_opp_worsening,   0,  80,  4.0, 0.002 },
    { "RFP_OPP_WORSENING_MARGIN", &tune_rfp_opp_worsening_margin, 0, 200, 10.0, 0.002 },
    { "RFP_NO_TT",          &tune_rfp_no_tt,           0,  60,  3.0, 0.002 },
    { "RFP_MIN_MARGIN",     &tune_rfp_min_margin,      0,  40,  2.0, 0.002 },
    { "RFP_BETA_BLEND",     &tune_rfp_beta_blend,      0, 1024, 50.0, 0.002 },
    { "HIST_CLEAR_MAIN", &tune_hist_clear_main, -2000, 2000, 200.0, 0.002 },
    { "HIST_CLEAR_CONT", &tune_hist_clear_cont, -2000, 2000, 200.0, 0.002 },
    { "HIST_CLEAR_PAWN", &tune_hist_clear_pawn, -2000, 2000, 200.0, 0.002 },
    { "HIST_BONUS_OFFSET", &tune_hist_bonus_offset, -200, 0, 10.0, 0.002 },
    { "HIST_BONUS_LINEAR", &tune_hist_bonus_linear, 100, 250, 7.5, 0.002 },
    { "HIST_BONUS_SCORE_DIFF", &tune_hist_bonus_score_diff, 0, 667, 33.4, 0.002 },
    { "HIST_BONUS_LIMIT", &tune_hist_bonus_limit, 1000, 4000, 150.0, 0.002 },
    { "HIST_MALUS_OFFSET", &tune_hist_malus_offset, -200, 50, 12.5, 0.002 },
    { "HIST_MALUS_LINEAR", &tune_hist_malus_linear, 75, 200, 6.2, 0.002 },
    { "HIST_MALUS_SCORE_DIFF", &tune_hist_malus_score_diff, 0, 667, 33.4, 0.002 },
    { "HIST_MALUS_LIMIT", &tune_hist_malus_limit, 1000, 4000, 150.0, 0.002 },
    { "CONT_BONUS_OFFSET", &tune_cont_bonus_offset, -200, 0, 10.0, 0.002 },
    { "CONT_BONUS_LINEAR", &tune_cont_bonus_linear, 100, 250, 7.5, 0.002 },
    { "CONT_BONUS_SCORE_DIFF", &tune_cont_bonus_score_diff, 0, 667, 33.4, 0.002 },
    { "CONT_BONUS_LIMIT", &tune_cont_bonus_limit, 1000, 4000, 150.0, 0.002 },
    { "CONT_MALUS_OFFSET", &tune_cont_malus_offset, -200, 50, 12.5, 0.002 },
    { "CONT_MALUS_LINEAR", &tune_cont_malus_linear, 75, 200, 6.2, 0.002 },
    { "CONT_MALUS_SCORE_DIFF", &tune_cont_malus_score_diff, 0, 667, 33.4, 0.002 },
    { "CONT_MALUS_LIMIT", &tune_cont_malus_limit, 1000, 4000, 150.0, 0.002 },
    { "HIST_SCORE_DIFF_MAX", &tune_hist_score_diff_max, 38, 307, 13.4, 0.002 },
    { "CONT_UPDATE_WEIGHT_2", &tune_cont_update_weight_2, 1, 2048, 102.3, 0.002 },
    { "CONT_UPDATE_WEIGHT_3", &tune_cont_update_weight_3, 1, 2048, 102.3, 0.002 },
    { "CONT_UPDATE_WEIGHT_4", &tune_cont_update_weight_4, 1, 2048, 102.3, 0.002 },
    { "CONT_UPDATE_WEIGHT_6", &tune_cont_update_weight_6, 1, 2048, 102.3, 0.002 },
    { "PRIOR_CMH_CAP", &tune_prior_cmh_cap, 400, 2400, 100.0, 0.002 },
    { "PRIOR_CMH_SCALE", &tune_prior_cmh_scale, 20, 240, 11.0, 0.002 },
    { "PRIOR_CMH_BIAS", &tune_prior_cmh_bias, 0, 200, 10.0, 0.002 },
    { "PAWN_HIST_WEIGHT", &tune_pawn_hist_weight, 0, 2048, 102.4, 0.002 },
    { "CONT_ORDER_WEIGHT_2", &tune_cont_order_weight_2, 1, 2048, 102.3, 0.002 },
    { "CONT_ORDER_WEIGHT_4", &tune_cont_order_weight_4, 1, 1024, 51.1, 0.002 },
    { "CONT_ORDER_WEIGHT_6", &tune_cont_order_weight_6, 1, 1024, 51.1, 0.002 },
    { "THREAT_ESCAPE_MINOR", &tune_threat_escape_minor, 2000, 12000, 500.0, 0.002 },
    { "THREAT_ENTER_MINOR", &tune_threat_enter_minor, 2000, 12000, 500.0, 0.002 },
    { "THREAT_ESCAPE_ROOK", &tune_threat_escape_rook, 3000, 16000, 650.0, 0.002 },
    { "THREAT_ENTER_ROOK", &tune_threat_enter_rook, 3000, 16000, 650.0, 0.002 },
    { "THREAT_ESCAPE_QUEEN", &tune_threat_escape_queen, 4000, 20000, 800.0, 0.002 },
    { "THREAT_ENTER_QUEEN", &tune_threat_enter_queen, 4000, 20000, 800.0, 0.002 },
    { "NODE_CACHE_BONUS", &tune_node_cache_bonus, 1000, 8000, 350.0, 0.002 },
    { "LMR_MIN_DEPTH", &tune_lmr_min_depth, 1, 3, 0.5, 0.002 },
    { "LMR_QUIET_SCALE", &tune_lmr_quiet_scale, 200, 700, 25.0, 0.002 },
    { "LMR_QUIET_BIAS", &tune_lmr_quiet_bias, 200, 800, 30.0, 0.002 },
    { "LMR_CAPTURE_SCALE", &tune_lmr_capture_scale, 200, 700, 25.0, 0.002 },
    { "LMR_CAPTURE_BIAS", &tune_lmr_capture_bias, 200, 900, 35.0, 0.002 },
    { "LMR_QUIET_NON_PV", &tune_lmr_quiet_non_pv, -2048, 4096, 307.2, 0.002 },
    { "LMR_QUIET_TT_CAPTURE", &tune_lmr_quiet_tt_capture, -2048, 4096, 307.2, 0.002 },
    { "LMR_QUIET_REFUTATION", &tune_lmr_quiet_refutation, -2048, 4096, 307.2, 0.002 },
    { "LMR_QUIET_CUT_NODE", &tune_lmr_quiet_cut_node, -2048, 4096, 307.2, 0.002 },
    { "LMR_QUIET_NOT_IMPROVING", &tune_lmr_quiet_not_improving, -2048, 4096, 307.2, 0.002 },
    { "LMR_QUIET_CHECK", &tune_lmr_quiet_check, -2048, 4096, 307.2, 0.002 },
    { "LMR_STAT_OFFSET", &tune_lmr_stat_offset, 5000, 10000, 250.0, 0.002 },
    { "LMR_STAT_DIV", &tune_lmr_stat_div, 5, 30, 1.2, 0.002 },
    { "LMR_CAPTURE_WINNING", &tune_lmr_capture_winning, -2048, 4096, 307.2, 0.002 },
    { "LMR_CAPTURE_BAD", &tune_lmr_capture_bad, -2048, 4096, 307.2, 0.002 },
    { "LMR_CAPTURE_CUT_NODE", &tune_lmr_capture_cut_node, -2048, 4096, 307.2, 0.002 },
    { "LMR_CAPTURE_NOT_IMPROVING", &tune_lmr_capture_not_improving, -2048, 2048, 204.8, 0.002 },
    { "LMR_CAPTURE_CHECK", &tune_lmr_capture_check, -2048, 4096, 307.2, 0.002 },
    { "LMR_TT_DEPTH", &tune_lmr_tt_depth, -2048, 4096, 307.2, 0.002 },
    { "LMR_DEEPER_MARGIN", &tune_lmr_deeper_margin, 12, 120, 5.4, 0.002 },
    { "PV_TT_MOVE_MIN_ROOT_DEPTH", &tune_pv_tt_move_min_root_depth, 4, 16, 0.6, 0.002 },
    { "ALPHA_IMPROVEMENT_MIN_DEPTH", &tune_alpha_improvement_min_depth, 1, 6, 0.5, 0.002 },
    { "LMP_BASE", &tune_lmp_base, 1, 8, 0.5, 0.002 },
    { "LMP_PV_SCALE", &tune_lmp_pv_scale, 0, 4, 0.5, 0.002 },
    { "HIST_PRUNE_MAX_DEPTH", &tune_hist_prune_max_depth, 4, 12, 0.5, 0.002 },
    { "HIST_PRUNE_LINEAR", &tune_hist_prune_linear, 100, 400, 15.0, 0.002 },
    { "HIST_PRUNE_QUAD", &tune_hist_prune_quad, 60, 300, 12.0, 0.002 },
    { "FUTILITY_LMR_SQ", &tune_futility_lmr_sq, 96, 384, 14.4, 0.002 },
    { "FUTILITY_STAT_DIV", &tune_futility_stat_div, 213, 1707, 74.7, 0.002 },
    { "SEE_CAPTURE_DEPTH", &tune_see_capture_depth, 1, 12, 0.6, 0.002 },
    { "SEE_CAPTURE_MULT", &tune_see_capture_mult, 60, 180, 6.0, 0.002 },
    { "SEE_QUIET_DEPTH", &tune_see_quiet_depth, 1, 12, 0.6, 0.002 },
    { "SEE_QUIET_MULT", &tune_see_quiet_mult, 10, 100, 4.5, 0.002 },
    { "SEE_QUIET_STAT_DIV", &tune_see_quiet_stat_div, 64, 256, 9.6, 0.002 },
    { "NMP_MIN_DEPTH",      &tune_nmp_min_depth,       1,   6,  0.5, 0.002 },
    /* Null move R in 1/PLY_GRAIN ply (the depth rate per ply of depth). */
    { "NMP_R_BASE",         &tune_nmp_r_base,       2048, 6144, 512.0, 0.002 },
    { "NMP_R_DEPTH_RATE",   &tune_nmp_r_depth_rate,  128,  512,  32.0, 0.002 },
    { "NMP_R_EVAL_DIV",     &tune_nmp_r_eval_div,    100,  400,  15.0, 0.002 },
    { "NMP_R_EVAL_MAX",     &tune_nmp_r_eval_max,   1024, 6144, 512.0, 0.002 },
    { "NMP_VERIFY_DEPTH",   &tune_nmp_verify_depth,    8,  24,  1.0, 0.002 },
    /* In 1/PLY_GRAIN ply: 0 to 3 plies, c_end half a ply. */
    /* Time management (limits.h). Tuned at one time control, they fit that one best. The stability
     * factor stays above the next one (1.2), so a changed best move always gets the most time. */
    { "TM_MOVES_LEFT",      &tune_tm_moves_left,      10,  40,  1.5, 0.002 },
    { "TM_SOFT_PERCENT",    &tune_tm_soft_percent,    30, 100,  3.5, 0.002 },
    { "TM_HARD_PERCENT",    &tune_tm_hard_percent,   150, 500, 15.0, 0.002 },
    { "TM_UNSTABLE_PERCENT", &tune_tm_unstable_percent, 130, 400, 13.0, 0.002 },
    { "TM_NODES_PERCENT",   &tune_tm_nodes_percent,   80, 200,  6.0, 0.002 },
    /* Correction history (search_constants.h). */
    { "CORR_PAWN_WEIGHT",   &tune_corr_pawn_weight,    0, 200, 10.0, 0.002 },
    { "CORR_NONPAWN_WEIGHT", &tune_corr_nonpawn_weight, 0, 200, 10.0, 0.002 },
    { "CORR_MAX",           &tune_corr_max,           16, 200,  9.0, 0.002 },
    { "CORR_RATE",          &tune_corr_rate,          16, 128,  6.0, 0.002 },
    { "SE_MIN_DEPTH",        &tune_se_min_depth,    2,   10,    0.5, 0.002 },
    { "SE_TT_DEPTH_MARGIN",  &tune_se_tt_depth_margin,    1,    6,    0.5, 0.002 },
    { "QS_FUTILITY_MARGIN", &tune_qs_futility_margin, 24, 72, 2.4, 0.002 },
    { "QS_MOVE_COUNT", &tune_qs_move_count, 2, 5, 0.5, 0.002 },
    { "QS_STAND_PAT_BLEND", &tune_qs_stand_pat_blend, 1, 1024, 51.1, 0.002 },
    { "QS_FAIL_HIGH_BLEND", &tune_qs_fail_high_blend, 1, 1024, 51.1, 0.002 },
    { "QS_TT_HALFMOVE_LIMIT", &tune_qs_tt_halfmove_limit, 60, 99, 1.9, 0.002 },
    { "SE_EXACT_MUL", &tune_se_exact_mul, 16, 128, 5.6, 0.002 },
    { "SE_DEPTH_MUL", &tune_se_depth_mul, 32, 128, 4.8, 0.002 },
    { "SE_DEPTH_SUB", &tune_se_depth_sub, 0, 512, 25.6, 0.002 },
    { "SE_DOUBLE_MARGIN", &tune_se_double_margin, 30, 150, 6.0, 0.002 },
    { "SE_TRIPLE_MARGIN", &tune_se_triple_margin, 90, 600, 25.5, 0.002 },
    { "SE_PV_BONUS", &tune_se_pv_bonus, 384, 3072, 134.4, 0.002 },
    { "SE_FAIL_HIGH_NEG", &tune_se_fail_high_neg, 1, 4, 0.5, 0.002 },
    { "SE_CUT_NODE_NEG", &tune_se_cut_node_neg, 1, 4, 0.5, 0.002 },
    { "SE_TT_ALPHA_NEG", &tune_se_tt_alpha_neg, 0, 3, 0.5, 0.002 },
    { "IIR_MIN_DEPTH",       &tune_iir_min_depth,    2,    8,    0.5, 0.002 },
    { "ASPIRATION_WINDOW",   &tune_aspiration_window,    8,   80,    3.6, 0.002 },
    { "CAPTURE_HISTORY_DIVISOR", &tune_capture_history_divisor,    8,   64,    2.8, 0.002 },
    /* The minimum depth stays above PROBCUT_REDUCTION (4), so the reduced search has a depth. */
    { "PROBCUT_MIN_DEPTH",   &tune_probcut_min_depth,    5,   10,    0.5, 0.002 },
    { "PROBCUT_MARGIN",      &tune_probcut_margin,  150,  400,   12.5, 0.002 },
    { "PROBCUT_IMPROVING",   &tune_probcut_improving,    0,  140,    7.0, 0.002 },
};
const int tunable_count = (int) (sizeof(tunables) / sizeof(tunables[0]));
#else
const tunable_t tunables[1] = { { 0 } };
const int tunable_count = 0;
#endif

void print_tunable_options(void) {
    for (int i = 0; i < tunable_count; i++) {
        printf("option name %s type spin default %d min %d max %d\n", tunables[i].name, *tunables[i].value,
               tunables[i].min, tunables[i].max);
    }
}

bool set_tunable(const char* name, size_t length, int value) {
    for (int i = 0; i < tunable_count; i++) {
        const tunable_t* t = &tunables[i];
        if (strlen(t->name) == length && strncasecmp(name, t->name, length) == 0) {
            *t->value = value < t->min ? t->min : (value > t->max ? t->max : value);
            init_lmr_table();   /* LMR_QUIET_/LMR_CAPTURE_ SCALE and BIAS shape it. */
            return true;
        }
    }
    return false;
}

void print_spsa_input(void) {
    if (tunable_count == 0) {
        printf("No tunable parameters: build with TIKI_TUNE (make TUNE=1, or -DTIKI_TUNE=ON).\n");
        return;
    }
    for (int i = 0; i < tunable_count; i++) {
        const tunable_t* t = &tunables[i];
        printf("%s, int, %d.0, %d.0, %d.0, %.2f, %.4f\n", t->name, *t->value, t->min, t->max, t->c_end, t->r_end);
    }
}
