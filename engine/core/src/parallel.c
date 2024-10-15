#include <stdio.h>
#include <pthread.h>
#include <sched.h>
#include "parallel.h"
#include "search.h"


search_threads_t par_instance;

#ifndef __USE_GNU
    #define CPU_SETSIZE 1024
    void CPU_ZERO(cpu_set_t* cpuset) {
        for (int i = 0; i < (CPU_SETSIZE + 63) / 64; i++) cpuset->__bits[i] = 0;
    }

    void CPU_SET(int cpu, cpu_set_t* cpuset) {
        cpuset->__bits[cpu / 64] |= (1UL << (cpu % 64));
}
#endif

bool enqueue(task_buffer_t* task_buffer, search_task_t* search_task) {
    uint8_t tail = atomic_load(&task_buffer->tail);
    size_t next_tail = (tail + 1) & (MAX_QUEUE_SIZE - 1);

    /* Check if buffer is full. */
    if (next_tail == atomic_load(&task_buffer->head)) return false;
    task_buffer->tasks[tail] = search_task;
    atomic_store(&task_buffer->tail, next_tail);
    return true;
}

bool dequeue(task_buffer_t* task_buffer, search_task_t** search_task) {
    uint8_t head = atomic_load(&task_buffer->head);
    if (head == atomic_load(&task_buffer->tail)) return false;    /* Check if buffer is empty. */
    search_task_t * head_task = task_buffer->tasks[head];
    *search_task = head_task;
    atomic_store(&task_buffer->head, (head + 1) & (MAX_QUEUE_SIZE - 1));
    return true;
}

void* search_thread(void* arg) {
    search_thread_data_t *data = (search_thread_data_t *) arg;
    task_buffer_t *buffer = &data->buffer;

    while (!atomic_load(&data->stop_processing)) {
        search_task_t* search_task;
        if (dequeue(buffer,&search_task)) {
            if (search_task != NULL) {
                negamax(-INF,INF,search_task->depth, search_task->board, search_task->search_state);
                if (search_task->search_state->pv_table[0][0] == NULL_MOVE) {
                    printf("null move:\n");
                    printf("depth: %d\n", search_task->depth);
                    printf("board:\n");
                    print_board(search_task->board, show|hex);
                }
                atomic_store(&search_task->is_complete, true);
            }
            else {
               exit(EXIT_FAILURE); /* Producer should never submit a search task. */
            }
        } else {
            sched_yield();
        }
    }
    return NULL;
}

bool submit_task(uint8_t slot, search_task_t* t) {
    task_buffer_t* buffer = &par_instance.search_thread_data[slot]->buffer;
    return enqueue(buffer, t);
}

void free_search_threads() {
    if (par_instance.search_threads != NULL) { /* If threads are running, send shutdown signal. */
        for (int i = 0; i < par_instance.num_search_threads; i++) {
            atomic_store(&par_instance.search_thread_data[i]->stop_processing, true);
        }
    }

    if (par_instance.search_threads != NULL) {  /* Join all the threads. */
        for (int i = 0; i < par_instance.num_search_threads; i++) {
            pthread_join(*par_instance.search_threads[i], NULL);
        }
    }

    if (par_instance.search_threads != NULL) { /* Free the threads. */
        for (int i=0; i< par_instance.num_search_threads; i++) {
            free(par_instance.search_threads[i]);
        }
        free(par_instance.search_threads);
    }

    if (par_instance.search_thread_data != NULL) {  /* Clear the thread queues. */
        for (int i=0; i<par_instance.num_search_threads; i++) {
            if (par_instance.search_thread_data[i] != NULL) {
                free(par_instance.search_thread_data[i]);
            }
        }
        free(par_instance.search_thread_data);
    }

    par_instance.search_threads = NULL;
    par_instance.search_thread_data = NULL;
}

void init_search_threads(int max_threads) {
    par_instance.num_search_threads = sysconf(_SC_NPROCESSORS_ONLN) - 1;
    if (par_instance.num_search_threads < 1) par_instance.num_search_threads = 1;

    if (max_threads > 0 && max_threads < par_instance.num_search_threads)
        par_instance.num_search_threads = max_threads;

    par_instance.search_thread_data =
            (search_thread_data_t**) malloc(par_instance.num_search_threads * sizeof(search_thread_data_t*));

    par_instance.search_threads = (pthread_t**) malloc(par_instance.num_search_threads * sizeof(pthread_t*));
    for (int i=0; i<par_instance.num_search_threads; i++) {
        par_instance.search_thread_data[i] = (search_thread_data_t *) malloc(sizeof(search_thread_data_t));
        atomic_init(&par_instance.search_thread_data[i]->buffer.head, 0);
        atomic_init(&par_instance.search_thread_data[i]->buffer.tail, 0);
        atomic_init(&par_instance.search_thread_data[i]->stop_processing, false);
        atomic_init(&par_instance.search_thread_data[i]->cancel_current_task, false);
    }

    for (int i=0; i<par_instance.num_search_threads; i++) {
        par_instance.search_threads[i] = (pthread_t*) malloc(sizeof(pthread_t*));
        pthread_create(par_instance.search_threads[i], NULL, search_thread, par_instance.search_thread_data[i]);
    }
}