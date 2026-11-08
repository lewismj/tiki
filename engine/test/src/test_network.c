#include "unity.h"
#include "test_network.h"

#include "../../core/src/nnue/nnue.h"

/*
 * A network that counts material, exactly, through the real layer stack (nnue.h):
 *
 * - Accumulator: piece kind k (type * 2 + theirs, kings are 10) has two units in the first half,
 *   2k ("plus") and 2k + 1 ("minus"), both with bias 128. Each piece of that kind adds 4 s to the
 *   first and subtracts 4 s from the second (from any square, in any king bucket), s a step per kind.
 *   Their partners in the second half (2k + 256, 2k + 1 + 256) have bias 128 and no weights.
 * - Hidden inputs: the pairwise product of x = 128 +- 4 s n and its partner 128 is
 *   (128 x) >> 9 = floor(x / 4) = 32 +- s n, exactly, while x stays within 0..QA
 *   (s n up to 31), so plus - minus = 2 s n.
 * - Hidden unit 0: bias QH / 2 (the middle of CReLU's range), weight +-w on the side to move's plus and
 *   minus inputs of each kind, the other side's kinds the negative, with 2 s w = 128 * pawns per piece:
 *   h = QH / 2 + 128 * (material difference in pawns), linear while it stays within 0..QH (a
 *   difference of up to 31.75 pawns).
 * - Output: weight 1016 on unit 0, bias -1016 * QH / 2, so eval = 1016 * 128 * pawns * 400 / (QH * QW)
 *   = 100 * pawns. The same in every bucket.
 *
 * Steps and weights per kind (P N B R Q): s = 2 2 2 4 8, w = 32 96 96 80 72, exact up to 15 pawns,
 * knights or bishops, 7 rooks and 3 queens of a side.
 */
#define UNIT_BIAS 128
#define OUT_WEIGHT 1016

void load_material_network(void) {
    static const int step[5] = { 2, 2, 2, 4, 8 };          /* P N B R Q */
    static const int weight[5] = { 32, 96, 96, 80, 72 };    /* 2 * step * weight = 128 * pawns per piece */
    TEST_ASSERT_TRUE(nnue_allocate());

    for (int bucket = 0; bucket < NNUE_KING_BUCKETS; bucket++) {
        for (int kind = 0; kind < 10; kind++) {
            for (int sq = 0; sq < 64; sq++) {
                const size_t row = (size_t) (bucket * NNUE_PIECE_SQUARES + kind * 64 + sq) * NNUE_HIDDEN;
                nnue_net.ft_weights[row + 2 * kind] = (int16_t) (4 * step[kind / 2]);
                nnue_net.ft_weights[row + 2 * kind + 1] = (int16_t) (-4 * step[kind / 2]);
            }
        }
    }
    for (int unit = 0; unit < 20; unit++) {
        nnue_net.ft_bias[unit] = UNIT_BIAS;
        nnue_net.ft_bias[unit + NNUE_HIDDEN / 2] = UNIT_BIAS;
    }

    for (int b = 0; b < NNUE_OUTPUT_BUCKETS; b++) {
        for (int kind = 0; kind < 10; kind++) {
            const int w = weight[kind / 2] * (kind & 1 ? -1 : 1);
            nnue_net.l1_weights[nnue_l1_index(b, 0, 2 * kind)] = (int8_t) w;          /* The side to move's inputs. */
            nnue_net.l1_weights[nnue_l1_index(b, 0, 2 * kind + 1)] = (int8_t) -w;
        }
        nnue_net.l1_bias[b][0] = NNUE_QH / 2;
        nnue_net.out_weights[b][0] = OUT_WEIGHT;
        nnue_net.out_bias[b] = -OUT_WEIGHT * (NNUE_QH / 2);
    }
}
