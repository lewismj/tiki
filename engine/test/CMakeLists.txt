project(tiki_engine_test)

set(CMAKE_C_STANDARD 23)

set (SOURCES    src/tiki_test_runner.c
        src/make_undo_move_tests.c)

add_executable(${PROJECT_NAME} ${SOURCES})


target_include_directories(${PROJECT_NAME} PRIVATE
        ${PROJECT_SOURCE_DIR}/../core/include
)


target_link_libraries(${PROJECT_NAME} tiki_engine_core unity)

install (TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})