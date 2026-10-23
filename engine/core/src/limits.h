#ifndef TIKI_LIMITS_H
#define TIKI_LIMITS_H

#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <stdatomic.h>
#include "types.h"


typedef struct align {
    uint64_t start_time;
    uint64_t stop_time;       /* Hard limit, the search is stopped.                   */
    uint64_t soft_stop_time;  /* Soft limit, no new iteration is started after this.  */
    uint64_t time_checks;     /* Number of clock checks made by the search.           */
    int depth;
    int moves_to_go;
    int move_time;
    int time;
    int increment;
    bool time_set;
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
    limits->infinite = false;
    limits->quiet = false;
    limits->moves_to_go = 30;
    limits->move_time = -1;
    limits->time = -1;
    limits->increment = 0;
    limits->start_time = 0ULL;
    limits->stop_time = 0ULL;
    limits->soft_stop_time = 0ULL;
    limits->time_checks = 0ULL;
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
