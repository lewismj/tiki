cmake_minimum_required (VERSION 3.24)
project(tiki)


set(CMAKE_C_STANDARD 23)

enable_testing() # Tests are registered in engine/test, run with 'ctest' from the build directory.

add_subdirectory(engine)