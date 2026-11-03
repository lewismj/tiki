#ifndef TIKI_TT_TESTS_H
#define TIKI_TT_TESTS_H


void test_tt_entry_layout();
void test_tt_pack_round_trip();
void test_tt_torn_entry_is_miss();
void test_tt_mate_score_round_trip();
void test_tt_stores_best_move();
void test_tt_replacement();
void test_castle_key_all_rights();
void test_tt_stores_static_eval();
void test_tt_read_and_cutoff();
void test_adjusted_eval();
void test_qsearch_saves_to_tt();
void test_tt_bucket_keeps_positions_apart();
void test_tt_bucket_replaces_least_valuable();
void test_go_keeps_tt();
void test_search_stores_exact_root();
void test_search_mate_score_with_warm_tt();

#endif
