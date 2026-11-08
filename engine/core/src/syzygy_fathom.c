/*
 * Fathom (engine/third_party/fathom), compiled here so it can be configured without changing its
 * files. Its popcount is the compiler's builtin: Fathom's own choice with an MSVC target is the
 * POPCNT instruction, which the plain x86-64 build doesn't enable; the builtin uses it whenever
 * the target has it.
 */
#define TB_CUSTOM_POP_COUNT(x) __builtin_popcountll(x)
#include "tbprobe.c"
