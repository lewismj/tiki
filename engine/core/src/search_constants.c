#include <math.h>
#include <string.h>

#include "search_constants.h"

/* Most valuable victim, least valuable attacker, used to order moves (i.e. not position evaluation).  */
const int mvv_lva[12][12] = {
    { 105, 205, 305, 405, 505, 605,  105, 205, 305, 405, 505, 605 },
    { 104, 204, 304, 404, 504, 604,  104, 204, 304, 404, 504, 604 },
    { 103, 203, 303, 403, 503, 603,  103, 203, 303, 403, 503, 603 },
    { 102, 202, 302, 402, 502, 602,  102, 202, 302, 402, 502, 602 },
    { 101, 201, 301, 401, 501, 601,  101, 201, 301, 401, 501, 601 },
    { 100, 200, 300, 400, 500, 600,  100, 200, 300, 400, 500, 600 },

    { 105, 205, 305, 405, 505, 605,  105, 205, 305, 405, 505, 605 },
    { 104, 204, 304, 404, 504, 604,  104, 204, 304, 404, 504, 604 },
    { 103, 203, 303, 403, 503, 603,  103, 203, 303, 403, 503, 603 },
    { 102, 202, 302, 402, 502, 602,  102, 202, 302, 402, 502, 602 },
    { 101, 201, 301, 401, 501, 601,  101, 201, 301, 401, 501, 601 },
    { 100, 200, 300, 400, 500, 600,  100, 200, 300, 400, 500, 600 }
};

int16_t lmr_quiet_table[LMR_TABLE_SIZE][LMR_TABLE_SIZE];
int16_t lmr_capture_table[LMR_TABLE_SIZE][LMR_TABLE_SIZE];

/* The reference's natural log (Math.hpp:68-92), so the tables are its values exactly. */
static float ref_log(float x) {
    int32_t xi, ei;
    memcpy(&xi, &x, sizeof(xi));
    const int32_t e = (int32_t) (((uint32_t) xi - 0x3f2aaaabu) & 0xff800000u);
    ei = xi - e;
    float m;
    memcpy(&m, &ei, sizeof(m));
    const float i = 1.19209290e-7f * (float) e;
    const float f = m - 1.0f;
    const float s = f * f;
    float r = -0.130187988f * f + 0.140889585f;
    const float t = -0.121489584f * f + 0.139809534f;
    r = r * s + t;
    r = r * f - 0.166845024f;
    r = r * f + 0.200121149f;
    r = r * f - 0.249996364f;
    r = r * f + 0.333331943f;
    r = r * f - 0.500000000f;
    r = r * s + f;
    return i * 0.693147182f + r;
}

/* reference Search.cpp:231-260: PLY_GRAIN * (bias + scale * ln(depth) * ln(move)), 0 for depth or move 0. */
void init_lmr_table() {
    const float quiet_bias = (float) LMR_QUIET_BIAS / 1000.0f, quiet_scale = (float) LMR_QUIET_SCALE / 1000.0f;
    const float capture_bias = (float) LMR_CAPTURE_BIAS / 1000.0f, capture_scale = (float) LMR_CAPTURE_SCALE / 1000.0f;
    for (int depth = 0; depth < LMR_TABLE_SIZE; depth++) {
        for (int move = 0; move < LMR_TABLE_SIZE; move++) {
            if (depth == 0 || move == 0) {
                lmr_quiet_table[depth][move] = lmr_capture_table[depth][move] = 0;
                continue;
            }
            const float l = ref_log((float) depth) * ref_log((float) move);
            lmr_quiet_table[depth][move] = (int16_t) (uint32_t) ((float) PLY_GRAIN * (quiet_bias + quiet_scale * l));
            lmr_capture_table[depth][move] = (int16_t) (uint32_t) ((float) PLY_GRAIN * (capture_bias + capture_scale * l));
        }
    }
}
