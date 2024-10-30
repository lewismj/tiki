#include <stdlib.h>
#include <stdio.h>
#include <core/tiki.h>


int main(int argc, char* argv[]) {
    setbuf(stdout,  0);
    setbuf(stdin, 0);

    /* Initialise tables.           */
    on_startup();

    /* Load NNUE evaluation model.  */
    const char *nnue_path = getenv("NNUE_PATH");
    if (nnue_path != NULL)  {
        nnue_init(nnue_path);
        printf("Initialized NNUE.\n");
        /* Run main UCI loop.           */
        uci_main();
    }
    else {
        fprintf(stderr, "Environment variable NNUE_PATH is not set.\n");
        exit(EXIT_FAILURE);
    }

//    for (int i=0; i<1; i++) {
//        board_t board;
//        search_state_t search_state;
//        init_search_state(&search_state);
//        limits_t limits;
//        limits.depth = 6;
//        reset_time_control(&limits);
//
//        parse_fen("r1b2r2/pp4k1/3P4/3p1RBp/7P/8/P5P1/R6K w - - 0 30", &board);
//        recalculate_hash(&board);
//        //print_board(&board, show|hex);
//        move_t mv = find_best_move(&board, &search_state, &limits);
//        printf("%d ",mv);
//        print(mv);
//    }

//    printf("******************************************\n");
//    for (int i=0; i< 1;i++) {
//        board_t board;
//        search_state_t search_state;
//        init_search_state(&search_state);
//        limits_t limits;
//        limits.depth = 6;
//        reset_time_control(&limits);
//        char *command = "position startpos moves d2d4 g8f6 c2c4 b8c6 d4d5 c6e5 b1c3 e5c4 e2e4 c4e5 f2f4 e5g6 e4e5 f6g8 h2h4 e7e6 g1f3 e6d5 f4f5 g6e7 f3g5 h7h6 g5f7 e8f7 f1d3 h6h5 e1g1 e7c6 c1g5 f8e7 f5f6 g7f6 e5f6 e7c5 g1h1 c6d4 c3d5 d7d6 b2b4 c7c6 b4c5 c6d5 d3g6 f7g6 f6f7 d8f8 d1d4 g8e7 c5d6 e7f5 d4f6 g6h7 f1f5 f8g7 f6g7 h7g7 f7f8R h8f8";
//        parse_position(command, &board, &search_state);
//        print_board(&board, show|hex);
//        char *go = "go depth 6";
//        parse_go(go, &board, &search_state, &limits);
//    }
    return EXIT_SUCCESS;
}