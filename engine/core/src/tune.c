#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "tune.h"

#ifdef TIKI_TUNE
int tune_rfp_margin = RFP_MARGIN_DEFAULT;
int tune_rfp_max_depth = RFP_MAX_DEPTH_DEFAULT;
int tune_futility_margin = FUTILITY_MARGIN_DEFAULT;
int tune_futility_max_depth = FUTILITY_MAX_DEPTH_DEFAULT;
int tune_lmp_base = LMP_BASE_DEFAULT;
int tune_see_quiet_margin = SEE_QUIET_MARGIN_DEFAULT;
int tune_see_capture_margin = SEE_CAPTURE_MARGIN_DEFAULT;
int tune_se_margin = SE_MARGIN_DEFAULT;
int tune_rfp_improving = RFP_IMPROVING_DEFAULT;
int tune_futility_base = FUTILITY_BASE_DEFAULT;
int tune_lmp_not_improving_pct = LMP_NOT_IMPROVING_PCT_DEFAULT;
int tune_nmp_min_depth = NMP_MIN_DEPTH_DEFAULT;
int tune_nmp_r_base = NMP_R_BASE_DEFAULT;
int tune_nmp_r_depth_div = NMP_R_DEPTH_DIV_DEFAULT;
int tune_nmp_r_eval_div = NMP_R_EVAL_DIV_DEFAULT;
int tune_nmp_r_eval_max = NMP_R_EVAL_MAX_DEFAULT;
int tune_nmp_verify_depth = NMP_VERIFY_DEPTH_DEFAULT;
int tune_lmr_history_divisor = LMR_HISTORY_DIVISOR_DEFAULT;
int tune_hist_prune_max_depth = HIST_PRUNE_MAX_DEPTH_DEFAULT;
int tune_hist_prune_margin = HIST_PRUNE_MARGIN_DEFAULT;
int tune_lmr_cut_node = LMR_CUT_NODE_DEFAULT;
int tune_lmr_not_improving = LMR_NOT_IMPROVING_DEFAULT;
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
int tune_se_reduction = SE_REDUCTION_DEFAULT;
int tune_lmr_killer_max = LMR_KILLER_MAX_DEFAULT;
int tune_lmr_base = LMR_BASE_DEFAULT;
int tune_lmr_divisor = LMR_DIVISOR_DEFAULT;
int tune_lmr_min_depth = LMR_MIN_DEPTH_DEFAULT;
int tune_lmr_min_moves = LMR_MIN_MOVES_DEFAULT;
int tune_hist_bonus_scale = HIST_BONUS_SCALE_DEFAULT;
int tune_hist_bonus_max = HIST_BONUS_MAX_DEFAULT;
int tune_lmp_max_depth = LMP_MAX_DEPTH_DEFAULT;
int tune_see_prune_max_depth = SEE_PRUNE_MAX_DEPTH_DEFAULT;
int tune_iir_min_depth = IIR_MIN_DEPTH_DEFAULT;
int tune_aspiration_window = ASPIRATION_WINDOW_DEFAULT;
int tune_delta_margin = DELTA_MARGIN_DEFAULT;
int tune_capture_history_divisor = CAPTURE_HISTORY_DIVISOR_DEFAULT;

/* name, variable, min, max, c_end, r_end. c_end is about a twentieth of the range, at least 0.5
 * (OpenBench's minimum for integers); r_end is OpenBench's usual 0.002. */
