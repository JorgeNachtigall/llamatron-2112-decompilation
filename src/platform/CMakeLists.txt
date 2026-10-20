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
        set(LLAMA_SDL2_SHARED ON CACHE INTERNAL "SDL2 is a shared library (packaging.cmake bundles it)")
    else()
        target_link_libraries(llama_sdl2 INTERFACE SDL2::SDL2-static)
        set(LLAMA_SDL2_SHARED OFF CACHE INTERNAL "SDL2 is a shared library (packaging.cmake bundles it)")
    endif()
endif()

add_library(llama_platform STATIC
    platform_sdl.c
    scale.c
    keymap.c
    padmap.c
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

# Host controls: keys and gamepads give the original's scan codes / joysticks.
if(NOT EMSCRIPTEN)
    add_executable(controls_selftest controls_selftest.c)
    target_link_libraries(controls_selftest PRIVATE llama_platform)
    add_test(NAME controls_selftest COMMAND controls_selftest)
endif()

# The host's scores.yak storage for the 5e89 diff test (tests/oracle/test_hiscore_save.py).
if(NOT EMSCRIPTEN)
    add_executable(scores_store testapp/scores_store.c)
    target_link_libraries(scores_store PRIVATE llama_platform llamatron_assets llama_assets_files)
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
            # A key press and release in one host event batch acts like a one-frame press.
            add_test(NAME llamatron_tap COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/tap_check.py --native $<TARGET_FILE:llamatron>)
        endif()
        set_tests_properties(llamatron_title_phase llamatron_to_game llamatron_replay PROPERTIES
            ENVIRONMENT "LLAMATRON_PREF_DIR=${_llama_prefs}")
        # High scores kept across runs (game86 5e89 / 60aa): store a table any score beats, play
        # a game to its end holding fire (20 retraces per frame; it is saved after the name
        # entry times out), then start again and read the new table back.
        set(_scores_prefs ${CMAKE_CURRENT_BINARY_DIR}/llamatron-scores-prefs)
        file(MAKE_DIRECTORY ${_scores_prefs})
        add_test(NAME llamatron_scores_reset COMMAND ${CMAKE_COMMAND} -E rm -f ${_scores_prefs}/scores.yak)
        add_test(NAME llamatron_scores_low COMMAND ${CMAKE_COMMAND} -E env LLAMATRON_PREF_DIR=${_scores_prefs}
            $<TARGET_FILE:llamatron> --frames 2 --mute --low-scores)
        add_test(NAME llamatron_scores_play COMMAND ${CMAKE_COMMAND} -E env LLAMATRON_PREF_DIR=${_scores_prefs}
            $<TARGET_FILE:llamatron> --vga --keyboard --mute --flash N --seed 0 --start-after 3
            --keep-scores --speed 20 --frames 1600 --tap 300:57:100000)
        add_test(NAME llamatron_scores_reload COMMAND ${CMAKE_COMMAND} -E env LLAMATRON_PREF_DIR=${_scores_prefs}
            $<TARGET_FILE:llamatron> --frames 2 --mute --keep-scores)
        set_tests_properties(llamatron_scores_low PROPERTIES DEPENDS llamatron_scores_reset
            PASS_REGULAR_EXPRESSION "scores loaded: yes hash=6b258e45")
        set_tests_properties(llamatron_scores_play PROPERTIES DEPENDS llamatron_scores_low TIMEOUT 300
            PASS_REGULAR_EXPRESSION "scores loaded: yes hash=6b258e45.*scores saved: yes hash=05d3be9c")
        set_tests_properties(llamatron_scores_reload PROPERTIES DEPENDS llamatron_scores_play
            PASS_REGULAR_EXPRESSION "scores loaded: yes hash=05d3be9c")
        set_tests_properties(llamatron_scores_reset llamatron_scores_low llamatron_scores_play
            llamatron_scores_reload PROPERTIES RESOURCE_LOCK llamatron_scores_prefs)
        # In-game sound (M5): a game held on fire with llama.inf sound S (lower case, as
        # llama.exe upper-cases it) requests and plays samples through snd.c; with N none.
        set(_sound_prefs ${CMAKE_CURRENT_BINARY_DIR}/llamatron-sound-prefs)
        file(WRITE ${_sound_prefs}/llama.inf "V K s 0 N")
        set(_sound_run --flash N --seed 0 --start-after 3 --tap 300:57:100000 --frames 1500)
        add_test(NAME llamatron_game_sound COMMAND ${CMAKE_COMMAND} -E env LLAMATRON_PREF_DIR=${_sound_prefs}
            $<TARGET_FILE:llamatron> ${_sound_run})
        set_tests_properties(llamatron_game_sound PROPERTIES PASS_REGULAR_EXPRESSION
            "settings V K S 0 N.*game sound requests=73 started=64 samples=470814 audible=285198 hash=4b7610d9")
        add_test(NAME llamatron_game_sound_off COMMAND ${CMAKE_COMMAND} -E env LLAMATRON_PREF_DIR=${_sound_prefs}
            $<TARGET_FILE:llamatron> ${_sound_run} --sound N)
        set_tests_properties(llamatron_game_sound_off PROPERTIES PASS_REGULAR_EXPRESSION
            "game sound requests=0 started=0 samples=470814 audible=0 ")
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
            # Native / web parity on a reference scenario: same page hashes at the same ticks.
            set(LLAMATRON_NATIVE_LLAMATRON "${PROJECT_SOURCE_DIR}/build/src/platform/llamatron" CACHE FILEPATH
                "Native llamatron executable the web build is compared with (web_llamatron_parity)")
            add_test(NAME web_llamatron_parity COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/web_parity.py --native ${LLAMATRON_NATIVE_LLAMATRON}
                --web-dir ${CMAKE_CURRENT_BINARY_DIR} --golden ${PROJECT_SOURCE_DIR}/tests/golden/reference_runs.json
                --run walk_vga)
            # A Space keydown + keyup dispatched back to back (one batch) in the version
            # scroller acts like a one-frame press, as on the native side (llamatron_tap).
            add_test(NAME web_llamatron_tap COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/tap_check.py --native ${LLAMATRON_NATIVE_LLAMATRON}
                --web-dir ${CMAKE_CURRENT_BINARY_DIR})
            set_tests_properties(web_llamatron_title web_llamatron_to_game web_llamatron_parity
                web_llamatron_tap PROPERTIES
                SKIP_RETURN_CODE 77 LABELS web TIMEOUT 300)
            # Display: high-DPI (device pixel ratio 2: the backbuffer is 2000x1500 for the
            # 1000x750 CSS canvas, which fills the 1000x900 page at 4:3) and the scale modes
            # (integer: the VGA title at 5x3 = 1600x1200; sharp: 4x2 enlarged, then smoothed).
            add_test(NAME web_display_hidpi COMMAND ${Python3_EXECUTABLE} ${_game} --scale-factor 2
                --query frames=2&vga&mute --expect "title 320x400 hash=c7ab74c4 canvas 2000x1500 hash=4a321bc6")
            add_test(NAME web_display_integer COMMAND ${Python3_EXECUTABLE} ${_game} --scale-factor 2
                --query frames=2&vga&mute&scale=integer
                --expect "title 320x400 hash=c7ab74c4 canvas 2000x1500 hash=8d444d14")
            add_test(NAME web_display_sharp COMMAND ${Python3_EXECUTABLE} ${_game}
                --query frames=2&vga&mute&scale=sharp
                --expect "title 320x400 hash=c7ab74c4 canvas 1000x750 hash=0434b058")
            set_tests_properties(web_display_hidpi web_display_integer web_display_sharp PROPERTIES
                SKIP_RETURN_CODE 77 LABELS web TIMEOUT 180)
            # In-game sound through the wasm build (llamatron_game_sound; the tap reaches the
            # game a frame later than natively, which gives another run).
            add_test(NAME web_game_sound COMMAND ${Python3_EXECUTABLE} ${_game} --timeout-s 120
                --query vga&keyboard&flash=N&seed=0&start=3&sound=S&taps=300:57:100000&frames=1500
                --expect "game sound requests=73 started=64 samples=470814 audible=284832 hash=2fc5c0fb")
            set_tests_properties(web_game_sound PROPERTIES SKIP_RETURN_CODE 77 LABELS web TIMEOUT 180)
            # High scores through IDBFS, three page loads on one browser profile (as
            # llamatron_scores_*): store a low table; play to a saved hi-score (the tap reaches
            # the game a frame later than natively, hence another table); reload and read it.
            set(_scores_profile ${CMAKE_CURRENT_BINARY_DIR}/scores-profile)
            set(_scores ${_game} --profile ${_scores_profile} --port 8472 --timeout-s 240)
            add_test(NAME web_scores_reset COMMAND ${CMAKE_COMMAND} -E rm -rf ${_scores_profile})
            add_test(NAME web_scores_low COMMAND ${Python3_EXECUTABLE} ${_scores}
                --query frames=2&mute&lowscores --expect "scores stored: yes" --expect "persist: saved")
            add_test(NAME web_scores_play COMMAND ${Python3_EXECUTABLE} ${_scores}
                --query vga&keyboard&mute&flash=N&seed=0&start=3&keepscores&speed=20&frames=1600&taps=300:57:100000
                --expect "scores loaded: yes hash=6b258e45" --expect "scores saved: yes hash=05d3be9c"
                --expect "persist: saved")
            add_test(NAME web_scores_reload COMMAND ${Python3_EXECUTABLE} ${_scores}
                --query frames=2&mute&keepscores --expect "scores loaded: yes hash=05d3be9c")
            set_tests_properties(web_scores_low PROPERTIES DEPENDS web_scores_reset)
            set_tests_properties(web_scores_play PROPERTIES DEPENDS web_scores_low)
            set_tests_properties(web_scores_reload PROPERTIES DEPENDS web_scores_play)
            set_tests_properties(web_scores_reset web_scores_low web_scores_play web_scores_reload
                PROPERTIES LABELS web RESOURCE_LOCK web_scores_profile)
            set_tests_properties(web_scores_low web_scores_play web_scores_reload PROPERTIES
                SKIP_RETURN_CODE 77 TIMEOUT 300)
        endif()
    endif()
endif()
