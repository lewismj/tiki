#ifndef TIKI_SYZYGY_TESTS_H
#define TIKI_SYZYGY_TESTS_H

void test_syzygy_bitboard_mirror();
void test_syzygy_without_tables();
void test_syzygy_probe_wdl();
void test_syzygy_en_passant();
void test_syzygy_root_keeps_the_result();
void test_syzygy_root_restriction_only_at_the_root();
void test_syzygy_search_plays_the_win();
void test_syzygy_search_scores_tablebase_wins();

#endif
