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
        /* Should make Evaluation a choice between handcrafted and NNUE. */
        fprintf(stderr, "Environment variable NNUE_PATH is not set.\n");
        exit(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}