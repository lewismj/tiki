#include "unity.h"
#include "search_tests.h"

#include "../../core/src/board.h"
#include "../../core/src/limits.h"
#include "../../core/src/search_state.h"
#include "../../core/src/search.h"
#include "../../core/src/evaluation.h"
#include "../../core/src/transposition.h"
#include "../../core/src/uci.h"
#include "../../core/src/bench.h"
#include "../../core/src/see.h"


static void setup_position(const char* fen, board_t* board, search_state_t* search_state, limits_t* limits, int depth) {
    char command[256];
    snprintf(command, sizeof(command), "position fen %s", fen);
    tt_clear();
    init_search_state(search_state);
    parse_position(command, board, search_state);
    reset_time_control(limits);
    limits->depth = depth;
}

void test_search_mate_in_one() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Back rank, Rd8#. */
    setup_position("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1", &board, &search_state, &limits, 3);
    move_t mv = find_best_move(&board, &search_state, &limits);
    TEST_ASSERT_EQUAL_INT(d1, get_source_square(mv));
    TEST_ASSERT_EQUAL_INT(d8, get_target_square(mv));

    setup_position("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1", &board, &search_state, &limits, 3);
    TEST_ASSERT_EQUAL_INT(MATE_VALUE - 1, negamax(-INF, INF, 3, &board, &search_state, &limits));
}

void test_search_with_helpers() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Lazy SMP helpers set up their own tables (start_helpers copies only the game history), the
     * search still finds the mate... */
    setup_position("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1", &board, &search_state, &limits, 6);
    limits.threads = 4;
    move_t mv = find_best_move(&board, &search_state, &limits);
    TEST_ASSERT_EQUAL_INT(d1, get_source_square(mv));
    TEST_ASSERT_EQUAL_INT(d8, get_target_square(mv));

    /* ...and runs from a position with game history (knights out and back, twice). */
    tt_clear();
    init_search_state(&search_state);
    parse_position("position startpos moves g1f3 g8f6 f3g1 f6g8 g1f3 g8f6 f3g1 f6g8", &board, &search_state);
    reset_time_control(&limits);
    limits.depth = 6;
    limits.threads = 4;
    mv = find_best_move(&board, &search_state, &limits);
    TEST_ASSERT_TRUE(make_move(&board, mv));
}

void test_mvv_lva_range_matches_constants() {
    /* The ordering bands are checked at compile time with MVV_LVA_MIN / MAX, they must be the
     * table's actual range. */
    int lowest = INT32_MAX, highest = INT32_MIN;
    for (int attacker = 0; attacker < 12; attacker++) {
        for (int victim = 0; victim < 12; victim++) {
            const int v = search_const.mvv_lva[attacker][victim];
            if (v < lowest) lowest = v;
            if (v > highest) highest = v;
        }
    }
    TEST_ASSERT_EQUAL_INT(MVV_LVA_MIN, lowest);
    TEST_ASSERT_EQUAL_INT(MVV_LVA_MAX, highest);
}

void test_pv_flag_applies_to_one_node() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    move_buffer_t buffer;
    int scores[MOVE_BUFFER_SIZE];

    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 1);
    generate_moves(&board, &buffer);
    const move_t pv_move = parse_move("e2e4", &board);
    int index = -1;
    for (int i = 0; i < buffer.index; i++) {
        if (buffer.moves[i] == pv_move) index = i;
    }
    TEST_ASSERT_TRUE(index >= 0);

    /* The PV move is also the TT move, so it's scored as the TT move and the flag isn't used... */
    search_state.pv_table[0][0] = pv_move;
    search_state.score_pv = true;
    score_moves(&buffer, scores, pv_move, &board, &search_state);
    TEST_ASSERT_EQUAL_INT(TT_MOVE_SCORE, scores[index]);
    TEST_ASSERT_FALSE(search_state.score_pv);

    /* ...and the next node to score the same move at this ply doesn't get it as the PV move. */
    score_moves(&buffer, scores, 0, &board, &search_state);
    TEST_ASSERT_TRUE(scores[index] < KILLER_2_SCORE);

    /* Flagged and not the TT move, it's scored as the PV move. */
    search_state.score_pv = true;
    score_moves(&buffer, scores, 0, &board, &search_state);
    TEST_ASSERT_EQUAL_INT(PV_MOVE_SCORE, scores[index]);
    TEST_ASSERT_FALSE(search_state.score_pv);
}

void test_search_at_max_ply() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* A search reaching MAX_PLY (check extensions keep the depth up) returns the static eval
     * there, within the tables: the PV lengths stay inside the path. */
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 3);
    nnue_reset(&search_state.nnue);
    search_state.ply = MAX_PLY - 2;
    const uint64_t nodes = search_state.nodes;
    const int score = negamax(-INF, INF, 3, &board, &search_state, &limits);
    TEST_ASSERT_TRUE(score > -MATE_SCORE && score < MATE_SCORE);
    TEST_ASSERT_EQUAL_INT(MAX_PLY - 2, search_state.ply);
    TEST_ASSERT_TRUE(search_state.nodes > nodes && search_state.nodes < nodes + 10000);
    TEST_ASSERT_TRUE(search_state.pv_length[MAX_PLY - 2] <= MAX_PLY);
}

void test_search_tables_cleared_between_searches() {
    search_state_t search_state;
    init_search_state(&search_state);
    search_state.history_moves[N][f3] = 123;
    search_state.killer_moves[0][3] = 77;
    search_state.pv_table[0][0] = 88;

    /* Before each 'go': nothing learnt by the previous search is kept. */
    clear_pv_table(&search_state);
    TEST_ASSERT_EQUAL_INT(0, search_state.history_moves[N][f3]);
    TEST_ASSERT_EQUAL_UINT32(0, search_state.killer_moves[0][3]);
    TEST_ASSERT_EQUAL_UINT32(0, search_state.pv_table[0][0]);
}

void test_search_stalemate_root() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Black to move has no legal moves and isn't in check. */
    setup_position("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", &board, &search_state, &limits, 3);
    TEST_ASSERT_EQUAL_INT(0, negamax(-INF, INF, 3, &board, &search_state, &limits));
    TEST_ASSERT_EQUAL_UINT32(0, find_best_move(&board, &search_state, &limits)); /* 'bestmove 0000'. */
}

