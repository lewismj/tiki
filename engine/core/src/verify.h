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
    vf_multicut,     /* singular search: another move >= s_beta and >= beta, node returned        */
    vf_futility,     /* quiet move skipped, static eval + margin <= alpha                         */
    vf_lmp,          /* quiet move skipped, too late in the list                                  */
    vf_see,          /* move skipped, loses material by SEE                                       */
    vf_history,      /* quiet move skipped, history too low                                       */
    vf_lmr,          /* reduced move failed low, not re-searched at full depth (not a cut node)    */
    vf_lmr_cutnode,  /* the same at a cut node                                                     */
    vf_se_reduce,    /* the TT move searched shallower (negative singular extension) failed low    */
    vf_qs_bad,       /* qsearch: losing capture not searched                                       */
    vf_qs_futility,  /* qsearch: capture skipped by futility                                       */
    vf_qs_count,     /* qsearch: moves skipped after the move count (the first one checked)        */
    vf_qs_evasion,   /* qsearch in check: evasions after the first not searched (one checked)       */
    /* The rest change the depth rather than skip work: checked by searching both ways, 'wrong' is
     * that the result changed against the window (below alpha / inside / at or above beta).        */
    vf_iir,          /* node without a TT move searched a ply shallower                            */
    vf_se_ext,       /* singular TT move searched deeper (+1..+3)                                  */
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
