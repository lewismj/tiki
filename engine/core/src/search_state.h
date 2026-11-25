#ifndef TIKI_SEARCH_STATE_H
#define TIKI_SEARCH_STATE_H

#include <unistd.h>
#include <string.h>
#include <stdatomic.h>
#include "search_constants.h"
#include "nnue/nnue.h"
#include "quiet_history.h"

static_assert(NNUE_STACK_SIZE > MAX_PLY + 1, "the NNUE stack must hold the deepest search path");

#ifdef TIKI_SEARCH_STATS
/* Move ordering statistics, printed by bench: beta cutoffs in negamax, how many on the first move
 * searched, how many by a quiet move and the quiet moves tried before each of those. */
typedef struct {
    uint64_t cutoffs;
    uint64_t first_move_cutoffs;
    uint64_t quiet_cutoffs;
    uint64_t quiets_before_cutoff;
    uint64_t singular_searches;   /* Searches without the TT move, the nodes they took, and what came of them. */
    uint64_t singular_nodes;
    uint64_t singular_extensions;
    uint64_t singular_reductions;
    uint64_t multi_cuts;
    uint64_t cutoff_at[5];        /* Cutoffs by the cutting move's place among those searched: 1, 2, 3-4, 5-8, 9+. */
    uint64_t capture_cutoffs;     /* Cutoffs by a capture (promotions that don't capture count as quiet). */
    uint64_t tt_move_nodes;       /* Move loops (negamax) with a TT move... */
    uint64_t tt_move_first;       /* ...where it was the first move searched (not pruned, legal)... */
    uint64_t tt_move_cutoffs;     /* ...and where it caused the node's cutoff. */
    uint64_t cutoff_nodes_tt;     /* Cutoffs at nodes with a TT move (the share of cutoffs it could take). */
    uint64_t no_tt_cutoff_at[2][5]; /* Cutoffs at nodes without a TT move: [capture, quiet][place as cutoff_at]. */
    uint64_t lmr_searches;        /* Reduced searches... */
    uint64_t lmr_fail_highs;      /* ...that beat alpha, so the move was searched again at full depth... */
    uint64_t lmr_research_holds;  /* ...where it still beat alpha (the reduction hid a good move). */
    /* Quiet ordering by [depth band 1-3, 4-6, 7+][non-PV, PV][no TT move, TT move]: move loops that
     * searched a quiet move, the quiet moves searched, the loops a quiet move cut off, and that
     * move's place among the quiets searched (1, 2, 3-4, 5-8, 9+). Singular searches aren't counted. */
    uint64_t quiet_nodes[3][2][2];
    uint64_t quiets_searched[3][2][2];
    uint64_t quiet_cut_nodes[3][2][2];
    uint64_t quiet_cut_rank[3][2][2][5];
    /* Reduced searches by [depth band][non-PV, PV][move place 2, 3, 4-5, 6-8, 9-16, 17+][reduction 1, 2, 3, 4+]:
     * [0] searches, [1] beat alpha (so re-searched at full depth), and what the full depth search
     * then found: [2] at or below alpha, [3] between alpha and beta, [4] at or above beta. */
    uint64_t lmr[3][2][6][4][5];
    /* Nodes of reduced searches by [reduction 1, 2, 3, 4+]: [0] the reduced searches, and the searches
     * again at full depth of those that beat alpha: [1] ending at or below alpha (the re-search
     * didn't hold), [2] above it (needed). */
    uint64_t lmr_cost[4][3];
    /* Main search nodes by where they are against the iteration's depth, at entry (before the node's
     * own check extension): [ply + depth - root depth + 10], the extensions less the reductions on
     * the path (0 for none, clamped to -10 .. +21); and by ply in quarters of the root depth (0 ..
     * 11, [12] 3x or more), so [8] onwards is at or beyond ply 2 * root depth. */
    uint64_t ext_net[32];
    uint64_t ply_quarters[13];
    /* Capture history's effect on ordering, by [depth band][non-PV, PV][no TT move, TT move], over the
     * captures other than the TT move at move loops with at least two of them: the loops, those
     * where it changed the top capture, the captures, those whose place among the captures it
     * changed; and for cutoffs by such a capture, its place (1, 2, 3+) as ordered and with no history. */
    uint64_t cap_loops[3][2][2];
    uint64_t cap_top_changed[3][2][2];
    uint64_t cap_moves[3][2][2];
    uint64_t cap_rank_changed[3][2][2];
    uint64_t cap_cut_rank[3][2][2][3];
    uint64_t cap_cut_rank_without[3][2][2][3];
    /* Captures searched (not the TT move) and those that cut off, by their capture history:
     * [0] below -1000 (disfavoured), [1] within +-1000, [2] above +1000 (favoured). */
    uint64_t cap_searched_by_history[3];
    uint64_t cap_cut_by_history[3];
    /* Capture history updates: bonuses, maluses, and updates leaving a value within 10% of the bound. */
    uint64_t cap_bonuses;
    uint64_t cap_maluses;
    uint64_t cap_near_bound_updates;
    /* Shadow comparison, nothing searched differently: the same captures ranked as the search orders
     * them and with experiment A's scores (4 * the victim's SEE value + the same capture history
     * term, no attacker term). By [depth band][non-PV, PV][no TT move, TT move]: loops with two or
     * more captures where A's top capture differs, and the cutting capture's place (1, 2, 3+) in A. */
    uint64_t victim_top_changed[3][2][2];
    uint64_t victim_cut_rank[3][2][2][3];
    /* Pairs of captures of different victim types (pawn .. queen) at the same loop, and how many
     * the two schemes order differently. [lower type][higher type]. */
    uint64_t victim_pairs[5][5];
    uint64_t victim_pair_flips[5][5];
    /* Where the nodes are: qsearch nodes below a qsearch root, and main search nodes by the depth they
     * were entered with ([16]: 16 or more; [0]: depth 0, a qsearch root), those entered and those that
     * reached the move loop (not cut off by the TT, a draw, pruning or the qsearch). */
    uint64_t qsearch_nodes;
    uint64_t nodes_at_depth[17];
    uint64_t expanded_at_depth[17];
    /* Why a main search node ended before its move loop, by entered depth: [0] draw, [1] TT cutoff,
     * [2] tablebase, [3] reverse futility, [4] null move, [5] ProbCut, [6] multi-cut. And null moves tried. */
    uint64_t exit_at_depth[7][17];
    uint64_t null_tried_at_depth[17];
    /* Moves searched at nodes entered with depth 2-4, by [depth - 2][kind: TT or first move, check, good
     * capture or promotion, killer, bad capture, other quiet][full depth, reduced]. */
    uint64_t move_kind[3][6][2];
    /* Move ordering (stat_order_cut, stat_order_loop_end), singular searches left out. By [depth
     * band 1-3, 4-6, 7+]: move loops by the node's expected type [PV or all node, cut node], those
     * that cut off and those cut by the first move searched; the cutting move's ordering band [TT or
     * PV move, good capture or queen promotion, killer, counter move, quiet, losing capture]; the
     * nodes the move loops took [didn't cut off, cut off] and, of the ones that cut off, the nodes
     * spent on the moves searched before the cutting move (by depth band and by its band). */
    uint64_t ord_loops[3][2];
    uint64_t ord_cuts[3][2];
    uint64_t ord_first[3][2];
    uint64_t ord_band[3][6];
    uint64_t ord_loop_nodes[3][2];
    uint64_t ord_wasted[3];
    uint64_t ord_wasted_band[6];
} search_stats_t;

