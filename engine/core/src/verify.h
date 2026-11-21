#ifndef TIKI_VERIFY_H
#define TIKI_VERIFY_H

/**
 * Search verification (a TIKI_VERIFY build, `tiki verify ...`): checks that need exact answers and a
 * record of how often each selective decision of the search is wrong. Nothing here changes a normal
 * build: the hooks in search.c compile to nothing without TIKI_VERIFY.
 *
 * Modes (verify_main):
 *   plain     selectivity off (verify_cfg.plain): the score at each depth must equal a plain
 *             alpha-beta reference (no TT, no pruning, same qsearch and draw rules).
 *   plain-tt  as plain with the TT on, cutoffs only from entries of exactly the same depth (so the
 *             score is still the fixed-depth one): checks bounds, fail-soft and mate scores in the TT.
 *   mirror    each position and its colour-flipped twin. Selectivity off: scores must be equal.
 *             Full search: the spread of the differences (generation order differs between the two,
 *             so order-dependent decisions may differ and exact equality isn't expected).
 *   repeat    the same position twice from a clean state: identical score, move and nodes.
 *   decisions the normal search, with a sample of every selective decision checked by doing the work
 *             it skipped (verify_decision_*): how often each feature is wrong, by depth, and what the
 *             skipped work would have cost. Wrong decisions are logged with their position.
 */

#include <stdbool.h>
#include <stdint.h>


typedef enum {
    vf_rfp,          /* reverse futility: node returned >= beta from the static eval              */
    vf_null,         /* null move: node returned >= beta from the null search                     */
    vf_probcut,      /* ProbCut: node returned >= beta from a reduced capture search              */
    vf_multicut,     /* singular search: another move >= s_beta >= beta, node returned            */
    vf_futility,     /* quiet move skipped, static eval + margin <= alpha                         */
    vf_lmp,          /* quiet move skipped, too late in the list                                  */
    vf_see,          /* move skipped, loses material by SEE                                       */
    vf_history,      /* quiet move skipped, history too low                                       */
    vf_lmr,          /* reduced move failed low, not re-searched at full depth (not a cut node)    */
    vf_lmr_cutnode,  /* the same at a cut node                                                     */
    vf_lmr_cut_term, /* at a cut node, re-searched with the reduction less the cut-node term only  */
    vf_se_reduce,    /* the TT move searched a ply shallower (singular_reduce) failed low          */
    vf_qs_bad,       /* qsearch: losing captures skipped (the first one checked)                   */
    vf_delta,        /* qsearch: capture skipped by delta pruning                                  */
    /* The rest change the depth rather than skip work: checked by searching both ways, 'wrong' is
     * that the result changed against the window (below alpha / inside / at or above beta).        */
    vf_iir,          /* node without a TT move searched a ply shallower                            */
    vf_se_ext,       /* singular TT move searched a ply deeper                                     */
    vf_check_ext,    /* node in check searched a ply deeper                                        */
    vf_count
} verify_feature_t;

#define VERIFY_DEPTHS 10   /* depth 1..9, 10+ */

typedef struct {
    uint64_t decisions[VERIFY_DEPTHS];   /* every time the feature acted                         */
    uint64_t checked[VERIFY_DEPTHS];     /* sampled and checked                                  */
    uint64_t wrong[VERIFY_DEPTHS];       /* the skipped work would have changed the outcome      */
    uint64_t miss_total[VERIFY_DEPTHS];  /* sum of |by how much| over the wrong ones (cp)        */
    uint64_t nodes[VERIFY_DEPTHS];       /* nodes the checks took: the work the feature saves    */
    uint64_t big[VERIFY_DEPTHS];         /* wrong by more than 100 cp, mates not counted         */
    uint64_t mates[VERIFY_DEPTHS];       /* wrong, and the skipped work found a mate score        */
} verify_counts_t;

typedef struct {
    bool active;          /* a TIKI_VERIFY build running 'verify'                                  */
    bool plain;           /* every selective feature off (and no correction / TT-adjusted eval)    */
    bool plain_tt;        /* with plain: TT on, cutoffs only from entries of exactly this depth     */
    bool decisions;       /* sample and check the selective decisions                              */
    uint32_t sample;      /* check one decision in 'sample' (per feature)                           */
    int nesting;          /* > 0 inside a check: decisions there are taken but not checked         */
    int skip_ply;         /* the node at this ply, entered next, doesn't take 'skip_feature'       */
    int skip_feature;     /* (a node-level check: the same node searched without the decision)     */
    uint64_t rng;
    verify_counts_t counts[vf_count];
    void* log;            /* FILE*, the wrong decisions                                            */
    int logged;
} verify_config_t;

extern verify_config_t verify_cfg;

/* tt_save outside the checks: every save, the saves dropped because the position's entry from this
 * search is deeper, and those of them that had a new best move (a fail high or exact result with a
 * move other than the stored one). */
typedef struct {
    uint64_t saves;
    uint64_t kept_deeper;
    uint64_t kept_deeper_new_move;
} verify_tt_counts_t;

extern verify_tt_counts_t verify_tt;

