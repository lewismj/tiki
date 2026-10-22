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
