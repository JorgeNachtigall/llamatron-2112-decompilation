# W1: loaders for the extractor's data files (formats.h, game_data.h). Pure C11, no SDL.
# Link llamatron_assets plus exactly one asset store (llama_assets_embedded / llama_assets_files).
add_library(llamatron_assets STATIC
    font.c
    format_error.c
    game_data.c
    levels.c
    palettes.c
    samples.c
    scores.c
    sprites.c
    texts.c
)
target_include_directories(llamatron_assets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${LLAMATRON_GENERATED_DIR})
target_link_libraries(llamatron_assets PUBLIC llama_engine llama_assets_api)
add_dependencies(llamatron_assets llamatron_generated)

add_executable(assets_formats_test test_formats.c)
target_link_libraries(assets_formats_test PRIVATE llamatron_assets llama_assets_files)
add_test(NAME assets_formats COMMAND assets_formats_test)

if(NOT EMSCRIPTEN)
    add_executable(assets_game_data_test test_game_data.c)
    target_include_directories(assets_game_data_test PRIVATE ${LLAMATRON_GENERATED_DIR})
    target_link_libraries(assets_game_data_test PRIVATE llamatron_assets llama_assets_files)
    add_test(NAME assets_game_data_files
        COMMAND assets_game_data_test --data "${LLAMATRON_GENERATED_DIR}/data")
    if(TARGET llama_assets_embedded)
        add_executable(assets_game_data_test_embedded test_game_data.c)
        target_include_directories(assets_game_data_test_embedded PRIVATE ${LLAMATRON_GENERATED_DIR})
        target_link_libraries(assets_game_data_test_embedded PRIVATE llamatron_assets llama_assets_embedded)
        add_test(NAME assets_game_data_embedded COMMAND assets_game_data_test_embedded)
    endif()
endif()
