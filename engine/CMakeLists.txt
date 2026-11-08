cmake_minimum_required (VERSION 3.24)
project(tiki)

set(CMAKE_C_STANDARD 23)

if (UNIX)
    add_compile_options(-fPIC)
    if (CMAKE_BUILD_TYPE MATCHES "^[Rr]elease")
        add_compile_options(-O3)
    endif()
endif()

# Windows: clang targeting the MSVC ABI (CC=clang CXX=clang++, not cl or clang-cl). The POSIX
# headers tiki uses come from platform/win32 (added to the core library). The CRT is linked
# statically, so tiki needs no Visual C++ redistributable, and the main thread gets Linux's
# 8MB stack (the Windows default is 1MB, the search recurses deeply).
if (WIN32)
    if (NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" OR CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        message(FATAL_ERROR "Windows builds need clang with the GNU style driver: -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++")
    endif()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_definitions(_CRT_DECLARE_NONSTDC_NAMES=1 _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
    # Unity otherwise includes <stdnoreturn.h>, whose 'noreturn' macro breaks the CRT's __declspec(noreturn).
    add_compile_definitions(UNITY_NORETURN=_Noreturn)
    add_link_options(-Wl,/STACK:8388608)
    if (CMAKE_BUILD_TYPE MATCHES "^[Rr]elease")
        add_compile_options(-O3)
    endif()
endif()

# The CPU the engine is built for, TIKI_ARCH, with Stockfish's names and compiler flags (its Makefile's
# ARCH), each level including those before it:
#     general-64           no CPU specific code, plain C network (Stockfish's name; any 64-bit CPU)
#     x86-64               SSE2, any x86-64 CPU
#     x86-64-sse41-popcnt  + SSSE3, SSE4.1, POPCNT
#     x86-64-avx2          + AVX2, BMI1 (magic bitboards: PEXT is slow on AMD Zen 1/2 and older)
#     x86-64-bmi2          + BMI2, PEXT slider attacks
#     x86-64-avxvnni       + AVX-VNNI (256 bit, Intel Alder Lake and later, AMD Zen 5)
#     x86-64-avx512        x86-64-bmi2 + AVX-512 F/BW/DQ/VL
#     x86-64-vnni512       + AVX-512 VNNI (Intel Ice Lake, Tiger Lake and later, AMD Zen 4 and later)
#     x86-64-avx512icl     + the rest of Ice Lake's AVX-512 extensions
#     native               the best of these the build machine runs (the default), general-64 on other CPUs
# The flags apply to the whole build, so a binary only runs on CPUs of its level. Every level gives the
# same evaluations and bench (integer arithmetic, -ffp-contract=off: no fused multiply-adds changing a
# floating point result, as AVX-512 implies FMA).
set(TIKI_ARCH "native" CACHE STRING "Target CPU level, Stockfish's names (x86-64-bmi2, x86-64-vnni512, ...), or native")
set(tiki_arches general-64 x86-64 x86-64-sse41-popcnt x86-64-avx2 x86-64-bmi2 x86-64-avxvnni x86-64-avx512 x86-64-vnni512
                x86-64-avx512icl)
set_property(CACHE TIKI_ARCH PROPERTY STRINGS native ${tiki_arches})
set(tiki_pext_arches x86-64-bmi2 x86-64-avxvnni x86-64-avx512 x86-64-vnni512 x86-64-avx512icl)

set(tiki_flags_general-64)
set(tiki_flags_x86-64 -msse -msse2)
set(tiki_flags_x86-64-sse41-popcnt ${tiki_flags_x86-64} -msse3 -mpopcnt -mssse3 -msse4.1)
set(tiki_flags_x86-64-avx2 ${tiki_flags_x86-64-sse41-popcnt} -mavx2 -mbmi)
set(tiki_flags_x86-64-bmi2 ${tiki_flags_x86-64-avx2} -mbmi2)
set(tiki_flags_x86-64-avxvnni ${tiki_flags_x86-64-bmi2} -mavxvnni)
set(tiki_flags_x86-64-avx512 ${tiki_flags_x86-64-bmi2} -mavx512f -mavx512bw -mavx512dq -mavx512vl)
set(tiki_flags_x86-64-vnni512 ${tiki_flags_x86-64-avx512} -mavx512vnni)
set(tiki_flags_x86-64-avx512icl ${tiki_flags_x86-64-vnni512} -mavx512ifma -mavx512vbmi -mavx512vbmi2
                                -mavx512vpopcntdq -mavx512bitalg -mvpclmulqdq -mgfni -mvaes)

# Whether this machine runs code built for 'arch': a program using the level's instructions, built with its
# flags, runs (an unsupported instruction, or one the OS hasn't enabled, crashes it). For x86-64-bmi2 and
# the levels above, which use PEXT, it also mustn't be an AMD CPU before Zen 3, where PEXT is microcoded
# and slow (Stockfish picks x86-64-avx2 there too). The result is cached per level.
include(CheckCSourceRuns)
function(tiki_arch_runs arch out)
    set(probe_x86-64-sse41-popcnt "__m128i v = _mm_max_epi32(_mm_set1_epi32(seed), _mm_setzero_si128());
    sink = _mm_cvtsi128_si32(v) + (int) _mm_popcnt_u64((unsigned long long) seed);")
    set(probe_x86-64-avx2 "__m256i v = _mm256_add_epi32(_mm256_set1_epi32(seed), _mm256_set1_epi32(seed));
    sink = _mm256_cvtsi256_si32(v) + (int) _tzcnt_u64((unsigned long long) seed);")
    set(probe_x86-64-bmi2 "${probe_x86-64-avx2}
    sink += (int) _pext_u64((unsigned long long) seed, 0xF0F0ull);")
    set(probe_x86-64-avxvnni "${probe_x86-64-bmi2}
    sink += _mm256_cvtsi256_si32(_mm256_dpbusd_avx_epi32(v, v, v));")
    set(probe_x86-64-avx512 "${probe_x86-64-bmi2}
    __m512i w = _mm512_maddubs_epi16(_mm512_set1_epi32(seed), _mm512_set1_epi32(seed));
    sink += _mm512_cvtsi512_si32(_mm512_permutexvar_epi64(_mm512_set1_epi64(seed), w));")
    set(probe_x86-64-vnni512 "${probe_x86-64-avx512}
    sink += _mm512_cvtsi512_si32(_mm512_dpbusd_epi32(w, w, w));")
    set(probe_x86-64-avx512icl "${probe_x86-64-vnni512}
    sink += _mm512_cvtsi512_si32(_mm512_permutexvar_epi8(_mm512_popcnt_epi64(w), w));")
    set(fast_pext "1")
    if (arch IN_LIST tiki_pext_arches)
        set(fast_pext "!(c == 0x444d4163 && family < 0x19)")    # 'cAMD' of AuthenticAMD, family < Zen 3
    endif()
    string(REPLACE "-" "_" name "TIKI_RUNS_${arch}")
    if (arch MATCHES "^(general-64|x86-64)$")
        set(${out} TRUE PARENT_SCOPE)
        return()
    endif()
    list(JOIN tiki_flags_${arch} " " CMAKE_REQUIRED_FLAGS)
    check_c_source_runs("
#include <cpuid.h>
#include <immintrin.h>
volatile int seed = 3, sink;
int main(void) {
    ${probe_${arch}}
    unsigned a, b, c, d;
    __get_cpuid(0, &a, &b, &c, &d);
    unsigned eax, ebx, ecx, edx;
    __get_cpuid(1, &eax, &ebx, &ecx, &edx);
    const unsigned family = ((eax >> 8) & 0xF) + (((eax >> 8) & 0xF) == 0xF ? (eax >> 20) & 0xFF : 0);
    (void) family;
    return ${fast_pext} ? 0 : 1;
}" ${name})
    set(${out} ${${name}} PARENT_SCOPE)
endfunction()

if (TIKI_ARCH STREQUAL "native" AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|x64)$")
    set(TIKI_ARCH_USED general-64)
    set(TIKI_ARCH_RUNS_HERE TRUE)
elseif (TIKI_ARCH STREQUAL "native")
    # The best level this machine runs.
    list(REVERSE tiki_arches)
    foreach (arch IN LISTS tiki_arches)
        tiki_arch_runs(${arch} runs)
        if (runs)
            set(TIKI_ARCH_USED ${arch})
            break()
        endif()
    endforeach()
    list(REVERSE tiki_arches)
    set(TIKI_ARCH_RUNS_HERE TRUE)
elseif (TIKI_ARCH IN_LIST tiki_arches)
    set(TIKI_ARCH_USED ${TIKI_ARCH})
    tiki_arch_runs(${TIKI_ARCH} TIKI_ARCH_RUNS_HERE)
else()
    list(JOIN tiki_arches ", " names)
    message(FATAL_ERROR "TIKI_ARCH: unknown '${TIKI_ARCH}', one of native, ${names}")
endif()
if (TIKI_ARCH STREQUAL "native")
    message(STATUS "TIKI_ARCH: native, ${TIKI_ARCH_USED}")
else()
    message(STATUS "TIKI_ARCH: ${TIKI_ARCH_USED}")
endif()
if (NOT TIKI_ARCH_RUNS_HERE)
    message(STATUS "TIKI_ARCH: this machine doesn't run ${TIKI_ARCH_USED} code, so its tests aren't run")
endif()
add_compile_options(${tiki_flags_${TIKI_ARCH_USED}} -ffp-contract=off)
# PEXT (BMI2) slider attacks from x86-64-bmi2 up, otherwise magic bitboards. This changes the layout of
# attack_mask_t, so it is a definition for every target.
if (TIKI_ARCH_USED IN_LIST tiki_pext_arches)
    add_compile_definitions(USE_PEXT)
endif()
if (TIKI_ARCH_USED STREQUAL "general-64")
    add_compile_definitions(TIKI_NO_SIMD)              # nnue.c: the plain C network.
endif()

# Sanitizers for all targets (including Unity), e.g. -DTIKI_SANITIZE=address,undefined or thread.
set(TIKI_SANITIZE "" CACHE STRING "Comma separated -fsanitize list, empty for none")
if (TIKI_SANITIZE)
    message(STATUS "Sanitizers: ${TIKI_SANITIZE}")
    add_compile_options(-fsanitize=${TIKI_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${TIKI_SANITIZE})
endif()

# Move ordering statistics (beta cutoffs, first move and quiet cutoffs), counted in the search and
# printed by bench. Off normally, so the search doesn't count them.
option(TIKI_SEARCH_STATS "Count move ordering statistics in the search, printed by bench" OFF)
if (TIKI_SEARCH_STATS)
    message(STATUS "TIKI_SEARCH_STATS: ON")
    add_compile_definitions(TIKI_SEARCH_STATS)
endif()

# SPSA tuning build (OpenBench): the parameters in core/src/tune.h become variables with UCI options
# of their names, and 'tiki tune' prints OpenBench's SPSA input. Off normally, they stay constants.
option(TIKI_TUNE "Expose the tunable search parameters as UCI options, for SPSA" OFF)
if (TIKI_TUNE)
    message(STATUS "TIKI_TUNE: ON")
    add_compile_definitions(TIKI_TUNE)
endif()

# The unit tests (Unity, fetched in extern/). OFF builds the engine without fetching anything.
option(TIKI_BUILD_TESTS "Build the unit tests" ON)

# A fully static tiki, so a release binary runs on any x86-64 Linux whatever its glibc version.
option(TIKI_STATIC "Link tiki statically" OFF)

# A network built into the engine, so tiki runs without a .nnue file or TIKI_EVALFILE (which
# still overrides it). The newest network in models/ by the date in its name (tiki-2026-10-23.nnue,
# file times aren't kept by git), or TIKI_EMBED_NETWORK_FILE. CMake reconfigures when a network is
# added to models/, and the engine is rebuilt when the embedded file changes.
option(TIKI_EMBED_NETWORK "Build the newest network in models/ into the engine" OFF)
set(TIKI_EMBED_NETWORK_FILE "" CACHE FILEPATH "Network to build in with TIKI_EMBED_NETWORK, empty for the newest in models/")

# Only a network the engine can load is built in: its header's version must be NNUE_VERSION (nnue.h).
# Networks of an older format stay in models/ for older engines, and are passed over.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/core/src/nnue/nnue.h" nnue_version_line REGEX "^#define NNUE_VERSION ")
# Configure again when nnue.h changes (a new NNUE_VERSION), so the check below isn't left stale.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/core/src/nnue/nnue.h")
string(REGEX MATCH "[0-9]+" TIKI_NETWORK_VERSION "${nnue_version_line}")

# The format version of the network at 'path' (uint32, little endian, after the 8 byte magic), or
# "none" if it isn't a tiki network.
function(tiki_network_version path out)
    file(READ "${path}" magic LIMIT 8 HEX)
    if (NOT magic STREQUAL "54494b494e4e5545")          # "TIKINNUE"
        set(${out} "none" PARENT_SCOPE)
        return()
    endif()
    file(READ "${path}" bytes OFFSET 8 LIMIT 4 HEX)
    string(LENGTH "${bytes}" length)
    if (length LESS 8)                                  # Truncated: no version to read.
        set(${out} "none" PARENT_SCOPE)
        return()
    endif()
    string(SUBSTRING "${bytes}" 0 2 b0)
    string(SUBSTRING "${bytes}" 2 2 b1)
    string(SUBSTRING "${bytes}" 4 2 b2)
    string(SUBSTRING "${bytes}" 6 2 b3)
    math(EXPR version "0x${b3}${b2}${b1}${b0}")
    set(${out} ${version} PARENT_SCOPE)
endfunction()

if (TIKI_EMBED_NETWORK)
    if (TIKI_EMBED_NETWORK_FILE)
        get_filename_component(TIKI_EMBEDDED_NETWORK "${TIKI_EMBED_NETWORK_FILE}" ABSOLUTE)
        if (NOT EXISTS "${TIKI_EMBEDDED_NETWORK}")
            message(FATAL_ERROR "TIKI_EMBED_NETWORK: no network at '${TIKI_EMBED_NETWORK_FILE}'")
        endif()
        tiki_network_version("${TIKI_EMBEDDED_NETWORK}" version)
        if (NOT version STREQUAL TIKI_NETWORK_VERSION)
            message(FATAL_ERROR "TIKI_EMBED_NETWORK: ${TIKI_EMBEDDED_NETWORK} is network format version "
                                "${version}, this engine reads version ${TIKI_NETWORK_VERSION}")
        endif()
    else()
        get_filename_component(models_dir "${CMAKE_CURRENT_SOURCE_DIR}/../models" ABSOLUTE)
        file(GLOB TIKI_NETWORKS CONFIGURE_DEPENDS "${models_dir}/*.nnue")
        list(SORT TIKI_NETWORKS COMPARE NATURAL ORDER DESCENDING)
        set(TIKI_EMBEDDED_NETWORK "")
        set(skipped "")
        foreach (network IN LISTS TIKI_NETWORKS)
            tiki_network_version("${network}" version)
            if (version STREQUAL TIKI_NETWORK_VERSION)
                set(TIKI_EMBEDDED_NETWORK "${network}")
                break()
            endif()
            get_filename_component(name "${network}" NAME)
            list(APPEND skipped "${name} (version ${version})")
        endforeach()
        if (skipped)
            message(STATUS "TIKI_EMBED_NETWORK: passed over networks of another format: ${skipped}")
        endif()
        if (NOT TIKI_EMBEDDED_NETWORK)
            message(FATAL_ERROR "TIKI_EMBED_NETWORK: no network of format version ${TIKI_NETWORK_VERSION} in "
                                "models/. Add one, set TIKI_EMBED_NETWORK_FILE, or configure with "
                                "-DTIKI_EMBED_NETWORK=OFF and set TIKI_EVALFILE when running.")
        endif()
    endif()
    message(STATUS "TIKI_EMBED_NETWORK: ${TIKI_EMBEDDED_NETWORK}")
endif()

# Google Benchmark micro-benchmarks (C++, fetched in extern/). 'bench' in the engine doesn't need it.
option(TIKI_BUILD_BENCHMARKS "Build the Google Benchmark micro-benchmarks" ON)

# Warnings for the engine's own targets (not third party code).
function(tiki_target_warnings target)
    if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wshadow)
    endif()
endfunction()

add_subdirectory(extern)
add_subdirectory(core)                  # core engine.
add_subdirectory(magic_generator)       # magic (minimal perfect hash) generator.
add_subdirectory(app)                   # main app.
if (TIKI_BUILD_TESTS)
    add_subdirectory(test)              # core tests.
endif()
if (TIKI_BUILD_BENCHMARKS)
    add_subdirectory(benchmark)         # Google Benchmark micro-benchmarks.
endif()