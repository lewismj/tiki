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
       // uci_main();
    }
    else {
        fprintf(stderr, "Environment variable NNUE_PATH is not set.\n");
        exit(EXIT_FAILURE);
    }

    for (int i=0; i<10;i++)
    {
        board_t board;
        search_state_t search_state;
        init_search_state(&search_state);
        limits_t limits;
        limits.depth = 8;
        reset_time_control(&limits);
        tt_clear();

        /* Move should NOT be d2e4! */
        parse_fen("rn1qkb1r/p3pppp/5n2/1p6/2pP2b1/1P2P3/3N1PPP/RNBQKB1R w KQkq - 1 9", &board);
        recalculate_hash(&board);
        move_t mv = find_best_move(&board, &search_state, &limits);
        printf("%d ",mv);
        print(mv);
    }


    return EXIT_SUCCESS;
}