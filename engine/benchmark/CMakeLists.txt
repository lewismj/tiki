project(tiki_benchmark C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 17)
find_package(Threads REQUIRED)

# C shim over the engine headers, the C++ benchmarks only see tiki_shim.h.
add_library(tiki_shim STATIC src/tiki_shim.c)
target_link_libraries(tiki_shim PUBLIC tiki_engine_core Threads::Threads)
tiki_target_warnings(tiki_shim)

add_executable(${PROJECT_NAME} src/micro_benchmarks.cpp)
target_link_libraries(${PROJECT_NAME} tiki_shim benchmark::benchmark)
