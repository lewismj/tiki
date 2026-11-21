#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "verify.h"
#include "board.h"
#include "evaluation.h"
#include "limits.h"
#include "move_generator.h"
#include "search.h"
#include "search_state.h"
#include "transposition.h"
#include "uci.h"
#include "tune.h"

verify_config_t verify_cfg = { .sample = 16, .skip_ply = -1, .rng = 0x9E3779B97F4A7C15ULL };

static const char* const feature_names[vf_count] = {
    "RFP", "null move", "ProbCut", "multi-cut", "futility", "LMP (first)", "SEE pruning", "history pruning",
    "LMR (fail low)",
};

#define VERIFY_LOG_MAX 50000

static int depth_bucket(int depth) {
    return depth < 1 ? 0 : depth >= VERIFY_DEPTHS ? VERIFY_DEPTHS - 1 : depth - 1;
}

bool verify_take(verify_feature_t feature, int depth) {
    if (!verify_cfg.decisions || verify_cfg.nesting > 0) return false;
    verify_cfg.counts[feature].decisions[depth_bucket(depth)]++;
    uint64_t x = verify_cfg.rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    verify_cfg.rng = x;
    return verify_cfg.sample <= 1 || x % verify_cfg.sample == 0;
}

void verify_record(verify_feature_t feature, int depth, bool wrong, int miss, uint64_t nodes, const char* fen,
                   const char* context) {
    verify_counts_t* c = &verify_cfg.counts[feature];
    const int d = depth_bucket(depth);
    c->checked[d]++;
    c->nodes[d] += nodes;
    if (!wrong) return;
    c->wrong[d]++;
    const int magnitude = miss < 0 ? -miss : miss;
    if (magnitude >= TB_WIN_MIN / 2) {
        c->mates[d]++;
    } else {
        c->miss_total[d] += (uint64_t) magnitude;
        if (magnitude > 100) c->big[d]++;
    }
    if (verify_cfg.log && verify_cfg.logged < VERIFY_LOG_MAX) {
        fprintf((FILE*) verify_cfg.log, "%s | %s | %s\n", feature_names[feature], fen, context);
        verify_cfg.logged++;
    }
}

/* ------------------------------------------------------------------------------------------------
 * The plain alpha-beta reference: no TT, no pruning, reductions or extensions, the same qsearch
 * (stand pat, then generate_captures; every evasion in check) and the same draw and mate rules.
 * Moves are ordered (score_move) only to keep it fast: the score with a full window doesn't depend
 * on the order.
 * ---------------------------------------------------------------------------------------------- */

static uint64_t ref_nodes;

static bool ref_in_check(board_t* board) {
    return board->side == white ? is_square_attacked_by_black(board, trailing_zero_count(board->pieces[K]))
                                : is_square_attacked_by_white(board, trailing_zero_count(board->pieces[k]));
}

static void ref_order(move_buffer_t* buffer, int* scores, board_t* board, search_state_t* search_state) {
    for (int i = 0; i < buffer->index; i++) scores[i] = score_move(buffer->moves[i], 0, board, search_state);
}

static int ref_qsearch(int alpha, int beta, board_t* board, search_state_t* search_state) {
    ref_nodes++;
    if (search_state->ply > MAX_PLY - 1) return evaluation(board, &search_state->nnue);
    const bool in_check = ref_in_check(board);
    int best = -INF;
    if (!in_check) {
        best = evaluation(board, &search_state->nnue);
        if (best >= beta) return best;
        if (best > alpha) alpha = best;
    }
    move_buffer_t buffer;
    int scores[MOVE_BUFFER_SIZE];
    if (in_check) generate_moves(board, &buffer);
    else generate_captures(board, &buffer);
    ref_order(&buffer, scores, board, search_state);
    bool legal = false;
    for (int i = 0; i < buffer.index; i++) {
        const move_t mv = pick_next_move(&buffer, scores, i);
        ++search_state->ply;
        if (!make_move(board, mv)) {
            --search_state->ply;
            pop_move(board);
            continue;
        }
        nnue_push(&search_state->nnue, board, mv);
        legal = true;
        const int score = -ref_qsearch(-beta, -alpha, board, search_state);
        --search_state->ply;
        pop_move(board);
        nnue_pop(&search_state->nnue);
        if (score > best) best = score;
        if (score > alpha) {
            alpha = score;
            if (score >= beta) return score;
        }
    }
    if (in_check && !legal) return -MATE_VALUE + search_state->ply;
    return best;
}