/**
 * A pruning rule's exemption, checked where it applies: every move the old rule pruned, those the
 * new rule now searches ('exempted'), and, by making the move, whether each was decided right:
 * an exempted move must give check, a move still pruned must not ('wrong' counts both).
 */
typedef struct {
    const char* name;
    uint64_t old_prunes;
    uint64_t exempted;
    uint64_t wrong;
    uint64_t illegal;   /* pseudo-legal moves that turn out illegal: nothing to decide */
} verify_rule_t;

extern verify_rule_t verify_delta_rule;
extern verify_rule_t verify_lmp_rule;

/**
 * Diagnostic (decisions mode): every quiet check SEE pruning skips, searched to see whether skipping
 * it was wrong, by depth and by its exact SEE loss (in 10 cp bins), so that what any check margin
 * would keep, recover and cost can be worked out afterwards without changing the search. Also by
 * the move's history and its place among the moves searched so far.
 */
#define VD_DEPTHS 9          /* depth 1..8, index = depth */
#define VD_LOSS_BINS 201     /* SEE loss / 10, the last bin 2000 cp and more */
#define VD_HISTORY 3         /* history < 0, 0..1999, >= 2000 */
#define VD_SEARCHED 3        /* moves searched before it: 1..3, 4..7, 8+ */
typedef struct {
    uint64_t n[VD_DEPTHS][VD_LOSS_BINS];
    uint64_t wrong[VD_DEPTHS][VD_LOSS_BINS];
    uint64_t mates[VD_DEPTHS][VD_LOSS_BINS];
    uint64_t nodes[VD_DEPTHS][VD_LOSS_BINS];
    uint64_t history_n[VD_HISTORY], history_wrong[VD_HISTORY], history_mates[VD_HISTORY];
    uint64_t searched_n[VD_SEARCHED], searched_wrong[VD_SEARCHED];
} verify_see_check_diag_t;

extern verify_see_check_diag_t verify_see_check_diag;

/**
 * LMR diagnostic (decisions mode, verify_lmr in search.c): sampled reduced quiet moves, the child
 * searched (null window at alpha) with the search's reduction, at full depth, with one ply less and
 * one ply more reduction, and with each adjustment of lmr_reduction_full left out. All before the
 * search's own reduced search and shallowest first, so a search never finds a deeper one's TT entry
 * for the child.
 *   Moves whose reduced search failed low ("fail"): wrong if the full depth search beats alpha, and
 *   of those, whether one ply less reduction already finds it (the size of the reduction) or only
 *   full depth does (the decision to reduce at all).
 *   Moves whose reduced search beat alpha ("beat", re-searched by the search): good if the full depth
 *   search beats alpha too, and of those, whether one ply more reduction would still have.
 *   Adjustments (VL_ADJ): where one added reduction, would the fail-low move have been found without
 *   it, at what cost in nodes; where one removed reduction, would the good move have been lost without it.
 */
#define VL_R 4           /* reduction 1, 2, 3, 4+ */
#define VL_PLACE 6       /* the move's place among those searched: 2, 3, 4-5, 6-8, 9-16, 17+ */
#define VL_ADJ 5         /* cut node, not improving, gives check, history, PV node */
#define VL_MARGINS 11    /* reduced score - best so far: 1, 2, 3-4, 5-8, 9-16, 17-32, 33-64, 65-128, 129-256, 257+, mate */
#define VL_SAMPLE 4        /* 1 in this many reduced quiet moves is checked (verify_lmr)... */
#define VL_SAMPLE_FAIL 8   /* ...and the deeper searches for 1 in this many of those that fail low. */
typedef struct {
    /* By [cut node][reduction][place]. */
    uint64_t fail_n[2][VL_R][VL_PLACE], fail_wrong[2][VL_R][VL_PLACE], fail_found_less[2][VL_R][VL_PLACE];
    /* By [cut node][reduction]: nodes of the reduced search, the search with one ply less and the full depth one. */
    uint64_t fail_nodes[2][VL_R][3];
    uint64_t fail_less_only;   /* Beat alpha one ply less reduced, but not at full depth (search instability). */
    uint64_t beat_n[2][VL_R], beat_good[2][VL_R], beat_more_n[2][VL_R], beat_more_kept[2][VL_R];
    /* Adjustments [VL_ADJ]: where it added reduction (fail-low moves): checked, wrong, found without it,
     * nodes with it and without it; where it removed reduction: good moves (beat alpha) checked, lost
     * without it, and fail-low moves checked with the nodes with it and without it. */
    uint64_t add_n[VL_ADJ], add_wrong[VL_ADJ], add_found[VL_ADJ], add_nodes_with[VL_ADJ], add_nodes_without[VL_ADJ];
    uint64_t rem_good_n[VL_ADJ], rem_lost[VL_ADJ], rem_fail_n[VL_ADJ], rem_nodes_with[VL_ADJ],
        rem_nodes_without[VL_ADJ];
    uint64_t searched_nodes;   /* Every node the diagnostic's own searches took (not the search's). */
    /* The state check: the reduced search repeated from the restored state (the first VL_REPEATS
     * samples), and how many came out with another score or node count. Must be 0. */
    uint64_t repeats, repeat_mismatches;
    /* Re-search margins: the reduced moves that beat alpha, by [depth band 2-3, 4-6, 7+][margin band
     * (VL_MARGINS): the reduced score above the best score so far, tiki's eval units]: checked;
     * confirmed (beats alpha at full depth); the shallower re-search (one ply less than full depth)
     * checked where it's deeper than the reduced search, and agreeing with full depth; the deeper one
     * (one ply more) changing full depth's verdict; at or beyond ply 2 * root depth; and the nodes
     * of the reduced, shallower, full depth and deeper searches. */
    uint64_t rs_n[3][VL_MARGINS], rs_confirm[3][VL_MARGINS], rs_shallow_n[3][VL_MARGINS],
        rs_shallow_agree[3][VL_MARGINS], rs_deeper_change[3][VL_MARGINS], rs_beyond_limit[3][VL_MARGINS];
    uint64_t rs_nodes[3][VL_MARGINS][4];
} verify_lmr_diag_t;
#define VL_REPEATS 2000

