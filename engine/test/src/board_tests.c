#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "board_tests.h"

#include "../../core/src/types.h"
#include "../../core/src/board.h"
#include "../../core/src/attack_mask.h"
#include "../../core/src/move_generator.h"
#include "../../core/src/uci.h"


void test_parse_fen_fields() {
    board_t board;
    parse_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/Pp2P3/2N2Q1p/1PPBBPPP/R3K2R b KQq a3 12 1", &board);

    TEST_ASSERT_EQUAL_INT(black, board.side);
    TEST_ASSERT_EQUAL_INT(white_king_side | white_queen_side | black_queen_side, board.castle_flag);
    TEST_ASSERT_EQUAL_INT(a3, board.en_passant);
    TEST_ASSERT_EQUAL_INT(12, board.fifty_move);

    TEST_ASSERT_TRUE(is_bit_set(&board.pieces[K], e1));
    TEST_ASSERT_TRUE(is_bit_set(&board.pieces[k], e8));
    TEST_ASSERT_TRUE(is_bit_set(&board.pieces[Q], f3));
    TEST_ASSERT_TRUE(is_bit_set(&board.pieces[p], b4));
    TEST_ASSERT_EQUAL_INT(8, pop_count(board.pieces[P]));
    TEST_ASSERT_EQUAL_INT(16, pop_count(board.occupancy[white]));
    TEST_ASSERT_EQUAL_INT(16, pop_count(board.occupancy[black]));
    TEST_ASSERT_EQUAL_UINT64(board.occupancy[white] | board.occupancy[black], board.occupancy[both]);
    TEST_ASSERT_EQUAL_UINT64(compute_hash(&board), board.hash);

    /* No castling, no en passant. */
    parse_fen("8/8/8/8/8/8/6k1/4K3 w - - 0 1", &board);
    TEST_ASSERT_EQUAL_INT(white, board.side);
    TEST_ASSERT_EQUAL_INT(0, board.castle_flag);
    TEST_ASSERT_EQUAL_INT(none_sq, board.en_passant);
    TEST_ASSERT_EQUAL_INT(2, pop_count(board.occupancy[both]));
}

void test_parse_move_promotions() {
    board_t board;
    parse_fen("8/P6k/8/8/8/8/p6K/8 w - - 0 1", &board);

    move_t queen = parse_move("a7a8q", &board);
    move_t knight = parse_move("a7a8n", &board);
    TEST_ASSERT_NOT_EQUAL(0, queen);
    TEST_ASSERT_NOT_EQUAL(0, knight);
    TEST_ASSERT_EQUAL_INT(Q, get_promoted_piece(queen));
    TEST_ASSERT_EQUAL_INT(N, get_promoted_piece(knight));

    /* A promotion needs the piece. */
    TEST_ASSERT_EQUAL_UINT32(0, parse_move("a7a8", &board));

    TEST_ASSERT_TRUE(make_move(&board, queen));
    TEST_ASSERT_TRUE(is_bit_set(&board.pieces[Q], a8));
    TEST_ASSERT_FALSE(is_bit_set(&board.pieces[P], a8));

    /* Black, lower case piece. */
    move_t rook = parse_move("a2a1r", &board);
    TEST_ASSERT_EQUAL_INT(r, get_promoted_piece(rook));
}

void test_parse_move_castling() {
    board_t board;
    parse_fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", &board);

    move_t king_side = parse_move("e1g1", &board);
    TEST_ASSERT_TRUE(get_king_side_castle_flag(king_side));
    TEST_ASSERT_TRUE(make_move(&board, king_side));
    TEST_ASSERT_TRUE(is_bit_set(&board.pieces[K], g1));
    TEST_ASSERT_TRUE(is_bit_set(&board.pieces[R], f1));
    TEST_ASSERT_EQUAL_INT(black_king_side | black_queen_side, board.castle_flag);

    move_t queen_side = parse_move("e8c8", &board);
    TEST_ASSERT_TRUE(get_queen_side_castle_flag(queen_side));
    TEST_ASSERT_TRUE(make_move(&board, queen_side));
    TEST_ASSERT_TRUE(is_bit_set(&board.pieces[k], c8));
    TEST_ASSERT_TRUE(is_bit_set(&board.pieces[r], d8));
    TEST_ASSERT_EQUAL_INT(0, board.castle_flag);
}

void test_parse_move_rejects_invalid() {
    board_t board;
    parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board);
    TEST_ASSERT_EQUAL_UINT32(0, parse_move("e2e5", &board));
    TEST_ASSERT_EQUAL_UINT32(0, parse_move("e7e5", &board)); /* Black's move, white to play. */
    TEST_ASSERT_NOT_EQUAL(0, parse_move("e2e4", &board));
}


