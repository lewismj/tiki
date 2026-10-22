#include <stdio.h>
#include <string.h>
#include "limits.h"

void handle_search_input(limits_t* limits, const char* input) {
    if (strcmp(input, "isready") == 0) {
        printf("readyok\n");
    } else if (strcmp(input, "stop") == 0) {
        limits->stop_search_flag = true;
    } else if (strcmp(input, "quit") == 0) {
        limits->stop_search_flag = true;
        limits->stop_engine_flag = true;
    }
}