extern verify_lmr_diag_t verify_lmr_diag;

/**
 * Write journal: while 'on', the TT, continuation history and correction history writes are recorded
 * (address, old value, size) so verify_journal_rollback can undo them, newest first. The
 * counterfactual searches of a diagnostic (verify_lmr) each start from the same state this way; the
 * small tables (history, capture history, killers) are copied instead. One search thread.
 */
typedef struct {
    void* addr;
    uint64_t old;
    uint8_t size;
} verify_journal_entry_t;

typedef struct {
    verify_journal_entry_t* entries;
    size_t len, cap;
    bool on;
} verify_journal_t;

extern verify_journal_t verify_journal;
void verify_journal_grow(void);
void verify_journal_rollback(void);

static inline void verify_journal_record(void* addr, uint64_t old, int size) {
    if (!verify_journal.on) return;
    if (verify_journal.len == verify_journal.cap) verify_journal_grow();
    verify_journal.entries[verify_journal.len++] = (verify_journal_entry_t) { addr, old, (uint8_t) size };
}

/* 'exempted': the new rule searches the move the old one pruned. 'move' is pseudo-legal on 'board'. */
void verify_rule_record(verify_rule_t* rule, bool exempted, void* board, uint32_t move);

/* Whether this decision is to be checked (a TIKI_VERIFY build in 'decisions' mode, outside a check,
 * and sampled). Counts the decision either way. */
bool verify_take(verify_feature_t feature, int depth);

/* The result of a check: 'wrong' if the skipped work changed the outcome, 'miss' by how much (cp),
 * 'nodes' what it took. 'fen' (the node's position) and 'context' are logged when it's wrong. */
void verify_record(verify_feature_t feature, int depth, bool wrong, int miss, uint64_t nodes, const char* fen,
                   const char* context);

/**
 * What a pruning decision skipped, for the skip-and-reduce features (not the depth changes): the
 * move's kind (node-level: the whole node, no move), and, for the wrong ones, how much was missed
 * and what kind of resource it was. 'bound' and 'found' are from the side whose resource was
 * missed: the mover for a move skipped (alpha, the move's score), the opponent for a node cut off
 * (-beta, -the node's score). Kinds, in this order:
 *   mate      'found' is a mate (or tablebase win) for that side;
 *   defence   that side was clearly worse (bound <= -100, or mated) and the resource gains >= 50 cp;
 *   tactic    that side ends clearly better (found >= +100) and the resource gains >= 50 cp;
 *   small     the rest (gains under 50 cp, or level positions).
 * Wrong ones are also looked up in the TT after the search: 'found later' if the position's entry
 * holds the skipped move as its best (a move) or says, from a deeper search than the decision's,
 * that the node is below beta (a node). 'no entry' if the position's entry was replaced.
 */
typedef enum { vc_node, vc_check, vc_capture, vc_quiet, vc_count } verify_class_t;

#define VERIFY_DELTAS 5        /* gain 1-20, 21-50, 51-100, >100 cp, mate scores involved */
#define VERIFY_KINDS 4         /* mate, defence, tactic, small                           */
#define VERIFY_DEPTH_BANDS 3   /* decision depth 1-3 (and qsearch), 4-6, 7+               */

typedef struct {
    uint64_t checked[vc_count];
    uint64_t wrong[vc_count];
    uint64_t delta[vc_count][VERIFY_DELTAS];
    uint64_t kind[vc_count][VERIFY_KINDS];
    uint64_t depth_band[vc_count][VERIFY_DEPTH_BANDS];
    uint64_t later[vc_count][3];   /* found later, not found, no entry */
} verify_class_counts_t;

extern verify_class_counts_t verify_classes[vf_count];

void verify_classify(verify_feature_t feature, int depth, bool wrong, verify_class_t cls, uint32_t move,
                     uint64_t hash, int bound, int found);

/* After each search: the wrong decisions of that search looked up in the TT (found later or not). */
void verify_resolve_later(void);

/* 'tiki verify <mode> <fen file> <depth> [sample] [log file]', returns the process exit code. */
int verify_main(int argc, char* argv[]);

#endif