void test_search_checkmated_root() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    setup_position("R5k1/5ppp/8/8/8/8/5PPP/6K1 b - - 0 1", &board, &search_state, &limits, 3);
    TEST_ASSERT_EQUAL_INT(-MATE_VALUE, negamax(-INF, INF, 3, &board, &search_state, &limits));
    TEST_ASSERT_EQUAL_UINT32(0, find_best_move(&board, &search_state, &limits));
}

void test_bench_positions_are_legal() {
    for (int i = 0; i < bench_position_count; i++) {
        board_t board;
        parse_fen(bench_positions[i], &board);

        /* The side that just moved can't be in check, and the side to move has a move. */
        square king_sq = trailing_zero_count(board.pieces[board.side == white ? k : K]);
        bool attacked = board.side == white ? is_square_attacked_by_white(&board, king_sq)
                                            : is_square_attacked_by_black(&board, king_sq);
        TEST_ASSERT_FALSE_MESSAGE(attacked, bench_positions[i]);

        move_buffer_t buffer;
        generate_moves(&board, &buffer);
        bool has_legal_move = false;
        for (int j = 0; j < buffer.index && !has_legal_move; j++) {
            has_legal_move = make_move(&board, buffer.moves[j]);
            pop_move(&board);
        }
        TEST_ASSERT_TRUE_MESSAGE(has_legal_move, bench_positions[i]);
    }
}

void test_bench_is_deterministic() {
    const size_t tt_entries = tt_size;

    uint64_t first = bench(4);
    uint64_t second = bench(4);
    TEST_ASSERT_TRUE(first > 0);
    TEST_ASSERT_EQUAL_UINT64(first, second);

    /* The TT size is restored after the run. */
    TEST_ASSERT_EQUAL_size_t(tt_entries, tt_size);
}

void test_time_checked_once_per_2048_nodes() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    setup_position("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                   &board, &search_state, &limits, 10);
    find_best_move(&board, &search_state, &limits);

    /* Used to also check before counting, several times on the same count. */
    TEST_ASSERT_TRUE(search_state.nodes > 10 * 2048);
    TEST_ASSERT_EQUAL_UINT64(search_state.nodes / 2048, limits.time_checks);
}

void test_time_limit_stops_at_first_check() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Already out of time, the first check (at 2048 nodes) stops the search. */
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 10);
    limits.time_set = true;
    limits.stop_time = 0;
    limits.soft_stop_time = UINT64_MAX; /* Only the hard limit, the soft one would stop after depth 1. */

    move_t mv = find_best_move(&board, &search_state, &limits);
    TEST_ASSERT_TRUE(limits.stop_search_flag);
    TEST_ASSERT_EQUAL_UINT64(1, limits.time_checks);
    TEST_ASSERT_TRUE(search_state.nodes >= 2048 && search_state.nodes < 2 * 2048);
    TEST_ASSERT_TRUE(make_move(&board, mv));
}

void test_pick_next_move_order() {
    move_buffer_t buffer;
    int scores[MOVE_BUFFER_SIZE];
    static const int input[] = { 5, 100, 0, 9000, 5, 20000, -3, 100 };
    const int n = (int) (sizeof(input) / sizeof(input[0]));

    /* Move i is just i + 1, so the picked moves can be checked against their scores. */
    buffer.index = n;
    for (int i = 0; i < n; i++) {
        buffer.moves[i] = (move_t) (i + 1);
        scores[i] = input[i];
    }

    bool seen[16] = { false };
    int previous = 1 << 30;
    for (int i = 0; i < n; i++) {
        move_t mv = pick_next_move(&buffer, scores, i);
        TEST_ASSERT_EQUAL_INT(input[mv - 1], scores[i]); /* Score still belongs to its move. */
        TEST_ASSERT_TRUE(scores[i] <= previous);         /* Best first. */
        TEST_ASSERT_FALSE(seen[mv]);                     /* Each move once. */
        seen[mv] = true;
        previous = scores[i];
    }
    TEST_ASSERT_EQUAL_UINT32(6, buffer.moves[0]); /* 20000 */
    TEST_ASSERT_EQUAL_UINT32(4, buffer.moves[1]); /* 9000  */
}

void test_quiescence_sees_quiet_queen_promotion() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* No captures, only a7a8=Q. The old qsearch skipped non-captures, so it stood pat. */
    setup_position("8/P7/8/8/8/8/k7/7K w - - 0 1", &board, &search_state, &limits, 1);
    const int stand_pat = evaluation(&board, &search_state.nnue);
    const int score = quiescence(-INF, INF, &board, &search_state, &limits);
    TEST_ASSERT_TRUE(score > stand_pat + 500);
}

void test_check_extended_at_horizon() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Mated, searched at depth 0. Without the extension this went to qsearch and stood pat. */
    setup_position("R5k1/5ppp/8/8/8/8/5PPP/6K1 b - - 0 1", &board, &search_state, &limits, 0);
    TEST_ASSERT_EQUAL_INT(-MATE_VALUE, negamax(-INF, INF, 0, &board, &search_state, &limits));

    /* Mate in 1 at depth 1: Rd8+ leaves the reply at depth 0, it must still see the mate. */
    setup_position("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1", &board, &search_state, &limits, 1);
    TEST_ASSERT_EQUAL_INT(MATE_VALUE - 1, negamax(-INF, INF, 1, &board, &search_state, &limits));
}

