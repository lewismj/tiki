#include <stdlib.h>

#include "types.h"
#include "bitboard_constants.h"
#include "bitboard_ops.h"
#include "mask_generator.h"
#include "attack_mask.h"


/**
 *
 */
typedef struct {
    bitboard* rook_blocker_masks;
    bitboard** rook_attack_table;
    bitboard* bishop_blocker_masks;
    bitboard** bishop_attack_table;
    bitboard* king_attacks;
    bitboard* knight_attacks;
    bitboard** pawn_attack_table;
#ifdef USE_PEXT
    bitboard* pext_attacks;
    bitboard* pext_rook_base;
    bitboard* pext_bishop_base;
#endif
} tiki_attack_mask_t;


static tiki_attack_mask_t* am_instance = NULL;

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
    if (am_instance != NULL) {
        free(am_instance->rook_blocker_masks);
        free_attack_table(am_instance->rook_attack_table);
        free(am_instance->bishop_blocker_masks);
        free_attack_table(am_instance->bishop_attack_table);
        free(am_instance->king_attacks);
        free(am_instance->knight_attacks);
        free(am_instance->pawn_attack_table[0]);
        free(am_instance->pawn_attack_table[1]);
#ifdef USE_PEXT
        free(am_instance->pext_attacks);
        free(am_instance->pext_rook_base);
        free(am_instance->pext_bishop_base);
#endif
        free(am_instance);
        am_instance = NULL;
    }
}

bitboard magic_rook_attack(square s, bitboard occupancy) {
    bitboard tmp = occupancy & am_instance->rook_blocker_masks[s];
    tmp *= bitboard_const.rook_magic_numbers[s];
    tmp >>= 64 - bitboard_const.rook_relevant_bits[s];
    return am_instance->rook_attack_table[s][(int)tmp];
}

bitboard magic_bishop_attack(square s, bitboard occupancy) {
    bitboard tmp = occupancy & am_instance->bishop_blocker_masks[s];
    tmp *= bitboard_const.bishop_magic_numbers[s];
    tmp >>= 64 - bitboard_const.bishop_relevant_bits[s];
    return am_instance->bishop_attack_table[s][(int)tmp];
}

bitboard king_attack(square s) {
    return am_instance->king_attacks[s];
}

bitboard knight_attack(square s) {
    return am_instance->knight_attacks[s];
}

bitboard pawn_attack(square s, colour c) {
    return am_instance->pawn_attack_table[c][s];
}

tiki_attack_mask_t* create_attack_mask() {
    am_instance = (tiki_attack_mask_t*)malloc(sizeof(tiki_attack_mask_t));
    if (am_instance != NULL) {
        /* 2^12 = 4096, possible blocker positions for the rook. */
        am_instance->rook_blocker_masks = create_blocker_masks(create_rook_blocker_mask);
        am_instance->rook_attack_table = create_attack_table(am_instance->rook_blocker_masks,
                                                             bitboard_const.rook_relevant_bits,
                                                             bitboard_const.rook_magic_numbers,
                                                             create_rook_attack_mask,
                                                             4096);

        /* 2^9 = 512, possible blocker positions for the Bishop. */
        am_instance->bishop_blocker_masks = create_blocker_masks(create_bishop_blocker_mask);
        am_instance->bishop_attack_table = create_attack_table(am_instance->bishop_blocker_masks,
                                                               bitboard_const.bishop_relevant_bits,
                                                               bitboard_const.bishop_magic_numbers,
                                                               create_bishop_attack_mask,
                                                               512);

        am_instance->king_attacks = (bitboard*) malloc(64 * sizeof(bitboard));
        am_instance->knight_attacks = (bitboard*) malloc(64 * sizeof(bitboard));
        for (int i=0;i<64;i++) {
            am_instance->king_attacks[i] = create_king_attack_mask(i);
            am_instance->knight_attacks[i] = create_knight_attack_mask(i);
        }

        am_instance->pawn_attack_table = (bitboard**) malloc(2 * sizeof(bitboard*));
        am_instance->pawn_attack_table[0] = (bitboard*) calloc(64, sizeof(bitboard));
        am_instance->pawn_attack_table[1] = (bitboard*) calloc(64, sizeof(bitboard));
        for (int i=0;i<64;i++) {
            am_instance->pawn_attack_table[0][i] = create_pawn_attack_mask(i, white);
            am_instance->pawn_attack_table[1][i] = create_pawn_attack_mask(i, black);
        }

#ifdef USE_PEXT

        /* Create PEXT tables. */
        am_instance->pext_attacks = (bitboard*) malloc((5248 + 102400) * sizeof(bitboard));
        am_instance->pext_rook_base = (bitboard*) malloc (64 * sizeof(bitboard));
        am_instance->pext_bishop_base = (bitboard*) malloc (64 * sizeof(bitboard));

        int index = 0;

        for (int sq=0; sq<64; sq++) {
            am_instance->pext_bishop_base[sq] = index;
            bitboard bishop_blocker = am_instance->bishop_blocker_masks[sq];
            int num_bits = pop_count(bishop_blocker);
            uint64_t indices = 1ULL << num_bits;
            for (uint64_t j = 0; j<indices; j++) {
                bitboard occupancy = __builtin_ia32_pdep_di(j, bishop_blocker);
                am_instance->pext_attacks[index++] = magic_bishop_attack(sq, occupancy);
            }
        }

        for (int sq=0; sq<64; sq++) {
            am_instance->pext_rook_base[sq] = index;
            bitboard rook_blocker = am_instance->rook_blocker_masks[sq];
            int num_bits = pop_count(rook_blocker);
            uint64_t indices = 1ULL << num_bits;
            for (uint64_t j = 0; j<indices; j++) {
                bitboard occupancy = __builtin_ia32_pdep_di (j, rook_blocker);
                am_instance->pext_attacks[index++] = magic_rook_attack(sq, occupancy);
            }
        }
#endif
        atexit(free_attack_mask_instance);
    }

    return am_instance;
}

bitboard rook_attack(square s, bitboard occupancy) {
    #ifdef USE_PEXT
        return am_instance->pext_attacks[am_instance->pext_rook_base[s] +
                                         __builtin_ia32_pext_di (occupancy, am_instance->rook_blocker_masks[s])];
    #else
        return magic_rook_attack(s,occupancy);
    #endif
}

bitboard bishop_attack(square s, bitboard occupancy) {
#ifdef USE_PEXT
    return am_instance->pext_attacks[am_instance->pext_bishop_base[s] +
                                     __builtin_ia32_pext_di (occupancy, am_instance->bishop_blocker_masks[s])];
#else
    return magic_bishop_attack(s,occupancy);
#endif
}

bitboard queen_attack(square s, bitboard occupancy) {
   return rook_attack(s, occupancy) | bishop_attack(s, occupancy);
}

void init_attack_table() {
   create_attack_mask();
}


