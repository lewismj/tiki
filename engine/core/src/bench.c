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
#ifdef TIKI_SEARCH_STATS
    search_stats_t stats = {0};
#endif
    history_summary_t history = {0};
#ifdef TIKI_SEARCH_STATS
    history_summary_t capture_history = {0};
#endif
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
#ifdef TIKI_SEARCH_STATS
        {
            /* Every counter is a uint64_t (search_state.h), summed word by word. */
            uint64_t* total = (uint64_t*) &stats;
            const uint64_t* add = (const uint64_t*) &search_state.stats;
            for (size_t w = 0; w < sizeof(stats) / sizeof(uint64_t); w++) total[w] += add[w];
        }
#endif

        /* The history table as this search left it. */
        for (int pc = 0; pc < 12; pc++) {
            for (int sq = 0; sq < 64; sq++) summarise(&history, search_state.history_moves[pc][sq]);
#ifdef TIKI_SEARCH_STATS
            for (int sq = 0; sq < 64; sq++)
                for (int v = 0; v < 6; v++) summarise(&capture_history, search_state.capture_history[pc][sq][v]);
#endif
        }
    }

    const uint64_t elapsed = clock_time_ms() - start;
    printf("\n===========================\n");
    printf("Total time (ms) : %" PRIu64 "\n", elapsed);
    printf("Nodes searched  : %" PRIu64 "\n", nodes);
    printf("Nodes/second    : %" PRIu64 "\n", elapsed ? nodes * 1000 / elapsed : nodes);
