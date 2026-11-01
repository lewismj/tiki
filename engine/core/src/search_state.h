#ifndef TIKI_SEARCH_STATE_H
#define TIKI_SEARCH_STATE_H

#include <unistd.h>
#include <string.h>
#include <stdatomic.h>
#include "search_constants.h"
#include "nnue/nnue.h"

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
    /* Reduced searches by [depth band][non-PV, PV][move place 4-5, 6-8, 9-16, 17+][reduction 1, 2, 3, 4+]:
     * [0] searches, [1] beat alpha (so re-searched at full depth), and what the full depth search
     * then found: [2] at or below alpha, [3] between alpha and beta, [4] at or above beta. */
    uint64_t lmr[3][2][4][4][5];
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
} search_stats_t;

/* Only 64-bit counters, so the bench can sum two of them word by word. */
static_assert(sizeof(search_stats_t) % sizeof(uint64_t) == 0, "search_stats_t holds uint64_t counters only");
#endif


typedef struct align {
    /*
     * pv_table, use to store the principal variation, we use 64.64
     * array rather than a vector (triangular matrix), the indexing
     * is simplified using a matrix.
     */
    uint64_t repetition_check[MAX_MOVES];
    uint32_t pv_table[MAX_PLY][MAX_PLY];
    int32_t history_moves[12][64];
    int32_t capture_history[12][64][6]; /* [moving piece][target square][captured piece type], see score_move. */
    uint32_t killer_moves[2][MAX_PLY];
    uint32_t excluded_moves[MAX_PLY]; /* The TT move a singular extension search leaves out at this ply (0: none). */
    uint8_t pv_length[MAX_PLY];
    uint64_t nodes;
    uint64_t pv_nodes;         /* Nodes that ordered the previous iteration's PV move first (tests). */
    uint64_t best_move_nodes;  /* Nodes the root's best move took in the last root search (time management). */
#ifdef TIKI_SEARCH_STATS
    search_stats_t stats;
#endif
    uint8_t ply;
    uint8_t root_depth;        /* Depth of the iteration being searched, bounds the singular extensions. */
    uint16_t repetition_index;
    uint16_t null_move_index;  /* repetition_check index of the position after the last null move (0 if none). */
    bool follow_pv;
    bool score_pv;
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
}

/**
 * History is kept from move to move in a game, halved before each search so older results fade.
 * Keeping it unchanged lost Elo (SPRT -16 +- 9, before the scaled bonus). Halving it against
 * clearing it: +4.7 +- 5.6 after 5960 games, stopped at LLR 1.23 [0, 5] (kept as not a loss).
 */
static inline_always void age_history(search_state_t* search_state) {
    int32_t* h = &search_state->history_moves[0][0];
    for (size_t i = 0; i < sizeof(search_state->history_moves) / sizeof(*h); i++) h[i] /= 2;
    int32_t* c = &search_state->capture_history[0][0][0];
    for (size_t i = 0; i < sizeof(search_state->capture_history) / sizeof(*c); i++) c[i] /= 2;
}

static inline_always void init_search_state(search_state_t* search_state) {
    search_state->nodes = 0;
    search_state->pv_nodes = 0;
    search_state->best_move_nodes = 0;
#ifdef TIKI_SEARCH_STATS
    memset(&search_state->stats, 0, sizeof(search_state->stats));
#endif
    search_state->thread_id = 0;
    atomic_init(&search_state->shared_nodes, 0);
    search_state->ply = 0;
    search_state->repetition_index = 0;
    search_state->null_move_index = 0;
    search_state->score = 0;
    search_state->score_pv = false;
    search_state->follow_pv = true;
    clear_search_tables(search_state);
    memset(search_state->history_moves, 0, sizeof (search_state->history_moves));
    memset(search_state->capture_history, 0, sizeof (search_state->capture_history));
    memset(search_state->repetition_check, 0ULL, sizeof(search_state->repetition_check));
    nnue_stack_init(&search_state->nnue);
}

/* Before each search ('go'). The game history from 'position' is kept for the repetition checks. */
static inline_always void clear_pv_table(search_state_t* search_state) {
    search_state->nodes = 0;
    search_state->pv_nodes = 0;
    search_state->best_move_nodes = 0;
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
 * Only positions with the same side to move (every second entry) since the last irreversible
 * move ('fifty_move' plies back) and the last null move can repeat. The position two plies back
 * can't, as each side has only made one move, so the scan starts four plies back.
 */
static inline_always bool contains_repetition(search_state_t* search_state, const uint64_t position_hash, int fifty_move) {
    int oldest = search_state->repetition_index - fifty_move;
    if (oldest < search_state->null_move_index) oldest = search_state->null_move_index;

    for (int i = search_state->repetition_index - 4; i >= oldest; i -= 2) {
        if (search_state->repetition_check[i] == position_hash) return true;
    }
    return false;
}


#endif
