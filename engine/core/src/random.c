#include "random.h"


uint32_t next_random_32(uint32_t* state) {
    uint32_t next = *state;

    next ^= next << 13;
    next ^= next >> 17;
    next ^= next << 5;
    *state = next;

    return next;
}


uint64_t next_random_64(uint32_t* state) {
    uint64_t n1 = (uint64_t)(next_random_32(state)) & 0xffff;
    uint64_t n2 = (uint64_t)(next_random_32(state)) & 0xffff;
    uint64_t n3 = (uint64_t)(next_random_32(state)) & 0xffff;
    uint64_t n4 = (uint64_t)(next_random_32(state)) & 0xffff;

    return  n1 | (n2 << 16) | (n3 << 32) | (n4 << 48);
}
