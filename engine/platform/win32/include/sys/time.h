#ifndef TIKI_WIN32_SYS_TIME_H
#define TIKI_WIN32_SYS_TIME_H

/**
 * clock_gettime for Windows builds (src/posix_win32.c), Linux uses the system <sys/time.h>.
 * The monotonic clocks use QueryPerformanceCounter, which has sub-microsecond resolution and is
 * cheap to read (no system call on current Windows), so the coarse clock is the same clock.
 */

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int clockid_t;

#define CLOCK_REALTIME          0
#define CLOCK_MONOTONIC         1
#define CLOCK_MONOTONIC_COARSE  6

int clock_gettime(clockid_t clock, struct timespec* time_spec);

#ifdef __cplusplus
}
#endif

#endif
