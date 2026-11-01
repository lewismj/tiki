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
#include "../../core/src/move_generator.h"
#include "test_network.h"
#include "test_positions.h"

#define START_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

static const char* const positions[] = {
        START_FEN,
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
        "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/8 b - - 3 54",
};
#define POSITION_COUNT ((int) (sizeof(positions) / sizeof(positions[0])))

/* The property tests (an invariant in every position) also run over the shared test_positions. */
#define PROPERTY_POSITION_COUNT (POSITION_COUNT + TEST_POSITION_COUNT)
static const char* property_position(int i) {
    return i < POSITION_COUNT ? positions[i] : test_positions[i - POSITION_COUNT];
}


static uint8_t* saved_network(size_t* size);

#ifdef _WIN32
#define TEMP_TEMPLATE "tiki_nnue_test_XXXXXX"       /* No /tmp, the current directory. */
#else
#define TEMP_TEMPLATE "/tmp/tiki_nnue_test_XXXXXX"
#endif

static int compare_ints(const void* a, const void* b) {
    return *(const int*) a - *(const int*) b;
}

static int sorted_features(const board_t* board, colour perspective, int* features) {
    int n = nnue_active_features(board, perspective, features);
    qsort(features, n, sizeof(int), compare_ints);
    return n;
}

/* occupancy[] from pieces[], for the boards built here (the output bucket counts occupancy[both]). */
static void set_occupancy(board_t* board) {
    board->occupancy[white] = board->occupancy[black] = 0;
    for (int pc = P; pc <= k; pc++) board->occupancy[pc < p ? white : black] |= board->pieces[pc];
    board->occupancy[both] = board->occupancy[white] | board->occupancy[black];
}

