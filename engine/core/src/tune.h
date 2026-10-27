#ifndef TIKI_TUNE_H
#define TIKI_TUNE_H

#include <stdbool.h>
#include <stddef.h>

#include "search_constants.h"

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
    SE_MARGIN_DEFAULT = SE_MARGIN
};

extern int tune_rfp_margin;
extern int tune_rfp_max_depth;
extern int tune_futility_margin;
extern int tune_futility_max_depth;
extern int tune_lmp_base;
extern int tune_see_quiet_margin;
extern int tune_see_capture_margin;
extern int tune_se_margin;

#undef RFP_MARGIN
#undef RFP_MAX_DEPTH
#undef FUTILITY_MARGIN
#undef FUTILITY_MAX_DEPTH
#undef LMP_BASE
#undef SEE_QUIET_MARGIN
#undef SEE_CAPTURE_MARGIN
#undef SE_MARGIN
#define RFP_MARGIN tune_rfp_margin
#define RFP_MAX_DEPTH tune_rfp_max_depth
#define FUTILITY_MARGIN tune_futility_margin
#define FUTILITY_MAX_DEPTH tune_futility_max_depth
#define LMP_BASE tune_lmp_base
#define SEE_QUIET_MARGIN tune_see_quiet_margin
#define SEE_CAPTURE_MARGIN tune_see_capture_margin
#define SE_MARGIN tune_se_margin
#endif

#endif
