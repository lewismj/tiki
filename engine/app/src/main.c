#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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

    /* Evaluation is HCE, the NNUE model isn't loaded until it is replaced. */

    /* 'tiki_app bench [depth]', run the benchmark and exit (e.g. for OpenBench/regression checks). */
    if (argc >= 2 && strcmp(argv[1], "bench") == 0) {
        int depth = argc >= 3 ? atoi(argv[2]) : 0;
        bench(depth > 0 ? depth : BENCH_DEFAULT_DEPTH);
        return EXIT_SUCCESS;
    }

    /* Run main UCI loop.           */
    uci_main();

    return EXIT_SUCCESS;
}