#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "board_tests.h"

#include "../../core/src/types.h"
#include "../../core/src/board.h"
#include "../../core/src/attack_mask.h"
#include "../../core/src/move_generator.h"
#include "../../core/src/uci.h"
#include "../../core/src/see.h"
#include "test_positions.h"


/* The en passant square is kept only when a pawn could take there (en_passant_capturable), by
 * make_move and by parse_fen, so the same position always has the same hash. */
void test_en_passant_only_when_capturable() {
    board_t board, plain;
    search_state_t search_state;
    init_search_state(&search_state);

    /* 1. e4: no black pawn beside it, no en passant square, and the hash is the position's own. */
    parse_position("position startpos moves e2e4", &board, &search_state);
    TEST_ASSERT_EQUAL_INT(none_sq, board.en_passant);
    TEST_ASSERT_EQUAL_UINT64(compute_hash(&board), board.hash);
    parse_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1", &plain);
    TEST_ASSERT_EQUAL_UINT64(plain.hash, board.hash);
    /* A FEN that names the square anyway is read as the same position. */
    parse_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1", &plain);
    TEST_ASSERT_EQUAL_INT(none_sq, plain.en_passant);
    TEST_ASSERT_EQUAL_UINT64(board.hash, plain.hash);

    /* With a black pawn on d4 the square is kept (and by a FEN), and undone with the move. */
    parse_fen("rnbqkbnr/ppp1pppp/8/8/3p4/8/PPPPPPPP/RNBQKBNR w KQkq - 0 3", &board);
    const uint64_t before = board.hash;
    TEST_ASSERT_TRUE(make_move(&board, parse_move("e2e4", &board)));
    TEST_ASSERT_EQUAL_INT(e3, board.en_passant);
    TEST_ASSERT_EQUAL_UINT64(compute_hash(&board), board.hash);
    parse_fen("rnbqkbnr/ppp1pppp/8/8/3pP3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 3", &plain);
    TEST_ASSERT_EQUAL_INT(e3, plain.en_passant);
    TEST_ASSERT_EQUAL_UINT64(plain.hash, board.hash);
    pop_move(&board);
    TEST_ASSERT_EQUAL_INT(none_sq, board.en_passant);
    TEST_ASSERT_EQUAL_UINT64(before, board.hash);

    /* Black's double push, a white pawn on e5 beside it. */
    parse_fen("rnbqkbnr/pppppppp/8/4P3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2", &board);
    TEST_ASSERT_TRUE(make_move(&board, parse_move("d7d5", &board)));
    TEST_ASSERT_EQUAL_INT(d6, board.en_passant);
    pop_move(&board);
    TEST_ASSERT_TRUE(make_move(&board, parse_move("a7a5", &board)));
    TEST_ASSERT_EQUAL_INT(none_sq, board.en_passant);
    TEST_ASSERT_EQUAL_UINT64(compute_hash(&board), board.hash);
}

/* Positions for the tests of the functions that answer for a move without making it: the test
 * positions, and ones where the answer depends on castling, en passant or a promotion. */
