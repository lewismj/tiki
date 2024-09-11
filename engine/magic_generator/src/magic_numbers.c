#include <inttypes.h>
#include <stdio.h>
#include <string.h>


/*
 * Use internal header files for magic and constant generation.
 * No need to expose all these via the engine core/tiki.h
 */
#include "../../core/src/types.h"
#include "../../core/src/bitboard_ops.h"
#include "../../core/src/random.h"
#include "../../core/src/mask_generator.h"

#include "magic_numbers.h"

void generate_square_constants() {
    for (int sq=0; sq<64; sq++) {
        bitboard b = 0ULL;
        set_bit(&b, sq);
        printf("0x%" PRIx64 ",\n", b);
    }
}

uint64_t candidate_magic_number(uint32_t* rng_state) {
    return next_random_64(rng_state) & next_random_64(rng_state) & next_random_64(rng_state);
}

uint64_t find_magic_number(int square,
                           unsigned int num_relevant_bits,
                           mask_function f,
                           move_function g,
                           uint32_t* rng_state) {

    static const int max_iterations = 100000000;

    bitboard occupancies[4096];
    bitboard attacks[4096];
    bitboard used_attacks[4096];


    bitboard attack_mask = f(square);

    int occupancy_indices = (int) 1 << num_relevant_bits;

    for (int index=0; index < occupancy_indices; index++) {
        occupancies[index] = set_occupancy_variation(index, num_relevant_bits, attack_mask);
        attacks[index] = g(square, occupancies[index]);
    }

    for (int i=0; i< max_iterations; i++) {
        bitboard candidate = candidate_magic_number(rng_state);
        if (pop_count((attack_mask * candidate) & 0xFF00000000000000) < 6) continue;
        memset(used_attacks, 0ULL, sizeof(used_attacks));

        bool fail = false;
        for (int index = 0; !fail && index < occupancy_indices; index++) {
            int magic_index = (int)((occupancies[index] * candidate) >> (64 - num_relevant_bits));
            if (used_attacks[magic_index] == 0ULL) used_attacks[magic_index] = attacks[index];
            else if (used_attacks[magic_index] != attacks[index]) fail = true;
        }
        if (!fail) return candidate;
    }

    printf("Failed to find magic number.\n");
    return 0ULL;
}

void generate_magic_numbers() {
    uint32_t rng_state = random_seed;

    printf("\nrook magic numbers:\n");
    for (int i=0; i<64; i++) {
        printf("0x%" PRIx64  "ULL,\n",
               find_magic_number(i,
                                 tiki_bitboard_const.rook_relevant_bits[i],
                                 create_rook_blocker_mask,
                                 create_rook_attack_mask, &rng_state));
    }

    printf("\nbishop magic numbers:\n");
    for (int i=0; i<64; i++) {
        printf("0x%" PRIx64  "ULL,\n",
               find_magic_number(i,
                                 tiki_bitboard_const.bishop_relevant_bits[i],
                                 create_bishop_blocker_mask,
                                 create_bishop_attack_mask, &rng_state));
    }

}