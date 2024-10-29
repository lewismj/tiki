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
    }
    else {
        fprintf(stderr, "Environment variable NNUE_PATH is not set.\n");
        exit(1);
    }

    /* Run main UCI loop.           */
    uci_main();

    return EXIT_SUCCESS;
}