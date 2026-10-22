project(tiki_app)

set(CMAKE_C_STANDARD 23)
find_package(Threads REQUIRED)


add_executable(${PROJECT_NAME} src/main.c)

# USE_PEXT/-mbmi2 come from tiki_engine_core. No AVX-512 flags: main.c inlines the search, so they
# would let the compiler emit AVX-512 anywhere and crash on CPUs without it.
tiki_target_warnings(${PROJECT_NAME})


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
