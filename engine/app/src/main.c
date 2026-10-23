#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <core/tiki.h>


int main(int argc, char* argv[]) {
    setbuf(stdout,  0);
    setbuf(stdin, 0);

    /* Initialise tables.           */
    on_startup();

    /* 'tiki_app nnue-eval <net> <fen>', the FEN may be given as several arguments. */
    if (argc >= 4 && strcmp(argv[1], "nnue-eval") == 0) {
        char fen[128] = "";
        for (int i = 3; i < argc; i++) {
            if (i > 3) strncat(fen, " ", sizeof(fen) - strlen(fen) - 1);
            strncat(fen, argv[i], sizeof(fen) - strlen(fen) - 1);
        }
        return nnue_eval_fen(argv[2], fen) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    /* Everything else searches, which needs the network. */
    if (!load_startup_network()) return EXIT_FAILURE;

    /* 'tiki_app bench [depth]', run the benchmark and exit (e.g. for OpenBench/regression checks). */
    if (argc >= 2 && strcmp(argv[1], "bench") == 0) {
        bench(argc >= 3 ? atoi(argv[2]) : 0);
        return EXIT_SUCCESS;
    }

    /* 'tiki_app datagen <file> [games] [depth] [seed]', NNUE training data (see training/README.md). */
    if (argc >= 3 && strcmp(argv[1], "datagen") == 0) {
        int games = argc >= 4 ? atoi(argv[3]) : 100;
        int depth = argc >= 5 ? atoi(argv[4]) : 8;
        uint32_t seed = argc >= 6 ? (uint32_t) strtoul(argv[5], NULL, 10) : 1;
        int64_t written = datagen_to_file(argv[2], games, depth, seed);
        if (written < 0) {
            fprintf(stderr, "Could not open '%s'.\n", argv[2]);
            return EXIT_FAILURE;
        }
        printf("%lld positions written to %s\n", (long long) written, argv[2]);
        return EXIT_SUCCESS;
    }

    /* Run main UCI loop.           */
    uci_main();

    return EXIT_SUCCESS;
}
