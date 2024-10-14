#ifndef TIKI_PARALLEL_H
#define TIKI_PARALLEL_H

#include <stdatomic.h>
#include <inttypes.h>

#include "types.h"
#include "board.h"
#include "search_state.h"

/**
 * Basic parallelism, when iterative deepening rely up the fact that other threads
 * may fill up the transposition table for us.
 *
 * Single 'producer', creating tasks that can be round-robin dispatched to consumer.
 * No need for a central queue, we know we can round robin dispatch, with each task
 * expected to take longer than previous.
 *
 * Each consumer will have its own queue and given tasks >= number of consumers
 * (i.e. threads) we use a lockless circular buffer per thread.
 */

#define MAX_QUEUE_SIZE 8 /* Max queue size *per worker*. */
/**
 * Search task is the pointer to search state, used for input/output from the task.
 *
 * Producer creates a fixed number of search tasks which it manages the lifetime of,
 * these tasks as enqueued to the threads.
 */
typedef struct align {
    search_state_t* search_state;
    board_t*        board;
    uint8_t         depth;
    atomic_bool     is_complete;
} search_task_t;

/**
 * Each consumer thread performing a search has is own circular buffer. If number of concurrent tasks
 * (searches) requested exceeds the available number of threads allocated.
 */
typedef struct align {
    search_task_t*  tasks[MAX_QUEUE_SIZE];
    atomic_uint head;
    atomic_uint tail;
} task_buffer_t;

/**
 * Thread function, takes a pointer to the queue it should be using and a cancel flag.
 */
typedef struct align {
    task_buffer_t buffer;               /* The thread functions circular buffer.        */
    atomic_bool cancel_current_task;    /* Cancel the current task flag.                */
    atomic_bool stop_processing;        /* Stop processing and shutdown thread flag.    */
} search_thread_data_t;

/**
 * Each thread will have its own 'thread data' that includes a circular buffer of tasks and some
 * flags to indicate task cancellation or system shutdown.
 */
typedef struct align {
    long num_search_threads;                    /* The number of search threads initialized.                        */
    pthread_t** search_threads;                 /* The search threads.                                              */
    search_thread_data_t** search_thread_data;  /* The I/O structure used to communicate with an individual thread. */
} search_threads_t;

extern search_threads_t par_instance;

/**
 *  Initialize the threading, max_threads is the number of threads to use for searching (iterative deepening).
 *  If max_threads = -1 , then it will choose max(1,  number of processors -1)
 *  Otherwise min(max_threads, number of processors -1).
 *  i.e. It would spin up more threads than available cores, little point on threads that are not IO bound.
 */
void init_search_threads(int max_threads);
void free_search_threads();

bool submit_task(uint8_t slot, search_task_t* t);

#endif
