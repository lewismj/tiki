#include <limits.h>
#include <string.h>
#include <unistd.h>

#include "unity.h"
#include "make_undo_move_tests.h"
#include "perf_tests.h"

#include "../../core/src/attack_mask.h"
#include "../../core/src/zobrist_key.h"

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

    UNITY_BEGIN();
    RUN_TEST(test_make_move_hash1);
    RUN_TEST(test_make_move_hash2);
    RUN_TEST(test_make_move_hash3);
    RUN_TEST(test_undo_restores_state1);
    RUN_TEST(perft_tests);
    if (!skip_full_perft) {
        printf("All perft.\n");
        RUN_TEST(full_perft_test);
    }
    return UNITY_END();
}