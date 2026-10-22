#include <limits.h>
#include <string.h>
#include <unistd.h>

#include "unity.h"
#include "make_undo_move_tests.h"
#include "perf_tests.h"
#include "uci_tests.h"

#include "../../core/src/attack_mask.h"
#include "../../core/src/zobrist_key.h"
#include "../../core/src/transposition.h"

/**
 * Ensure pre-computed tables are initialised prior to running tests.
 */
void setUp() {
    init_attack_table();
    init_zobrist_key();
}

void tearDown() {
}



int main(int argc, char* argv[]) {
    static  char path[PATH_MAX];
    bool skip_full_perft = false;

    if (argc == 2) {
        strncpy(path, argv[1], sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
        const char *file_suffix = "/data/perf-test.csv";
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

    /* Small table, the UCI tests clear it on 'ucinewgame'. */
    init_transposition_table(1);

    UNITY_BEGIN();
    RUN_TEST(test_make_move_hash1);
    RUN_TEST(test_make_move_hash2);
    RUN_TEST(test_make_move_hash3);
    RUN_TEST(test_undo_restores_state1);
    RUN_TEST(test_uci_command_tokens);
    RUN_TEST(test_uci_stop_does_not_quit);
    RUN_TEST(test_uci_quit_exits);
    RUN_TEST(test_uci_newgame_resets_position);
    RUN_TEST(test_go_infinite_limits);
    RUN_TEST(test_go_clock_limits);
    RUN_TEST(test_search_input_stop);
    RUN_TEST(test_search_input_quit);
    RUN_TEST(test_search_input_isready);
    RUN_TEST(perft_tests);
    if (!skip_full_perft) {
        printf("All perft.\n");
        RUN_TEST(full_perft_test);
    }
    int result = UNITY_END();
    free_transposition_table();
    return result;
}