#include <stdlib.h>

#include "types.h"
#include "bitboard_constants.h"
#include "bitboard_ops.h"
#include "mask_generator.h"
#include "attack_mask.h"


bitboard** create_attack_table(const bitboard* const  mask_array,
                               const unsigned int* const relevant_bit_array,
                               const bitboard* const  magic_numbers,
                               move_function f,
                               int sz) {
    bitboard** result = (bitboard**) malloc(64 * sizeof(bitboard*));
    for (unsigned int sq=0; sq<64; sq++) {
        result[sq] = (bitboard*) calloc(sz, sizeof(bitboard));
    }
    for (unsigned int sq=0; sq<64; sq++) {
        unsigned int relevant_bits = relevant_bit_array[sq];
        int indices = 1 << relevant_bits;
        for (int i=0; i<indices; i++) {
            bitboard occupancy = set_occupancy_variation(i, relevant_bits, mask_array[sq]);
            unsigned int shift = 64 - relevant_bits;
            unsigned int j = (unsigned int) ((occupancy * magic_numbers[sq]) >> shift);
            result[sq][j] = f(sq,occupancy);
        }
    }

    return result;
}

void free_attack_table(bitboard** table) {
    for (unsigned int sq=0; sq<64; sq++) {
        free(table[sq]);
    }
    free(table);
}

bitboard* create_blocker_masks(mask_function f) {
    bitboard* masks = (bitboard*) malloc(64*sizeof(bitboard*));
    for (int sq=0; sq<64; sq++) {
        masks[sq] = f(sq);
    }
    return masks;
}


void free_attack_mask_instance() {
    if (instance  != NULL) {
        free(instance->rook_blocker_masks);
        free_attack_table(instance->rook_attack_table);
        free(instance->bishop_blocker_masks);
        free_attack_table(instance->bishop_attack_table);
        free(instance->king_attacks);
        free(instance->knight_attacks);
        free(instance->pawn_attack_table[0]);
        free(instance->pawn_attack_table[1]);
#ifdef USE_PEXT
        free(instance->pext_attacks);
        free(instance->pext_rook_base);
        free(instance->pext_bishop_base);
#endif
        free(instance);
        instance = NULL;
    }
}

bitboard magic_rook_attack(square s, bitboard occupancy) {
    bitboard tmp = occupancy & instance->rook_blocker_masks[s];
    tmp *= tiki_bitboard_const.rook_magic_numbers[s];
    tmp >>= 64 - tiki_bitboard_const.rook_relevant_bits[s];
    return instance->rook_attack_table[s][(int)tmp];
}

bitboard magic_bishop_attack(square s, bitboard occupancy) {
    bitboard tmp = occupancy & instance->bishop_blocker_masks[s];
    tmp *= tiki_bitboard_const.bishop_magic_numbers[s];
    tmp >>= 64 - tiki_bitboard_const.bishop_relevant_bits[s];
    return instance->bishop_attack_table[s][(int)tmp];
}

bitboard king_attack(square s) {
    return instance->king_attacks[s];
}

bitboard knight_attack(square s) {
    return instance->king_attacks[s];
}

bitboard pawn_attack(square s, colour c) {
    return instance->pawn_attack_table[c][s];
}

tiki_attack_mask_t* create_attack_mask() {
    instance = (tiki_attack_mask_t*)malloc(sizeof(tiki_attack_mask_t));
    if (instance != NULL) {
        /* 2^12 = 4096, possible blocker positions for the rook. */
        instance->rook_blocker_masks = create_blocker_masks(create_rook_blocker_mask);
        instance->rook_attack_table = create_attack_table(instance->rook_blocker_masks,
                                                          tiki_bitboard_const.rook_relevant_bits,
                                                          tiki_bitboard_const.rook_magic_numbers,
                                                          create_rook_attack_mask,
                                                          4096);

        /* 2^9 = 512, possible blocker positions for the Bishop. */
        instance->bishop_blocker_masks = create_blocker_masks(create_bishop_blocker_mask);
        instance->bishop_attack_table = create_attack_table(instance->bishop_blocker_masks,
                                                          tiki_bitboard_const.bishop_relevant_bits,
                                                          tiki_bitboard_const.bishop_magic_numbers,
                                                          create_bishop_attack_mask,
                                                          512);

        instance->king_attacks = (bitboard*) malloc(64 * sizeof(bitboard));
        instance->knight_attacks = (bitboard*) malloc(64* sizeof(bitboard));
        for (int i=0;i<64;i++) {
            instance->king_attacks[i] = create_king_attack_mask(i);
            instance->knight_attacks[i] = create_knight_attack_mask(i);
        }

        instance->pawn_attack_table = (bitboard**) malloc(2 * sizeof(bitboard*));
        instance->pawn_attack_table[0] = (bitboard*) calloc(64, sizeof(bitboard));
        instance->pawn_attack_table[1] = (bitboard*) calloc(64, sizeof(bitboard));
        for (int i=0;i<64;i++) {
            instance->pawn_attack_table[0][i] = create_pawn_attack_mask(i,white);
            instance->pawn_attack_table[1][i] = create_pawn_attack_mask(i, black);
        }

#ifdef USE_PEXT

        /* Create PEXT tables. */
        instance->pext_attacks = (bitboard*) malloc(  (5248 + 102400) * sizeof(bitboard));
        instance->pext_rook_base = (bitboard*) malloc (64 * sizeof(bitboard));
        instance->pext_bishop_base = (bitboard*) malloc (64* sizeof(bitboard));

        int index = 0;

        for (int sq=0; sq<64; sq++) {
            instance->pext_bishop_base[sq] = index;
            bitboard bishop_blocker = instance->bishop_blocker_masks[sq];
            int num_bits = pop_count(bishop_blocker);
            uint64_t indices = 1ULL << num_bits;
            for (uint64_t j = 0; j<indices; j++) {
                bitboard occupancy = __builtin_ia32_pdep_di(j, bishop_blocker);
                instance->pext_attacks[index++] = magic_bishop_attack(sq, occupancy);
            }
        }

        for (int sq=0; sq<64; sq++) {
            instance->pext_rook_base[sq] = index;
            bitboard rook_blocker = instance->rook_blocker_masks[sq];
            int num_bits = pop_count(rook_blocker);
            uint64_t indices = 1ULL << num_bits;
            for (uint64_t j = 0; j<indices; j++) {
                bitboard occupancy = __builtin_ia32_pdep_di (j, rook_blocker);
                instance->pext_attacks[index++] = magic_rook_attack(sq, occupancy);
            }
        }
#endif
        atexit(free_attack_mask_instance);
    }

    return instance;
}

bitboard rook_attack(square s, bitboard occupancy) {
    #ifdef USE_PEXT
        return instance->pext_attacks[instance->pext_rook_base[s] +
                    __builtin_ia32_pext_di (occupancy, instance->rook_blocker_masks[s])];
    #else
        return magic_rook_attack(s,occupancy);
    #endif
}

bitboard bishop_attack(square s, bitboard occupancy) {
#ifdef USE_PEXT
    return instance->pext_attacks[instance->pext_bishop_base[s] +
                                  __builtin_ia32_pext_di (occupancy, instance->bishop_blocker_masks[s])];
#else
    return magic_bishop_attack(s,occupancy);
#endif
}

