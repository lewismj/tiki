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


//void init_evaluation_masks() {
//    /* Calculate rank and file masks per square. */
//    bitboard file_mask[64];
//    bitboard rank_mask[64];
//
//    /* Rank/File masks per square. */
//    for (int sq = 0; sq < 64; sq++) {
//        bitboard rank_b = 0ULL;
//        bitboard file_b = 0ULL;
//
//        int rank = sq / 8;
//        int file = sq % 8;
//
//        for (int i = 0; i < 8; i++) {
//            rank_b |= 1ULL << (rank * 8 + i);
//            file_b |= 1ULL << (i * 8 + file);
//        }
//        rank_mask[sq] = rank_b;
//        file_mask[sq] = file_b;
//    }
//
//    for (int sq = 0; sq < 64; sq++) {
//        bitboard isolated_pawn_mask = 0ULL;
//        bitboard passed_pawn_mask = 0ULL;
//
//        int file = sq % 8;
//
//        passed_pawn_mask |= file_mask[sq];
//        if (file == 0) {
//            passed_pawn_mask |= file_mask[sq + 1];
//        }
//        else if (file == 7) {
//            passed_pawn_mask |= file_mask[sq - 1];
//        } else {
//            passed_pawn_mask |= file_mask[sq + 1];
//            passed_pawn_mask |= file_mask[sq - 1];
//        }
//        evaluation_mask_instance.passed_pawn_mask[white][sq] = passed_pawn_mask;
//        evaluation_mask_instance.passed_pawn_mask[black][sq] = passed_pawn_mask;
//    }
//    for (int rank = 0; rank < 8; rank++) {
//        for (int file = 0; file < 8; file++) {
//            square sq = rank * 8 + file;
//            for (int i = 0; i < 8 - rank; i++) {
//                evaluation_mask_instance.passed_pawn_mask[white][sq] &= ~rank_mask[ (7 - i) * 8 + file];
//            }
//
//            for (int i = 0; i < rank + 1; i++) {
//                evaluation_mask_instance.passed_pawn_mask[black][sq] &= ~rank_mask[i * 8 + file];
//            }
//        }
//    }
//}