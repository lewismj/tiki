#include <stdlib.h>
#include <stdio.h>
#include <core/tiki.h>

static int perft(board_t* b, int depth) {
    if (depth == 0 ) return 1;

    int sum = 0;

    align move_buffer_t buffer;
    buffer.index = 0;
    generate_moves(b, &buffer);
    for (int i=0; i<buffer.index; i++) {
        int num_moves = 0;
        if (make_move(b, buffer.moves[i])) {
            num_moves += perft(b, depth - 1);
        }
        pop_move(b);
        sum += num_moves;
    }

    return sum;
}


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

//    alignas(64) board_t board;
//    limits_t limits;
//    limits.depth = 8;
//    search_state_t search_state;
//    init_search_state(&search_state);
//
//    // unsafe_parse_fen("4k3/p7/8/8/8/8/8/4K3 w - - 0 1", &board);
//    parse_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", &board);
//
//    for (int i=0; i<4; i++) {
//        volatile int cancel_flag = 0;
//        struct timeval start, end;
//        gettimeofday(&start, NULL);
//
//        move_t best_move = find_best_move(&board, &search_state, &limits);
//
//        gettimeofday(&end, NULL);
//        printf("best move:\n");
//        print_move(best_move, show);
//        printf("move=%d\n", best_move);
//        long seconds = end.tv_sec - start.tv_sec;
//        long microseconds = end.tv_usec - start.tv_usec;
//        double elapsed = seconds * 1000.0 + microseconds / 1000.0;
//        printf("Elapsed time: %.3f sec\n", elapsed / 1000);
//    }


//    for (int i=0; i<10;i++)
//    {
//        board_t board;
//        search_state_t search_state;
//        init_search_state(&search_state);
//        limits_t limits;
//        limits.depth = 9;
//        reset_time_control(&limits);
//        tt_clear();
//
//
//        parse_fen("r1bqk2r/ppp1bpp1/5n1p/3p4/3P4/2N2N2/PPP1QPPP/R3KB1R w KQkq - 2 9", &board);
//        parse_go(" go wtime 239834 btime 240348 movestogo 32", &board, &search_state, &limits);
//        //move_t mv = find_best_move(&board, &search_state, &limits);
//        //printf("%d ",mv);
//        //print(mv);
//    }


    return EXIT_SUCCESS;
}