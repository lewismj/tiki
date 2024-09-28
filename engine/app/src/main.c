#include <stdlib.h>
#include <stdio.h>
#include <stdalign.h>
#include <sys/time.h>

#include "../../core/src/types.h"
#include "../../core/src/attack_mask.h"
#include "../../core/src/zobrist_key.h"
#include "../../core/src/board.h"
#include "../../core/src/move_generator.h"
#include "../../core/src/evaluation/evaluation_mask.h"
#include "../../core/src/evaluation/evaluation.h"

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
    init_attack_table();
    init_zobrist_key();
    init_evaluation_masks();
    printf("... done\n");


    alignas(64) board_t board;
    //unsafe_parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board);
    unsafe_parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &board);
    move_buffer_t buffer1;
    buffer1.index = 0;


    struct timeval start, end;
    // Use elapsed time not clock time here:
    printf("Starting Perft.\n");
    double sum = 0;
    for (int i =0; i<10; i++) {
        gettimeofday(&start, NULL);
        int pn = perft(&board, 6);
        gettimeofday(&end, NULL);
        printf("Perft (startpos) :%d\n", pn);
        long seconds = end.tv_sec - start.tv_sec;
        long microseconds = end.tv_usec - start.tv_usec;
        double elapsed = seconds * 1000.0 + microseconds / 1000.0;
        sum+=elapsed/1000;
        printf("Elapsed time: %.3f sec\n", elapsed / 1000);
    }
    printf("Average elapsed time: %.3f sec\n", sum/10);

    return EXIT_SUCCESS;
}