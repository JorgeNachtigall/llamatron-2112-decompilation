# The port's options screen (PLAN M7, W1): host-side, drawn with the game's text engine.
# settings.h (llama.inf fields) comes from src/platform; the screen itself needs no SDL.
add_library(llamatron_options STATIC options_screen.c)
target_include_directories(llamatron_options PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/platform)
target_link_libraries(llamatron_options PUBLIC llamatron_game)
target_compile_options(llamatron_options PRIVATE
    $<$<C_COMPILER_ID:Clang,AppleClang,GNU>:-Wall -Wextra -Wpedantic>)

if(NOT EMSCRIPTEN)
    add_executable(options_selftest options_selftest.c ${PROJECT_SOURCE_DIR}/src/platform/settings.c)
    target_link_libraries(options_selftest PRIVATE llamatron_options llama_assets_files)
    if(TARGET llamatron_generated)
        add_dependencies(options_selftest llamatron_generated)
    endif()
    add_test(NAME options_selftest COMMAND options_selftest
        --dump ${CMAKE_CURRENT_BINARY_DIR}/options.ppm)
endif()
