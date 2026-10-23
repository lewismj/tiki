cmake_minimum_required(VERSION 3.8)

## Build dependencies should generally be downloaded from git and built from source.
##
## However, some dependencies this is impractical or not available,
## i.e. Python & Doxygen, so use find_package.

set(CMAKE_CXX_STANDARD 20)


# Enable testing
enable_testing()

# Set policy CMP0135 to NEW to handle the DOWNLOAD_EXTRACT_TIMESTAMP option
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()


include(FetchContent)

FetchContent_Declare(
        unity
        GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
        GIT_TAG        v2.6.0
)

FetchContent_MakeAvailable(unity)

if (TIKI_BUILD_BENCHMARKS)
    # Only the library, none of its own tests or install rules.
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
            googlebenchmark
            GIT_REPOSITORY https://github.com/google/benchmark.git
            GIT_TAG        v1.9.1
    )

    FetchContent_MakeAvailable(googlebenchmark)

    # Clang 22 reports __COUNTER__ (used by benchmark.h) as a C2y extension, an error under the
    # -pedantic-errors benchmark builds itself with.
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-Wno-c2y-extensions TIKI_HAS_WNO_C2Y_EXTENSIONS)
    if (TIKI_HAS_WNO_C2Y_EXTENSIONS)
        target_compile_options(benchmark PRIVATE -Wno-c2y-extensions)
        target_compile_options(benchmark_main PRIVATE -Wno-c2y-extensions)
    endif()
endif()