static int ref_negamax(int alpha, int beta, int depth, board_t* board, search_state_t* search_state) {
    ref_nodes++;
    if (search_state->ply == 0) nnue_reset_root(&search_state->nnue, board);
    if (search_state->ply > 0 && is_draw(board, search_state)) return 0;
    if (search_state->ply >= MAX_PLY - 1) return evaluation(board, &search_state->nnue);
    if (depth == 0) return ref_qsearch(alpha, beta, board, search_state);

    const bool in_check = ref_in_check(board);
    move_buffer_t buffer;
    int scores[MOVE_BUFFER_SIZE];
    generate_moves(board, &buffer);
    ref_order(&buffer, scores, board, search_state);
    int best = -INF;
    bool legal = false;
    for (int i = 0; i < buffer.index; i++) {
        const move_t mv = pick_next_move(&buffer, scores, i);
        if (!make_move(board, mv)) {
            pop_move(board);
            continue;
        }
        nnue_push(&search_state->nnue, board, mv);
        legal = true;
        search_state->path_moves[search_state->ply] = mv;
        ++search_state->ply;
        ++search_state->repetition_index;
        record_position(search_state, search_state->repetition_index, board->hash, board->fifty_move);
        const int score = -ref_negamax(-beta, -alpha, depth - 1, board, search_state);
        --search_state->ply;
        --search_state->repetition_index;
        pop_move(board);
        nnue_pop(&search_state->nnue);
        if (score > best) best = score;
        if (score > alpha) {
            alpha = score;
            if (score >= beta) return score;
        }
    }
    if (!legal) return in_check ? -MATE_VALUE + search_state->ply : 0;
    return best;
}

/* ------------------------------------------------------------------------------------------------
 * Positions, the colour flip and the searches.
 * ---------------------------------------------------------------------------------------------- */

#define VERIFY_MAX_POSITIONS 20000

static int read_fens(const char* path, char (**fens)[128]) {
    FILE* file = fopen(path, "r");
    if (!file) return -1;
    *fens = malloc(sizeof(**fens) * VERIFY_MAX_POSITIONS);
    char line[512];
    int n = 0;
    while (n < VERIFY_MAX_POSITIONS && fgets(line, sizeof(line), file)) {
        line[strcspn(line, "|\r\n")] = 0;          /* 'fen | anything' lines keep the FEN */
        size_t end = strlen(line);
        while (end > 0 && line[end - 1] == ' ') line[--end] = 0;
        if (!line[0] || line[0] == '#') continue;
        snprintf((*fens)[n++], 128, "%s", line);
    }
    fclose(file);
    return n;
}

/* The same position with the colours swapped and the board turned over (rank r becomes 9 - r). */
static void flip_fen(const char* in, char* out, size_t size) {
    char placement[100], side[4], castle[8], ep[4], half[8] = "0", full[8] = "1";
    if (sscanf(in, "%99s %3s %7s %3s %7s %7s", placement, side, castle, ep, half, full) < 4) {
        snprintf(out, size, "%s", in);
        return;
    }
    char ranks[8][16];
    int r = 0;
    for (char* tok = strtok(placement, "/"); tok && r < 8; tok = strtok(NULL, "/")) snprintf(ranks[r++], 16, "%s", tok);
    char flipped[100] = "";
    for (int i = 7; i >= 0; i--) {
        for (char* c = ranks[i]; *c; c++) {
            const char ch = isalpha((unsigned char) *c) ? (char) (isupper((unsigned char) *c) ? tolower(*c) : toupper(*c)) : *c;
            strncat(flipped, (char[]) { ch, 0 }, sizeof(flipped) - strlen(flipped) - 1);
        }
        if (i) strncat(flipped, "/", sizeof(flipped) - strlen(flipped) - 1);
    }
    char rights[8] = "";
    if (strchr(castle, 'k')) strcat(rights, "K");
    if (strchr(castle, 'q')) strcat(rights, "Q");
    if (strchr(castle, 'K')) strcat(rights, "k");
    if (strchr(castle, 'Q')) strcat(rights, "q");
    if (!rights[0]) strcpy(rights, "-");
    char flipped_ep[4] = "-";
    if (ep[0] != '-' && ep[1]) snprintf(flipped_ep, sizeof(flipped_ep), "%c%c", ep[0], (char) ('1' + '8' - ep[1]));
    snprintf(out, size, "%s %s %s %s %s %s", flipped, side[0] == 'w' ? "b" : "w", rights, flipped_ep, half, full);
}

/* A move in UCI with its ranks turned over, to compare a position's move with its flipped twin's. */
static void flip_uci(const char* in, char* out) {
    strcpy(out, in);
    if (strlen(in) >= 4) {
        out[1] = (char) ('1' + '8' - in[1]);
        out[3] = (char) ('1' + '8' - in[3]);
    }
}

static board_t verify_board;
static search_state_t verify_state;

typedef struct {
    int score;
    move_t move;
    uint64_t nodes;
} search_result_t;

