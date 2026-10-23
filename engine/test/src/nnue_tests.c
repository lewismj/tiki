#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "unity.h"
#include "nnue_tests.h"

#include "../../core/src/board.h"
#include "../../core/src/random.h"
#include "../../core/src/uci.h"
#include "../../core/src/nnue/nnue.h"
#include "../../core/src/datagen.h"
#include "../../core/src/search_constants.h"

#define START_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

static const char* const positions[] = {
        START_FEN,
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
        "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/8 b - - 3 54",
};
#define POSITION_COUNT ((int) (sizeof(positions) / sizeof(positions[0])))


static int compare_ints(const void* a, const void* b) {
    return *(const int*) a - *(const int*) b;
}

static int sorted_features(const board_t* board, colour perspective, int* features) {
    int n = nnue_active_features(board, perspective, features);
    qsort(features, n, sizeof(int), compare_ints);
    return n;
}

/* Colours swapped and the board flipped vertically, the same position for the other side. */
static void colour_flip(const board_t* in, board_t* out) {
    memset(out, 0, sizeof(*out));
    for (int pc = P; pc <= k; pc++) {
        bitboard pieces = in->pieces[pc];
        int flipped = pc < p ? pc + 6 : pc - 6;
        while (pieces) out->pieces[flipped] |= 1ULL << (get_lsb_and_pop_bit(&pieces) ^ 56);
    }
    out->side = in->side ^ 1;
    out->en_passant = none_sq;
}

/* Files reversed (castling ignored, the features don't use it). */
static void mirror(const board_t* in, board_t* out) {
    memset(out, 0, sizeof(*out));
    for (int pc = P; pc <= k; pc++) {
        bitboard pieces = in->pieces[pc];
        while (pieces) out->pieces[pc] |= 1ULL << (get_lsb_and_pop_bit(&pieces) ^ 7);
    }
    out->side = in->side;
    out->en_passant = none_sq;
}

/* Small random weights, the accumulators stay well inside int16. */
static void random_network(uint32_t seed) {
    TEST_ASSERT_TRUE(nnue_allocate());
    uint32_t state = seed;
    for (size_t i = 0; i < (size_t) NNUE_INPUTS * NNUE_HIDDEN; i++)
        nnue_net.ft_weights[i] = (int16_t) ((int) (next_random_32(&state) % 129) - 64);
    for (int i = 0; i < NNUE_HIDDEN; i++)
        nnue_net.ft_bias[i] = (int16_t) ((int) (next_random_32(&state) % 101) - 50);
    for (int i = 0; i < 2 * NNUE_HIDDEN; i++)
        nnue_net.out_weights[i] = (int16_t) ((int) (next_random_32(&state) % 255) - 127);
    nnue_net.out_bias = (int32_t) (next_random_32(&state) % 2001) - 1000;
}


void test_nnue_feature_index_known_values() {
    /* Start position, white's king e1 is mirrored to d1: bucket 7 * 4 + 3 = 31. */
    const int base = 31 * NNUE_PIECE_SQUARES;
    TEST_ASSERT_EQUAL_INT(base + 0 * 64 + (e2 ^ 7), nnue_feature_index(white, e1, P, e2));   /* 21875 */
    TEST_ASSERT_EQUAL_INT(21875, nnue_feature_index(white, e1, P, e2));
    TEST_ASSERT_EQUAL_INT(22396, nnue_feature_index(white, e1, Q, d1));                     /* own queen, kind 8 */
    TEST_ASSERT_EQUAL_INT(22467, nnue_feature_index(white, e1, k, e8));                     /* kings share kind 10 */
    TEST_ASSERT_EQUAL_INT(21899, nnue_feature_index(white, e1, p, e7));                     /* their pawn, kind 1 */

    /* Black's view of its own e7 pawn is white's view of e2. */
    TEST_ASSERT_EQUAL_INT(21875, nnue_feature_index(black, e8, p, e7));

    /* King on a1, no mirror: bucket 28, white pawn a2 = 48. */
    TEST_ASSERT_EQUAL_INT(28 * NNUE_PIECE_SQUARES + 48, nnue_feature_index(white, a1, P, a2));
}

void test_nnue_features_in_range_and_unique() {
    int features[NNUE_MAX_ACTIVE];
    for (int i = 0; i < POSITION_COUNT; i++) {
        board_t board;
        parse_fen(positions[i], &board);
        for (int c = white; c <= black; c++) {
            int n = sorted_features(&board, c, features);
            TEST_ASSERT_EQUAL_INT(pop_count(board.occupancy[both]), n);
            for (int f = 0; f < n; f++) {
                TEST_ASSERT_TRUE(features[f] >= 0 && features[f] < NNUE_INPUTS);
                if (f) TEST_ASSERT_TRUE(features[f] != features[f - 1]);
            }
        }
    }
}

void test_nnue_feature_symmetry() {
    int a[NNUE_MAX_ACTIVE], b[NNUE_MAX_ACTIVE];
    for (int i = 0; i < POSITION_COUNT; i++) {
        board_t board, flipped, mirrored;
        parse_fen(positions[i], &board);
        colour_flip(&board, &flipped);
        mirror(&board, &mirrored);

        for (int c = white; c <= black; c++) {
            /* Colour flip swaps the perspectives. */
            int n = sorted_features(&board, c, a);
            TEST_ASSERT_EQUAL_INT(n, sorted_features(&flipped, c ^ 1, b));
            TEST_ASSERT_EQUAL_INT_ARRAY(a, b, n);

            /* Horizontal mirror doesn't change anything ("_hm"). */
            TEST_ASSERT_EQUAL_INT(n, sorted_features(&mirrored, c, b));
            TEST_ASSERT_EQUAL_INT_ARRAY(a, b, n);
        }
    }
}

