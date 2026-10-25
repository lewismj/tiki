#include <stdio.h>
#include <string.h>

#include "bench.h"
#include "board.h"
#include "search_state.h"
#include "search.h"
#include "transposition.h"
#include "limits.h"
#include "uci.h"


/* Middlegames, tactics and endgames, from the Stockfish bench set. */
const char* const bench_positions[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 10",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 11",
        "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
        "rq3rk1/ppp2ppp/1bnpb3/3N2B1/3NP3/7P/PPPQ1PP1/2KR3R w - - 7 14",
        "r1bq1r1k/1pp1n1pp/1p1p4/4p2Q/4Pp2/1BNP4/PPP2PPP/3R1RK1 w - - 2 14",
        "r3r1k1/2p2ppp/p1p1bn2/8/1q2P3/2NPQN2/PPP3PP/R4RK1 b - - 2 15",
        "r1bbk1nr/pp3p1p/2n5/1N4p1/2Np1B2/8/PPP2PPP/2KR1B1R w kq - 0 13",
        "r1bq1rk1/ppp1nppp/4n3/3p3Q/3P4/1BP1B3/PP1N2PP/R4RK1 w - - 1 16",
        "4r1k1/r1q2ppp/ppp2n2/4P3/5Rb1/1N1BQ3/PPP3PP/R5K1 w - - 1 17",
        "2rqkb1r/ppp2p2/2npb1p1/1N1Nn2p/2P1PP2/8/PP2B1PP/R1BQK2R b KQ - 0 11",
        "r1bq1r1k/b1p1npp1/p2p3p/1p6/3PP3/1B2NN2/PP3PPP/R2Q1RK1 w - - 1 16",
        "3r1rk1/p5pp/bpp1pp2/8/q1PP1P2/b3P3/P2NQRPP/1R2B1K1 b - - 6 22",
        "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/8 b - - 3 54",
        "8/8/8/8/5kp1/P7/8/1K1N4 w - - 0 1",
        "8/3k4/8/8/8/4B3/4KB2/2B5 w - - 0 1",
};
const int bench_position_count = (int) (sizeof(bench_positions) / sizeof(bench_positions[0]));


/* Entries used (non-zero), the sum of their |values| and how many are near +-HISTORY_MAX. */
typedef struct {
    uint64_t entries, used, total, near_bound;
} history_summary_t;

static void summarise(history_summary_t* summary, int value) {
    summary->entries++;
    if (value == 0) return;
    const int magnitude = value < 0 ? -value : value;
    summary->used++;
    summary->total += (uint64_t) magnitude;
    if (magnitude >= HISTORY_MAX * 9 / 10) summary->near_bound++;
}

static void print_summary(const char* name, const history_summary_t* summary) {
    const double used = (double) (summary->used ? summary->used : 1);
    printf("%s: %.2f%% of entries used, mean |value| %.0f, %.1f%% of those near the bound\n", name,
           100.0 * (double) summary->used / (double) (summary->entries ? summary->entries : 1),
           (double) summary->total / used, 100.0 * (double) summary->near_bound / used);
}

uint64_t bench(int depth) {
    if (depth <= 0) depth = BENCH_DEFAULT_DEPTH;
    static board_t board;
    static search_state_t search_state;
    limits_t limits;
    char command[256];

    /* A fixed TT size, so the node count doesn't depend on the 'Hash' setting. */
    const size_t previous_mb = (tt_size * sizeof(transposition_node_t)) >> 20;
    init_transposition_table(BENCH_HASH_MB);

    uint64_t nodes = 0;
    uint64_t cutoffs = 0, first_move_cutoffs = 0, quiet_cutoffs = 0, quiets_before_cutoff = 0;
    history_summary_t history = {0};
    const uint64_t start = clock_time_ms();

    for (int i = 0; i < bench_position_count; i++) {
        printf("\nPosition: %d/%d (%s)\n", i + 1, bench_position_count, bench_positions[i]);

        /* Same state as 'ucinewgame', so each result only depends on the position. */
        tt_clear();
        init_search_state(&search_state);
        snprintf(command, sizeof(command), "position fen %s", bench_positions[i]);
        parse_position(command, &board, &search_state);

        reset_time_control(&limits);
        limits.depth = depth;
        tt_new_search();
        print_bestmove(find_best_move(&board, &search_state, &limits));
        nodes += search_state.nodes;
        cutoffs += search_state.cutoffs;
        first_move_cutoffs += search_state.first_move_cutoffs;
        quiet_cutoffs += search_state.quiet_cutoffs;
        quiets_before_cutoff += search_state.quiets_before_cutoff;

        /* The history table as this search left it. */
        for (int pc = 0; pc < 12; pc++) {
            for (int sq = 0; sq < 64; sq++) summarise(&history, search_state.history_moves[pc][sq]);
        }
    }

    const uint64_t elapsed = clock_time_ms() - start;
    printf("\n===========================\n");
    printf("Total time (ms) : %" PRIu64 "\n", elapsed);
    printf("Nodes searched  : %" PRIu64 "\n", nodes);
    printf("Nodes/second    : %" PRIu64 "\n", elapsed ? nodes * 1000 / elapsed : nodes);
    /* Move ordering: the higher the first move share and the fewer quiets tried, the better. */
    printf("Cutoffs         : %" PRIu64 ", %.1f%% on the first move\n", cutoffs,
           cutoffs ? 100.0 * (double) first_move_cutoffs / (double) cutoffs : 0.0);
    printf("Quiet cutoffs   : %" PRIu64 ", %.2f quiets tried before each\n", quiet_cutoffs,
           quiet_cutoffs ? (double) quiets_before_cutoff / (double) quiet_cutoffs : 0.0);
    print_summary("History         ", &history);

    init_transposition_table(previous_mb ? previous_mb : BENCH_HASH_MB);
    return nodes;
}
