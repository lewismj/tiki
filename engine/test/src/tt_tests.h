#ifndef TIKI_TT_TESTS_H
#define TIKI_TT_TESTS_H


void test_tt_entry_layout();
void test_tt_mate_score_round_trip();
void test_tt_stores_best_move();
void test_tt_replacement();
void test_castle_key_all_rights();
void test_go_keeps_tt();
void test_search_stores_exact_root();
void test_search_mate_score_with_warm_tt();

#endif