/* tiki's own search at 'depth' from a clean state (as bench: 'ucinewgame', then the position). */
static search_result_t tiki_search(const char* fen, int depth) {
    char command[256];
    limits_t limits;
    tt_clear();
    init_search_state(&verify_state);
    snprintf(command, sizeof(command), "position fen %s", fen);
    parse_position(command, &verify_board, &verify_state);
    reset_time_control(&limits);
    limits.depth = depth;
    limits.quiet = true;
    tt_new_search();
    const move_t move = find_best_move(&verify_board, &verify_state, &limits);
    return (search_result_t) { verify_state.score, move, verify_state.nodes };
}

static search_result_t reference_search(const char* fen, int depth) {
    char command[256];
    init_search_state(&verify_state);
    snprintf(command, sizeof(command), "position fen %s", fen);
    parse_position(command, &verify_board, &verify_state);
    ref_nodes = 0;
    verify_state.ply = 0;
    const int score = ref_negamax(-INF, INF, depth, &verify_board, &verify_state);
    return (search_result_t) { score, 0, ref_nodes };
}

static void print_decisions(void) {
    printf("\nSelective decisions, checked by doing the skipped work (1 in %u sampled)\n", verify_cfg.sample);
    printf("%-16s %5s %12s %9s %8s %9s %9s %6s %12s\n", "feature", "depth", "decisions", "checked", "wrong %",
           ">100cp %", "mean miss", "mates", "nodes/check");
    for (int f = 0; f < vf_count; f++) {
        const verify_counts_t* c = &verify_cfg.counts[f];
        uint64_t all_decisions = 0, all_checked = 0, all_wrong = 0, all_big = 0, all_mates = 0;
        bool named = false;
        for (int d = 0; d < VERIFY_DEPTHS; d++) {
            all_decisions += c->decisions[d];
            all_checked += c->checked[d];
            all_wrong += c->wrong[d];
            all_big += c->big[d];
            all_mates += c->mates[d];
            if (!c->checked[d]) continue;
            const char* name = named ? "" : feature_names[f];
            named = true;
            const uint64_t non_mate = c->wrong[d] - c->mates[d];
            printf("%-16s %4d%s %12" PRIu64 " %9" PRIu64 " %8.2f %9.3f %9.0f %6" PRIu64 " %12.0f\n", name, d + 1,
                   d == VERIFY_DEPTHS - 1 ? "+" : " ", c->decisions[d], c->checked[d],
                   100.0 * (double) c->wrong[d] / (double) c->checked[d],
                   100.0 * (double) c->big[d] / (double) c->checked[d],
                   non_mate ? (double) c->miss_total[d] / (double) non_mate : 0.0, c->mates[d],
                   (double) c->nodes[d] / (double) c->checked[d]);
        }
        if (all_checked) {
            printf("%-16s %5s %12" PRIu64 " %9" PRIu64 " %8.2f %9.3f %9s %6" PRIu64 "\n", feature_names[f], "all",
                   all_decisions, all_checked, 100.0 * (double) all_wrong / (double) all_checked,
                   100.0 * (double) all_big / (double) all_checked, "", all_mates);
        }
    }
}

static void usage(void) {
    fprintf(stderr,
            "tiki verify <mode> <fen file> <depth> [sample] [log file]\n"
            "  plain      selectivity off, depths 1..depth against the plain alpha-beta reference\n"
            "  plain-tt   as plain, TT cutoffs from entries of exactly the same depth\n"
            "  mirror     each position against its colour-flipped twin, selectivity off (exact)\n"
            "  mirror-full  the same with the normal search (the spread of the differences)\n"
            "  repeat     each position twice from a clean state, normal search (identical)\n"
            "  decisions  the normal search, 1 in 'sample' selective decisions checked\n");
}

