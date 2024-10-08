project(tiki_engine_core)

set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_C_STANDARD 23)
include(CheckCSourceCompiles)

include_directories(${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR})

set (SOURCES
        src/bitboard_constants.c
        src/bitboard_ops.c
        src/mask_generator.c
        src/random.c
        src/attack_mask.c
        src/move_encoding.c
        src/zobrist_key.c
        src/board.c
        src/move_generator.c
        src/hce/evaluation_mask.c
        src/search_constants.c
        src/transposition.c
        src/parallel.c)

add_library(${PROJECT_NAME} ${SOURCES})
set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC ${PROJECT_SOURCE_DIR}/include)

# Copy these PEXT settings to executable definition.
set(CMAKE_REQUIRED_FLAGS "-mbmi2")
check_c_source_compiles("
#include <immintrin.h>
int main() {
    unsigned long long src = 0b10101010, mask = 0b1111;
    unsigned long long res = _pext_u64(src, mask);
    return 0;
}" PEXT_SUPPORTED)

if(PEXT_SUPPORTED)
    message(STATUS "PEXT instruction is supported.")
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_PEXT)
    target_compile_options(${PROJECT_NAME} PRIVATE "-mbmi2")
    target_link_options(${PROJECT_NAME} PRIVATE "-mbmi2")
else()
    message(STATUS "PEXT instruction is not supported.")
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE atomic)


if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message("Clang C compiler is being used.")
elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    message("GCC C compiler is being used.")
else()
    message("Unknown C compiler is being used.")
endif()

if (CMAKE_BUILD_TYPE MATCHES "^[Rr]elease")
    message(STATUS "Release build use: -O3")
    target_compile_options(${PROJECT_NAME} PRIVATE "-O3")
endif()


install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

