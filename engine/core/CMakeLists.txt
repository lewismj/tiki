project(tiki_engine_core)

set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_C_STANDARD 23)

include_directories(${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR})

set (SOURCES
        src/types.c
        src/bitboard_constants.c
        src/bitboard_ops.c
        src/mask_generator.c
        src/random.c
        src/attack_mask.c
        src/move_encoding.c
        src/zobrist_key.c
        src/board.c
        src/move_generator.c
        src/search_constants.c
        src/search.c
        src/transposition.c
        src/uci.c
        src/uci_input.c
        src/bench.c
        src/datagen.c
        src/tools.c
        src/limits.c
        src/nnue/nnue.c
        src/nnue/nnue_embedded.c
)

add_library(${PROJECT_NAME} ${SOURCES})
set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC ${PROJECT_SOURCE_DIR}/include)

# PUBLIC, the attack lookups are inlined from headers into every target that links the core.
if(TIKI_USE_PEXT)
    target_compile_definitions(${PROJECT_NAME} PUBLIC USE_PEXT)
    target_compile_options(${PROJECT_NAME} PUBLIC "-mbmi2")
endif()

# Only the network code, so AVX2 instructions can't appear anywhere else.
if(TIKI_USE_AVX2)
    set_source_files_properties(src/nnue/nnue.c PROPERTIES COMPILE_OPTIONS "-mavx2")
endif()

tiki_target_warnings(${PROJECT_NAME})

# The network the assembler includes (.incbin) in nnue_embedded.c, rebuilt when the file changes.
if (TIKI_EMBED_NETWORK)
    get_filename_component(network_name "${TIKI_EMBEDDED_NETWORK}" NAME)
    set_source_files_properties(src/nnue/nnue_embedded.c PROPERTIES
            COMPILE_DEFINITIONS "TIKI_EMBEDDED_NETWORK=\"${TIKI_EMBEDDED_NETWORK}\";TIKI_EMBEDDED_NETWORK_NAME=\"${network_name}\""
            OBJECT_DEPENDS "${TIKI_EMBEDDED_NETWORK}")
endif()

find_package(Threads REQUIRED)
if (WIN32)
    # <pthread.h>, <unistd.h>, <strings.h> and <sys/time.h> over Win32, PUBLIC so the app and tests get them too.
    target_sources(${PROJECT_NAME} PRIVATE ../platform/win32/src/posix_win32.c)
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../platform/win32/include)
    target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE atomic Threads::Threads m)
endif()


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


target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

