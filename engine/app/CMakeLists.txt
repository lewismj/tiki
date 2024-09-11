project(tiki_app)

set(CMAKE_C_STANDARD 23)

add_executable(${PROJECT_NAME} src/main.c)

if (CMAKE_BUILD_TYPE MATCHES "^[Rr]elease")
    target_compile_options(${PROJECT_NAME} PRIVATE "-O3")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/../core/include)


target_link_libraries(${PROJECT_NAME} tiki_engine_core)

install (TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})