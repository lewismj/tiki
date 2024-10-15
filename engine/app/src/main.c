#include <stdlib.h>
#include <stdio.h>
#include <stdalign.h>
#include <sys/time.h>

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
    setbuf(stdout, 0); // Jetbrains debug need zero buffer.
    printf("Tiki Chess\n\n");
    printf("Initializing tables ...");
    on_startup();
    printf("... done\n");
    alignas(64) board_t board;


   // unsafe_parse_fen("4k3/p7/8/8/8/8/8/4K3 w - - 0 1", &board);
    unsafe_parse_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", &board);

    for (int i=0; i<4; i++) {
        volatile int cancel_flag = 0;
        struct timeval start, end;
        gettimeofday(&start, NULL);

        move_t best_move = find_best_move(&board, 9,  &cancel_flag);

        gettimeofday(&end, NULL);
        printf("best move:\n");
        print_move(best_move, show);
        printf("move=%d\n", best_move);
        long seconds = end.tv_sec - start.tv_sec;
        long microseconds = end.tv_usec - start.tv_usec;
        double elapsed = seconds * 1000.0 + microseconds / 1000.0;
        printf("Elapsed time: %.3f sec\n", elapsed / 1000);
    }

//    unsafe_parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",&board);
//    struct timeval start, end;
//    // Use elapsed time not clock time here:
//    printf("Starting Perft.\n");
//    double sum = 0;
//    for (int i =0; i<10; i++) {
//        gettimeofday(&start, NULL);
//        int pn = perft(&board, 6);
//        gettimeofday(&end, NULL);
//        printf("Perft (startpos) :%d\n", pn);
//        long seconds = end.tv_sec - start.tv_sec;
//        long microseconds = end.tv_usec - start.tv_usec;
//        double elapsed = seconds * 1000.0 + microseconds / 1000.0;
//        sum+=elapsed/1000;
//        printf("Elapsed time: %.3f sec\n", elapsed / 1000);
//    }
    printf("Average elapsed time: %.3f sec\n", sum/10);




    return EXIT_SUCCESS;
}