static const char* const move_property_positions[] = {
    "5k2/8/8/8/8/8/8/4K2R w K - 0 1",              /* O-O checks along the f-file. */
    "3k4/8/8/8/8/8/8/R3K3 w Q - 0 1",              /* O-O-O checks along the d-file. */
    "r3k3/8/8/8/8/8/8/3K4 b q - 0 1",              /* Black's O-O-O. */
    "4k3/8/8/3pP3/8/8/8/4RK2 w - d6 0 1",          /* En passant, the capturing pawn uncovers the rook. */
    "7k/8/8/4pP2/8/8/8/B3K3 w - e6 0 1",           /* En passant, the captured pawn uncovers the bishop. */
    "8/8/8/8/k2pP2R/8/8/4K3 b - e3 0 1",           /* En passant that would leave the mover's king in check. */
    "3k4/P7/8/8/8/8/8/4K3 w - - 0 1",              /* Promotions: queen and rook check, bishop and knight don't. */
    "8/8/8/8/8/2k5/1p6/R3K3 b - - 0 1",            /* Black promotes with and without a capture. */
    "4k3/8/8/8/4N3/8/8/4RK2 w - - 0 1",            /* Discovered checks by the knight, and a double check. */
    "4k3/4r3/8/8/8/8/4B3/4K3 w - - 0 1",           /* A pinned bishop: no move of it is legal. */
    "4k3/8/8/8/1b6/8/3N4/4K3 w - - 0 1",           /* A pinned knight has no legal move. */
    "4k3/4r3/8/8/8/4R3/8/4K3 w - - 0 1",           /* A pinned rook: only moves along the file are legal. */
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
};
#define MOVE_PROPERTY_POSITION_COUNT ((int) (sizeof(move_property_positions) / sizeof(move_property_positions[0])))

static const char* move_property_position(int i) {
    return i < TEST_POSITION_COUNT ? test_positions[i] : move_property_positions[i - TEST_POSITION_COUNT];
}

/* Whether the side to move is in check. */
static bool to_move_in_check(board_t* board) {
    return board->side == white ? is_square_attacked_by_black(board, trailing_zero_count(board->pieces[K]))
                                : is_square_attacked_by_white(board, trailing_zero_count(board->pieces[k]));
}

static bool is_special_move(move_t mv) {
    return get_enpassant_flag(mv) || get_promoted_piece(mv) || get_king_side_castle_flag(mv) ||
           get_queen_side_castle_flag(mv);
}

/* move_gives_check against making the move and testing the king, for every legal move of every
 * position and of the positions a move further on (which adds positions in check and after
 * captures). */
void test_move_gives_check_matches_make() {
    static board_t board;
    int moves = 0, checks = 0, special_checks = 0;
    for (int i = 0; i < TEST_POSITION_COUNT + MOVE_PROPERTY_POSITION_COUNT; i++) {
        parse_fen(move_property_position(i), &board);
        move_buffer_t buffer;
        generate_moves(&board, &buffer);
        for (int m = 0; m < buffer.index; m++) {
            const move_t mv = buffer.moves[m];
            const bool predicted = move_gives_check(&board, mv);
            if (make_move(&board, mv)) {
                const bool actual = to_move_in_check(&board);
                TEST_ASSERT_EQUAL_INT_MESSAGE(actual, predicted, move_property_position(i));
                moves++;
                checks += actual;
                special_checks += actual && is_special_move(mv);
                move_buffer_t replies;
                generate_moves(&board, &replies);
                for (int r = 0; r < replies.index; r++) {
                    const bool reply_predicted = move_gives_check(&board, replies.moves[r]);
                    if (make_move(&board, replies.moves[r])) {
                        TEST_ASSERT_EQUAL_INT_MESSAGE(to_move_in_check(&board), reply_predicted,
                                                      move_property_position(i));
                        moves++;
                    }
                    pop_move(&board);
                }
            }
            pop_move(&board);
        }
    }
    /* The comparison saw the cases it is there for. */
    TEST_ASSERT_TRUE(moves > 50000);
    TEST_ASSERT_TRUE(checks > 100);
    TEST_ASSERT_TRUE(special_checks >= 6);
}

/* Compares quiet_move_is_legal with make_move over the position's quiet moves of pieces other than
 * the king (the moves the function is for). The position must not be in check. */
static void check_quiet_legality(board_t* board, const char* fen, int* moves, int* illegal) {
    move_buffer_t buffer;
    generate_moves(board, &buffer);
    for (int m = 0; m < buffer.index; m++) {
        const move_t mv = buffer.moves[m];
        const piece moved = get_piece_moved(mv);
        if (get_capture_flag(mv) || get_promoted_piece(mv) || moved == K || moved == k) continue;
        const bool predicted = quiet_move_is_legal(board, mv);
        const bool legal = make_move(board, mv);
        pop_move(board);
        TEST_ASSERT_EQUAL_INT_MESSAGE(legal, predicted, fen);
        (*moves)++;
        *illegal += !legal;
    }
}

