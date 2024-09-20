#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <locale.h>

#include "../../core/src/types.h"
#include "../../core/src/bitboard_ops.h"
#include "../../core/src/bitboard_constants.h"
#include "../../core/src/random.h"
#include "../../core/src/mask_generator.h"
#include "../../core/src/attack_mask.h"
#include "../../core/src/zobrist_key.h"
#include "../../core/src/board.h"
#include "../../core/src/move_generator.h"


int main(int argc, char* argv[]) {
    setbuf(stdout, 0); // Jetbrains debug need zero buffer.
    printf("Tiki Chess\n\n");

    init_attack_table();
    init_zobrist_key();



    return EXIT_SUCCESS;
}