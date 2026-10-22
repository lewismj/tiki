#ifndef TIKI_UCI_INPUT_H
#define TIKI_UCI_INPUT_H

#include <stdio.h>
#include "limits.h"

/**
 * Input thread, reads UCI commands from a stream into a queue for the UCI loop.
 *
 * While a search is running, 'stop', 'quit' and 'isready' are handled on the input thread
 * (see handle_search_input) so the search sees them straight away, unless another 'go' is already
 * queued (they then belong to that search). Every other command is queued and handled in order
 * once the search has finished, so nothing is lost.
 */

/* Start reading 'in' on a new thread. */
void input_start(FILE* in);

/* Wait for the input thread to finish (after 'quit' or end of input), frees anything still queued. */
void input_join();

/* Next queued command, blocks until one is available. NULL once input has ended. Caller frees. */
char* input_pop();

/* Number of queued commands. */
size_t input_pending();

/**
 * Called after the limits are set up for a 'go'. A 'stop' or 'quit' already queued behind the 'go'
 * applies to this search, so it is taken from the queue and the stop flag is set.
 */
void input_search_started(limits_t* limits);

void input_search_finished();

/* 'go infinite', block until 'stop' or 'quit' (or end of input). Returns at once if there's no input thread. */
void input_wait_for_stop(limits_t* limits);

#endif