void test_null_move_guards() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    const int low_beta = -10000; /* Static eval is always above this. */

    /* Middlegame, pieces on both sides, not in check, one ply in. */
    setup_position("r1bqkb1r/pppp1ppp/2n2n2/4p3/4P3/2N2N2/PPPP1PPP/R1BQKB1R w KQkq - 4 4",
                   &board, &search_state, &limits, 5);
    search_state.ply = 1;
    search_state.repetition_index = 1;
    TEST_ASSERT_TRUE(can_null_move(&board, &search_state, 5, low_beta, evaluation(&board, &search_state.nnue), false, false));

    /* Depth, check and PV node. */
    TEST_ASSERT_FALSE(can_null_move(&board, &search_state, NULL_MOVE_DEPTH_BOUND - 1, low_beta, evaluation(&board, &search_state.nnue), false, false));
    TEST_ASSERT_FALSE(can_null_move(&board, &search_state, 5, low_beta, evaluation(&board, &search_state.nnue), true, false));
    TEST_ASSERT_FALSE(can_null_move(&board, &search_state, 5, low_beta, evaluation(&board, &search_state.nnue), false, true));

    /* Static eval below beta. */
    TEST_ASSERT_FALSE(can_null_move(&board, &search_state, 5, evaluation(&board, &search_state.nnue) + 1, evaluation(&board, &search_state.nnue), false, false));
    TEST_ASSERT_TRUE(can_null_move(&board, &search_state, 5, evaluation(&board, &search_state.nnue), evaluation(&board, &search_state.nnue), false, false));

    /* Root. */
    search_state.ply = 0;
    TEST_ASSERT_FALSE(can_null_move(&board, &search_state, 5, low_beta, evaluation(&board, &search_state.nnue), false, false));
    search_state.ply = 1;

    /* Straight after a null move, but not two plies after one. */
    search_state.null_move_index = 1;
    TEST_ASSERT_FALSE(can_null_move(&board, &search_state, 5, low_beta, evaluation(&board, &search_state.nnue), false, false));
    search_state.repetition_index = 2;
    TEST_ASSERT_TRUE(can_null_move(&board, &search_state, 5, low_beta, evaluation(&board, &search_state.nnue), false, false));
    search_state.null_move_index = 0;
    search_state.repetition_index = 1;

    /* Side to move has only king and pawns (the opponent's pieces don't matter). */
    setup_position("4k3/8/8/2r5/8/8/PPP5/4K3 w - - 0 1", &board, &search_state, &limits, 5);
    search_state.ply = 1;
    search_state.repetition_index = 1;
    TEST_ASSERT_FALSE(can_null_move(&board, &search_state, 5, low_beta, evaluation(&board, &search_state.nnue), false, false));
    setup_position("4k3/8/8/2r5/8/8/PPP5/4K3 b - - 0 1", &board, &search_state, &limits, 5);
    search_state.ply = 1;
    search_state.repetition_index = 1;
    TEST_ASSERT_TRUE(can_null_move(&board, &search_state, 5, low_beta, evaluation(&board, &search_state.nnue), false, false));
}

void test_history_bonus_and_bound() {
    search_state_t search_state;
    init_search_state(&search_state);
    const move_t mv = encode_move(g1, f3, N, 0, 0, 0, 0, 0, 0);
    const int32_t* h = &search_state.history_moves[N][f3];

    /* From zero the bonus is 16 * depth^2... */
    update_history(&search_state, mv, 3);
    TEST_ASSERT_EQUAL_INT(144, *h);

    /* ...capped at 1600 from depth 10. */
    search_state.history_moves[N][f3] = 0;
    update_history(&search_state, mv, 10);
    TEST_ASSERT_EQUAL_INT(1600, *h);
    search_state.history_moves[N][f3] = 0;
    update_history(&search_state, mv, 40);
    TEST_ASSERT_EQUAL_INT(1600, *h);

    /* Deep, long searches saturate at HISTORY_MAX instead of growing without bound. */
    for (int i = 0; i < 100000; i++) update_history(&search_state, mv, 40);
    TEST_ASSERT_TRUE(*h <= HISTORY_MAX);
    TEST_ASSERT_TRUE(*h > HISTORY_MAX * 9 / 10);

    /* Near the bound the same bonus adds less than it did from zero. */
    search_state.history_moves[N][f3] = HISTORY_MAX / 2;
    update_history(&search_state, mv, 10);
    TEST_ASSERT_EQUAL_INT(HISTORY_MAX / 2 + 1600 - (HISTORY_MAX / 2) * 1600 / HISTORY_MAX, *h);
}

void test_history_malus_and_bound() {
    search_state_t search_state;
    init_search_state(&search_state);
    const move_t mv = encode_move(g1, f3, N, 0, 0, 0, 0, 0, 0);
    const int32_t* h = &search_state.history_moves[N][f3];

    /* From zero the malus is 16 * depth^2, and a bonus of the same depth from there lands back
     * near zero (gravity adds 144 * 144 / HISTORY_MAX = 2 as it pulls toward the opposite sign). */
    penalise_history(&search_state, mv, 3);
    TEST_ASSERT_EQUAL_INT(-144, *h);
    update_history(&search_state, mv, 3);
    TEST_ASSERT_EQUAL_INT(144 * 144 / HISTORY_MAX, *h);

    /* Saturates at -HISTORY_MAX. */
    for (int i = 0; i < 100000; i++) penalise_history(&search_state, mv, 40);
    TEST_ASSERT_TRUE(*h >= -HISTORY_MAX);
    TEST_ASSERT_TRUE(*h < -HISTORY_MAX * 9 / 10);

    /* Near the bound the same malus takes off less than it did from zero. */
    search_state.history_moves[N][f3] = -HISTORY_MAX / 2;
    penalise_history(&search_state, mv, 10);
    TEST_ASSERT_EQUAL_INT(-HISTORY_MAX / 2 - 1600 + (HISTORY_MAX / 2) * 1600 / HISTORY_MAX, *h);
}

void test_penalised_quiet_stays_above_losing_captures() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* 1.e4 e5 2.Qh5 Nc6: Qxf7+ loses the queen to ...Kxf7 (SEE < 0). */
    setup_position("r1bqkbnr/pppp1ppp/2n5/4p2Q/4P3/8/PPPP1PPP/RNB1KBNR w KQkq - 2 3", &board, &search_state, &limits, 1);
    const move_t quiet = parse_move("g1f3", &board);
    const move_t losing_capture = parse_move("h5f7", &board);

    for (int i = 0; i < 100000; i++) penalise_history(&search_state, quiet, 40);
    TEST_ASSERT_TRUE(score_move(losing_capture, 0, &board, &search_state) < 0);
    TEST_ASSERT_TRUE(score_move(quiet, 0, &board, &search_state) >
                     score_move(losing_capture, 0, &board, &search_state));
}

