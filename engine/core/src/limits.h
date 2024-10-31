#ifndef TIKI_LIMITS_H
#define TIKI_LIMITS_H

#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include "types.h"


typedef struct align {
    uint64_t start_time;
    uint64_t stop_time;
    int depth;
    int moves_to_go;
    int move_time;
    int time;
    int increment;
    bool time_set;
    bool stop_search_flag;
    bool stop_engine_flag;
} limits_t;

static inline_always void reset_time_control(limits_t* limits) {
    limits->stop_search_flag = false;
    limits->stop_engine_flag = false;
    limits->time_set = false;
    limits->moves_to_go = 30;
    limits->move_time = -1;
    limits->time = -1;
    limits->increment = 0;
    limits->start_time = 0ULL;
    limits->stop_time = 0ULL;
}

static inline_always uint64_t clock_time_ms() {
    struct timespec time_spec;
    clock_gettime(CLOCK_MONOTONIC_COARSE, &time_spec);
    return (uint64_t) time_spec.tv_sec * 1000 + time_spec.tv_nsec / 1000000;
}

void print_limits(const limits_t* limits);


#endif
