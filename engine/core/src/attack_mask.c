#include "types.h"
#include "bitboard_constants.h"
#include "bitboard_ops.h"
#include "mask_generator.h"
#include "attack_mask.h"


align tiki_attack_mask_t am_instance;

void set_blocker_masks(bitboard* mask_array, mask_function f) {
    for (int sq=0; sq<64; sq++) {
        mask_array[sq] = f(sq);
    }
}

void set_attack_table(bool init_rook_table) {
    bitboard*  mask_array = 0;
    const unsigned int* relevant_bit_array;
    const bitboard* magic_numbers;
    move_function f;
    int sz;

    if (init_rook_table) {
        mask_array = am_instance.rook_blocker_masks;
        relevant_bit_array = bitboard_const.rook_relevant_bits;
        magic_numbers = bitboard_const.rook_magic_numbers;
        sz = 4096;
        f = create_rook_attack_mask;
    } else {
        mask_array = am_instance.bishop_blocker_masks;
        relevant_bit_array = bitboard_const.bishop_relevant_bits;
        magic_numbers = bitboard_const.bishop_magic_numbers;
        sz = 412;
        f = create_bishop_attack_mask;
    }

    for (unsigned int sq=0; sq<64; sq++) {
        unsigned int relevant_bits = relevant_bit_array[sq];
        int indices = 1 << relevant_bits;
        for (int i=0; i<indices; i++) {
            bitboard occupancy = set_occupancy_variation(i, relevant_bits, mask_array[sq]);
            unsigned int shift = 64 - relevant_bits;
            unsigned int j = (unsigned int) ((occupancy * magic_numbers[sq]) >> shift);
            if (init_rook_table) am_instance.rook_attack_table[sq][j] = f(sq,occupancy);
            else am_instance.bishop_attack_table[sq][j] = f(sq, occupancy);
        }
    }
 }


void init_attack_table() {
    /* 2^12 = 4096, possible blocker positions for the rook. */
    set_blocker_masks(am_instance.rook_blocker_masks, create_rook_blocker_mask);
    set_attack_table(true);

    /* 2^9 = 512, possible blocker positions for the Bishop. */
    set_blocker_masks(am_instance.bishop_blocker_masks, create_bishop_blocker_mask);
    set_attack_table(false);

    /* King attacks. */
    for (int i=0;i<64;i++) {
        am_instance.king_attacks[i] = create_king_attack_mask(i);
        am_instance.knight_attacks[i] = create_knight_attack_mask(i);
    }

    /* Pawn attacks. */
    for (int i=0;i<64;i++) {
        am_instance.pawn_attack_table[0][i] = create_pawn_attack_mask(i, white);
        am_instance.pawn_attack_table[1][i] = create_pawn_attack_mask(i, black);
    }

#ifdef USE_PEXT
    /* Set PEXT tables. */
    int index = 0;
    for (int sq=0; sq<64; sq++) {
        am_instance.pext_bishop_base[sq] = index;
        bitboard bishop_blocker = am_instance.bishop_blocker_masks[sq];
        int num_bits = pop_count(bishop_blocker);
        uint64_t indices = 1ULL << num_bits;
        for (uint64_t j = 0; j<indices; j++) {
            bitboard occupancy = __builtin_ia32_pdep_di(j, bishop_blocker);
            am_instance.pext_attacks[index++] = magic_bishop_attack(sq, occupancy);
        }
    }

    for (int sq=0; sq<64; sq++) {
        am_instance.pext_rook_base[sq] = index;
        bitboard rook_blocker = am_instance.rook_blocker_masks[sq];
        int num_bits = pop_count(rook_blocker);
        uint64_t indices = 1ULL << num_bits;
        for (uint64_t j = 0; j<indices; j++) {
            bitboard occupancy = __builtin_ia32_pdep_di (j, rook_blocker);
            am_instance.pext_attacks[index++] = magic_rook_attack(sq, occupancy);
        }
    }
#endif
}