void test_history_stays_below_killers_and_captures() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* 1.e4 d5, exd5 is a capture. */
    setup_position("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2", &board, &search_state, &limits, 1);
    const move_t quiet = parse_move("g1f3", &board);
    const move_t killer = parse_move("b1c3", &board);
    const move_t capture = parse_move("e4d5", &board);

    for (int i = 0; i < 100000; i++) update_history(&search_state, quiet, 40);
    search_state.killer_moves[1][0] = killer; /* Second killer, the lower of the two. */

    const int quiet_score = score_move(quiet, 0, &board, &search_state);
    const int killer_score = score_move(killer, 0, &board, &search_state);
    const int capture_score = score_move(capture, 0, &board, &search_state);
    TEST_ASSERT_TRUE(quiet_score < killer_score);
    TEST_ASSERT_TRUE(killer_score < capture_score);
}

void test_en_passant_scored_as_pawn_capture() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* White e5, black just played d7d5: exd6 e.p. and, for comparison, exf6 taking a pawn on f6. */
    setup_position("rnbqkbnr/ppp1p1pp/5p2/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3", &board, &search_state, &limits, 1);
    const move_t en_passant = parse_move("e5d6", &board);
    const move_t pawn_takes_pawn = parse_move("e5f6", &board);
    TEST_ASSERT_TRUE(get_enpassant_flag(en_passant));
    TEST_ASSERT_FALSE(get_enpassant_flag(pawn_takes_pawn));

    /* Used to score 0, below every quiet move with history. */
    TEST_ASSERT_EQUAL_INT(score_move(pawn_takes_pawn, 0, &board, &search_state),
                          score_move(en_passant, 0, &board, &search_state));

    /* Black side as well. */
    setup_position("rnbqkbnr/pppp1ppp/8/8/3Pp3/5P2/PPP3PP/RNBQKBNR b KQkq d3 0 3", &board, &search_state, &limits, 1);
    const move_t black_en_passant = parse_move("e4d3", &board);
    const move_t black_pawn_takes_pawn = parse_move("e4f3", &board);
    TEST_ASSERT_TRUE(get_enpassant_flag(black_en_passant));
    TEST_ASSERT_EQUAL_INT(score_move(black_pawn_takes_pawn, 0, &board, &search_state),
                          score_move(black_en_passant, 0, &board, &search_state));
}

void test_losing_capture_ordered_after_quiet_moves() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Qxd5 loses the queen for a pawn (c6 defends), Ke2 is a quiet move with no history. */
    setup_position("4k3/8/2p5/3p4/8/8/8/3QK3 w - - 0 1", &board, &search_state, &limits, 1);
    const int losing = score_move(parse_move("d1d5", &board), 0, &board, &search_state);
    const int quiet = score_move(parse_move("e1e2", &board), 0, &board, &search_state);
    TEST_ASSERT_TRUE(losing < quiet);

    /* Undefended, ordered as a normal capture. */
    setup_position("4k3/8/8/3p4/8/8/8/3QK3 w - - 0 1", &board, &search_state, &limits, 1);
    TEST_ASSERT_TRUE(score_move(parse_move("d1d5", &board), 0, &board, &search_state) >= 10000);
}

void test_reverse_futility_guards() {
    /* Eval 500 above beta 0 at depth 3: 500 - 3 * RFP_MARGIN >= 0. */
    TEST_ASSERT_TRUE(can_reverse_futility_prune(3, 500, 0, false, false, 1));
    /* Margin grows with depth. */
    TEST_ASSERT_FALSE(can_reverse_futility_prune(3, 3 * RFP_MARGIN - 1, 0, false, false, 1));
    TEST_ASSERT_TRUE(can_reverse_futility_prune(3, 3 * RFP_MARGIN, 0, false, false, 1));
    /* Too deep, in check, PV node, root, mate bound. */
    TEST_ASSERT_FALSE(can_reverse_futility_prune(RFP_MAX_DEPTH + 1, 5000, 0, false, false, 1));
    TEST_ASSERT_FALSE(can_reverse_futility_prune(3, 500, 0, true, false, 1));
    TEST_ASSERT_FALSE(can_reverse_futility_prune(3, 500, 0, false, true, 1));
    TEST_ASSERT_FALSE(can_reverse_futility_prune(3, 500, 0, false, false, 0));
    TEST_ASSERT_FALSE(can_reverse_futility_prune(3, MATE_VALUE, MATE_VALUE - 10, false, false, 1));
}

void test_futility_guards() {
    /* Eval 400 below alpha 0 at depth 2: -400 + 2 * FUTILITY_MARGIN <= 0. */
    TEST_ASSERT_TRUE(is_futile(2, -400, 0, false, false));
    TEST_ASSERT_FALSE(is_futile(2, -2 * FUTILITY_MARGIN + 1, 0, false, false));
    TEST_ASSERT_TRUE(is_futile(2, -2 * FUTILITY_MARGIN, 0, false, false));
    /* Too deep, in check, PV node, mate bound. */
    TEST_ASSERT_FALSE(is_futile(FUTILITY_MAX_DEPTH + 1, -5000, 0, false, false));
    TEST_ASSERT_FALSE(is_futile(2, -400, 0, true, false));
    TEST_ASSERT_FALSE(is_futile(2, -400, 0, false, true));
    TEST_ASSERT_FALSE(is_futile(2, -MATE_VALUE, -MATE_VALUE + 10, false, false));
}

void test_late_move_pruning_guards() {
    /* Depth 2: pruned from the 3 + 4 = 7th move searched. */
    TEST_ASSERT_FALSE(can_late_move_prune(2, LMP_BASE + 3, 0, false, false));
    TEST_ASSERT_TRUE(can_late_move_prune(2, LMP_BASE + 4, 0, false, false));
    TEST_ASSERT_TRUE(can_late_move_prune(1, LMP_BASE + 1, 0, false, false));
    /* Too deep, in check, PV node, mate bound. */
    TEST_ASSERT_FALSE(can_late_move_prune(LMP_MAX_DEPTH + 1, 200, 0, false, false));
    TEST_ASSERT_FALSE(can_late_move_prune(2, 50, 0, true, false));
    TEST_ASSERT_FALSE(can_late_move_prune(2, 50, 0, false, true));
    TEST_ASSERT_FALSE(can_late_move_prune(2, 50, -MATE_VALUE + 10, false, false));
    /* Never before the first move: base + depth^2 >= 1. */
    TEST_ASSERT_FALSE(can_late_move_prune(0, 0, 0, false, false));
}

