project(tiki_engine_test)

set(CMAKE_C_STANDARD 23)
find_package(Threads REQUIRED)

set (SOURCES
        src/tiki_test_runner.c
        src/make_undo_move_tests.c
        src/perft_tests.c)

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

install (TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})