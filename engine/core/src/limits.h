#ifndef TIKI_LIMITS_H
#define TIKI_LIMITS_H

#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "types.h"

typedef struct align {
    bool time_set;
    int start_time;
    int stop_time;
    int moves_to_go;
    int move_time;
    int time;
    int increment;
    atomic_bool cancel_flag;
} limits_t;

static inline_always void reset_time_control(limits_t* limits) {
    limits->time_set = false;
    limits->move_time = 30;
    limits->move_time = 0;
    limits->increment = 0;
}

static inline_always int clock_time_ms() {
    struct timespec time_spec;
    clock_gettime(CLOCK_MONOTONIC_COARSE, &time_spec);
    return time_spec.tv_sec * 1000 + time_spec.tv_nsec / 1000000;
}


#endif
