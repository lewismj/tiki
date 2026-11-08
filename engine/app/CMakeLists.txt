project(tiki_app)

set(CMAKE_C_STANDARD 23)
find_package(Threads REQUIRED)


add_executable(${PROJECT_NAME} src/main.c)
# The target stays tiki_app, the binary is tiki (tiki.exe), as in the release packages.
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME tiki)

# The CPU flags and USE_PEXT come from TIKI_ARCH (engine/CMakeLists.txt).
tiki_target_warnings(${PROJECT_NAME})


if (CMAKE_BUILD_TYPE MATCHES "^[Rr]elease")
    message(STATUS "Optimisation on.")
    target_compile_options(${PROJECT_NAME} PRIVATE "-O3")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/../core/include)

target_link_libraries(${PROJECT_NAME} tiki_engine_core Threads::Threads)

if (TIKI_STATIC)
    target_link_options(${PROJECT_NAME} PRIVATE -static)
endif()

install (TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})