/* quiet_move_is_legal against make_move, in every position that isn't in check and every such
 * position a move further on. */
void test_quiet_move_is_legal_matches_make() {
    static board_t board;
    int moves = 0, illegal = 0;
    for (int i = 0; i < TEST_POSITION_COUNT + MOVE_PROPERTY_POSITION_COUNT; i++) {
        const char* fen = move_property_position(i);
        parse_fen(fen, &board);
        if (!to_move_in_check(&board)) check_quiet_legality(&board, fen, &moves, &illegal);
        move_buffer_t buffer;
        generate_moves(&board, &buffer);
        for (int m = 0; m < buffer.index; m++) {
            if (make_move(&board, buffer.moves[m]) && !to_move_in_check(&board)) {
                check_quiet_legality(&board, fen, &moves, &illegal);
            }
            pop_move(&board);
        }
    }
    TEST_ASSERT_TRUE(moves > 30000);
    TEST_ASSERT_TRUE(illegal > 20);   /* Pinned pieces leaving the line of the pin. */
}

/* hash_after_move is the hash make_move gives, for the moves it is exact for: no promotion, castling
 * or en passant capture, the castling rights unchanged and no en passant square left behind. */
void test_hash_after_move_matches_make() {
    static board_t board;
    int exact = 0, captures = 0, cleared_en_passant = 0;
    for (int i = 0; i < TEST_POSITION_COUNT + MOVE_PROPERTY_POSITION_COUNT; i++) {
        parse_fen(move_property_position(i), &board);
        move_buffer_t buffer;
        generate_moves(&board, &buffer);
        for (int m = 0; m < buffer.index; m++) {
            const move_t mv = buffer.moves[m];
            const uint64_t predicted = hash_after_move(&board, mv);
            const int castle_before = board.castle_flag;
            const bool had_en_passant = board.en_passant != none_sq;
            const bool legal = make_move(&board, mv);
            const bool simple = !is_special_move(mv) && board.castle_flag == castle_before &&
                                board.en_passant == none_sq;
            if (legal && simple) {
                TEST_ASSERT_EQUAL_UINT64_MESSAGE(board.hash, predicted, move_property_position(i));
                exact++;
                captures += get_capture_flag(mv) != 0;
                cleared_en_passant += had_en_passant;
            }
            pop_move(&board);
        }
    }
    TEST_ASSERT_TRUE(exact > 2000);
    TEST_ASSERT_TRUE(captures > 100);
    TEST_ASSERT_TRUE(cleared_en_passant > 0);
}

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

void test_move_to_uci() {
    board_t board;
    char uci[6];
    parse_fen("8/P6k/8/8/8/8/p6K/8 w - - 0 1", &board);
    TEST_ASSERT_EQUAL_STRING("h2g3", move_to_uci(parse_move("h2g3", &board), uci));

    /* Promotions are lower case for both colours (GUIs reject "a7a8Q" as an illegal move). */
    TEST_ASSERT_EQUAL_STRING("a7a8q", move_to_uci(parse_move("a7a8q", &board), uci));
    TEST_ASSERT_EQUAL_STRING("a7a8n", move_to_uci(parse_move("a7a8n", &board), uci));
    parse_fen("8/P6k/8/8/8/8/p6K/8 b - - 0 1", &board);
    TEST_ASSERT_EQUAL_STRING("a2a1r", move_to_uci(parse_move("a2a1r", &board), uci));
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


static int see_of(const char* fen, const char* move) {
    board_t board;
    parse_fen(fen, &board);
    move_t mv = parse_move(move, &board);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, mv, move);
    return see(&board, mv);
}

