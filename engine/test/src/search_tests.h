#ifndef TIKI_SEARCH_TESTS_H
#define TIKI_SEARCH_TESTS_H


void test_search_mate_in_one();
void test_search_stalemate_root();
void test_search_checkmated_root();
void test_bench_positions_are_legal();
void test_bench_is_deterministic();
void test_time_checked_once_per_2048_nodes();
void test_time_limit_stops_at_first_check();
void test_pick_next_move_order();
void test_quiescence_sees_quiet_queen_promotion();
void test_check_extended_at_horizon();
void test_null_move_guards();
void test_history_bonus_and_bound();
void test_history_stays_below_killers_and_captures();
void test_en_passant_scored_as_pawn_capture();
void test_losing_capture_ordered_after_quiet_moves();
void test_reverse_futility_guards();
void test_futility_guards();
void test_lmr_reduction();
void test_iir_reduction();
void test_aspiration_search();

#endif
