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

/* name, variable, min, max, c_end, r_end. c_end is about a twentieth of the range, at least 0.5
 * (OpenBench's minimum for integers); r_end is OpenBench's usual 0.002. */
const tunable_t tunables[] = {
    { "RFP_MARGIN",         &tune_rfp_margin,         40, 200,  8.0, 0.002 },
    { "RFP_MAX_DEPTH",      &tune_rfp_max_depth,       3,  10,  0.5, 0.002 },
    { "FUTILITY_MARGIN",    &tune_futility_margin,    50, 300, 12.0, 0.002 },
    { "FUTILITY_MAX_DEPTH", &tune_futility_max_depth,  1,   6,  0.5, 0.002 },
    { "LMP_BASE",           &tune_lmp_base,            1,   8,  0.5, 0.002 },
    { "SEE_QUIET_MARGIN",   &tune_see_quiet_margin,   20, 150,  6.0, 0.002 },
    { "SEE_CAPTURE_MARGIN", &tune_see_capture_margin,  5,  60,  3.0, 0.002 },
    { "SE_MARGIN",          &tune_se_margin,           1,   6,  0.5, 0.002 },
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
