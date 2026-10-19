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
    settings.c
)
target_include_directories(llama_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(llama_platform PUBLIC llama_sdl2)
target_link_libraries(llama_platform PUBLIC llama_assets_api)
if(EMSCRIPTEN)
    # Persistent storage (settings, later scores): IDBFS at /persist, see web/persist.js.
    target_link_options(llama_platform PUBLIC -lidbfs.js
        "SHELL:--pre-js ${CMAKE_CURRENT_SOURCE_DIR}/web/persist.js")
endif()
target_compile_options(llama_platform PRIVATE
    $<$<C_COMPILER_ID:Clang,AppleClang,GNU>:-Wall -Wextra>)

# Asset store (pure C). Executables link exactly one implementation; see assets.h.
option(LLAMATRON_EMBED_ASSETS "Compile the extracted data into native executables" ON)
add_library(llama_assets_api INTERFACE)
target_include_directories(llama_assets_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(llama_assets_files STATIC assets.c)
target_link_libraries(llama_assets_files PUBLIC llama_assets_api)
if(EMSCRIPTEN)
    target_compile_definitions(llama_assets_files PRIVATE LLAMATRON_DATA_DIR="/data")
elseif(LLAMATRON_GENERATED_DIR)
    target_compile_definitions(llama_assets_files PRIVATE LLAMATRON_DATA_DIR="${LLAMATRON_GENERATED_DIR}/data")
endif()

if(LLAMATRON_ASSETS_BLOB_C AND NOT EMSCRIPTEN)
    add_library(llama_assets_embedded STATIC assets.c ${LLAMATRON_ASSETS_BLOB_C})
    set_source_files_properties(${LLAMATRON_ASSETS_BLOB_C} PROPERTIES GENERATED TRUE)
    target_link_libraries(llama_assets_embedded PUBLIC llama_assets_api)
    target_compile_definitions(llama_assets_embedded PRIVATE LLAMATRON_EMBED_ASSETS)
    add_dependencies(llama_assets_embedded llamatron_generated)
endif()
if(TARGET llama_assets_embedded AND LLAMATRON_EMBED_ASSETS)
    set(LLAMA_ASSETS llama_assets_embedded)
else()
    set(LLAMA_ASSETS llama_assets_files)
endif()

# Test app: window + generated test pattern and palette (no original data needed).
add_executable(platform_testpattern testapp/testpattern.c)
target_link_libraries(platform_testpattern PRIVATE llama_platform llama_engine llama_assets_files)
if(EMSCRIPTEN)
    set_target_properties(platform_testpattern PROPERTIES SUFFIX ".html"
        LINK_DEPENDS ${LLAMA_WEB_SHELL})
    target_link_options(platform_testpattern PRIVATE -sALLOW_MEMORY_GROWTH=1
        "SHELL:--shell-file ${LLAMA_WEB_SHELL}")
endif()

# M1 front screen (fs.exe behaviour). Native: pictures embedded in the binary; web: read from
# the preloaded /data.
add_executable(llamatron_title apps/title_main.c)
target_link_libraries(llamatron_title PRIVATE llama_platform llama_engine ${LLAMA_ASSETS})
if(TARGET llamatron_generated)
    add_dependencies(llamatron_title llamatron_generated)
endif()
if(EMSCRIPTEN)
    set_target_properties(llamatron_title PROPERTIES SUFFIX ".html"
        LINK_DEPENDS ${LLAMA_WEB_SHELL})
    target_link_options(llamatron_title PRIVATE -sALLOW_MEMORY_GROWTH=1
        "SHELL:--shell-file ${LLAMA_WEB_SHELL}"
        "SHELL:--preload-file ${LLAMATRON_GENERATED_DIR}/data@/data")
endif()

# The game executable: title screen, then the game through its entry API (src/game/game.h).
# No game logic in the shell.
if(EXISTS ${PROJECT_SOURCE_DIR}/src/game/CMakeLists.txt)
    add_executable(llamatron apps/llamatron_main.c apps/input_script.c)
    target_link_libraries(llamatron PRIVATE llama_platform llama_engine llamatron_game ${LLAMA_ASSETS})
    target_compile_options(llamatron PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang,GNU>:-Wall -Wextra>)
    if(TARGET llamatron_generated)
        add_dependencies(llamatron llamatron_generated)
    endif()
    if(EMSCRIPTEN)
        set_target_properties(llamatron PROPERTIES SUFFIX ".html" LINK_DEPENDS ${LLAMA_WEB_SHELL})
        target_link_options(llamatron PRIVATE -sALLOW_MEMORY_GROWTH=1
            "SHELL:--shell-file ${LLAMA_WEB_SHELL}"
            "SHELL:--preload-file ${LLAMATRON_GENERATED_DIR}/data@/data")
    else()
        # Empty settings dir: the defaults come from the user's llama.inf (config.dat).
        set(_llama_prefs ${CMAKE_CURRENT_BINARY_DIR}/llamatron-test-prefs)
        add_test(NAME llamatron_title_phase COMMAND llamatron --frames 2 --vga --mute)
        set_tests_properties(llamatron_title_phase PROPERTIES
            PASS_REGULAR_EXPRESSION "title 320x400 hash=c7ab74c4")
        add_test(NAME llamatron_to_game COMMAND llamatron --frames 10 --start-after 3 --mute --seed 0)
        set_tests_properties(llamatron_to_game PROPERTIES
            PASS_REGULAR_EXPRESSION "llamatron phase=game frames=10")
        # Input scripts (PROBE.KEY format): round trip, then the selftest's script replayed
        # (240 ticks need ~336 frames at the 70.086 Hz display clock).
        add_executable(input_script_selftest apps/input_script_selftest.c apps/input_script.c)
        set(_script ${CMAKE_CURRENT_BINARY_DIR}/input_script_selftest.key)
        add_test(NAME input_script_selftest COMMAND input_script_selftest ${_script})
        add_test(NAME llamatron_replay COMMAND llamatron --replay ${_script} --frames 400 --mute --seed 0)
        set_tests_properties(llamatron_replay PROPERTIES DEPENDS input_script_selftest
            PASS_REGULAR_EXPRESSION "replayed 4 of 4 key bytes")
        # Visual check against the DOSBox-X reference runs (golden page hashes only); reports
        # the first differing capture, fails only with LLAMATRON_VISUAL_STRICT=1.
        find_package(Python3 COMPONENTS Interpreter)
        if(Python3_Interpreter_FOUND)
            add_test(NAME llamatron_visual_walk_vga COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/visual_check.py --llamatron $<TARGET_FILE:llamatron>
                --golden ${PROJECT_SOURCE_DIR}/tests/golden/reference_runs.json --run walk_vga
                --workdir ${CMAKE_CURRENT_BINARY_DIR}/visual)
            set_tests_properties(llamatron_visual_walk_vga PROPERTIES
                ENVIRONMENT "LLAMATRON_PREF_DIR=${_llama_prefs}" SKIP_REGULAR_EXPRESSION "visual: skipped")
        endif()
        set_tests_properties(llamatron_title_phase llamatron_to_game llamatron_replay PROPERTIES
            ENVIRONMENT "LLAMATRON_PREF_DIR=${_llama_prefs}")
    endif()
endif()

# Personal-use packaging: Llamatron.app (LLAMATRON_MACOS_BUNDLE) and web-dist.
include(${CMAKE_CURRENT_SOURCE_DIR}/packaging.cmake)

# Settings persistence (native): save in one run, read back in the next; storage redirected
# into the build tree so tests never touch the user's own data directory.
if(NOT EMSCRIPTEN)
    set(_prefs ${CMAKE_CURRENT_BINARY_DIR}/test-prefs)
    file(MAKE_DIRECTORY ${_prefs})
    add_test(NAME settings_persist_reset COMMAND ${CMAKE_COMMAND} -E rm -f ${_prefs}/llama.inf)
    add_test(NAME settings_persist_save COMMAND platform_testpattern --frames 1 --save-settings "E J S 0 Y")
    add_test(NAME settings_persist_load COMMAND platform_testpattern --frames 1)
    set_tests_properties(settings_persist_save settings_persist_load PROPERTIES
        ENVIRONMENT "LLAMATRON_PREF_DIR=${_prefs}"
        PASS_REGULAR_EXPRESSION "settings loaded: E J S 0 Y")
    set_tests_properties(settings_persist_save PROPERTIES DEPENDS settings_persist_reset)
    set_tests_properties(settings_persist_load PROPERTIES DEPENDS settings_persist_save)
endif()

# Web smoke tests (PLAN.md §5): run in the web build tree (ctest --test-dir build-web). Local
# headless Chrome loads the page from a loopback server; the app prints the hash of the title
# picture and of the rendered canvas (1000x750 for the tool's 1000x900 window). Skipped (77)
# when no Chrome/Chromium is installed. Real time (--report: the page posts its output lines):
# every page first loads IDBFS (web/persist.js), and headless virtual time does not wait for
# IndexedDB.
if(EMSCRIPTEN)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        set(_smoke ${CMAKE_CURRENT_SOURCE_DIR}/tools/web_smoke.py --dir ${CMAKE_CURRENT_BINARY_DIR}
            --page llamatron_title.html --report)
        add_test(NAME web_smoke_title_vga COMMAND ${Python3_EXECUTABLE} ${_smoke} --query frames=2
            --expect "title 320x400 hash=c7ab74c4 canvas 1000x750 hash=1b1b4ce3")
        add_test(NAME web_smoke_title_ega COMMAND ${Python3_EXECUTABLE} ${_smoke} --query frames=2&ega
            --expect "title 640x350 hash=d0a1684d canvas 1000x750 hash=aae527de")
        # Title sound through the wasm build: same audio hash as native (snd_hash_title).
        add_test(NAME web_smoke_title_sound COMMAND ${Python3_EXECUTABLE} ${_smoke} --query frames=140
            --expect "sound ticks=100 samples=43961 hash=16d7fc03")
        set_tests_properties(web_smoke_title_sound PROPERTIES SKIP_RETURN_CODE 77 LABELS web TIMEOUT 180)
        # One Space press + release dispatched back to back (inside one frame) through SDL's
        # browser event path must still rotate the palette once (input latch, input.h).
        set(_tp ${CMAKE_CURRENT_SOURCE_DIR}/tools/web_smoke.py --dir ${CMAKE_CURRENT_BINARY_DIR}
            --page platform_testpattern.html --report)
        add_test(NAME web_smoke_tap_space COMMAND ${Python3_EXECUTABLE} ${_tp} --query frames=4&tap=Space
            --expect "frames=4 ticks=2 display_hash=3cbbd24e palette_phase=1")
        add_test(NAME web_smoke_no_tap COMMAND ${Python3_EXECUTABLE} ${_tp} --query frames=4
            --expect "frames=4 ticks=2 display_hash=3cbbd24e palette_phase=0")
        set_tests_properties(web_smoke_tap_space web_smoke_no_tap PROPERTIES
            SKIP_RETURN_CODE 77 LABELS web TIMEOUT 180)
        set_tests_properties(web_smoke_title_vga web_smoke_title_ega PROPERTIES
            SKIP_RETURN_CODE 77 LABELS web TIMEOUT 180)
        # Settings persistence (IDBFS): save in one browser run, read back in the next. Same
        # profile and port, since IndexedDB is per origin.
        set(_profile ${CMAKE_CURRENT_BINARY_DIR}/test-profile)
        set(_persist ${_tp} --profile ${_profile} --port 8471)
        add_test(NAME web_persist_reset COMMAND ${CMAKE_COMMAND} -E rm -rf ${_profile})
        add_test(NAME web_persist_save COMMAND ${Python3_EXECUTABLE} ${_persist}
            --query frames=2&settings=E_J_S_0_Y --expect "settings saved: yes" --expect "persist: saved")
        add_test(NAME web_persist_load COMMAND ${Python3_EXECUTABLE} ${_persist}
            --query frames=2 --expect "settings loaded: E J S 0 Y")
        set_tests_properties(web_persist_reset web_persist_save web_persist_load PROPERTIES
            LABELS web RESOURCE_LOCK web_persist_profile)
        set_tests_properties(web_persist_save web_persist_load PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
        set_tests_properties(web_persist_save PROPERTIES DEPENDS web_persist_reset)
        set_tests_properties(web_persist_load PROPERTIES DEPENDS web_persist_save)
        # The game executable: title phase, then title -> game hand-off.
        if(TARGET llamatron)
            set(_game ${CMAKE_CURRENT_SOURCE_DIR}/tools/web_smoke.py --dir ${CMAKE_CURRENT_BINARY_DIR}
                --page llamatron.html --report)
            add_test(NAME web_llamatron_title COMMAND ${Python3_EXECUTABLE} ${_game}
                --query frames=2&vga&mute --expect "title 320x400 hash=c7ab74c4")
            add_test(NAME web_llamatron_to_game COMMAND ${Python3_EXECUTABLE} ${_game}
                --query frames=10&start=3&mute&seed=0 --expect "llamatron phase=game frames=10")
            set_tests_properties(web_llamatron_title web_llamatron_to_game PROPERTIES
                SKIP_RETURN_CODE 77 LABELS web TIMEOUT 120)
        endif()
    endif()
endif()