void test_see() {
    /* Undefended pawn. */
    TEST_ASSERT_EQUAL_INT(100, see_of("4k3/8/8/3p4/8/8/8/3QK3 w - - 0 1", "d1d5"));
    /* Queen takes a pawn defended by a pawn. */
    TEST_ASSERT_EQUAL_INT(100 - 900, see_of("4k3/8/2p5/3p4/8/8/8/3QK3 w - - 0 1", "d1d5"));
    /* Pawn takes a knight defended by a pawn. */
    TEST_ASSERT_EQUAL_INT(320 - 100, see_of("4k3/8/2p5/3n4/4P3/8/8/4K3 w - - 0 1", "e4d5"));
    /* Doubled rooks, the second rook (x-ray) makes the recapture lose, so black doesn't take. */
    TEST_ASSERT_EQUAL_INT(100, see_of("3rk3/8/8/3p4/8/8/3R4/3RK3 w - - 0 1", "d2d5"));
    /* Without the second rook black recaptures. */
    TEST_ASSERT_EQUAL_INT(100 - 500, see_of("3rk3/8/8/3p4/8/8/3R4/4K3 w - - 0 1", "d2d5"));
    /* The black king can't recapture, the bishop on b2 covers d4. */
    TEST_ASSERT_EQUAL_INT(100, see_of("8/8/8/4k3/3p4/8/1B6/3RK3 w - - 0 1", "d1d4"));
    /* En passant, the target square is empty. */
    TEST_ASSERT_EQUAL_INT(100, see_of("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6"));
    /* Black to move, rook takes a knight defended by a pawn. */
    TEST_ASSERT_EQUAL_INT(320 - 500, see_of("4k3/3r4/8/8/3N4/4P3/8/4K3 b - - 0 1", "d7d4"));
}

/* see_ge(t) == (see() >= t) for every move of 'board', thresholds around see() and across the range. */
static void check_see_ge(const board_t* board, int* checked) {
    move_buffer_t buffer;
    buffer.index = 0;
    generate_moves((board_t*) board, &buffer);
    for (int m = 0; m < buffer.index; m++) {
        const move_t mv = buffer.moves[m];
        const int value = see(board, mv);
        for (int t = -2000; t <= 2000; t += 50) {
            TEST_ASSERT_EQUAL_INT_MESSAGE(value >= t, see_ge(board, mv, t), "see_ge against see over the range");
        }
        for (int t = value - 1; t <= value + 1; t++) {
            TEST_ASSERT_EQUAL_INT_MESSAGE(value >= t, see_ge(board, mv, t), "see_ge against see at its value");
        }
        (*checked)++;
    }
}