/* Only 64-bit counters, so the bench can sum two of them word by word. */
static_assert(sizeof(search_stats_t) % sizeof(uint64_t) == 0, "search_stats_t holds uint64_t counters only");
#endif


/**
 * Correction history: how far the search's scores have come out from the static eval in positions
 * sharing a feature, used to correct the eval (search.c, eval_correction). Per side to move, three
 * tables: the pawn structure (each colour's pawns), white's other pieces and black's other pieces
 * (each piece type's squares), indexed by a hash of those squares. Entries in 1/CORR_GRAIN cp.
 * Kept for the game (cleared by 'ucinewgame'). 192 KB, kept outside the search state: one table,
 * main_corr_history, shared by the main search and every Lazy SMP helper, so each thread learns from
 * the others' searches. Entries are loaded and stored atomically (relaxed: plain moves on x86); two
 * threads updating one entry at once can lose an update, which only costs a little learning.
 */
#define CORR_BITS 14
#define CORR_SIZE (1 << CORR_BITS)
#define CORR_TABLES 3
typedef _Atomic int16_t corr_history_t[2][CORR_TABLES][CORR_SIZE];
extern corr_history_t main_corr_history;
extern quiet_history_t main_quiet_history;

typedef struct align {
    /*
     * pv_table, use to store the principal variation, we use 64.64
     * array rather than a vector (triangular matrix), the indexing
     * is simplified using a matrix.
     */
    uint64_t repetition_check[MAX_MOVES];
    int16_t repetition_distance[MAX_MOVES]; /* Per entry of repetition_check, see find_repetition. */
    uint32_t pv_table[MAX_PLY][MAX_PLY];
    int32_t capture_history[12][64][6]; /* [moving piece][target square][captured piece type], see score_move. */
    uint32_t killer_moves[MAX_PLY + 1]; /* One per ply (quiet_history.h), the next ply's cleared at node entry. */
    uint32_t excluded_moves[MAX_PLY]; /* The TT move a singular extension search leaves out at this ply (0: none). */
    int static_evals[MAX_PLY];        /* The network's eval at each ply of the search path, -INF in check (improving). */
    uint8_t nmp_min_ply;              /* No null move below this ply (0: none), during a null move verification. */
    uint32_t path_moves[MAX_PLY];     /* The move made at each ply of the search path (0: a null move), set before */
                                      /* its child is searched (continuation history).                             */
    quiet_history_t* quiet;           /* Quiet history, counter moves, node cache (quiet_history.h). */
    corr_history_t* corr_history;     /* See corr_history_t. */
    uint8_t pv_length[MAX_PLY];
    uint64_t nodes;
    uint64_t pv_nodes;         /* Nodes that ordered the previous iteration's PV move first (tests). */
    uint64_t best_move_nodes;  /* Nodes the root's best move took in the last root search (time management)... */
    uint64_t root_search_start; /* ...and 'nodes' when that root search started (re-searches start again). */
#ifdef TIKI_SEARCH_STATS
    search_stats_t stats;
#endif
    uint8_t ply;
    uint8_t root_depth;        /* Depth of the iteration being searched, bounds the singular extensions. */
    uint16_t repetition_index;
    uint16_t null_move_index;  /* repetition_check index of the position after the last null move (0 if none). */
    bool follow_pv;
    bool score_pv;
    bool corr_shared;          /* Other threads update the correction history during this search (learn_correction). */
    int score;                 /* Score of the last completed iteration (side to move). */
    int thread_id;             /* 0 for the main thread, which checks the clock and reports. */
    _Atomic uint64_t shared_nodes; /* Helper threads publish 'nodes' here for the main thread. */
    nnue_stack_t nnue;         /* Accumulators along the search path (when a network is loaded). */
} search_state_t;


