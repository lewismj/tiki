#ifndef TIKI_PARALLEL_H
#define TIKI_PARALLEL_H



/*
 * #include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

// Define your types (replace these with your actual definitions)
typedef struct {
    // Example fields
    int some_field;
} search_state_t;

typedef struct {
    // Example fields
    int another_field;
} board_t;

typedef struct {
    // Example fields
    int move_value; // Placeholder for the move representation
} move_t;

// Function declaration
static move_t find_best_move_at_depth(search_state_t* search_state,
                                        board_t* board,
                                        int depth,
                                        atomic_bool cancel_flag);

// Struct to hold parameters for the thread
typedef struct {
    search_state_t* search_state;
    board_t* board;
    int depth;
    atomic_bool* cancel_flag; // Pointer to cancel flag
    move_t* best_move;        // Pointer to store the result
} thread_params_t;

// Thread function that wraps the call to find_best_move_at_depth
void* thread_find_best_move(void* arg) {
    thread_params_t* params = (thread_params_t*)arg;
    // Call the function and store the result
    *params->best_move = find_best_move_at_depth(params->search_state,
                                                  params->board,
                                                  params->depth,
                                                  *params->cancel_flag);
    return NULL;
}

// Example usage
int main() {
    // Create instances of your structures
    search_state_t search_state = { .some_field = 1 }; // Initialize as needed
    board_t board = { .another_field = 2 }; // Initialize as needed

    // Initialize a cancel flag
    atomic_bool cancel_flag;
    atomic_init(&cancel_flag, false); // Initialize to false

    // Set the depth for the search
    int depth = 5;

    // Allocate memory for the best_move
    move_t best_move;

    // Set up the parameters for the thread
    thread_params_t params;
    params.search_state = &search_state;
    params.board = &board;
    params.depth = depth;
    params.cancel_flag = &cancel_flag; // Pass the address of cancel_flag
    params.best_move = &best_move;      // Pass the address to store the result

    // Create a thread to execute find_best_move_at_depth
    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, thread_find_best_move, &params) != 0) {
        perror("Failed to create thread");
        return 1; // Handle thread creation failure
    }

    // Wait for the thread to finish
    pthread_join(thread_id, NULL);

    // Use the result stored in best_move
    printf("Best move value: %d\n", best_move.move_value);

    return 0;
}

// Example implementation of find_best_move_at_depth
static move_t find_best_move_at_depth(search_state_t* search_state,
                                        board_t* board,
                                        int depth,
                                        atomic_bool cancel_flag) {
    move_t best_move;
    // Your search logic goes here...

    // Example logic for demonstration purposes
    best_move.move_value = depth * 10; // Placeholder logic
    return best_move;
}

 */


#endif
