project(tiki_engine_core)


set(CMAKE_C_STANDARD 23)


include_directories(${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR})


set (SOURCES
        src/bitboard_constants.c
        src/bitboard_ops.c
        src/mask_generator.c
        src/random.c
        src/attack_mask.c
        src/move_encoding.c)



add_library(${PROJECT_NAME} ${SOURCES})


set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC ${PROJECT_SOURCE_DIR}/include)


target_compile_options(${PROJECT_NAME} PRIVATE "-mbmi2")
target_link_options(${PROJECT_NAME} PRIVATE "-mbmi2")

install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

