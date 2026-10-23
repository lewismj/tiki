#include <limits.h>
#include <string.h>
#include <unistd.h>

#include "unity.h"
#include "make_undo_move_tests.h"
#include "perf_tests.h"
#include "uci_tests.h"
#include "repetition_tests.h"
#include "tt_tests.h"
#include "input_tests.h"
#include "board_tests.h"
#include "search_tests.h"
#include "nnue_tests.h"

#include "../../core/src/attack_mask.h"
#include "../../core/src/zobrist_key.h"
#include "../../core/src/transposition.h"

/* The pre-computed tables don't change, they're initialised once in main. */
void setUp() {
}

void tearDown() {
}



int main(int argc, char* argv[]) {
    static  char path[PATH_MAX];
    bool skip_full_perft = false;

    if (argc == 2) {
        strncpy(path, argv[1], sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    } else if (getcwd(path, sizeof(path)) == NULL) {
        fprintf(stderr,"getcwd() error.\n");
        exit(-1);
    }

    const char *perft_test_file = "/data/perft-tests.csv";
    strncat(path, perft_test_file, sizeof(path) - strlen(path) - 1);
    if (access(path, F_OK) == 0) {
        printf("Found '%s' perft test file.\n", path);
    } else {
        skip_full_perft = true;
        printf("Missing perft test file'%s', run from test directory or param to test directory.\n", path);
    }
    g_perft_file = &path[0];

    init_attack_table();
    init_zobrist_key();
    init_lmr_table();

    /* Small table, the UCI tests clear it on 'ucinewgame'. */
    init_transposition_table(1);

    UNITY_BEGIN();
    RUN_TEST(test_make_move_hash1);
    RUN_TEST(test_make_move_hash2);
    RUN_TEST(test_make_move_hash3);
    RUN_TEST(test_undo_restores_state1);
    RUN_TEST(test_incremental_hash_matches_full);
    RUN_TEST(test_pop_on_empty_stack_is_clamped);
    RUN_TEST(test_parse_fen_fields);
    RUN_TEST(test_parse_move_promotions);
    RUN_TEST(test_parse_move_castling);
    RUN_TEST(test_parse_move_rejects_invalid);
    RUN_TEST(test_slider_attacks_match_reference);
    RUN_TEST(test_generate_captures_matches_filtered_moves);
    RUN_TEST(test_generate_captures_promotions);
    RUN_TEST(test_see);
    RUN_TEST(test_board_to_fen_round_trip);
    RUN_TEST(test_nnue_feature_index_known_values);
    RUN_TEST(test_nnue_features_in_range_and_unique);
    RUN_TEST(test_nnue_feature_symmetry);
    RUN_TEST(test_nnue_evaluate_known_value);
    RUN_TEST(test_nnue_evaluate_colour_symmetric);
    RUN_TEST(test_nnue_save_load_round_trip);
    RUN_TEST(test_datagen_output_format);
    RUN_TEST(test_datagen_deterministic);
    RUN_TEST(test_search_mate_in_one);
    RUN_TEST(test_search_stalemate_root);
    RUN_TEST(test_search_checkmated_root);
    RUN_TEST(test_bench_positions_are_legal);
    RUN_TEST(test_bench_is_deterministic);
    RUN_TEST(test_time_checked_once_per_2048_nodes);
    RUN_TEST(test_time_limit_stops_at_first_check);
    RUN_TEST(test_pick_next_move_order);
    RUN_TEST(test_quiescence_sees_quiet_queen_promotion);
    RUN_TEST(test_check_extended_at_horizon);
    RUN_TEST(test_null_move_guards);
    RUN_TEST(test_history_bonus_and_bound);
    RUN_TEST(test_history_stays_below_killers_and_captures);
    RUN_TEST(test_en_passant_scored_as_pawn_capture);
    RUN_TEST(test_losing_capture_ordered_after_quiet_moves);
    RUN_TEST(test_reverse_futility_guards);
    RUN_TEST(test_futility_guards);
    RUN_TEST(test_lmr_reduction);
    RUN_TEST(test_iir_reduction);
    RUN_TEST(test_aspiration_search);
    RUN_TEST(test_uci_command_tokens);
    RUN_TEST(test_uci_stop_does_not_quit);
    RUN_TEST(test_uci_quit_exits);
    RUN_TEST(test_uci_newgame_resets_position);
    RUN_TEST(test_go_infinite_limits);
    RUN_TEST(test_go_clock_limits);
    RUN_TEST(test_search_input_stop);
    RUN_TEST(test_search_input_quit);
    RUN_TEST(test_search_input_isready);
    RUN_TEST(test_position_resets_history);
    RUN_TEST(test_position_records_root);
    RUN_TEST(test_position_irreversible_move_clears_history);
    RUN_TEST(test_position_detects_game_repetition);
    RUN_TEST(test_position_long_history);
    RUN_TEST(test_go_preserves_history);
    RUN_TEST(test_fifty_move_draw);
    RUN_TEST(test_position_history_overflow);
    RUN_TEST(test_repetition_same_side_to_move);
    RUN_TEST(test_repetition_limited_by_fifty_move);
    RUN_TEST(test_repetition_limited_by_null_move);
    RUN_TEST(test_insufficient_material);
    RUN_TEST(test_search_scores_insufficient_material_as_draw);
    RUN_TEST(test_tt_entry_layout);
    RUN_TEST(test_tt_mate_score_round_trip);
    RUN_TEST(test_tt_stores_best_move);
    RUN_TEST(test_tt_replacement);
    RUN_TEST(test_castle_key_all_rights);
    RUN_TEST(test_go_keeps_tt);
    RUN_TEST(test_search_stores_exact_root);
    RUN_TEST(test_search_mate_score_with_warm_tt);
    RUN_TEST(test_input_queues_commands_in_order);
    RUN_TEST(test_input_stop_during_search);
    RUN_TEST(test_input_quit_during_search);
    RUN_TEST(test_input_commands_during_search_are_kept);
    RUN_TEST(test_input_stop_queued_before_search);
    RUN_TEST(test_input_stop_for_later_search);
    RUN_TEST(test_input_stop_after_queued_go);
    RUN_TEST(test_input_wait_for_stop);
    RUN_TEST(test_uci_loop_keeps_commands_during_search);
    RUN_TEST(test_uci_loop_go_infinite_stop);
    RUN_TEST(test_soft_time_limit);
    RUN_TEST(test_stopped_search_returns_legal_move);
    RUN_TEST(perft_tests);
    if (!skip_full_perft) {
        printf("All perft.\n");
        RUN_TEST(full_perft_test);
    }
    int result = UNITY_END();
    free_transposition_table();
    return result;
}