int verify_main(int argc, char* argv[]) {
#ifndef TIKI_VERIFY
    (void) argc;
    (void) argv;
    fprintf(stderr, "'verify' needs a TIKI_VERIFY build (cmake -DTIKI_VERIFY=ON).\n");
    return EXIT_FAILURE;
#else
    if (argc < 5) {
        usage();
        return EXIT_FAILURE;
    }
    const char* mode = argv[2];
    char (*fens)[128] = NULL;
    const int count = read_fens(argv[3], &fens);
    if (count <= 0) {
        fprintf(stderr, "No positions in '%s'.\n", argv[3]);
        return EXIT_FAILURE;
    }
    /* TIKI_VERIFY_SET="NAME=value,NAME=value": tunable parameters for this run (a TIKI_TUNE build too),
     * to compare a feature's decisions with other settings. */
    const char* settings = getenv("TIKI_VERIFY_SET");
    if (settings && *settings) {
        char copy[512];
        snprintf(copy, sizeof(copy), "%s", settings);
        for (char* item = strtok(copy, ","); item; item = strtok(NULL, ",")) {
            char* eq = strchr(item, '=');
            if (!eq || !set_tunable(item, (size_t) (eq - item), atoi(eq + 1))) {
                fprintf(stderr, "TIKI_VERIFY_SET: can't set '%s' (needs a TIKI_TUNE build and a tunable name)\n", item);
                return EXIT_FAILURE;
            }
            printf("set %s\n", item);
        }
    }
    const int depth = atoi(argv[4]);
    if (argc >= 6) verify_cfg.sample = (uint32_t) atoi(argv[5]);
    if (argc >= 7) verify_cfg.log = fopen(argv[6], "w");
    verify_cfg.active = true;
    int failures = 0;

    if (strcmp(mode, "plain") == 0 || strcmp(mode, "plain-tt") == 0) {
        verify_cfg.plain = true;
        verify_cfg.plain_tt = strcmp(mode, "plain-tt") == 0;
        uint64_t tiki_total = 0, ref_total = 0;
        for (int i = 0; i < count; i++) {
            for (int d = 1; d <= depth; d++) {
                const search_result_t t = tiki_search(fens[i], d);
                const search_result_t r = reference_search(fens[i], d);
                tiki_total += t.nodes;
                ref_total += r.nodes;
                if (t.score != r.score) {
                    failures++;
                    printf("MISMATCH %s depth %d: tiki %d, reference %d\n", fens[i], d, t.score, r.score);
                }
            }
            if ((i + 1) % 50 == 0) printf("%d/%d positions, %d mismatches\n", i + 1, count, failures);
        }
        printf("%s: %d positions x depths 1-%d, %d mismatches; nodes tiki %" PRIu64 ", reference %" PRIu64 "\n", mode,
               count, depth, failures, tiki_total, ref_total);
    } else if (strcmp(mode, "mirror") == 0 || strcmp(mode, "mirror-full") == 0) {
        const bool full = strcmp(mode, "mirror-full") == 0;
        verify_cfg.plain = !full;
        int same_move = 0, diff_hist[5] = {0};   /* |score difference|: 0, 1-10, 11-50, 51-200, >200 */
        int64_t diff_sum = 0;
        for (int i = 0; i < count; i++) {
            char flipped[128], uci_a[8], uci_b[8], uci_bf[8];
            flip_fen(fens[i], flipped, sizeof(flipped));
            const search_result_t a = tiki_search(fens[i], depth);
            const search_result_t b = tiki_search(flipped, depth);
            move_to_uci(a.move, uci_a);
            move_to_uci(b.move, uci_b);
            flip_uci(uci_b, uci_bf);
            const int diff = a.score - b.score;
            const int mag = diff < 0 ? -diff : diff;
            diff_hist[mag == 0 ? 0 : mag <= 10 ? 1 : mag <= 50 ? 2 : mag <= 200 ? 3 : 4]++;
            diff_sum += diff;
            if (strcmp(uci_a, uci_bf) == 0) same_move++;
            if (mag != 0 && (!full || mag > 50)) {
                if (!full) failures++;
                printf("%s %s: %d (%s, %" PRIu64 " nodes) vs flipped %d (%s, %" PRIu64 " nodes)\n",
                       full ? "DIFFERS" : "MISMATCH", fens[i], a.score, uci_a, a.nodes, b.score, uci_b, b.nodes);
            }
        }
        printf("%s depth %d, %d positions: same move %d; |score difference| 0: %d, 1-10: %d, 11-50: %d, 51-200: %d, "
               ">200: %d; mean (position - flipped) %+.2f\n", mode, depth, count, same_move, diff_hist[0], diff_hist[1],
               diff_hist[2], diff_hist[3], diff_hist[4], (double) diff_sum / count);
    } else if (strcmp(mode, "repeat") == 0) {
        for (int i = 0; i < count; i++) {
            const search_result_t a = tiki_search(fens[i], depth);
            const search_result_t b = tiki_search(fens[i], depth);
            if (a.score != b.score || a.move != b.move || a.nodes != b.nodes) {
                failures++;
                printf("MISMATCH %s: %d / %" PRIu64 " nodes, then %d / %" PRIu64 " nodes\n", fens[i], a.score, a.nodes,
                       b.score, b.nodes);
            }
        }
        printf("repeat depth %d, %d positions: %d not identical\n", depth, count, failures);
    } else if (strcmp(mode, "decisions") == 0) {
        verify_cfg.decisions = true;
        uint64_t nodes = 0;
        for (int i = 0; i < count; i++) {
            nodes += tiki_search(fens[i], depth).nodes;
            if ((i + 1) % 50 == 0) printf("%d/%d positions\n", i + 1, count);
        }
        printf("decisions depth %d, %d positions, %" PRIu64 " nodes (checks included)\n", depth, count, nodes);
        print_decisions();
    } else {
        usage();
        failures = 1;
    }

    if (verify_cfg.log) fclose((FILE*) verify_cfg.log);
    free(fens);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
#endif
}