/* Colours swapped and the board flipped vertically, the same position for the other side. */
static void colour_flip(const board_t* in, board_t* out) {
    memset(out, 0, sizeof(*out));
    for (int pc = P; pc <= k; pc++) {
        bitboard pieces = in->pieces[pc];
        int flipped = pc < p ? pc + 6 : pc - 6;
        while (pieces) out->pieces[flipped] |= 1ULL << (get_lsb_and_pop_bit(&pieces) ^ 56);
    }
    set_occupancy(out);
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
    set_occupancy(out);
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
    for (int b = 0; b < NNUE_OUTPUT_BUCKETS; b++) {
        for (int i = 0; i < 2 * NNUE_HIDDEN; i++)
            nnue_net.out_weights[b][i] = (int16_t) ((int) (next_random_32(&state) % 255) - 127);
        nnue_net.out_bias[b] = (int32_t) (next_random_32(&state) % 2001) - 1000;
    }
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
    for (int i = 0; i < PROPERTY_POSITION_COUNT; i++) {
        board_t board;
        parse_fen(property_position(i), &board);
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
    for (int i = 0; i < PROPERTY_POSITION_COUNT; i++) {
        board_t board, flipped, mirrored;
        parse_fen(property_position(i), &board);
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

    /* 32 pieces: the last output bucket. */
    const int b = NNUE_OUTPUT_BUCKETS - 1;
    TEST_ASSERT_EQUAL_INT(b, nnue_output_bucket(32));

    /* Only the output bias, QA * QB is 1.0, which is SCALE centipawns. */
    nnue_net.out_bias[b] = NNUE_QA * NNUE_QB;
    TEST_ASSERT_EQUAL_INT(NNUE_SCALE, nnue_evaluate(&board));

    /* One hidden unit at activation 1.0 (bias QA) with weight 1.0 (QB) for the side to move: +1.0. */
    nnue_net.ft_bias[0] = NNUE_QA;
    nnue_net.out_weights[b][0] = NNUE_QB;
    TEST_ASSERT_EQUAL_INT(2 * NNUE_SCALE, nnue_evaluate(&board));

    /* The same unit counted against the side to move instead: back to +0.0 + bias. */
    nnue_net.out_weights[b][0] = 0;
    nnue_net.out_weights[b][NNUE_HIDDEN] = -NNUE_QB;
    TEST_ASSERT_EQUAL_INT(0, nnue_evaluate(&board));

    /* SCReLU clamps at QA, a larger accumulator doesn't add more. */
    nnue_net.ft_bias[0] = 2 * NNUE_QA;
    TEST_ASSERT_EQUAL_INT(0, nnue_evaluate(&board));
    nnue_free();
}

void test_nnue_output_buckets() {
    TEST_ASSERT_EQUAL_INT(0, nnue_output_bucket(2));
    TEST_ASSERT_EQUAL_INT(0, nnue_output_bucket(5));
    TEST_ASSERT_EQUAL_INT(1, nnue_output_bucket(6));
    TEST_ASSERT_EQUAL_INT(6, nnue_output_bucket(29));
    TEST_ASSERT_EQUAL_INT(7, nnue_output_bucket(30));
    TEST_ASSERT_EQUAL_INT(7, nnue_output_bucket(32));

    /* A position's evaluation uses its bucket's output layer and no other. */
    random_network(99);
    for (int i = 0; i < PROPERTY_POSITION_COUNT; i++) {
        board_t board;
        parse_fen(property_position(i), &board);
        const int bucket = nnue_output_bucket(pop_count(board.occupancy[both]));
        const int before = nnue_evaluate(&board);

        const int other = (bucket + 1) % NNUE_OUTPUT_BUCKETS;
        nnue_net.out_bias[other] += NNUE_QA * NNUE_QB;
        TEST_ASSERT_EQUAL_INT_MESSAGE(before, nnue_evaluate(&board), property_position(i));
        nnue_net.out_bias[other] -= NNUE_QA * NNUE_QB;

        /* +1.0 on its own bias is +SCALE centipawns (give or take the truncating division). */
        nnue_net.out_bias[bucket] += NNUE_QA * NNUE_QB;
        TEST_ASSERT_INT_WITHIN_MESSAGE(1, before + NNUE_SCALE, nnue_evaluate(&board), property_position(i));
        nnue_net.out_bias[bucket] -= NNUE_QA * NNUE_QB;
    }
    nnue_free();
}

void test_nnue_load_single_output_network() {
    /* A version 1 network (one output layer): bucket 3's layer of a random network, on its own. */
    random_network(2024);
    size_t size;
    uint8_t* data = saved_network(&size);
    const size_t outputs = size - (size_t) NNUE_OUTPUT_BUCKETS * (2 * NNUE_HIDDEN * sizeof(int16_t) + sizeof(int32_t));
    const uint32_t version = NNUE_VERSION_SINGLE_OUTPUT;
    memcpy(data + 8, &version, sizeof(version));
    memmove(data + outputs, nnue_net.out_weights[3], sizeof(nnue_net.out_weights[3]));
    memcpy(data + outputs + sizeof(nnue_net.out_weights[3]), &nnue_net.out_bias[3], sizeof(int32_t));
    const size_t single_size = outputs + sizeof(nnue_net.out_weights[3]) + sizeof(int32_t);

    int16_t weights[2 * NNUE_HIDDEN];
    memcpy(weights, nnue_net.out_weights[3], sizeof(weights));
    const int32_t bias = nnue_net.out_bias[3];
    int16_t ft_bias[NNUE_HIDDEN];
    memcpy(ft_bias, nnue_net.ft_bias, sizeof(ft_bias));
    nnue_free();

    /* Through a file too, nnue_load checks the size. */
    char path[] = TEMP_TEMPLATE;
    int fd = mkstemp(path);
    TEST_ASSERT_TRUE(fd >= 0);
    TEST_ASSERT_EQUAL_INT((int) single_size, (int) write(fd, data, (unsigned) single_size));
    close(fd);
    TEST_ASSERT_TRUE(nnue_load(path));
    unlink(path);

    TEST_ASSERT_EQUAL_INT16_ARRAY(ft_bias, nnue_net.ft_bias, NNUE_HIDDEN);
    for (int b = 0; b < NNUE_OUTPUT_BUCKETS; b++) {
        TEST_ASSERT_EQUAL_INT16_ARRAY(weights, nnue_net.out_weights[b], 2 * NNUE_HIDDEN);
        TEST_ASSERT_EQUAL_INT32(bias, nnue_net.out_bias[b]);
    }

    /* The version 1 layout with version 2 in the header (or the reverse) is rejected. */
    const uint32_t current = NNUE_VERSION;
    memcpy(data + 8, &current, sizeof(current));
    TEST_ASSERT_FALSE(nnue_load_memory(data, single_size, "wrong version"));
    free(data);
    nnue_free();
}

void test_nnue_evaluate_colour_symmetric() {
    random_network(12345);
    for (int i = 0; i < PROPERTY_POSITION_COUNT; i++) {
        board_t board, flipped;
        parse_fen(property_position(i), &board);
        colour_flip(&board, &flipped);
        /* Same position for the other colour, same score for the side to move. */
        TEST_ASSERT_EQUAL_INT_MESSAGE(nnue_evaluate(&board), nnue_evaluate(&flipped), property_position(i));
    }
    nnue_free();
}

void test_nnue_save_load_round_trip() {
#ifdef _WIN32
    char path[] = "tiki_nnue_test_XXXXXX";      /* No /tmp, the current directory. */
#else
    char path[] = "/tmp/tiki_nnue_test_XXXXXX";
#endif
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

    TEST_ASSERT_EQUAL_STRING(path, nnue_loaded_path());

    /* A truncated or missing file is rejected, keeping the current network. */
    TEST_ASSERT_EQUAL_INT(0, truncate(path, 1000));
    TEST_ASSERT_FALSE(nnue_load(path));
    TEST_ASSERT_FALSE(nnue_load("/nonexistent/tiki.nnue"));
    TEST_ASSERT_TRUE(nnue_is_loaded());
    TEST_ASSERT_EQUAL_STRING(path, nnue_loaded_path());
    board_t board;
    parse_fen(positions[0], &board);
    TEST_ASSERT_EQUAL_INT(expected[0], nnue_evaluate(&board));
    unlink(path);
}

/* The current network in the file format, plus one spare byte. The caller frees it. */
static uint8_t* saved_network(size_t* size) {
    char path[] = TEMP_TEMPLATE;
    int fd = mkstemp(path);
    TEST_ASSERT_TRUE(fd >= 0);
    close(fd);
    TEST_ASSERT_TRUE(nnue_save(path));

    FILE* file = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(file);
    fseek(file, 0, SEEK_END);
    *size = (size_t) ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t* data = malloc(*size + 1);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL_size_t(*size, fread(data, 1, *size, file));
    fclose(file);
    unlink(path);
    return data;
}

void test_nnue_load_memory() {
    random_network(4242);
    int expected[POSITION_COUNT];
    for (int i = 0; i < POSITION_COUNT; i++) {
        board_t board;
        parse_fen(positions[i], &board);
        expected[i] = nnue_evaluate(&board);
    }
    size_t size;
    uint8_t* data = saved_network(&size);
    nnue_free();

    TEST_ASSERT_TRUE(nnue_load_memory(data, size, "memory"));
    TEST_ASSERT_EQUAL_STRING("memory", nnue_loaded_path());
    for (int i = 0; i < POSITION_COUNT; i++) {
        board_t board;
        parse_fen(positions[i], &board);
        TEST_ASSERT_EQUAL_INT(expected[i], nnue_evaluate(&board));
    }

    /* Short, long or with the wrong magic, rejected keeping the current network. */
    data[size] = 0;
    TEST_ASSERT_FALSE(nnue_load_memory(data, size - 1, "short"));
    TEST_ASSERT_FALSE(nnue_load_memory(data, size + 1, "long"));
    TEST_ASSERT_FALSE(nnue_load_memory(NULL, 0, "none"));
    data[0] ^= 1;
    TEST_ASSERT_FALSE(nnue_load_memory(data, size, "magic"));
    data[0] ^= 1;
    TEST_ASSERT_EQUAL_STRING("memory", nnue_loaded_path());

    /* A file with anything after the network is rejected too. */
    char path[] = TEMP_TEMPLATE;
    int fd = mkstemp(path);
    TEST_ASSERT_TRUE(fd >= 0);
    TEST_ASSERT_EQUAL_INT((int) (size + 1), (int) write(fd, data, (unsigned) (size + 1)));
    close(fd);
    TEST_ASSERT_FALSE(nnue_load(path));
    TEST_ASSERT_EQUAL_STRING("memory", nnue_loaded_path());
    unlink(path);

    free(data);
    nnue_free();
}

void test_nnue_embedded_network() {
    const char* name = nnue_embedded_name();
    if (name == NULL) {
        /* Built without TIKI_EMBED_NETWORK. */
        TEST_ASSERT_FALSE(nnue_load_embedded());
        return;
    }
    TEST_ASSERT_TRUE(nnue_load_embedded());
    TEST_ASSERT_EQUAL_STRING(name, nnue_loaded_path());
    board_t board;
    parse_fen(START_FEN, &board);
    const int start = nnue_evaluate(&board);
    TEST_ASSERT_TRUE(start > -100 && start < 100);

    /* Its name through EvalFile selects it again, whatever the current directory holds. */
    load_material_network();
    char command[256];
    snprintf(command, sizeof(command), "setoption name EvalFile value %s", name);
    parse_setoption(command);
    TEST_ASSERT_EQUAL_STRING(name, nnue_loaded_path());
    TEST_ASSERT_EQUAL_INT(start, nnue_evaluate(&board));
}

/* Moves of each kind the walk made, so the test can check they were all covered. */
typedef struct {
    int evaluated;
    int castles;
    int en_passants;
    int promotions;
    int null_moves;
} walk_counts_t;

/**
 * Random walk from 'board': a few random moves per node, each made with nnue_push as the search
 * does. Evaluates at about half the nodes, so some evaluations catch up over several moves, and
 * compares the incremental eval with a full refresh.
 */
static void walk(board_t* board, nnue_stack_t* stack, int depth, uint32_t* rng, walk_counts_t* counts) {
    if (depth == 0) return;

    /* As in the search, not in check (the king could then be captured). */
    const bool in_check = board->side == white ?
                          is_square_attacked_by_black(board, trailing_zero_count(board->pieces[K])) :
                          is_square_attacked_by_white(board, trailing_zero_count(board->pieces[k]));
    if (!in_check && next_random_32(rng) % 6 == 0) {
        /* Null move: no push, the same accumulators with the other side to move. */
        make_null_move(board);
        TEST_ASSERT_EQUAL_INT(nnue_evaluate(board), nnue_evaluate_incremental(stack, board));
        counts->null_moves++;
        walk(board, stack, depth - 1, rng, counts);
        pop_null_move(board);
    }

    move_buffer_t buffer;
    generate_moves(board, &buffer);
    for (int i = 0; i < buffer.index; i++) {
        const move_t mv = buffer.moves[i];
        const bool special = get_king_side_castle_flag(mv) || get_queen_side_castle_flag(mv) ||
                             get_enpassant_flag(mv) || get_promoted_piece(mv);
        /* Every special move, about one in four of the rest. */
        if (!special && next_random_32(rng) % 4 != 0) continue;

        const bool legal = make_move(board, mv);
        nnue_push(stack, board, mv);
        if (legal) {
            counts->castles += get_king_side_castle_flag(mv) || get_queen_side_castle_flag(mv);
            counts->en_passants += get_enpassant_flag(mv) != 0;
            counts->promotions += get_promoted_piece(mv) != 0;
            if (next_random_32(rng) & 1) {
                TEST_ASSERT_EQUAL_INT(nnue_evaluate(board), nnue_evaluate_incremental(stack, board));
                counts->evaluated++;
            }
            walk(board, stack, depth - 1, rng, counts);
        }
        pop_move(board);
        nnue_pop(stack);
    }
}

void test_nnue_incremental_matches_refresh() {
    static const char* const walk_positions[] = {
            /* Castling both sides, en passant after the pawn pushes. */
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
            /* En passant now (c6), promotions with and without captures for both sides, castling. */
            "r3k2r/1P4P1/8/2pP4/8/8/1p4p1/R3K2R w KQkq c6 0 1",
            "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/8 b - - 3 54",
            /* Mostly king moves, each one a refresh through the cache. */
            "8/2k5/8/3p4/4P3/8/5K2/8 w - - 0 1",
    };
    /* One stack for every position, so the refresh cache also starts from unrelated positions. */
    static nnue_stack_t stack;
    nnue_stack_init(&stack);
    random_network(4242);
    walk_counts_t counts = { 0 };
    uint32_t rng = 99;

    for (int i = 0; i < (int) (sizeof(walk_positions) / sizeof(walk_positions[0])); i++) {
        board_t board;
        parse_fen(walk_positions[i], &board);
        nnue_reset(&stack);
        TEST_ASSERT_EQUAL_INT(nnue_evaluate(&board), nnue_evaluate_incremental(&stack, &board));
        walk(&board, &stack, 5, &rng, &counts);
        TEST_ASSERT_EQUAL_INT(0, stack.top);
    }
    TEST_ASSERT_TRUE(counts.evaluated > 1000);
    TEST_ASSERT_TRUE(counts.castles > 0);
    TEST_ASSERT_TRUE(counts.en_passants > 0);
    TEST_ASSERT_TRUE(counts.promotions > 0);
    TEST_ASSERT_TRUE(counts.null_moves > 0);
    nnue_free();
}

void test_nnue_output_simd_matches_scalar() {
    /* Accumulators over the whole int16 range (below 0, above QA) and weights up to the int16
     * limits, the worst case for overflow. */
    random_network(5150);
    uint32_t rng = 7;
    nnue_accumulator_t accumulator;
    for (int trial = 0; trial < 200; trial++) {
        const int16_t extreme = (int16_t) (trial % 2 ? 32767 : -32768);
        const int bucket = trial % NNUE_OUTPUT_BUCKETS;
        for (int i = 0; i < 2 * NNUE_HIDDEN; i++) {
            nnue_net.out_weights[bucket][i] =
                    (int16_t) (trial % 4 < 2 ? extreme : (int) (next_random_32(&rng) % 65536) - 32768);
        }
        for (int c = white; c <= black; c++) {
            for (int i = 0; i < NNUE_HIDDEN; i++) {
                accumulator.values[c][i] = (int16_t) (trial % 3 == 0 ? NNUE_QA + (int) (next_random_32(&rng) % 4)
                                                                     : (int) (next_random_32(&rng) % 65536) - 32768);
            }
        }
        for (int side = white; side <= black; side++) {
            TEST_ASSERT_EQUAL_INT(nnue_output_scalar(&accumulator, side, bucket), nnue_output(&accumulator, side, bucket));
        }
    }
    nnue_free();
}

void test_nnue_output_small_matches_scalar() {
    /* The int16 / madd output (small_output) against the int64 reference, at its worst case for
     * overflow: every weight +-127 (or random within it), accumulators clamped below 0 and above QA. */
    random_network(6060);
    nnue_net.small_output = true;
    uint32_t rng = 11;
    nnue_accumulator_t accumulator;
    for (int trial = 0; trial < 200; trial++) {
        const int16_t extreme = (int16_t) (trial % 2 ? NNUE_OUT_WEIGHT_SMALL : -NNUE_OUT_WEIGHT_SMALL);
        const int bucket = trial % NNUE_OUTPUT_BUCKETS;
        for (int i = 0; i < 2 * NNUE_HIDDEN; i++) {
            nnue_net.out_weights[bucket][i] = (int16_t) (trial % 4 < 2 ? extreme
                    : (int) (next_random_32(&rng) % (2 * NNUE_OUT_WEIGHT_SMALL + 1)) - NNUE_OUT_WEIGHT_SMALL);
        }
        for (int c = white; c <= black; c++) {
            for (int i = 0; i < NNUE_HIDDEN; i++) {
                accumulator.values[c][i] = (int16_t) (trial % 3 == 0 ? NNUE_QA + (int) (next_random_32(&rng) % 4)
                                                                     : (int) (next_random_32(&rng) % 65536) - 32768);
            }
        }
        for (int side = white; side <= black; side++) {
            TEST_ASSERT_EQUAL_INT(nnue_output_scalar(&accumulator, side, bucket), nnue_output(&accumulator, side, bucket));
        }
    }
    nnue_free();
}

void test_nnue_load_sets_small_output() {
    /* Built in code: the int64 output, whatever the weights. */
    random_network(77);
    TEST_ASSERT_FALSE(nnue_net.small_output);
    size_t size;
    uint8_t* data = saved_network(&size);
    nnue_free();

    /* random_network's output weights are within +-127, the first of them set to each limit. */
    const size_t out_at = 8 + 3 * sizeof(uint32_t) + ((size_t) NNUE_INPUTS * NNUE_HIDDEN + NNUE_HIDDEN) * sizeof(int16_t);
    const int16_t limits[4] = { NNUE_OUT_WEIGHT_SMALL, -NNUE_OUT_WEIGHT_SMALL, NNUE_OUT_WEIGHT_SMALL + 1,
                                -NNUE_OUT_WEIGHT_SMALL - 1 };
    for (int i = 0; i < 4; i++) {
        memcpy(data + out_at, &limits[i], sizeof(int16_t));
        TEST_ASSERT_TRUE(nnue_load_memory(data, size, "limits"));
        TEST_ASSERT_EQUAL_INT16(limits[i], nnue_net.out_weights[0][0]);
        TEST_ASSERT_EQUAL(i < 2, nnue_net.small_output);
    }

    /* The last output weight counts too. */
    const int16_t large = NNUE_OUT_WEIGHT_SMALL + 1, small = 0;
    memcpy(data + out_at, &small, sizeof(int16_t));
    memcpy(data + out_at + (NNUE_OUTPUT_BUCKETS * 2 * NNUE_HIDDEN - 1) * sizeof(int16_t), &large, sizeof(int16_t));
    TEST_ASSERT_TRUE(nnue_load_memory(data, size, "last"));
    TEST_ASSERT_FALSE(nnue_net.small_output);

    TEST_ASSERT_TRUE(nnue_allocate());
    TEST_ASSERT_FALSE(nnue_net.small_output);
    free(data);
    nnue_free();
}

void test_nnue_refresh_matches_plain_sum() {
    /* The accumulator code (SIMD if built) against a plain sum of the feature rows. */
    random_network(2718);
    int features[NNUE_MAX_ACTIVE];
    for (int i = 0; i < PROPERTY_POSITION_COUNT; i++) {
        board_t board;
        parse_fen(property_position(i), &board);
        nnue_accumulator_t accumulator;
        nnue_refresh(&board, &accumulator);
        for (int c = white; c <= black; c++) {
            int n = nnue_active_features(&board, c, features);
            for (int j = 0; j < NNUE_HIDDEN; j++) {
                int sum = nnue_net.ft_bias[j];
                for (int f = 0; f < n; f++) sum += nnue_net.ft_weights[(size_t) features[f] * NNUE_HIDDEN + j];
                TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t) sum, accumulator.values[c][j], property_position(i));
            }
        }
    }
    nnue_free();
}