void test_see_pruning_guards() {
    TEST_ASSERT_TRUE(can_see_prune(SEE_PRUNE_MAX_DEPTH, 1, 0, false, false));
    /* Too deep, in check, PV node, mate bound, before the first move. */
    TEST_ASSERT_FALSE(can_see_prune(SEE_PRUNE_MAX_DEPTH + 1, 1, 0, false, false));
    TEST_ASSERT_FALSE(can_see_prune(2, 1, 0, true, false));
    TEST_ASSERT_FALSE(can_see_prune(2, 1, 0, false, true));
    TEST_ASSERT_FALSE(can_see_prune(2, 1, -MATE_VALUE + 10, false, false));
    TEST_ASSERT_FALSE(can_see_prune(2, 0, 0, false, false));

    TEST_ASSERT_EQUAL_INT(-3 * SEE_QUIET_MARGIN, see_prune_threshold(3, true));
    TEST_ASSERT_EQUAL_INT(-9 * SEE_CAPTURE_MARGIN, see_prune_threshold(3, false));
}

void test_see_pruning_thresholds() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* 1.e4 d5: Qg4 hangs the queen to ...Bxg4, Nf3 is safe. */
    setup_position("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2", &board, &search_state, &limits, 1);
    TEST_ASSERT_TRUE(see(&board, parse_move("d1g4", &board)) < see_prune_threshold(SEE_PRUNE_MAX_DEPTH, true));
    TEST_ASSERT_FALSE(see(&board, parse_move("g1f3", &board)) < see_prune_threshold(1, true));

    /* 1.e4 e5 2.Qh5 Nc6: Qxf7+ Kxf7 loses 800, pruned at depth 2 but not at depth 7, where the
     * threshold (-980) allows for a sacrifice. */
    setup_position("r1bqkbnr/pppp1ppp/2n5/4p2Q/4P3/8/PPPP1PPP/RNB1KBNR w KQkq - 2 3", &board, &search_state, &limits, 1);
    const int qxf7 = see(&board, parse_move("h5f7", &board));
    TEST_ASSERT_EQUAL_INT(-800, qxf7);
    TEST_ASSERT_TRUE(qxf7 < see_prune_threshold(2, false));
    TEST_ASSERT_FALSE(qxf7 < see_prune_threshold(7, false));
}

void test_quiescence_searches_promotion_with_negative_history() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* As test_quiescence_sees_quiet_queen_promotion, but the malus has made a8=Q's history
     * negative, which must not end the qsearch as if it were a losing capture. */
    setup_position("8/P7/8/8/8/8/k7/7K w - - 0 1", &board, &search_state, &limits, 1);
    const move_t promotion = parse_move("a7a8q", &board);
    for (int i = 0; i < 100; i++) penalise_history(&search_state, promotion, 20);
    TEST_ASSERT_TRUE(score_move(promotion, 0, &board, &search_state) < 0);

    const int stand_pat = evaluation(&board, &search_state.nnue);
    TEST_ASSERT_TRUE(quiescence(-INF, INF, &board, &search_state, &limits) > stand_pat + 500);
}

void test_lmr_reduction() {
    /* No reduction for the first moves or shallow depths. */
    TEST_ASSERT_EQUAL_INT(0, lmr_reduction(1, 10, false));
    TEST_ASSERT_EQUAL_INT(0, lmr_reduction(3, 1, false));

    /* Grows with depth and with the move number, never decreasing. */
    for (int depth = 2; depth < 40; depth++) {
        for (int moves = 1; moves < 60; moves++) {
            int r = lmr_reduction(depth, moves, false);
            TEST_ASSERT_TRUE(r >= lmr_reduction(depth - 1, moves, false));
            TEST_ASSERT_TRUE(r >= lmr_reduction(depth, moves - 1, false));
            /* At least one ply is left to search. */
            TEST_ASSERT_TRUE(depth - 1 - r >= 1);
            /* PV nodes are reduced less, by one ply. */
            int pv = lmr_reduction(depth, moves, true);
            TEST_ASSERT_TRUE(pv == r || pv == r - 1);
        }
    }

    /* A deep late move is reduced by more than the old flat 1. */
    TEST_ASSERT_TRUE(lmr_reduction(12, 20, false) >= 2);

    /* Moves past the end of the table use the last column. */
    TEST_ASSERT_EQUAL_INT(lmr_reduction(20, LMR_TABLE_SIZE - 1, false), lmr_reduction(20, 200, false));
}

void test_iir_reduction() {
    const move_t tt_move = encode_move(e2, e4, P, 0, 0, 1, 0, 0, 0);
    TEST_ASSERT_EQUAL_INT(1, iir_reduction(IIR_MIN_DEPTH, 0));
    TEST_ASSERT_EQUAL_INT(1, iir_reduction(12, 0));
    TEST_ASSERT_EQUAL_INT(0, iir_reduction(IIR_MIN_DEPTH - 1, 0));
    TEST_ASSERT_EQUAL_INT(0, iir_reduction(12, tt_move));
}

void test_aspiration_search() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Mate in 2 from a previous score of 0, the window has to widen all the way to the mate. */
    setup_position("r5k1/5ppp/8/8/8/8/4RPPP/4R1K1 w - - 0 1", &board, &search_state, &limits, 5);
    TEST_ASSERT_EQUAL_INT(MATE_VALUE - 3, aspiration_search(&board, &search_state, &limits, 5, 0));

    /* A previous score far too high, the search fails low and widens down to the real score. */
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 5);
    int score = aspiration_search(&board, &search_state, &limits, 5, 800);
    TEST_ASSERT_TRUE(score > -200 && score < 200);
    TEST_ASSERT_EQUAL_UINT32(search_state.pv_table[0][0] != 0, 1);

    /* Shallow depth, full window, same as a plain full window search. */
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 2);
    int full = negamax(-INF, INF, 2, &board, &search_state, &limits);
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 2);
    TEST_ASSERT_EQUAL_INT(full, aspiration_search(&board, &search_state, &limits, 2, 800));
}

