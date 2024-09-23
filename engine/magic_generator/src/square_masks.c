#include <stdio.h>
#include <inttypes.h>

#include "../../core/src/types.h"
#include "../../core/src/bitboard_ops.h"

#include "square_masks.h"

void generate_square_constants() {
    for (int sq=0; sq<64; sq++) {
        bitboard b = 0ULL;
        set_bit(&b, sq);
        printf("0x%" PRIx64 ",\n", b);
    }
}

void generate_rank_masks() {
    for (int sq = 0; sq < 64; sq++) {
        bitboard b = 0ULL;
        int rank = sq / 8;
        for (int file = 0; file < 8; file++) {
            b |= 1ULL << (rank * 8 + file);
        }
        printf("0x%" PRIx64 ",\n", b);
    }
}

void generate_file_masks() {
    for (int sq = 0; sq < 64; sq++) {
        bitboard b = 0ULL;
        int file = sq % 8;
        for (int rank = 0; rank < 8; rank++) {
            b |= 1ULL << (rank * 8 + file);
        }
        printf("0x%" PRIx64 ",\n", b);
    }
}


/**
 * n.b. the rank and file masks are used to generate the isolated pawn
 *      & passed pawn masks.
 */
bitboard rank_mask[64] = {
        0xff,
        0xff,
        0xff,
        0xff,
        0xff,
        0xff,
        0xff,
        0xff,
        0xff00,
        0xff00,
        0xff00,
        0xff00,
        0xff00,
        0xff00,
        0xff00,
        0xff00,
        0xff0000,
        0xff0000,
        0xff0000,
        0xff0000,
        0xff0000,
        0xff0000,
        0xff0000,
        0xff0000,
        0xff000000,
        0xff000000,
        0xff000000,
        0xff000000,
        0xff000000,
        0xff000000,
        0xff000000,
        0xff000000,
        0xff00000000,
        0xff00000000,
        0xff00000000,
        0xff00000000,
        0xff00000000,
        0xff00000000,
        0xff00000000,
        0xff00000000,
        0xff0000000000,
        0xff0000000000,
        0xff0000000000,
        0xff0000000000,
        0xff0000000000,
        0xff0000000000,
        0xff0000000000,
        0xff0000000000,
        0xff000000000000,
        0xff000000000000,
        0xff000000000000,
        0xff000000000000,
        0xff000000000000,
        0xff000000000000,
        0xff000000000000,
        0xff000000000000,
        0xff00000000000000,
        0xff00000000000000,
        0xff00000000000000,
        0xff00000000000000,
        0xff00000000000000,
        0xff00000000000000,
        0xff00000000000000,
        0xff00000000000000
};

bitboard file_mask[64] = {
        0x101010101010101,
        0x202020202020202,
        0x404040404040404,
        0x808080808080808,
        0x1010101010101010,
        0x2020202020202020,
        0x4040404040404040,
        0x8080808080808080,
        0x101010101010101,
        0x202020202020202,
        0x404040404040404,
        0x808080808080808,
        0x1010101010101010,
        0x2020202020202020,
        0x4040404040404040,
        0x8080808080808080,
        0x101010101010101,
        0x202020202020202,
        0x404040404040404,
        0x808080808080808,
        0x1010101010101010,
        0x2020202020202020,
        0x4040404040404040,
        0x8080808080808080,
        0x101010101010101,
        0x202020202020202,
        0x404040404040404,
        0x808080808080808,
        0x1010101010101010,
        0x2020202020202020,
        0x4040404040404040,
        0x8080808080808080,
        0x101010101010101,
        0x202020202020202,
        0x404040404040404,
        0x808080808080808,
        0x1010101010101010,
        0x2020202020202020,
        0x4040404040404040,
        0x8080808080808080,
        0x101010101010101,
        0x202020202020202,
        0x404040404040404,
        0x808080808080808,
        0x1010101010101010,
        0x2020202020202020,
        0x4040404040404040,
        0x8080808080808080,
        0x101010101010101,
        0x202020202020202,
        0x404040404040404,
        0x808080808080808,
        0x1010101010101010,
        0x2020202020202020,
        0x4040404040404040,
        0x8080808080808080,
        0x101010101010101,
        0x202020202020202,
        0x404040404040404,
        0x808080808080808,
        0x1010101010101010,
        0x2020202020202020,
        0x4040404040404040,
        0x8080808080808080
};

void generate_isolated_and_passed_pawn_masks() {
    bitboard isolated_pawns[64];
    bitboard passed_pawns[2][64];

    for (int sq = 0; sq < 64; sq++) {
        bitboard isolated_pawn_mask = 0ULL;
        bitboard passed_pawn_mask = 0ULL;

        int file = sq % 8;

        passed_pawn_mask |= file_mask[sq];
        if (file == 0) {
            isolated_pawn_mask |= file_mask[sq + 1];
            passed_pawn_mask |= file_mask[sq + 1];
        }
        else if (file == 7) {
            isolated_pawn_mask |= file_mask[sq - 1];
            passed_pawn_mask |= file_mask[sq - 1];
        } else
        {
            isolated_pawn_mask |= file_mask[sq + 1];
            isolated_pawn_mask |= file_mask[sq - 1];
            passed_pawn_mask |= file_mask[sq + 1];
            passed_pawn_mask |= file_mask[sq - 1];
        }
        isolated_pawns[sq] = isolated_pawn_mask;
        passed_pawns[white][sq] = passed_pawn_mask;
        passed_pawns[black][sq] = passed_pawn_mask;
    }
    for (int rank = 0; rank < 8; rank++)
    {
        for (int file = 0; file < 8; file++)
        {
            square sq = rank * 8 + file;
            for (int i = 0; i < 8 - rank; i++)
                passed_pawns[white][sq] &= ~rank_mask[(7 - i) * 8 + file];

            for (int i = 0; i < rank + 1; i++)
                passed_pawns[black][sq] &= ~rank_mask[i * 8 + file];
        }
    }

    printf("\nisolated_pawns\n");
    for (int sq=0; sq<64; sq++) {
        printf("0x%" PRIx64 ",\n", isolated_pawns[sq]);
    }

    printf("\npassed pawns:\n");
    printf("{\n");
    for (int i = 0; i < 2; i++) {
        printf("    {");
        for (int j = 0; j < 64; j++) {
            printf("0x%" PRIx64, passed_pawns[i][j]);
            if (j < 63) {
                printf(", ");  // Add a comma between elements
            }
        }
        printf("}");
        if (i < 1) {
            printf(",\n");
        }
    }

    printf("\n};\n");
}