/**
 * The tables tied to one search's path from the root (PV, killers, excluded moves), emptied before
 * each search. History isn't one of them, see age_history.
 */
static inline_always void clear_search_tables(search_state_t* search_state) {
    memset(search_state->pv_table, 0ULL, sizeof(search_state->pv_table));
    memset(search_state->pv_length, 0ULL, sizeof(search_state->pv_length));
    memset(search_state->killer_moves, 0, sizeof (search_state->killer_moves));
    memset(search_state->excluded_moves, 0, sizeof (search_state->excluded_moves));
    search_state->nmp_min_ply = 0;
    memset(search_state->path_moves, 0, sizeof (search_state->path_moves));
}

/**
 * Before each search: the quiet history ages by the reference's rule (quiet_history_new_search);
 * the capture history is halved (tiki's rule, until it's ported too).
 */
static inline_always void age_history(search_state_t* search_state) {
    quiet_history_new_search(search_state->quiet);
    int32_t* c = &search_state->capture_history[0][0][0];
    for (size_t i = 0; i < sizeof(search_state->capture_history) / sizeof(*c); i++) c[i] /= 2;
}

/* A new search state (as 'ucinewgame') using 'table' for its quiet history, cleared, and the
 * shared correction history, 'corrections', as it is (shared, so init_search_state clears it). */
