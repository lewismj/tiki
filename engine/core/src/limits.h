#ifndef TIKI_LIMITS_H
#define TIKI_LIMITS_H

#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <stdatomic.h>
#include "types.h"

/* 'Move Overhead' option (ms): kept back from the clock for what every move loses outside the
 * search (the GUI, process switching, output), so the engine doesn't lose on time. */
#define MOVE_OVERHEAD_DEFAULT 30
#define MOVE_OVERHEAD_MAX 5000

/*
 * Time management for a clock ('wtime'/'btime', not 'movetime'). From what's left (less the move
 * overhead) and the increment, a base time per move:
 *     base = available / TM_MOVES_LEFT + increment * TM_INCREMENT_PERCENT / 100
 * ('movestogo n' divides by n instead). Then two limits:
 *   - soft = base * TM_SOFT_PERCENT / 100, checked between depths: no new depth once the time used
 *     passes soft * scale, where the scale (search.c, tm_scale) is larger when the best move just
 *     changed or got few of the root's nodes, and smaller when it has been stable and took most;
 *   - hard = base * TM_HARD_PERCENT / 100, never more than half of what's left, stops the search
 *     wherever it is.
 */
#define TM_MOVES_LEFT 20
#define TM_INCREMENT_PERCENT 75
#define TM_SOFT_PERCENT 60
#define TM_HARD_PERCENT 250
#define TM_SCALE_MIN_DEPTH 5     /* The soft limit is scaled from this depth (the first are near instant). */
#define TM_STABILITY_MAX 4       /* Depths of an unchanged best move counted towards the scale.           */


typedef struct align {
    uint64_t start_time;
    uint64_t stop_time;       /* Hard limit, the search is stopped.                   */
    uint64_t soft_stop_time;  /* Soft limit, no new iteration is started after this (times the scale if adaptive). */
    uint64_t time_checks;     /* Number of clock checks made by the search (main thread only). */
    uint64_t nodes;           /* 'go nodes', the main thread's node limit, 0 for none.          */
    int depth;
    int threads;              /* Search threads, 1 plus Lazy SMP helpers ('Threads' option).    */
    int moves_to_go;
    int move_time;
    int time;
    int increment;
    bool time_set;
    bool adaptive;            /* The soft limit is scaled by the search's stability (a clock, not 'movetime'). */
    bool infinite;
    bool quiet;               /* No 'info' output (datagen, bench tools).           */
    /* Set by the input thread ('stop', 'quit') while the search reads them. */
    atomic_bool stop_search_flag;
    atomic_bool stop_engine_flag;
} limits_t;

static inline_always void reset_time_control(limits_t* limits) {
    limits->stop_search_flag = false;
    limits->stop_engine_flag = false;
    limits->time_set = false;
    limits->adaptive = false;
    limits->infinite = false;
    limits->quiet = false;
    limits->threads = 1;
    limits->moves_to_go = 30;
    limits->move_time = -1;
    limits->time = -1;
    limits->increment = 0;
    limits->start_time = 0ULL;
    limits->stop_time = 0ULL;
    limits->soft_stop_time = 0ULL;
    limits->time_checks = 0ULL;
    limits->nodes = 0ULL;
}

static inline_always uint64_t clock_time_ms() {
    struct timespec time_spec;
    clock_gettime(CLOCK_MONOTONIC_COARSE, &time_spec);
    return (uint64_t) time_spec.tv_sec * 1000 + time_spec.tv_nsec / 1000000;
}


/**
 * Handle a line of input received while a search is running, called from the input thread.
 *  'stop'    - stop the search.
 *  'quit'    - stop the search and exit the engine.
 *  'isready' - reply 'readyok', search continues.
 * Anything else is ignored.
 */
void handle_search_input(limits_t* limits, const char* input);


#endif
