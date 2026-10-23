project(tiki_engine_test)

set(CMAKE_C_STANDARD 23)
find_package(Threads REQUIRED)

set (SOURCES
        src/tiki_test_runner.c
        src/make_undo_move_tests.c
        src/perft_tests.c
        src/uci_tests.c
        src/repetition_tests.c
        src/tt_tests.c
        src/input_tests.c
        src/board_tests.c
        src/search_tests.c
        src/nnue_tests.c
        src/test_network.c)

add_executable(${PROJECT_NAME} ${SOURCES})


if (CMAKE_BUILD_TYPE MATCHES "^[Rr]elease")
    target_compile_options(${PROJECT_NAME} PRIVATE "-O3")
endif()


target_include_directories(${PROJECT_NAME} PRIVATE
        ${PROJECT_SOURCE_DIR}/../core/include
)

set(DATA_DIRECTORY "${CMAKE_BINARY_DIR}/engine/test/data/")
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data/perft-tests.csv DESTINATION ${DATA_DIRECTORY})

target_link_libraries(${PROJECT_NAME} tiki_engine_core unity Threads::Threads)
tiki_target_warnings(${PROJECT_NAME})

# The runner looks for <dir>/data/perft-tests.csv (dir is the argument), and runs the full perft
# suite only when it's found. 'unit' points at a directory without it, 'perft_full' at the copy.
add_test(NAME unit COMMAND ${PROJECT_NAME} ${CMAKE_CURRENT_BINARY_DIR}/unit)
set_tests_properties(unit PROPERTIES TIMEOUT 900)

add_test(NAME perft_full COMMAND ${PROJECT_NAME} ${CMAKE_BINARY_DIR}/engine/test)
set_tests_properties(perft_full PROPERTIES TIMEOUT 3600 LABELS slow)

install (TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# The 'bench' command runs end to end (the node count is checked for determinism in the unit tests).
# It needs a network, so it's skipped (exit 77) when TIKI_EVALFILE isn't set.
add_test(NAME bench COMMAND sh -c "[ -n \"$TIKI_EVALFILE\" ] || exit 77; exec \"$1\" bench 6" sh $<TARGET_FILE:tiki_app>)
set_tests_properties(bench PROPERTIES TIMEOUT 300 PASS_REGULAR_EXPRESSION "Nodes searched" SKIP_RETURN_CODE 77)