/* Reference slider attacks, walk each ray until the edge or the first blocker (included). */
static bitboard ray_attacks(square s, bitboard occupancy, const int directions[4][2]) {
    bitboard attacks = 0ULL;
    for (int d = 0; d < 4; d++) {
        int rank = s / 8 + directions[d][0];
        int file = s % 8 + directions[d][1];
        while (rank >= 0 && rank < 8 && file >= 0 && file < 8) {
            bitboard bit = 1ULL << (rank * 8 + file);
            attacks |= bit;
            if (occupancy & bit) break;
            rank += directions[d][0];
            file += directions[d][1];
        }
    }
    return attacks;
}

static uint64_t next_random(uint64_t* state) {
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

void test_slider_attacks_match_reference() {
    static const int rook_directions[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    static const int bishop_directions[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    uint64_t state = 0x9e3779b97f4a7c15ULL;

    /* rook_attack/bishop_attack are the PEXT or magic lookups, whichever this build uses. */
    for (int sq = 0; sq < 64; sq++) {
        for (int i = 0; i < 500; i++) {
            /* Mix sparse and dense boards. */
            bitboard occupancy = next_random(&state);
            if (i % 2) occupancy &= next_random(&state);
            if (i % 3) occupancy &= next_random(&state);

            TEST_ASSERT_EQUAL_UINT64(ray_attacks(sq, occupancy, rook_directions), rook_attack(sq, occupancy));
            TEST_ASSERT_EQUAL_UINT64(ray_attacks(sq, occupancy, bishop_directions), bishop_attack(sq, occupancy));
            TEST_ASSERT_EQUAL_UINT64(ray_attacks(sq, occupancy, rook_directions), magic_rook_attack(sq, occupancy));
            TEST_ASSERT_EQUAL_UINT64(ray_attacks(sq, occupancy, bishop_directions), magic_bishop_attack(sq, occupancy));
        }
    }
}


static int compare_moves(const void* a, const void* b) {
    move_t x = *(const move_t*) a, y = *(const move_t*) b;
    return (x > y) - (x < y);
}

/* What generate_captures should produce: captures and promotions, promotions to a queen only. */
static bool is_quiescence_move(move_t mv) {
    piece promoted = get_promoted_piece(mv);
    if (promoted) return promoted == Q || promoted == q;
    return get_capture_flag(mv);
}

/* Walks the tree, at each node compares generate_captures with generate_moves filtered. */
static int capture_mismatches(board_t* board, int depth) {
    move_buffer_t all, captures;
    move_t expected[MOVE_BUFFER_SIZE];
    int n = 0;

    generate_moves(board, &all);
    generate_captures(board, &captures);
    for (int i = 0; i < all.index; i++) {
        if (is_quiescence_move(all.moves[i])) expected[n++] = all.moves[i];
    }

    int mismatches = 0;
    if (n != captures.index) {
        mismatches++;
    } else {
        qsort(expected, n, sizeof(move_t), compare_moves);
        qsort(captures.moves, captures.index, sizeof(move_t), compare_moves);
        if (memcmp(expected, captures.moves, n * sizeof(move_t)) != 0) mismatches++;
    }

    if (depth > 1) {
        for (int i = 0; i < all.index; i++) {
            if (make_move(board, all.moves[i])) mismatches += capture_mismatches(board, depth - 1);
            pop_move(board);
        }
    }
    return mismatches;
}

void test_generate_captures_matches_filtered_moves() {
    /* Both sides, en passant, promotions with and without capture, castling (excluded). */
    static const char* const positions[] = {
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/Pp2P3/2N2Q1p/1PPBBPPP/R3K2R b KQkq a3 0 1",
            "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
            "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
            "n1n5/PPPk4/8/8/8/8/4Kppp/5N1N b - - 0 1",
            "n1n5/PPPk4/8/8/8/8/4Kppp/5N1N w - - 0 1",
            "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3",
    };
    for (size_t i = 0; i < sizeof(positions) / sizeof(positions[0]); i++) {
        board_t board;
        parse_fen(positions[i], &board);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, capture_mismatches(&board, 3), positions[i]);
    }
}

void test_generate_captures_promotions() {
    board_t board;
    move_buffer_t buffer;

    /* Quiet promotion and a capture promotion, one queen move each. */
    parse_fen("1n5k/P7/8/8/8/8/8/K7 w - - 0 1", &board);
    generate_captures(&board, &buffer);
    TEST_ASSERT_EQUAL_INT(2, buffer.index);
    for (int i = 0; i < buffer.index; i++) {
        TEST_ASSERT_EQUAL_INT(Q, get_promoted_piece(buffer.moves[i]));
    }

    /* No captures or promotions, nothing generated. */
    parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board);
    generate_captures(&board, &buffer);
    TEST_ASSERT_EQUAL_INT(0, buffer.index);
}