static inline_always void init_search_state_with(search_state_t* search_state, quiet_history_t* table,
                                                 corr_history_t* corrections) {
    search_state->quiet = table;
    quiet_history_clear(table);
    search_state->corr_history = corrections;
    search_state->corr_shared = false;
    search_state->nodes = 0;
    search_state->pv_nodes = 0;
    search_state->best_move_nodes = 0;
    search_state->root_search_start = 0;
#ifdef TIKI_SEARCH_STATS
    memset(&search_state->stats, 0, sizeof(search_state->stats));
#endif
    search_state->thread_id = 0;
    atomic_init(&search_state->shared_nodes, 0);
    search_state->ply = 0;
    search_state->repetition_index = 0;
    memset(search_state->repetition_distance, 0, sizeof(search_state->repetition_distance));
    search_state->null_move_index = 0;
    search_state->score = 0;
    search_state->score_pv = false;
    search_state->follow_pv = true;
    clear_search_tables(search_state);
    memset(search_state->capture_history, 0, sizeof (search_state->capture_history));
    memset(search_state->repetition_check, 0ULL, sizeof(search_state->repetition_check));
    nnue_stack_init(&search_state->nnue);
}

/* A new search state (as 'ucinewgame'), with the main continuation and correction history tables,
 * both cleared. */
static inline_always void init_search_state(search_state_t* search_state) {
    memset(main_corr_history, 0, sizeof(corr_history_t));
    init_search_state_with(search_state, &main_quiet_history, &main_corr_history);
}

/* Before each search ('go'). The game history from 'position' is kept for the repetition checks. */
static inline_always void clear_pv_table(search_state_t* search_state) {
    search_state->nodes = 0;
    search_state->pv_nodes = 0;
    search_state->best_move_nodes = 0;
    search_state->root_search_start = 0;
    atomic_init(&search_state->shared_nodes, 0);
    search_state->ply = 0;
    search_state->null_move_index = 0;
    search_state->score = 0;
    search_state->score_pv = false;
    search_state->follow_pv = true;
    clear_search_tables(search_state);
    age_history(search_state);
    nnue_reset(&search_state->nnue);
}

/**
 * repetition_check[repetition_index] holds the current position, entries below it are the
 * earlier positions (game history from 'position', then the search path).
 *
 * The position at 'index' (its hash already stored) against the earlier ones: the distance in
 * plies back to its last earlier occurrence, negated if that occurrence was itself a repetition
 * (so this is the position's third occurrence), 0 if none. Only
 * positions with the same side to move (every second entry) since the last irreversible move
 * ('fifty_move' plies back) and the last null move can repeat. The position two plies back can't,
 * as each side has only made one move, so the scan starts four plies back. Worked out once, when
 * the position is recorded (record_position); is_repetition then only reads it.
 */
static inline_always int find_repetition(const search_state_t* search_state, int index, int fifty_move) {
    int oldest = index - fifty_move;
    if (oldest < search_state->null_move_index) oldest = search_state->null_move_index;

    const uint64_t position_hash = search_state->repetition_check[index];
    for (int i = index - 4; i >= oldest; i -= 2) {
        if (search_state->repetition_check[i] == position_hash) {
            return search_state->repetition_distance[i] != 0 ? -(index - i) : index - i;
        }
    }
    return 0;
}

/* Stores the position at 'index' of the history with its repetition distance (find_repetition). */
static inline_always void record_position(search_state_t* search_state, int index, uint64_t position_hash,
                                          int fifty_move) {
    search_state->repetition_check[index] = position_hash;
    search_state->repetition_distance[index] = (int16_t) find_repetition(search_state, index, fifty_move);
}

/**
 * Whether the current position, 'ply' plies from the root, is a draw by repetition: if it
 * repeats once a position after the root (a side that can repeat once can keep repeating),
 * or for the third time anywhere (a negative distance). A single earlier occurrence at or before
 * the root isn't a draw, the other side doesn't have to repeat it again.
 */
static inline_always bool is_repetition(const search_state_t* search_state, int ply) {
    const int distance = search_state->repetition_distance[search_state->repetition_index];
    return distance != 0 && distance < ply;
}


#endif
