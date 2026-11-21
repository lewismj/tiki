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
    vf_lmr,          /* reduced move failed low, not re-searched at full depth                    */
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

/* Whether this decision is to be checked (a TIKI_VERIFY build in 'decisions' mode, outside a check,
 * and sampled). Counts the decision either way. */
bool verify_take(verify_feature_t feature, int depth);

/* The result of a check: 'wrong' if the skipped work changed the outcome, 'miss' by how much (cp),
 * 'nodes' what it took. 'fen' (the node's position) and 'context' are logged when it's wrong. */
void verify_record(verify_feature_t feature, int depth, bool wrong, int miss, uint64_t nodes, const char* fen,
                   const char* context);

/* 'tiki verify <mode> <fen file> <depth> [sample] [log file]', returns the process exit code. */
int verify_main(int argc, char* argv[]);

#endif