void test_nnue_evaluate_known_value() {
    TEST_ASSERT_TRUE(nnue_allocate());
    board_t board;
    parse_fen(START_FEN, &board);

    /* Only the output bias, QA * QB is 1.0, which is SCALE centipawns. */
    nnue_net.out_bias = NNUE_QA * NNUE_QB;
    TEST_ASSERT_EQUAL_INT(NNUE_SCALE, nnue_evaluate(&board));

    /* One hidden unit at activation 1.0 (bias QA) with weight 1.0 (QB) for the side to move: +1.0. */
    nnue_net.ft_bias[0] = NNUE_QA;
    nnue_net.out_weights[0] = NNUE_QB;
    TEST_ASSERT_EQUAL_INT(2 * NNUE_SCALE, nnue_evaluate(&board));

    /* The same unit counted against the side to move instead: back to +0.0 + bias. */
    nnue_net.out_weights[0] = 0;
    nnue_net.out_weights[NNUE_HIDDEN] = -NNUE_QB;
    TEST_ASSERT_EQUAL_INT(0, nnue_evaluate(&board));

    /* SCReLU clamps at QA, a larger accumulator doesn't add more. */
    nnue_net.ft_bias[0] = 2 * NNUE_QA;
    TEST_ASSERT_EQUAL_INT(0, nnue_evaluate(&board));
    nnue_free();
}

void test_nnue_evaluate_colour_symmetric() {
    random_network(12345);
    for (int i = 0; i < POSITION_COUNT; i++) {
        board_t board, flipped;
        parse_fen(positions[i], &board);
        colour_flip(&board, &flipped);
        /* Same position for the other colour, same score for the side to move. */
        TEST_ASSERT_EQUAL_INT_MESSAGE(nnue_evaluate(&board), nnue_evaluate(&flipped), positions[i]);
    }
    nnue_free();
}

void test_nnue_save_load_round_trip() {
    char path[] = "/tmp/tiki_nnue_test_XXXXXX";
    int fd = mkstemp(path);
    TEST_ASSERT_TRUE(fd >= 0);
    close(fd);

    random_network(777);
    int expected[POSITION_COUNT];
    for (int i = 0; i < POSITION_COUNT; i++) {
        board_t board;
        parse_fen(positions[i], &board);
        expected[i] = nnue_evaluate(&board);
    }
    TEST_ASSERT_TRUE(nnue_save(path));
    nnue_free();
    TEST_ASSERT_FALSE(nnue_is_loaded());

    TEST_ASSERT_TRUE(nnue_load(path));
    for (int i = 0; i < POSITION_COUNT; i++) {
        board_t board;
        parse_fen(positions[i], &board);
        TEST_ASSERT_EQUAL_INT(expected[i], nnue_evaluate(&board));
    }

    /* A truncated file is rejected and leaves no network. */
    TEST_ASSERT_EQUAL_INT(0, truncate(path, 1000));
    TEST_ASSERT_FALSE(nnue_load(path));
    TEST_ASSERT_FALSE(nnue_is_loaded());
    TEST_ASSERT_FALSE(nnue_load("/nonexistent/tiki.nnue"));
    unlink(path);
}


static char* run_datagen(uint32_t seed, uint64_t* written) {
    FILE* out = tmpfile();
    TEST_ASSERT_NOT_NULL(out);
    const datagen_options_t options = { .games = 2, .depth = 3, .random_plies = 8, .max_plies = 80, .seed = seed };
    *written = datagen(out, &options);

    long size = ftell(out);
    char* text = calloc((size_t) size + 1, 1);
    rewind(out);
    TEST_ASSERT_EQUAL_size_t((size_t) size, fread(text, 1, (size_t) size, out));
    fclose(out);
    return text;
}

void test_datagen_output_format() {
    uint64_t written;
    char* text = run_datagen(42, &written);
    TEST_ASSERT_TRUE(written > 0);

    uint64_t lines = 0;
    char fen[128];
    for (char* line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        char* bar1 = strstr(line, " | ");
        TEST_ASSERT_NOT_NULL(bar1);
        char* bar2 = strstr(bar1 + 3, " | ");
        TEST_ASSERT_NOT_NULL(bar2);

        /* The FEN parses back to itself. */
        *bar1 = '\0';
        board_t board;
        parse_fen(line, &board);
        board_to_fen(&board, atoi(strrchr(line, ' ') + 1), fen, sizeof(fen));
        TEST_ASSERT_EQUAL_STRING(line, fen);

        /* Score is an integer below the mate range, result one of three values. */
        int score = atoi(bar1 + 3);
        TEST_ASSERT_TRUE(score > -MATE_SCORE && score < MATE_SCORE);
        const char* result = bar2 + 3;
        TEST_ASSERT_TRUE(strcmp(result, "1.0") == 0 || strcmp(result, "0.5") == 0 || strcmp(result, "0.0") == 0);
        lines++;
    }
    TEST_ASSERT_EQUAL_UINT64(written, lines);
    free(text);
}

void test_datagen_deterministic() {
    uint64_t a_written, b_written, c_written;
    char* a = run_datagen(7, &a_written);
    char* b = run_datagen(7, &b_written);
    char* c = run_datagen(8, &c_written);
    TEST_ASSERT_EQUAL_UINT64(a_written, b_written);
    TEST_ASSERT_EQUAL_STRING(a, b);
    TEST_ASSERT_TRUE(strcmp(a, c) != 0);
    free(a);
    free(b);
    free(c);
}
