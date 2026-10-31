#include "unity.h"
#include "test_network.h"

#include "../../core/src/nnue/nnue.h"

#define UNIT_BIAS 127       /* About QA / 2, SCReLU (x^2) is close to linear for small changes. */
#define PER_PIECE 2         /* Accumulator change per piece of a kind. */

void load_material_network(void) {
    static const int centipawns[5] = { 100, 300, 300, 500, 900 };   /* P N B R Q */
    TEST_ASSERT_TRUE(nnue_allocate());

    /* Kind k (type * 2 + theirs, kings are 10) adds PER_PIECE to unit k, from any square or bucket. */
    for (int bucket = 0; bucket < NNUE_KING_BUCKETS; bucket++) {
        for (int kind = 0; kind < 10; kind++) {
            for (int sq = 0; sq < 64; sq++) {
                const size_t row = (size_t) (bucket * NNUE_PIECE_SQUARES + kind * 64 + sq) * NNUE_HIDDEN;
                nnue_net.ft_weights[row + kind] = PER_PIECE;
            }
        }
    }

    /* SCReLU(UNIT_BIAS + PER_PIECE * n) ~ UNIT_BIAS^2 + 2 * UNIT_BIAS * PER_PIECE * n, the constant
     * cancels between own and their units. Weight w for 'cp' centipawns per piece:
     * 2 * UNIT_BIAS * PER_PIECE * w / QA * SCALE / (QA * QB) = cp. Only the side to move's half,
     * the same in every output bucket. */
    for (int kind = 0; kind < 10; kind++) {
        nnue_net.ft_bias[kind] = UNIT_BIAS;
        const double weight = (double) centipawns[kind / 2] * NNUE_QA * NNUE_QA * NNUE_QB /
                              (2.0 * UNIT_BIAS * PER_PIECE * NNUE_SCALE);
        for (int b = 0; b < NNUE_OUTPUT_BUCKETS; b++) {
            nnue_net.out_weights[b][kind] = (int16_t) ((kind & 1 ? -1 : 1) * (int) (weight + 0.5));
        }
    }
}
