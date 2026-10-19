# SDL2 host layer. Native: SDL2 from the system (Homebrew on macOS). Web: Emscripten's SDL2 port.
add_library(llama_sdl2 INTERFACE)
set(LLAMA_WEB_SHELL "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
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
    set_target_properties(platform_testpattern PROPERTIES SUFFIX ".html"
        LINK_DEPENDS ${LLAMA_WEB_SHELL})
    target_link_options(platform_testpattern PRIVATE -sALLOW_MEMORY_GROWTH=1
        "SHELL:--shell-file ${LLAMA_WEB_SHELL}")
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
    set_target_properties(llamatron_title PROPERTIES SUFFIX ".html"
        LINK_DEPENDS ${LLAMA_WEB_SHELL})
    target_link_options(llamatron_title PRIVATE -sALLOW_MEMORY_GROWTH=1
        "SHELL:--shell-file ${LLAMA_WEB_SHELL}"
        "SHELL:--preload-file ${LLAMATRON_GENERATED_DIR}/data@/data")
else()
    target_compile_definitions(llamatron_title PRIVATE LLAMATRON_DATA_DIR="${LLAMATRON_GENERATED_DIR}/data")
endif()

# Web smoke tests (PLAN.md §5): run in the web build tree (ctest --test-dir build-web). Local
# headless Chrome loads the page from a loopback server; the app prints the hash of the title
# picture and of the rendered canvas (1000x750 for the tool's 1000x900 window). Skipped (77)
# when no Chrome/Chromium is installed.
if(EMSCRIPTEN)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        set(_smoke ${CMAKE_CURRENT_SOURCE_DIR}/tools/web_smoke.py --dir ${CMAKE_CURRENT_BINARY_DIR}
            --page llamatron_title.html)
        add_test(NAME web_smoke_title_vga COMMAND ${Python3_EXECUTABLE} ${_smoke} --query frames=2
            --expect "title 320x400 hash=c7ab74c4 canvas 1000x750 hash=1b1b4ce3")
        add_test(NAME web_smoke_title_ega COMMAND ${Python3_EXECUTABLE} ${_smoke} --query frames=2&ega
            --expect "title 640x350 hash=d0a1684d canvas 1000x750 hash=aae527de")
        set_tests_properties(web_smoke_title_vga web_smoke_title_ega PROPERTIES
            SKIP_RETURN_CODE 77 LABELS web TIMEOUT 180)
    endif()
endif()
