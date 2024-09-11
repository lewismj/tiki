cmake_minimum_required (VERSION 3.24)
project(tiki)

set(CMAKE_C_STANDARD 23)

if (UNIX)
    add_compile_options(-fPIC)
    if (CMAKE_BUILD_TYPE MATCHES "^[Rr]elease")
        add_compile_options(-O3)
    endif()
endif()

add_subdirectory(extern)
add_subdirectory(core)                  # core engine.
add_subdirectory(magic_generator)       # magic (minimal perfect hash) generator.
add_subdirectory(app)                   # main app.
add_subdirectory(test)                  # core tests.