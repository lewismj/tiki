#include <stdio.h>
#include <inttypes.h>
#include "bitboard_ops.h"
#include "bitboard_constants.h"

/**
 * Print bitboard to standard output.
 */
void print_bitboard(const bitboard* b, showable options) {
    if (options & show || options & min) {
        for (int rank=0; rank <8; rank++) {
            for (int file=0; file <8; file++) {
                if (file == 0) printf("%2d  ", 8 - rank);
                is_bit_set(b, rank * 8 + file) ? printf(" 1 ") : printf(" 0 ");
            }
            printf("\n");
        }
        printf("     A  B  C  D  E  F  G  H\n");
    }

    if (options & hex)
    {
        printf("hex: 0x%" PRIx64 "\n", *b);
    }
    printf("\n");
}
