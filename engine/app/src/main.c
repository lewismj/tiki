#include <stdlib.h>
#include <stdio.h>
#include <core/tiki.h>

#include <sys/time.h>

#define start_position "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq - 0 1"


int main(int argc, char* argv[]) {
    setbuf(stdout,  0);
    setbuf(stdin, 0);

    printf("Tiki Chess\n\n");
    on_startup();
    const char *nnue_path = getenv("NNUE_PATH");
//    printf("NNUE: %s\n", nnue_path);
    if (nnue_path != NULL) {
        nnue_init(nnue_path);
    } else {
        fprintf(stderr, "Environment variable NNUE_PATH is not set.\n");
        exit(1);
    }


//        alignas(64) board_t board;
//
//
//        struct timeval start, end;
//        gettimeofday(&start, NULL);
//        parse_fen(start_position, &board);
//        print_board(&board, show|hex);
//        printf("*****\n");
//
//        atomic_bool cancel_flag = false;
//        gettimeofday(&start, NULL);
//
//        search_state_t search_state;
//        init_search_state(&search_state);
//        move_t best_move = find_best_move(&board, &search_state, 6, &cancel_flag);
//
//
//        gettimeofday(&end, NULL);
//        printf("best move:\n");
//        print_move(best_move, show);
//        printf("move=%d\n", best_move);
//        long seconds = end.tv_sec - start.tv_sec;
//        long microseconds = end.tv_usec - start.tv_usec;
//        double elapsed = seconds * 1000.0 + microseconds / 1000.0;
//        printf("Elapsed time: %.3f sec\n", elapsed / 1000);



  // uci_main();


    return EXIT_SUCCESS;
}