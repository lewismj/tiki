#include "evaluation_mask.h"

typedef struct {
    bitboard rank_mask[64];
    bitboard file_mask[64];
    bitboard isolated_pawn_mask[64];
    bitboard passed_pawn_mask[2][64];
} evaluation_mask_t;


static evaluation_mask_t evaluation_mask_instance = {
        .rank_mask = {
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
        },
        .file_mask = {
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
        }
};

void init_evaluation_masks() {
    for (int sq = 0; sq < 64; sq++) {
        bitboard isolated_pawn_mask = 0ULL;
        bitboard passed_pawn_mask = 0ULL;

        int file = sq % 8;

        passed_pawn_mask |= evaluation_mask_instance.file_mask[sq];
        if (file == 0) {
            isolated_pawn_mask |= evaluation_mask_instance.file_mask[sq + 1];
            passed_pawn_mask |= evaluation_mask_instance.file_mask[sq + 1];
        }
        else if (file == 7) {
            isolated_pawn_mask |= evaluation_mask_instance.file_mask[sq - 1];
            passed_pawn_mask |= evaluation_mask_instance.file_mask[sq - 1];
        } else
        {
            isolated_pawn_mask |= evaluation_mask_instance.file_mask[sq + 1];
            isolated_pawn_mask |= evaluation_mask_instance.file_mask[sq - 1];
            passed_pawn_mask |= evaluation_mask_instance.file_mask[sq + 1];
            passed_pawn_mask |= evaluation_mask_instance.file_mask[sq - 1];
        }
        evaluation_mask_instance.isolated_pawn_mask[sq] = isolated_pawn_mask;
        evaluation_mask_instance.passed_pawn_mask[white][sq] = passed_pawn_mask;
        evaluation_mask_instance.passed_pawn_mask[black][sq] = passed_pawn_mask;
    }
    for (int rank = 0; rank < 8; rank++)
    {
        for (int file = 0; file < 8; file++)
        {
            square sq = rank * 8 + file;
            for (int i = 0; i < 8 - rank; i++) {
                evaluation_mask_instance.passed_pawn_mask[white][sq] &=
                        ~evaluation_mask_instance.rank_mask[ (7 - i) * 8 + file];
            }

            for (int i = 0; i < rank + 1; i++) {
                evaluation_mask_instance.passed_pawn_mask[black][sq] &=
                        ~evaluation_mask_instance.rank_mask[i * 8 + file];
            }
        }
    }
}