void test_nnue_cache_rebuilt_for_new_network() {
    static nnue_stack_t stack;
    nnue_stack_init(&stack);
    board_t board;
    parse_fen(positions[2], &board);

    /* Fill the cache with one network, then evaluate the same position with another. */
    random_network(1);
    nnue_reset(&stack);
    TEST_ASSERT_EQUAL_INT(nnue_evaluate(&board), nnue_evaluate_incremental(&stack, &board));
    random_network(2);
    nnue_reset(&stack);
    TEST_ASSERT_EQUAL_INT(nnue_evaluate(&board), nnue_evaluate_incremental(&stack, &board));
    TEST_ASSERT_EQUAL_UINT32(nnue_generation(), stack.cache_generation);
    nnue_free();
}

void test_nnue_setoption_evalfile() {
#ifdef _WIN32
    char path[] = "tiki_nnue_test_XXXXXX";      /* No /tmp, the current directory. */
#else
    char path[] = "/tmp/tiki_nnue_test_XXXXXX";
#endif
    int fd = mkstemp(path);
    TEST_ASSERT_TRUE(fd >= 0);
    close(fd);

    random_network(31337);
    board_t board;
    parse_fen(positions[1], &board);
    const int expected = nnue_evaluate(&board);
    TEST_ASSERT_TRUE(nnue_save(path));
    load_material_network();
    TEST_ASSERT_NOT_EQUAL(expected, nnue_evaluate(&board));

    char command[128];
    snprintf(command, sizeof(command), "setoption name EvalFile value %s", path);
    parse_setoption(command);
    TEST_ASSERT_EQUAL_STRING(path, nnue_loaded_path());
    TEST_ASSERT_EQUAL_INT(expected, nnue_evaluate(&board));

    /* A bad path keeps the network. */
    parse_setoption("setoption name EvalFile value /nonexistent/tiki.nnue");
    TEST_ASSERT_EQUAL_STRING(path, nnue_loaded_path());
    TEST_ASSERT_EQUAL_INT(expected, nnue_evaluate(&board));
    unlink(path);
}

void test_material_network_values() {
    /* The tests' material network (setUp), close to the usual piece values for the side to move. */
    static const struct { const char* fen; int centipawns; } cases[] = {
            { START_FEN, 0 },
            { "rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 900 },     /* Black has no queen. */
            { "rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1", -900 },
            { "4k3/8/8/8/8/8/P7/4K3 w - - 0 1", 100 },
            { "4k3/8/8/8/8/8/8/R3K3 w - - 0 1", 500 },
            { "4k3/8/8/8/8/8/8/N3K3 b - - 0 1", -300 },
    };
    for (int i = 0; i < (int) (sizeof(cases) / sizeof(cases[0])); i++) {
        board_t board;
        parse_fen(cases[i].fen, &board);
        const int eval = nnue_evaluate(&board);
        /* SCReLU isn't quite linear, within 10%. */
        TEST_ASSERT_TRUE_MESSAGE(abs(eval - cases[i].centipawns) <= abs(cases[i].centipawns) / 10 + 2, cases[i].fen);
    }
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