/* Doubled rooks on the e-file, Re8+ Rxe8 Rxe8#, mate in 2. */
#define SMP_MATE_IN_2_FEN "r5k1/5ppp/8/8/8/8/4RPPP/4R1K1 w - - 0 1"

void test_smp_search_finds_mate() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    setup_position(SMP_MATE_IN_2_FEN, &board, &search_state, &limits, 6);
    limits.threads = 4;
    move_t mv = find_best_move(&board, &search_state, &limits);

    TEST_ASSERT_EQUAL_INT(MATE_VALUE - 3, search_state.score);
    TEST_ASSERT_EQUAL_INT(e2, get_source_square(mv));
    TEST_ASSERT_EQUAL_INT(e8, get_target_square(mv));
    /* The main thread finished, the helpers were stopped. */
    TEST_ASSERT_TRUE(limits.stop_search_flag);
}

void test_smp_matches_single_thread_move() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Back rank, Rd8#, every thread count must find it. */
    for (int threads = 1; threads <= 8; threads *= 2) {
        setup_position("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1", &board, &search_state, &limits, 5);
        limits.threads = threads;
        move_t mv = find_best_move(&board, &search_state, &limits);
        TEST_ASSERT_EQUAL_INT(d1, get_source_square(mv));
        TEST_ASSERT_EQUAL_INT(d8, get_target_square(mv));
    }
}

void test_smp_stopped_search_returns_legal_move() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Stopped before anything completes, helpers must exit and a legal move is still returned. */
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 20);
    limits.threads = 4;
    limits.stop_search_flag = true;
    move_t mv = find_best_move(&board, &search_state, &limits);
    TEST_ASSERT_NOT_EQUAL(0, mv);
    TEST_ASSERT_TRUE(make_move(&board, mv));
}

void test_smp_time_limit_stops_helpers() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Already out of time, the main thread's first clock check stops every thread. */
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, MAX_SEARCH_DEPTH);
    limits.threads = 4;
    limits.time_set = true;
    limits.stop_time = 0;
    limits.soft_stop_time = UINT64_MAX;

    move_t mv = find_best_move(&board, &search_state, &limits);
    TEST_ASSERT_TRUE(limits.stop_search_flag);
    TEST_ASSERT_EQUAL_UINT64(1, limits.time_checks); /* Only the main thread checks the clock. */
    TEST_ASSERT_TRUE(make_move(&board, mv));
}

void test_pv_followed_only_on_pv_path() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* The previous iteration's PV is 1.e4 Nf6. Nf6 is also legal after White's other first moves,
     * but only the node after 1.e4 is on the PV: the root and that node order a PV move first.
     * The search after Nf6 is at depth 0 (qsearch), which used to leave the flag set for every
     * later sibling. */
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 2);
    search_state.pv_table[0][0] = parse_move("e2e4", &board);
    TEST_ASSERT_TRUE(make_move(&board, search_state.pv_table[0][0]));
    search_state.pv_table[0][1] = parse_move("g8f6", &board);
    pop_move(&board);
    search_state.follow_pv = true;
    negamax(-INF, INF, 2, &board, &search_state, &limits);
    TEST_ASSERT_EQUAL_UINT64(2, search_state.pv_nodes);

    /* Not following the PV, no node is on it. */
    setup_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board, &search_state, &limits, 2);
    search_state.pv_table[0][0] = parse_move("e2e4", &board);
    search_state.follow_pv = false;
    negamax(-INF, INF, 2, &board, &search_state, &limits);
    TEST_ASSERT_EQUAL_UINT64(0, search_state.pv_nodes);
}

void test_store_killer() {
    search_state_t search_state;
    init_search_state(&search_state);
    search_state.ply = 3;
    const move_t first = encode_move(g1, f3, N, 0, 0, 0, 0, 0, 0);
    const move_t second = encode_move(b1, c3, N, 0, 0, 0, 0, 0, 0);

    /* The newest killer first, the previous one second... */
    store_killer(&search_state, first);
    store_killer(&search_state, second);
    TEST_ASSERT_EQUAL_UINT32(second, search_state.killer_moves[0][3]);
    TEST_ASSERT_EQUAL_UINT32(first, search_state.killer_moves[1][3]);

    /* ...and other plies are untouched. */
    TEST_ASSERT_EQUAL_UINT32(0, search_state.killer_moves[0][2]);
}

void test_killers_are_not_captures() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* A tactical middlegame: captures are ordered by MVV-LVA, never as killers. */
    setup_position("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                   &board, &search_state, &limits, 6);
    limits.quiet = true;
    find_best_move(&board, &search_state, &limits);
    for (int slot = 0; slot < 2; slot++) {
        for (int ply = 0; ply < MAX_PLY; ply++) {
            const move_t killer = search_state.killer_moves[slot][ply];
            if (killer) TEST_ASSERT_FALSE(get_capture_flag(killer));
        }
    }
}

void test_can_reduce() {
    /* A late quiet move at depth is reduced. */
    TEST_ASSERT_TRUE(can_reduce(LMR_MIN_DEPTH, LMR_MIN_MOVES, true, false, false, false));
    /* Too early in the list, too shallow, not quiet, a check, in check, a killer: not reduced. */
    TEST_ASSERT_FALSE(can_reduce(LMR_MIN_DEPTH, LMR_MIN_MOVES - 1, true, false, false, false));
    TEST_ASSERT_FALSE(can_reduce(LMR_MIN_DEPTH - 1, LMR_MIN_MOVES, true, false, false, false));
    TEST_ASSERT_FALSE(can_reduce(LMR_MIN_DEPTH, LMR_MIN_MOVES, false, false, false, false));
    TEST_ASSERT_FALSE(can_reduce(LMR_MIN_DEPTH, LMR_MIN_MOVES, true, true, false, false));
    TEST_ASSERT_FALSE(can_reduce(LMR_MIN_DEPTH, LMR_MIN_MOVES, true, false, true, false));
    TEST_ASSERT_FALSE(can_reduce(LMR_MIN_DEPTH, LMR_MIN_MOVES, true, false, false, true));
}

