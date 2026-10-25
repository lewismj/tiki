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
# statically, so tiki_app needs no Visual C++ redistributable, and the main thread gets Linux's
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

# PEXT (BMI2) slider attacks, otherwise magic bitboards. This changes the layout of attack_mask_t,
# so it is applied to the core library as a PUBLIC property and every target linking it agrees.
# The default is whether the build machine supports BMI2 (the compiler accepts -mbmi2 regardless).
# PEXT is microcoded and slow on AMD Zen 1/2, turn it off there.
include(CheckCSourceRuns)
if (WIN32)
    # __builtin_cpu_supports needs compiler-rt, which clang doesn't link for the MSVC target, so read
    # CPUID leaf 7 (EBX bit 8 BMI2, bit 5 AVX2). A check keeps its first result, so the
    # __builtin_cpu_supports checks below are then skipped.
    foreach (feature IN ITEMS "BMI2;8" "AVX2;5")
        list(GET feature 0 name)
        list(GET feature 1 bit)
        check_c_source_runs("
#include <cpuid.h>
int main(void) {
    unsigned a, b, c, d;
    return __get_cpuid_count(7, 0, &a, &b, &c, &d) && (b & (1u << ${bit})) ? 0 : 1;
}" TIKI_HOST_HAS_${name})
    endforeach()
endif()
check_c_source_runs("
int main(void) {
    __builtin_cpu_init();
    return __builtin_cpu_supports(\"bmi2\") ? 0 : 1;
}" TIKI_HOST_HAS_BMI2)
option(TIKI_USE_PEXT "Use BMI2 PEXT for slider attacks (requires a BMI2 CPU)" ${TIKI_HOST_HAS_BMI2})
message(STATUS "TIKI_USE_PEXT: ${TIKI_USE_PEXT}")

# AVX2 for the network's accumulator and output layer (only nnue.c is built with -mavx2, a plain
# C version is used otherwise). The default is whether the build machine supports AVX2.
check_c_source_runs("
int main(void) {
    __builtin_cpu_init();
    return __builtin_cpu_supports(\"avx2\") ? 0 : 1;
}" TIKI_HOST_HAS_AVX2)
option(TIKI_USE_AVX2 "Use AVX2 for the network (requires an AVX2 CPU)" ${TIKI_HOST_HAS_AVX2})
message(STATUS "TIKI_USE_AVX2: ${TIKI_USE_AVX2}")

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

# A fully static tiki_app, so a release binary runs on any x86-64 Linux whatever its glibc version.
option(TIKI_STATIC "Link tiki_app statically" OFF)

# A network built into the engine, so tiki_app runs without a .nnue file or TIKI_EVALFILE (which
# still overrides it). The newest network in models/ by the date in its name (tiki-2026-10-23.nnue,
# file times aren't kept by git), or TIKI_EMBED_NETWORK_FILE. CMake reconfigures when a network is
# added to models/, and the engine is rebuilt when the embedded file changes.
option(TIKI_EMBED_NETWORK "Build the newest network in models/ into the engine" OFF)
set(TIKI_EMBED_NETWORK_FILE "" CACHE FILEPATH "Network to build in with TIKI_EMBED_NETWORK, empty for the newest in models/")
if (TIKI_EMBED_NETWORK)
    if (TIKI_EMBED_NETWORK_FILE)
        get_filename_component(TIKI_EMBEDDED_NETWORK "${TIKI_EMBED_NETWORK_FILE}" ABSOLUTE)
    else()
        get_filename_component(models_dir "${CMAKE_CURRENT_SOURCE_DIR}/../models" ABSOLUTE)
        file(GLOB TIKI_NETWORKS CONFIGURE_DEPENDS "${models_dir}/*.nnue")
        list(SORT TIKI_NETWORKS COMPARE NATURAL)
        list(POP_BACK TIKI_NETWORKS TIKI_EMBEDDED_NETWORK)
    endif()
    if (NOT TIKI_EMBEDDED_NETWORK OR NOT EXISTS "${TIKI_EMBEDDED_NETWORK}")
        message(FATAL_ERROR "TIKI_EMBED_NETWORK: no network found ('${TIKI_EMBED_NETWORK_FILE}', or models/*.nnue)")
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
add_subdirectory(test)                  # core tests.
if (TIKI_BUILD_BENCHMARKS)
    add_subdirectory(benchmark)         # Google Benchmark micro-benchmarks.
endif()