#include <stdlib.h>
#include <stdio.h>

#include "magic_numbers.h"

int main(int argc, char* argv[]) {
    setbuf(stdout, 0); // Jetbrains debug need zero buffer.
    printf("tiki\n");

    printf("square constants:\n");
    generate_square_constants();
    printf("\nmagic numbers:\n");
    generate_magic_numbers();

    return EXIT_SUCCESS;
}