void test_pv_is_legal() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* The PV of every bench position is a line of legal moves from the root. */
    for (int i = 0; i < bench_position_count; i++) {
        setup_position(bench_positions[i], &board, &search_state, &limits, 6);
        limits.quiet = true;
        find_best_move(&board, &search_state, &limits);
        TEST_ASSERT_TRUE_MESSAGE(search_state.pv_length[0] > 0, bench_positions[i]);
        for (int ply = 0; ply < search_state.pv_length[0]; ply++) {
            move_buffer_t buffer;
            generate_moves(&board, &buffer);
            bool generated = false;
            for (int j = 0; j < buffer.index; j++) {
                if (buffer.moves[j] == search_state.pv_table[0][ply]) generated = true;
            }
            TEST_ASSERT_TRUE_MESSAGE(generated, bench_positions[i]);
            TEST_ASSERT_TRUE_MESSAGE(make_move(&board, search_state.pv_table[0][ply]), bench_positions[i]);
        }
    }
}

void test_mate_distance_scores() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Mate in 2 (1.Nf6+ gxf6 2.Bxf7#), 3 plies from the root. */
    const char* fen = "r2qkb1r/pp2nppp/3p4/2pNN1B1/2BnP3/3P4/PPP2PPP/R2bK2R w KQkq - 1 1";
    setup_position(fen, &board, &search_state, &limits, 5);
    limits.quiet = true;
    find_best_move(&board, &search_state, &limits);
    TEST_ASSERT_EQUAL_INT(MATE_VALUE - 3, search_state.score);

    /* After 1.Nf6+ Black is mated in 2 plies, whatever it plays. */
    tt_clear();
    init_search_state(&search_state);
    char command[256];
    snprintf(command, sizeof(command), "position fen %s moves d5f6", fen);
    parse_position(command, &board, &search_state);
    reset_time_control(&limits);
    limits.depth = 5;
    limits.quiet = true;
    find_best_move(&board, &search_state, &limits);
    TEST_ASSERT_EQUAL_INT(-MATE_VALUE + 2, search_state.score);
}

void test_quiescence_stand_pat() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* No captures, the static eval: beta when it's at or above beta, otherwise the eval itself. */
    setup_position("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1", &board, &search_state, &limits, 1);
    const int eval = evaluation(&board, &search_state.nnue);
    TEST_ASSERT_EQUAL_INT(eval - 10, quiescence(eval - 11, eval - 10, &board, &search_state, &limits));
    TEST_ASSERT_EQUAL_INT(eval, quiescence(-INF, INF, &board, &search_state, &limits));
}

void test_quiescence_delta_pruning() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* exd5 wins a pawn. With alpha just out of reach of eval + pawn + margin it isn't searched
     * (only the qsearch node is counted), a point lower and it is. */
    setup_position("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1", &board, &search_state, &limits, 1);
    const int eval = evaluation(&board, &search_state.nnue);
    const int reach = eval + see_values[P] + DELTA_MARGIN;
    search_state.nodes = 0;
    quiescence(reach, reach + 1, &board, &search_state, &limits);
    TEST_ASSERT_EQUAL_UINT64(1, search_state.nodes);
    search_state.nodes = 0;
    quiescence(reach - 1, reach, &board, &search_state, &limits);
    TEST_ASSERT_TRUE(search_state.nodes > 1);
}

void test_quiescence_in_check() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* Mated, the qsearch used to stand pat. */
    setup_position("R5k1/5ppp/8/8/8/8/5PPP/6K1 b - - 0 1", &board, &search_state, &limits, 1);
    TEST_ASSERT_EQUAL_INT(-MATE_VALUE, quiescence(-INF, INF, &board, &search_state, &limits));

    /* In check with a way out, ...Kf7: the quiet evasion is searched (the qsearch otherwise only
     * searches captures), so it isn't scored as mate. */
    setup_position("R5k1/6pp/8/8/8/8/5PPP/6K1 b - - 0 1", &board, &search_state, &limits, 1);
    const int score = quiescence(-INF, INF, &board, &search_state, &limits);
    TEST_ASSERT_TRUE(score > -MATE_SCORE && score < MATE_SCORE);
}

void test_fifty_move_rule_and_mate() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* The 100th half-move mates: a loss, not a draw. */
    setup_position("R5k1/5ppp/8/8/8/8/5PPP/6K1 b - - 100 80", &board, &search_state, &limits, 1);
    TEST_ASSERT_FALSE(is_draw(&board, &search_state));

    /* In check but not mated, or not in check: a draw. */
    setup_position("R5k1/6pp/8/8/8/8/5PPP/6K1 b - - 100 80", &board, &search_state, &limits, 1);
    TEST_ASSERT_TRUE(is_draw(&board, &search_state));
    setup_position("6k1/5ppp/8/8/8/8/5PPP/R5K1 b - - 100 80", &board, &search_state, &limits, 1);
    TEST_ASSERT_TRUE(is_draw(&board, &search_state));
}

void test_smp_helpers_grow_and_shrink() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* More helpers than before (a new, larger allocation), then fewer (reused). */
    const int threads[] = {2, 6, 3};
    for (int i = 0; i < 3; i++) {
        setup_position("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                       &board, &search_state, &limits, 6);
        limits.threads = threads[i];
        limits.quiet = true;
        const move_t mv = find_best_move(&board, &search_state, &limits);
        TEST_ASSERT_TRUE(make_move(&board, mv));
    }
}

