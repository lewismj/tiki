#include "nnue.h"

/**
 * The network built into the binary. With TIKI_EMBED_NETWORK=ON, CMake defines
 * TIKI_EMBEDDED_NETWORK (the .nnue file's absolute path) and TIKI_EMBEDDED_NETWORK_NAME (its file
 * name), and the assembler includes the file as read only data. .incbin rather than C23 #embed,
 * which needs clang 19 or GCC 15, and it's the same for ELF (Linux) and COFF (Windows) apart from
 * the section name.
 */

#ifdef TIKI_EMBEDDED_NETWORK

#if defined(_WIN32)
#define EMBEDDED_SECTION ".section .rdata,\"dr\"\n"
#else
#define EMBEDDED_SECTION ".section .rodata\n"
#endif

__asm__(EMBEDDED_SECTION
        ".balign 64\n"
        ".globl tiki_embedded_network\n"
        "tiki_embedded_network:\n"
        ".incbin \"" TIKI_EMBEDDED_NETWORK "\"\n"
        "tiki_embedded_network_end:\n"
        ".balign 8\n"
        ".globl tiki_embedded_network_size\n"
        "tiki_embedded_network_size:\n"
        ".quad tiki_embedded_network_end - tiki_embedded_network\n"
        ".text\n");

extern const unsigned char tiki_embedded_network[];
extern const uint64_t tiki_embedded_network_size;

const char* nnue_embedded_name(void) {
    return TIKI_EMBEDDED_NETWORK_NAME;
}

bool nnue_load_embedded(void) {
    return nnue_load_memory(tiki_embedded_network, (size_t) tiki_embedded_network_size, TIKI_EMBEDDED_NETWORK_NAME);
}

#else

const char* nnue_embedded_name(void) {
    return NULL;
}

bool nnue_load_embedded(void) {
    return false;
}

#endif
