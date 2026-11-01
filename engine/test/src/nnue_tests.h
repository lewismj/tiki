#ifndef TIKI_NNUE_TESTS_H
#define TIKI_NNUE_TESTS_H


void test_nnue_feature_index_known_values();
void test_nnue_features_in_range_and_unique();
void test_nnue_feature_symmetry();
void test_nnue_evaluate_known_value();
void test_nnue_output_buckets();
void test_nnue_load_single_output_network();
void test_nnue_evaluate_colour_symmetric();
void test_nnue_save_load_round_trip();
void test_nnue_incremental_matches_refresh();
void test_nnue_output_simd_matches_scalar();
void test_nnue_output_small_matches_scalar();
void test_nnue_load_sets_small_output();
void test_nnue_refresh_matches_plain_sum();
void test_nnue_cache_rebuilt_for_new_network();
void test_nnue_setoption_evalfile();
void test_nnue_load_memory();
void test_nnue_embedded_network();
void test_material_network_values();
void test_datagen_output_format();
void test_datagen_deterministic();

#endif
