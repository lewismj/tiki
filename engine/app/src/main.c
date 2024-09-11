#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

#include "../../core/src/types.h"
#include "../../core/src/bitboard_ops.h"
#include "../../core/src/bitboard_constants.h"
#include "../../core/src/random.h"
#include "../../core/src/mask_generator.h"
#include "../../core/src/attack_mask.h"
#include "../../core/src/move_encoding.h"

int main(int argc, char* argv[]) {
    setbuf(stdout, 0); // Jetbrains debug need zero buffer.
    printf("tiki\n");

    uint32_t move = encode_move(d2,d4,P,none,0,0,1,0,0);

    //print_move(move);
    piece promoted_piece = get_promoted_piece(move);
    printf("piece: %d\n", promoted_piece);

    return EXIT_SUCCESS;
}