void test_node_limit_stops_search() {
    board_t board;
    search_state_t search_state;
    limits_t limits;

    /* 'go nodes 5000': stopped at the first check past the limit (every 2048 nodes). */
    setup_position("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                   &board, &search_state, &limits, MAX_SEARCH_DEPTH);
    limits.nodes = 5000;
    limits.quiet = true;
    const move_t mv = find_best_move(&board, &search_state, &limits);
    TEST_ASSERT_TRUE(limits.stop_search_flag);
    TEST_ASSERT_TRUE(search_state.nodes >= 5000 && search_state.nodes < 5000 + 2048 + MAX_PLY);
    TEST_ASSERT_TRUE(make_move(&board, mv));
}

void test_tt_lookup() {
    tt_clear();
    const uint64_t hash = 0x0123456789abcdefULL;
    const move_t stored = encode_move(g1, f3, N, 0, 0, 0, 0, 0, 0);
    move_t mv;
    int score, depth;
    tt_entry_type type;
    TEST_ASSERT_FALSE(tt_lookup(hash, 0, &mv, &score, &depth, &type));

    /* A shallow upper bound gives no cutoff at depth 8, but the lookup still returns it. */
    tt_save(hash, tt_alpha, 3, 0, -42, stored);
    move_t probe_move;
    TEST_ASSERT_EQUAL_INT(TT_NOT_FOUND, tt_probe(hash, 8, 0, -100, 100, &probe_move));
    TEST_ASSERT_TRUE(tt_lookup(hash, 0, &mv, &score, &depth, &type));
    TEST_ASSERT_EQUAL_UINT32(stored, mv);
    TEST_ASSERT_EQUAL_INT(-42, score);
    TEST_ASSERT_EQUAL_INT(3, depth);
    TEST_ASSERT_EQUAL_INT(tt_alpha, type);

    /* Mate scores come back relative to the root: mate 5 plies from the root found at ply 2 is
     * mate 3 plies from that node, so 7 plies from a root 4 plies above it. */
    tt_save(hash, tt_exact, 6, 2, MATE_VALUE - 5, stored);
    TEST_ASSERT_TRUE(tt_lookup(hash, 4, &mv, &score, &depth, &type));
    TEST_ASSERT_EQUAL_INT(MATE_VALUE - 7, score);
    TEST_ASSERT_EQUAL_INT(tt_exact, type);
}

void test_singular_extension_conditions() {
    const move_t mv = encode_move(g1, f3, N, 0, 0, 0, 0, 0, 0);
    TEST_ASSERT_TRUE(can_singular_extend(8, 3, 10, mv, 6, tt_beta, 50));
    TEST_ASSERT_TRUE(can_singular_extend(8, 3, 10, mv, 8 - SE_TT_DEPTH_MARGIN, tt_exact, 50));

    TEST_ASSERT_FALSE(can_singular_extend(8, 0, 10, mv, 6, tt_beta, 50));       /* the root */
    TEST_ASSERT_FALSE(can_singular_extend(8, 20, 10, mv, 6, tt_beta, 50));      /* ply >= 2 * root depth */
    TEST_ASSERT_FALSE(can_singular_extend(SE_MIN_DEPTH - 1, 3, 10, mv, 6, tt_beta, 50));
    TEST_ASSERT_FALSE(can_singular_extend(8, 3, 10, 0, 6, tt_beta, 50));        /* no TT move */
    TEST_ASSERT_FALSE(can_singular_extend(8, 3, 10, mv, 4, tt_beta, 50));       /* entry too shallow */
    TEST_ASSERT_FALSE(can_singular_extend(8, 3, 10, mv, 6, tt_alpha, 50));      /* only an upper bound */
    TEST_ASSERT_FALSE(can_singular_extend(8, 3, 10, mv, 6, tt_beta, MATE_VALUE - 10));

    TEST_ASSERT_EQUAL_INT(50 - SE_MARGIN * 8, singular_beta(50, 8));
}

void test_singular_result() {
    /* TT score 100 at depth 8, so s_beta = 84. */
    const int s_beta = singular_beta(100, 8);

    /* Every other move below s_beta: singular, at PV and non-PV nodes alike. */
    TEST_ASSERT_EQUAL_INT(singular_extend, singular_result(s_beta - 1, s_beta, 100, 50, false));
    TEST_ASSERT_EQUAL_INT(singular_extend, singular_result(s_beta - 1, s_beta, 100, 50, true));

    /* Another move reached s_beta: never anything at a PV node. */
    TEST_ASSERT_EQUAL_INT(singular_none, singular_result(s_beta, s_beta, 100, 50, true));
    TEST_ASSERT_EQUAL_INT(singular_none, singular_result(s_beta, s_beta, 100, 90, true));

    /* Outside the PV: s_beta >= beta, another move beats beta, multi-cut. */
    TEST_ASSERT_EQUAL_INT(singular_multi_cut, singular_result(s_beta, s_beta, 100, 84, false));
    /* s_beta < beta but the TT move's stored score is at or above beta: the TT move a ply shallower. */
    TEST_ASSERT_EQUAL_INT(singular_reduce, singular_result(s_beta, s_beta, 100, 90, false));
    TEST_ASSERT_EQUAL_INT(singular_reduce, singular_result(s_beta, s_beta, 100, 100, false));
    /* ...and below beta: nothing, the TT move isn't reduced on a score that doesn't beat beta. */
    TEST_ASSERT_EQUAL_INT(singular_none, singular_result(s_beta, s_beta, 100, 101, false));
}

void test_excluded_move_search() {
    board_t board;
    search_state_t search_state;
    limits_t limits;
    move_t mv;
    int score, depth;
    tt_entry_type type;

    /* Rd8# is the only mate in one. Without it the node fails low against a mate in one window,
     * with it it fails high. */
    setup_position("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1", &board, &search_state, &limits, 3);
    const move_t rd8 = parse_move("d1d8", &board);
    search_state.excluded_moves[0] = rd8;
    TEST_ASSERT_EQUAL_INT(MATE_VALUE - 2, negamax(MATE_VALUE - 2, MATE_VALUE - 1, 3, &board, &search_state, &limits));
    /* A search without a move isn't the node's result, so it leaves nothing in the TT. */
    TEST_ASSERT_FALSE(tt_lookup(board.hash, 0, &mv, &score, &depth, &type));
    search_state.excluded_moves[0] = 0;
    TEST_ASSERT_EQUAL_INT(MATE_VALUE - 1, negamax(MATE_VALUE - 2, MATE_VALUE - 1, 3, &board, &search_state, &limits));

    /* Kxg2 is white's only legal move. Without it there's nothing left to play, which is not mate
     * or stalemate: the node fails low. */
    setup_position("7k/8/8/8/8/8/6q1/7K w - - 0 1", &board, &search_state, &limits, 4);
    search_state.excluded_moves[0] = parse_move("h1g2", &board);
    TEST_ASSERT_EQUAL_INT(-50, negamax(-50, -49, 4, &board, &search_state, &limits));
    TEST_ASSERT_FALSE(tt_lookup(board.hash, 0, &mv, &score, &depth, &type));
    search_state.excluded_moves[0] = 0;
    TEST_ASSERT_EQUAL_INT(-49, negamax(-50, -49, 4, &board, &search_state, &limits));
}