#ifdef TIKI_SEARCH_STATS
    /* Move ordering: the higher the first move share and the fewer quiets tried, the better. */
    printf("Cutoffs         : %" PRIu64 ", %.1f%% on the first move\n", stats.cutoffs,
           stats.cutoffs ? 100.0 * (double) stats.first_move_cutoffs / (double) stats.cutoffs : 0.0);
    printf("Quiet cutoffs   : %" PRIu64 ", %.2f quiets tried before each\n", stats.quiet_cutoffs,
           stats.quiet_cutoffs ? (double) stats.quiets_before_cutoff / (double) stats.quiet_cutoffs : 0.0);
    printf("Singular        : %" PRIu64 " searches (%.1f%% of the nodes), %" PRIu64 " extensions, %" PRIu64
           " reductions, %" PRIu64 " multi-cuts\n", stats.singular_searches,
           nodes ? 100.0 * (double) stats.singular_nodes / (double) nodes : 0.0,
           stats.singular_extensions, stats.singular_reductions, stats.multi_cuts);
    {
        const double c = stats.cutoffs ? 100.0 / (double) stats.cutoffs : 0.0;
        const uint64_t* at = stats.cutoff_at;
        printf("Cutoff move     : #1 %.1f%%, #2 %.1f%%, #3-4 %.1f%%, #5-8 %.1f%%, #9+ %.1f%% (top 4: %.1f%%)\n",
               c * (double) at[0], c * (double) at[1], c * (double) at[2], c * (double) at[3], c * (double) at[4],
               c * (double) (at[0] + at[1] + at[2]));
        printf("Cutoff kind     : %.1f%% captures, %.1f%% quiet (promotions without a capture count as quiet)\n",
               c * (double) stats.capture_cutoffs, c * (double) stats.quiet_cutoffs);
        printf("TT move         : at %" PRIu64 " move loops, searched first at %.1f%%, the cutoff move at %.1f%% of "
               "them; %.1f%% of cutoffs had one\n", stats.tt_move_nodes,
               stats.tt_move_nodes ? 100.0 * (double) stats.tt_move_first / (double) stats.tt_move_nodes : 0.0,
               stats.tt_move_nodes ? 100.0 * (double) stats.tt_move_cutoffs / (double) stats.tt_move_nodes : 0.0,
               c * (double) stats.cutoff_nodes_tt);
        for (int k = 0; k < 2; k++) {
            const uint64_t* n = stats.no_tt_cutoff_at[k];
            const uint64_t total = n[0] + n[1] + n[2] + n[3] + n[4];
            const double d = total ? 100.0 / (double) total : 0.0;
            printf("No TT, %s: %" PRIu64 " cutoffs, #1 %.1f%%, #2 %.1f%%, #3-4 %.1f%%, #5-8 %.1f%%, #9+ %.1f%%\n",
                   k == 0 ? "captures" : "quiets  ", total, d * (double) n[0], d * (double) n[1], d * (double) n[2],
                   d * (double) n[3], d * (double) n[4]);
        }
        static const char* bands[3] = { "1-3", "4-6", "7+ " };
        printf("Quiet ordering  : depth PV TT |  move loops  quiets/loop  cuts/quiet  loops cut |"
               " cut at quiet #1    #2  #3-4  #5-8   #9+\n");
        for (int b = 0; b < 3; b++) {
            for (int pv = 0; pv < 2; pv++) {
                for (int t = 0; t < 2; t++) {
                    const uint64_t loops = stats.quiet_nodes[b][pv][t], quiets = stats.quiets_searched[b][pv][t];
                    const uint64_t cut = stats.quiet_cut_nodes[b][pv][t];
                    const uint64_t* rank = stats.quiet_cut_rank[b][pv][t];
                    const double r = cut ? 100.0 / (double) cut : 0.0;
                    printf("                    %s  %c  %c | %11" PRIu64 "  %11.2f  %9.1f%%  %8.1f%% |"
                           "            %5.1f %5.1f %5.1f %5.1f %5.1f\n",
                           bands[b], pv ? 'y' : 'n', t ? 'y' : 'n', loops,
                           loops ? (double) quiets / (double) loops : 0.0,
                           quiets ? 100.0 * (double) cut / (double) quiets : 0.0,
                           loops ? 100.0 * (double) cut / (double) loops : 0.0,
                           r * (double) rank[0], r * (double) rank[1], r * (double) rank[2], r * (double) rank[3],
                           r * (double) rank[4]);
                }
            }
        }

        /* LMR, three views of stats.lmr: by depth band and PV, by reduction, by the move's place. */
        static const char* places[4] = { "4-5 ", "6-8 ", "9-16", "17+ " };
        static const char* reductions[4] = { "1 ", "2 ", "3 ", "4+" };
        uint64_t by_depth[3][2][5] = {{{0}}}, by_reduction[4][5] = {{0}}, by_place[4][5] = {{0}};
        for (int b = 0; b < 3; b++)
            for (int pv = 0; pv < 2; pv++)
                for (int pl = 0; pl < 4; pl++)
                    for (int r = 0; r < 4; r++)
                        for (int k = 0; k < 5; k++) {
                            const uint64_t v = stats.lmr[b][pv][pl][r][k];
                            by_depth[b][pv][k] += v;
                            by_reduction[r][k] += v;
                            by_place[pl][k] += v;
                        }
        printf("LMR (reduced)   : %-14s|    searches  beat alpha | re-searched: <= alpha  in window  >= beta\n", "");
        for (int b = 0; b < 3; b++) {
            for (int pv = 0; pv < 2; pv++) {
                const uint64_t* row = by_depth[b][pv];
                const double beat = row[1] ? 100.0 / (double) row[1] : 0.0;
                printf("                  depth %s PV %c | %11" PRIu64 "  %9.2f%% |             %6.1f%%    %6.1f%%  %6.1f%%\n",
                       bands[b], pv ? 'y' : 'n', row[0], row[0] ? 100.0 * (double) row[1] / (double) row[0] : 0.0,
                       beat * (double) row[2], beat * (double) row[3], beat * (double) row[4]);
            }
        }
        for (int r = 0; r < 4; r++) {
            const uint64_t* row = by_reduction[r];
            const double beat = row[1] ? 100.0 / (double) row[1] : 0.0;
            printf("                  reduction %s  | %11" PRIu64 "  %9.2f%% |             %6.1f%%    %6.1f%%  %6.1f%%\n",
                   reductions[r], row[0], row[0] ? 100.0 * (double) row[1] / (double) row[0] : 0.0,
                   beat * (double) row[2], beat * (double) row[3], beat * (double) row[4]);
        }
        for (int pl = 0; pl < 4; pl++) {
            const uint64_t* row = by_place[pl];
            const double beat = row[1] ? 100.0 / (double) row[1] : 0.0;
            printf("                  move #%s    | %11" PRIu64 "  %9.2f%% |             %6.1f%%    %6.1f%%  %6.1f%%\n",
                   places[pl], row[0], row[0] ? 100.0 * (double) row[1] / (double) row[0] : 0.0,
                   beat * (double) row[2], beat * (double) row[3], beat * (double) row[4]);
        }
        printf("Capture history : depth PV TT | move loops  top capture changed  captures moved |"
               " cut at #1 #2 #3+ as ordered | with no history\n");
        for (int b = 0; b < 3; b++) {
            for (int pv = 0; pv < 2; pv++) {
                for (int t = 0; t < 2; t++) {
                    const uint64_t loops = stats.cap_loops[b][pv][t], moves = stats.cap_moves[b][pv][t];
                    const uint64_t* cut = stats.cap_cut_rank[b][pv][t];
                    const uint64_t* cut0 = stats.cap_cut_rank_without[b][pv][t];
                    const uint64_t cuts = cut[0] + cut[1] + cut[2];
                    const double k = cuts ? 100.0 / (double) cuts : 0.0;
                    printf("                    %s  %c  %c | %10" PRIu64 "  %18.1f%%  %13.1f%% |"
                           " %5.1f %5.1f %5.1f          | %5.1f %5.1f %5.1f\n",
                           bands[b], pv ? 'y' : 'n', t ? 'y' : 'n', loops,
                           loops ? 100.0 * (double) stats.cap_top_changed[b][pv][t] / (double) loops : 0.0,
                           moves ? 100.0 * (double) stats.cap_rank_changed[b][pv][t] / (double) moves : 0.0,
                           k * (double) cut[0], k * (double) cut[1], k * (double) cut[2],
                           k * (double) cut0[0], k * (double) cut0[1], k * (double) cut0[2]);
                }
            }
        }
        static const char* history_bands[3] = { "below -1000", "-1000..1000", "above +1000" };
        for (int h = 0; h < 3; h++) {
            const uint64_t searched = stats.cap_searched_by_history[h], cut_h = stats.cap_cut_by_history[h];
            printf("                  history %s: %10" PRIu64 " captures searched, %5.1f%% cut off\n",
                   history_bands[h], searched, searched ? 100.0 * (double) cut_h / (double) searched : 0.0);
        }
        printf("                  updates: %" PRIu64 " bonuses, %" PRIu64 " maluses, %.2f%% left a value near the bound\n",
               stats.cap_bonuses, stats.cap_maluses,
               stats.cap_bonuses + stats.cap_maluses ?
               100.0 * (double) stats.cap_near_bound_updates / (double) (stats.cap_bonuses + stats.cap_maluses) : 0.0);
        print_summary("                  table", &capture_history);
        printf("Victim prior A  : depth PV TT | move loops  A changes top capture | cutting capture at #1 #2 #3+:"
               " as ordered | in A\n");
        for (int b = 0; b < 3; b++) {
            for (int pv = 0; pv < 2; pv++) {
                for (int t = 0; t < 2; t++) {
                    const uint64_t loops = stats.cap_loops[b][pv][t];
                    const uint64_t* cut = stats.cap_cut_rank[b][pv][t];
                    const uint64_t* cutv = stats.victim_cut_rank[b][pv][t];
                    const uint64_t cuts = cut[0] + cut[1] + cut[2];
                    const double k = cuts ? 100.0 / (double) cuts : 0.0;
                    printf("                    %s  %c  %c | %10" PRIu64 "  %20.1f%% |"
                           "                         %5.1f %5.1f %5.1f | %5.1f %5.1f %5.1f\n",
                           bands[b], pv ? 'y' : 'n', t ? 'y' : 'n', loops,
                           loops ? 100.0 * (double) stats.victim_top_changed[b][pv][t] / (double) loops : 0.0,
                           k * (double) cut[0], k * (double) cut[1], k * (double) cut[2],
                           k * (double) cutv[0], k * (double) cutv[1], k * (double) cutv[2]);
                }
            }
        }
        static const char* victim_names[5] = { "P", "N", "B", "R", "Q" };
        printf("                  victim pairs ordered differently by A:\n");
        for (int lo = 0; lo < 5; lo++) {
            for (int hi = lo + 1; hi < 5; hi++) {
                const uint64_t pairs = stats.victim_pairs[lo][hi];
                if (!pairs) continue;
                printf("                    %s/%s: %9" PRIu64 " pairs, %5.1f%% flipped\n", victim_names[lo],
                       victim_names[hi], pairs, 100.0 * (double) stats.victim_pair_flips[lo][hi] / (double) pairs);
            }
        }
        printf("LMR             : %" PRIu64 " reduced searches, %.1f%% beat alpha (re-searched), %.1f%% still beat alpha in "
               "the full depth null window search (not necessarily >= beta, see the table above)\n", stats.lmr_searches,
               stats.lmr_searches ? 100.0 * (double) stats.lmr_fail_highs / (double) stats.lmr_searches : 0.0,
               stats.lmr_searches ? 100.0 * (double) stats.lmr_research_holds / (double) stats.lmr_searches : 0.0);
    }
#endif
    print_summary("History         ", &history);

    init_transposition_table(previous_mb ? previous_mb : BENCH_HASH_MB);
    return nodes;
}
