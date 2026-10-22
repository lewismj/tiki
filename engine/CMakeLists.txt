cmake_minimum_required (VERSION 3.24)
project(tiki)

set(CMAKE_C_STANDARD 23)

if (UNIX)
    add_compile_options(-fPIC)
    if (CMAKE_BUILD_TYPE MATCHES "^[Rr]elease")
        add_compile_options(-O3)
    endif()
endif()

# PEXT (BMI2) slider attacks, otherwise magic bitboards. This changes the layout of attack_mask_t,
# so it is applied to the core library as a PUBLIC property and every target linking it agrees.
# The default is whether the build machine supports BMI2 (the compiler accepts -mbmi2 regardless).
# PEXT is microcoded and slow on AMD Zen 1/2, turn it off there.
include(CheckCSourceRuns)
check_c_source_runs("
int main(void) {
    __builtin_cpu_init();
    return __builtin_cpu_supports(\"bmi2\") ? 0 : 1;
}" TIKI_HOST_HAS_BMI2)
option(TIKI_USE_PEXT "Use BMI2 PEXT for slider attacks (requires a BMI2 CPU)" ${TIKI_HOST_HAS_BMI2})
message(STATUS "TIKI_USE_PEXT: ${TIKI_USE_PEXT}")

# Sanitizers for all targets (including Unity), e.g. -DTIKI_SANITIZE=address,undefined or thread.
set(TIKI_SANITIZE "" CACHE STRING "Comma separated -fsanitize list, empty for none")
if (TIKI_SANITIZE)
    message(STATUS "Sanitizers: ${TIKI_SANITIZE}")
    add_compile_options(-fsanitize=${TIKI_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${TIKI_SANITIZE})
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