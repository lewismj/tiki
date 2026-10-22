#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <core/tiki.h>


int main(int argc, char* argv[]) {
    setbuf(stdout,  0);
    setbuf(stdin, 0);

    /* Initialise tables.           */
    on_startup();

    /* Evaluation is HCE, the NNUE model isn't loaded until it is replaced. */

    /* 'tiki_app bench [depth]', run the benchmark and exit (e.g. for OpenBench/regression checks). */
    if (argc >= 2 && strcmp(argv[1], "bench") == 0) {
        bench(argc >= 3 ? atoi(argv[2]) : 0);
        return EXIT_SUCCESS;
    }

    /* Run main UCI loop.           */
    uci_main();

    return EXIT_SUCCESS;
}