const tunable_t tunables[] = {
    { "RFP_MARGIN",         &tune_rfp_margin,         40, 200,  8.0, 0.002 },
    { "RFP_MAX_DEPTH",      &tune_rfp_max_depth,       3,  10,  0.5, 0.002 },
    { "RFP_IMPROVING",      &tune_rfp_improving,       0, 100,  5.0, 0.002 },
    { "FUTILITY_BASE",      &tune_futility_base,       0, 150,  8.0, 0.002 },
    { "FUTILITY_MARGIN",    &tune_futility_margin,    40, 250, 10.0, 0.002 },
    { "FUTILITY_MAX_DEPTH", &tune_futility_max_depth,  1,  12,  0.5, 0.002 },
    { "LMP_BASE",           &tune_lmp_base,            1,   8,  0.5, 0.002 },
    { "LMP_NOT_IMPROVING_PCT", &tune_lmp_not_improving_pct, 40, 100, 3.0, 0.002 },
    { "SEE_QUIET_MARGIN",   &tune_see_quiet_margin,   20, 150,  6.0, 0.002 },
    { "SEE_CAPTURE_MARGIN", &tune_see_capture_margin,  5,  60,  3.0, 0.002 },
    { "SE_MARGIN",          &tune_se_margin,           1,   6,  0.5, 0.002 },
    { "NMP_MIN_DEPTH",      &tune_nmp_min_depth,       1,   6,  0.5, 0.002 },
    { "NMP_R_BASE",         &tune_nmp_r_base,          2,   6,  0.5, 0.002 },
    { "NMP_R_DEPTH_DIV",    &tune_nmp_r_depth_div,     2,   8,  0.5, 0.002 },
    { "NMP_R_EVAL_DIV",     &tune_nmp_r_eval_div,    100, 400, 15.0, 0.002 },
    { "NMP_R_EVAL_MAX",     &tune_nmp_r_eval_max,      1,   6,  0.5, 0.002 },
    { "NMP_VERIFY_DEPTH",   &tune_nmp_verify_depth,    8,  24,  1.0, 0.002 },
    { "LMR_HISTORY_DIVISOR", &tune_lmr_history_divisor, 400, 4000, 150.0, 0.002 },
    { "HIST_PRUNE_MAX_DEPTH", &tune_hist_prune_max_depth, 1, 8,  0.5, 0.002 },
    { "HIST_PRUNE_MARGIN",  &tune_hist_prune_margin,  50,  2000,  100.0, 0.002 },
    { "LMR_CUT_NODE",       &tune_lmr_cut_node,        0,   3,  0.5, 0.002 },
    { "LMR_NOT_IMPROVING",  &tune_lmr_not_improving,   0,   3,  0.5, 0.002 },
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
    { "SE_MIN_DEPTH",        &tune_se_min_depth,    4,   10,    0.5, 0.002 },
    { "SE_TT_DEPTH_MARGIN",  &tune_se_tt_depth_margin,    1,    6,    0.5, 0.002 },
    { "SE_REDUCTION",        &tune_se_reduction,    0,    3,    0.5, 0.002 },
    { "LMR_KILLER_MAX",      &tune_lmr_killer_max,    0,    3,    0.5, 0.002 },
    { "LMR_BASE",            &tune_lmr_base,    0,  150,    7.5, 0.002 },
    { "LMR_DIVISOR",         &tune_lmr_divisor,  150,  400,   12.5, 0.002 },
    { "LMR_MIN_DEPTH",       &tune_lmr_min_depth,    3,    6,    0.5, 0.002 },
    { "LMR_MIN_MOVES",       &tune_lmr_min_moves,    1,    6,    0.5, 0.002 },
    { "HIST_BONUS_SCALE",    &tune_hist_bonus_scale,    4,   40,    1.8, 0.002 },
    { "HIST_BONUS_MAX",      &tune_hist_bonus_max,  400, 4000,  180.0, 0.002 },
    { "LMP_MAX_DEPTH",       &tune_lmp_max_depth,    3,   10,    0.5, 0.002 },
    { "SEE_PRUNE_MAX_DEPTH", &tune_see_prune_max_depth,    4,   12,    0.5, 0.002 },
    { "IIR_MIN_DEPTH",       &tune_iir_min_depth,    2,    8,    0.5, 0.002 },
    { "ASPIRATION_WINDOW",   &tune_aspiration_window,    8,   80,    3.6, 0.002 },
    { "DELTA_MARGIN",        &tune_delta_margin,   50,  400,   17.5, 0.002 },
    { "CAPTURE_HISTORY_DIVISOR", &tune_capture_history_divisor,    8,   64,    2.8, 0.002 },
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
            init_lmr_table();   /* LMR_BASE and LMR_DIVISOR shape it. */
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
