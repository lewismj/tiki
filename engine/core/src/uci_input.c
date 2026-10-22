#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "uci_input.h"
#include "uci.h"

#define INPUT_QUEUE_SIZE 1024
#define INPUT_LINE_SIZE 100000


static pthread_t thread;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER; /* Queue, stop flag or closed changed. */

static FILE* input;
static char* queue[INPUT_QUEUE_SIZE];
static size_t count;
static bool started;
static bool closed;                 /* No more input will arrive.               */
static limits_t* search_limits;     /* Limits of the running search, or NULL.   */


/* Caller holds the lock. */
static void push(const char* line) {
    while (count == INPUT_QUEUE_SIZE) pthread_cond_wait(&changed, &lock);
    queue[count++] = strdup(line);
    pthread_cond_broadcast(&changed);
}

/* Caller holds the lock. */
static char* remove_at(size_t i) {
    char* line = queue[i];
    memmove(&queue[i], &queue[i + 1], (count - i - 1) * sizeof(queue[0]));
    --count;
    pthread_cond_broadcast(&changed);
    return line;
}

/* Caller holds the lock. A queued 'go' means later commands belong to that search, not the running one. */
static bool go_queued() {
    for (size_t i = 0; i < count; i++) {
        if (parse_uci_command(queue[i]) == uci_cmd_go) return true;
    }
    return false;
}

static void* input_thread(void* arg) {
    static char line[INPUT_LINE_SIZE];

    while (fgets(line, sizeof(line), input)) {
        line[strcspn(line, "\r\n")] = '\0';
        uci_command_t command = parse_uci_command(line);

        pthread_mutex_lock(&lock);
        if (search_limits != NULL && !go_queued() &&
            (command == uci_cmd_stop || command == uci_cmd_quit || command == uci_cmd_isready)) {
            handle_search_input(search_limits, line);
            pthread_cond_broadcast(&changed);
        } else {
            push(line);
        }
        pthread_mutex_unlock(&lock);

        if (command == uci_cmd_quit) break;
    }

    pthread_mutex_lock(&lock);
    /* End of input is treated as 'quit', a running search still completes ('go infinite' ends). */
    if (feof(input) || ferror(input)) push("quit");
    closed = true;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
    return NULL;
}

void input_start(FILE* in) {
    pthread_mutex_lock(&lock);
    input = in;
    count = 0;
    closed = false;
    search_limits = NULL;
    started = true;
    pthread_mutex_unlock(&lock);
    pthread_create(&thread, NULL, input_thread, NULL);
}

void input_join() {
    if (!started) return;
    pthread_join(thread, NULL);

    pthread_mutex_lock(&lock);
    while (count > 0) free(remove_at(count - 1));
    started = false;
    pthread_mutex_unlock(&lock);
}

char* input_pop() {
    pthread_mutex_lock(&lock);
    while (count == 0 && !closed) pthread_cond_wait(&changed, &lock);
    char* line = count > 0 ? remove_at(0) : NULL;
    pthread_mutex_unlock(&lock);
    return line;
}

size_t input_pending() {
    pthread_mutex_lock(&lock);
    size_t n = count;
    pthread_mutex_unlock(&lock);
    return n;
}

void input_search_started(limits_t* limits) {
    pthread_mutex_lock(&lock);
    search_limits = limits;

    /* Commands up to the next 'go' were sent during this search. */
    for (size_t i = 0; i < count; i++) {
        uci_command_t command = parse_uci_command(queue[i]);
        if (command == uci_cmd_go) break;
        if (command == uci_cmd_stop) {
            free(remove_at(i));
            limits->stop_search_flag = true;
            break;
        }
        if (command == uci_cmd_quit) {
            /* Left in the queue, the UCI loop exits on it after 'bestmove'. */
            limits->stop_search_flag = true;
            break;
        }
    }
    pthread_mutex_unlock(&lock);
}

void input_search_finished() {
    pthread_mutex_lock(&lock);
    search_limits = NULL;
    pthread_mutex_unlock(&lock);
}

void input_wait_for_stop(limits_t* limits) {
    pthread_mutex_lock(&lock);
    while (started && !closed && !limits->stop_search_flag) pthread_cond_wait(&changed, &lock);
    pthread_mutex_unlock(&lock);
}