void test_see_ge_matches_see() {
    /* The test_see positions and some with long exchanges, x-rays, kings and en passant, then the
     * shared test_positions, plus every position a move away from each. */
    static const char* const fens[] = {
            "4k3/8/8/3p4/8/8/8/3QK3 w - - 0 1",
            "3rk3/8/8/3p4/8/8/3R4/3RK3 w - - 0 1",
            "8/8/8/4k3/3p4/8/1B6/3RK3 w - - 0 1",
            "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/Pp2P3/2N2Q1p/1PPBBPPP/R3K2R w KQkq - 0 1",
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/Pp2P3/2N2Q1p/1PPBBPPP/R3K2R b KQkq a3 0 1",
            "1k1r3q/1ppn3p/p4b2/4p3/8/P2N2P1/1PP1R1BP/2K1Q3 w - - 0 1",
            "1k1r4/1pp4p/p7/4p3/8/P5P1/1PP4P/2K1R3 w - - 0 1",
            "2r1r1k1/pp1bppbp/3p1np1/q3P3/2P2P2/1P2B3/P1N1B1PP/2RQ1RK1 b - - 0 1",
            "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 0 1",
            "3q2k1/pb3p1p/4pbp1/2r5/PpN2N2/1P2P2P/5PP1/Q2R2K1 b - - 0 1",
            "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
            "rnbqkb1r/ppp1pppp/5n2/3p4/2PP4/2N5/PP2PPPP/R1BQKBNR b KQkq - 0 1",
            "6k1/1b4pp/8/3q4/3R4/3Q4/3R4/6K1 b - - 0 1",
    };
    const char* all_fens[sizeof(fens) / sizeof(fens[0]) + TEST_POSITION_COUNT];
    size_t count = 0;
    for (size_t i = 0; i < sizeof(fens) / sizeof(fens[0]); i++) all_fens[count++] = fens[i];
    for (int i = 0; i < TEST_POSITION_COUNT; i++) all_fens[count++] = test_positions[i];

    int checked = 0;
    for (size_t i = 0; i < count; i++) {
        board_t board;
        parse_fen(all_fens[i], &board);
        check_see_ge(&board, &checked);

        move_buffer_t buffer;
        buffer.index = 0;
        generate_moves(&board, &buffer);
        for (int m = 0; m < buffer.index; m++) {
            /* An illegal move is made too, and undone with pop_move all the same. */
            if (make_move(&board, buffer.moves[m])) check_see_ge(&board, &checked);
            pop_move(&board);
        }
    }
    TEST_ASSERT_TRUE(checked > 80000);   /* 88769 moves. */
}

void test_board_to_fen_round_trip() {
    static const char* const fens[] = {
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
            "r3k2r/p1ppqpb1/bn2pnp1/3PN3/Pp2P3/2N2Q1p/1PPBBPPP/R3K2R b Kq a3 12 30",
            "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 11",
            "4k3/8/8/8/8/8/8/4K2R w K - 99 70",
    };
    const int count = (int) (sizeof(fens) / sizeof(fens[0]));
    char fen[128];
    for (int i = 0; i < count + TEST_POSITION_COUNT; i++) {
        const char* input = i < count ? fens[i] : test_positions[i - count];
        board_t board;
        parse_fen(input, &board);
        /* The full move number isn't on the board, take it from the input. */
        const char* fullmove = strrchr(input, ' ') + 1;
        board_to_fen(&board, atoi(fullmove), fen, sizeof(fen));
        TEST_ASSERT_EQUAL_STRING(input, fen);
    }
}

/* generate_noisy's moves: captures (any promotion) and queen promotions without a capture. */
static bool is_noisy_move(move_t mv) {
    if (get_capture_flag(mv)) return true;
    const piece promoted = get_promoted_piece(mv);
    return promoted == Q || promoted == q;
}

/* Walks the tree, at each node checks that generate_noisy then generate_quiets give generate_moves'
 * moves, each once, the noisy ones first. */
static int staged_mismatches(board_t* board, int depth) {
    move_buffer_t all, staged;
    generate_moves(board, &all);
    generate_noisy(board, &staged);
    const int noisy = staged.index;
    generate_quiets(board, &staged);

    int mismatches = 0;
    for (int i = 0; i < staged.index; i++) {
        if (is_noisy_move(staged.moves[i]) != (i < noisy)) mismatches++;
    }
    if (staged.index != all.index) {
        mismatches++;
    } else {
        qsort(all.moves, all.index, sizeof(move_t), compare_moves);
        qsort(staged.moves, staged.index, sizeof(move_t), compare_moves);
        if (memcmp(all.moves, staged.moves, all.index * sizeof(move_t)) != 0) mismatches++;
    }

    if (depth > 1) {
        generate_moves(board, &all);
        for (int i = 0; i < all.index; i++) {
            if (make_move(board, all.moves[i])) mismatches += staged_mismatches(board, depth - 1);
            pop_move(board);
        }
    }
    return mismatches;
}

