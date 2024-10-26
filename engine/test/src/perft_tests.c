#include <unistd.h>
#include <pthread.h>

#include "unity.h"

#include "perf_tests.h"
#include "../../core/src/board.h"
#include "../../core/src/move_generator.h"
#include "../../core/src/uci.h"


#define MAX_TEST_THREADS 4
#define MAX_LINE_LENGTH 512
char* g_perft_file = NULL;

/**
 * Thread arguments, used in the full perft tests so that we can use multiple
 * threads to run all the 6K+ Perft tests.
 */
typedef struct {
    int id;
    int start_index;
    int end_index;
    perft_data_t* buff;
} thread_args_t;



static int calc_perft(board_t* b, int depth)
{
    if (depth == 0 ) return 1;

    int sum = 0;
    move_buffer_t buffer;
    buffer.index = 0;
    generate_moves(b, &buffer);
    for (int i=0; i<buffer.index; i++) {
        int num_moves = 0;
        if (make_move(b, buffer.moves[i])) {
            num_moves += calc_perft(b, depth - 1);
        }
        pop_move(b);
        sum += num_moves;
    }

    return sum;
}


void perft_tests() {
   int num_perft_positions = (int) (sizeof(perft_data)/sizeof(perft_data[0]));

    for (int i=0; i<num_perft_positions; i++) {
        printf("perft check position [%d]: %s ", i, perft_data[i].position);
        for (int j=0; j<6; j++) {
            if (perft_data[i].values[j] !=-1) {
                board_t board;
                parse_fen(perft_data[i].position, &board);
                int num_moves = calc_perft(&board, j+1);
                if (perft_data[i].values[j] != num_moves) {
                    printf(" case [%d] failed, depth: %d, actual: %d, expected: %d\n", i, j+1, num_moves, perft_data[i].values[j]);
                }
                TEST_ASSERT_EQUAL_INT(perft_data[i].values[j], num_moves);
            }
        }
        printf("\n");
    }
}

void* worker_thread(void* args) {
    thread_args_t* thread_args = (thread_args_t*)args;
    int start = thread_args->start_index;
    int end = thread_args->end_index;

    printf("Thread %d working on indices %d to %d\n", thread_args->id, start, end);

    for (int i = start; i < end; ++i) {
        printf("perft check position [%d]: %s \n", i, thread_args->buff[i].position);
        /*
         * todo: can make this a parameter, running 6K+ positions at depths 1,2,3,4,5, and 6,
         * todo: will take a long time to run for a unit test.
         * todo: 4 or 5 is a good compromise for quicker development test.
         * todo: the 'slow' subset of 102 perft tests are run at depth 1 through 6.
         */
        for (int j=0; j < 4; j++) {
            if (thread_args->buff[i].values[j] !=-1) {
                //(" %d ", thread_args->buff[i].values[j]);
                board_t board;
                parse_fen(thread_args->buff[i].position, &board);
                int num_moves = calc_perft(&board, j+1);
                if (thread_args->buff[i].values[j] != num_moves) {
                    printf(" case [%d] failed, j: %d, actual: %d, expected: %d\n", i, j+1, num_moves, thread_args->buff[i].values[j]);
                }
                TEST_ASSERT_EQUAL_INT(thread_args->buff[i].values[j], num_moves);
            }
        }
        //printf("\n");
    }

    pthread_exit(NULL);
}


void full_perft_test() {
    setbuf(stdout, 0); // Jetbrains debug need zero buffer.

    FILE *file = fopen(g_perft_file, "r");
    if (!file) {
        TEST_FAIL_MESSAGE("Failed to open perft data file.");
    }

    /* Rather than create a linked list we just use array. */
    int limit=8192;
    perft_data_t* full_perft_data = (perft_data_t*) malloc(limit*sizeof(perft_data_t));

    int count =0;
    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), file) && count<limit) {
        char *token = strtok(line, ",");
        int len = strlen(token);
        if (token) {
            full_perft_data[count].position = malloc(len+1 * sizeof(char));
            strncpy(full_perft_data[count].position, token, len);
            full_perft_data[count].position[len] = '\0';
        }
        while ((token = strtok(NULL, ",")) != NULL) {
            int depth = atoi(token);
            token = strtok(NULL, ",");
            int value = (token != NULL) ? atoi(token) : -1;
            if (depth >= 1 && depth <= 6) {
                full_perft_data[count].values[depth - 1] = value;
            }
        }
        count++;
    }
    fclose(file);

    printf("Number of perft records to process: %d\n", count);

    /* We have all the perft data, need to split it amongst a number of threads. */
    pthread_t threads[MAX_TEST_THREADS];
    int items_per_thread = count / MAX_TEST_THREADS;
    int remainder = count % MAX_TEST_THREADS;

    thread_args_t thread_args[MAX_TEST_THREADS];
    for (int i = 0; i < MAX_TEST_THREADS; ++i) {
        thread_args[i].id = i;
        thread_args[i].start_index = i * items_per_thread;
        thread_args[i].buff = full_perft_data;

        if (i == MAX_TEST_THREADS - 1) thread_args[i].end_index = (i + 1) * items_per_thread + remainder;
        else thread_args[i].end_index = (i + 1) * items_per_thread;

        pthread_create(&threads[i], NULL, worker_thread, (void*) &thread_args[i]);
    }

    for (int i = 0; i < MAX_TEST_THREADS; ++i)
        pthread_join(threads[i], NULL);

    for (int i=0;i<count;i++) free(full_perft_data[i].position);
    free(full_perft_data);

}