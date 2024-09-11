project(tiki_magic_generator)

set(CMAKE_C_STANDARD 23)

set (SOURCES
        src/magic_numbers.c
        src/main.c)

add_executable(${PROJECT_NAME} ${SOURCES})


target_include_directories(${PROJECT_NAME} PRIVATE
        ${PROJECT_SOURCE_DIR}/../core/include
)

target_link_libraries(${PROJECT_NAME} tiki_engine_core)

install (TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})