static const char* const staged_positions[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/Pp2P3/2N2Q1p/1PPBBPPP/R3K2R b KQkq a3 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "n1n5/PPPk4/8/8/8/8/4Kppp/5N1N b - - 0 1",
        "n1n5/PPPk4/8/8/8/8/4Kppp/5N1N w - - 0 1",
        "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3",
};
#define STAGED_POSITION_COUNT ((int) (sizeof(staged_positions) / sizeof(staged_positions[0])))

void test_staged_generation_matches_all_moves() {
    for (int i = 0; i < STAGED_POSITION_COUNT; i++) {
        board_t board;
        parse_fen(staged_positions[i], &board);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, staged_mismatches(&board, 3), staged_positions[i]);
    }
    for (int i = 0; i < TEST_POSITION_COUNT; i++) {
        board_t board;
        parse_fen(test_positions[i], &board);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, staged_mismatches(&board, 1), test_positions[i]);
    }
}

/* A pool of distinct moves, from many positions, to test move_is_pseudo_legal against. */
#define POOL_MAX 20000
static move_t pool[POOL_MAX];
static int pool_size;

static void add_to_pool(const move_buffer_t* buffer) {
    for (int i = 0; i < buffer->index && pool_size < POOL_MAX; i++) pool[pool_size++] = buffer->moves[i];
}

/* Collects every move generated in the positions and those one move on. */
static void collect_moves(board_t* board, int depth) {
    move_buffer_t all;
    generate_moves(board, &all);
    add_to_pool(&all);
    if (depth > 1) {
        for (int i = 0; i < all.index; i++) {
            if (make_move(board, all.moves[i])) collect_moves(board, depth - 1);
            pop_move(board);
        }
    }
}

/* At the position and those one move on: every pool move is pseudo-legal exactly when generate_moves
 * gives it. */
static int pseudo_legal_mismatches(board_t* board, int depth) {
    move_buffer_t all;
    generate_moves(board, &all);
    qsort(all.moves, all.index, sizeof(move_t), compare_moves);
    int mismatches = move_is_pseudo_legal(board, 0) ? 1 : 0;
    for (int i = 0; i < pool_size; i++) {
        const bool generated = bsearch(&pool[i], all.moves, all.index, sizeof(move_t), compare_moves) != NULL;
        if (move_is_pseudo_legal(board, pool[i]) != generated) mismatches++;
    }
    if (depth > 1) {
        move_buffer_t moves;
        generate_moves(board, &moves);
        for (int i = 0; i < moves.index; i++) {
            if (make_move(board, moves.moves[i])) mismatches += pseudo_legal_mismatches(board, depth - 1);
            pop_move(board);
        }
    }
    return mismatches;
}

void test_move_is_pseudo_legal() {
    pool_size = 0;
    for (int i = 0; i < STAGED_POSITION_COUNT; i++) {
        board_t board;
        parse_fen(staged_positions[i], &board);
        collect_moves(&board, 2);
    }
    for (int i = 0; i < TEST_POSITION_COUNT; i++) {
        board_t board;
        parse_fen(test_positions[i], &board);
        collect_moves(&board, 1);
    }
    /* Distinct moves only. */
    qsort(pool, pool_size, sizeof(move_t), compare_moves);
    int distinct = 0;
    for (int i = 0; i < pool_size; i++) {
        if (distinct == 0 || pool[distinct - 1] != pool[i]) pool[distinct++] = pool[i];
    }
    pool_size = distinct;
    TEST_ASSERT_TRUE(pool_size > 1000);

    for (int i = 0; i < STAGED_POSITION_COUNT; i++) {
        board_t board;
        parse_fen(staged_positions[i], &board);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, pseudo_legal_mismatches(&board, 2), staged_positions[i]);
    }
    for (int i = 0; i < TEST_POSITION_COUNT; i++) {
        board_t board;
        parse_fen(test_positions[i], &board);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, pseudo_legal_mismatches(&board, 1), test_positions[i]);
    }
}
