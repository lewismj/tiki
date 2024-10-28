project(tiki_app)

set(CMAKE_C_STANDARD 23)
find_package(Threads REQUIRED)


add_executable(${PROJECT_NAME} src/main.c)


# Use PEXT if available.
set(CMAKE_REQUIRED_FLAGS "-mbmi2")
check_c_source_compiles("
#include <immintrin.h>
int main() {
    unsigned long long src = 0b10101010, mask = 0b1111;
    unsigned long long res = _pext_u64(src, mask);
    return 0;
}" PEXT_SUPPORTED)

# Conditionally add USE_PEXT macro and -mbmi2 compiler option if PEXT is supported
if(PEXT_SUPPORTED)
    message(STATUS "PEXT instruction is supported.")
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_PEXT)
    target_compile_options(${PROJECT_NAME} PRIVATE "-mbmi2")
    target_link_options(${PROJECT_NAME} PRIVATE "-mbmi2")
else()
    message(STATUS "PEXT instruction is not supported.")
endif()

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
#    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_AVX-512)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_AVX2)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mavx512f -mavx512dq -mavx512bw -mavx512vl")
endif()


if (CMAKE_BUILD_TYPE MATCHES "^[Rr]elease")
    message(STATUS "Optimisation on.")
    target_compile_options(${PROJECT_NAME} PRIVATE "-O3")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/../core/include)

target_link_libraries(${PROJECT_NAME} tiki_engine_core Threads::Threads)

install (TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(CODE [[
        message("Copy NNUE model to bin dir.")
        file(INSTALL
                DESTINATION "${CMAKE_INSTALL_BINDIR}/bin"
                TYPE SHARED_LIBRARY
                FILES "${NNUE_PATH}")
    ]])
