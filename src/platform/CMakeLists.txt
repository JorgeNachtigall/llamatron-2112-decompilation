# SDL2 host layer. Native: SDL2 from the system (Homebrew on macOS). Web: Emscripten's SDL2 port.
add_library(llama_sdl2 INTERFACE)
if(EMSCRIPTEN)
    target_compile_options(llama_sdl2 INTERFACE -sUSE_SDL=2)
    target_link_options(llama_sdl2 INTERFACE -sUSE_SDL=2)
else()
    find_package(SDL2 REQUIRED CONFIG)
    if(TARGET SDL2::SDL2)
        target_link_libraries(llama_sdl2 INTERFACE SDL2::SDL2)
    else()
        target_link_libraries(llama_sdl2 INTERFACE SDL2::SDL2-static)
    endif()
endif()

add_library(llama_platform STATIC
    platform_sdl.c
    keymap.c
)
target_include_directories(llama_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(llama_platform PUBLIC llama_sdl2)
target_compile_options(llama_platform PRIVATE
    $<$<C_COMPILER_ID:Clang,AppleClang,GNU>:-Wall -Wextra>)

# Test app: window + generated test pattern and palette (no original data needed).
add_executable(platform_testpattern testapp/testpattern.c)
target_link_libraries(platform_testpattern PRIVATE llama_platform llama_engine)
if(EMSCRIPTEN)
    set_target_properties(platform_testpattern PROPERTIES SUFFIX ".html")
    target_link_options(platform_testpattern PRIVATE -sALLOW_MEMORY_GROWTH=1)
endif()

# M1 front screen (fs.exe behaviour). Reads the extractor's title pictures at run time:
# natively from the build tree, on the web from the preloaded /data.
add_executable(llamatron_title apps/title_main.c)
target_link_libraries(llamatron_title PRIVATE llama_platform llama_engine)
if(TARGET llamatron_generated)
    add_dependencies(llamatron_title llamatron_generated)
endif()
if(EMSCRIPTEN)
    target_compile_definitions(llamatron_title PRIVATE LLAMATRON_DATA_DIR="/data")
    set_target_properties(llamatron_title PROPERTIES SUFFIX ".html")
    target_link_options(llamatron_title PRIVATE -sALLOW_MEMORY_GROWTH=1
        "SHELL:--preload-file ${LLAMATRON_GENERATED_DIR}/data@/data")
else()
    target_compile_definitions(llamatron_title PRIVATE LLAMATRON_DATA_DIR="${LLAMATRON_GENERATED_DIR}/data")
endif()
