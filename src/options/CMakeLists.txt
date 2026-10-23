# The port's options screen (PLAN M7, W1): host-side, drawn with the game's text engine.
# settings.h (llama.inf fields) comes from src/platform; the screen itself needs no SDL.
add_library(llamatron_options STATIC options_screen.c)
target_include_directories(llamatron_options PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/platform)
target_link_libraries(llamatron_options PUBLIC llamatron_game)

if(NOT EMSCRIPTEN)
    add_executable(options_selftest options_selftest.c ${PROJECT_SOURCE_DIR}/src/platform/settings.c)
    target_link_libraries(options_selftest PRIVATE llamatron_options llama_assets_files)
    if(TARGET llamatron_generated)
        add_dependencies(options_selftest llamatron_generated)
    endif()
    add_test(NAME options_selftest COMMAND options_selftest
        --dump ${CMAKE_CURRENT_BINARY_DIR}/options.ppm)
endif()

# The game executable opens the screen from its title (apps/llamatron_main.c).
if(TARGET llamatron)
    target_link_libraries(llamatron PRIVATE llamatron_options)
endif()
if(TARGET llamatron AND NOT EMSCRIPTEN)
    # Through the real executable: O on the title, Right (video -> EGA), Down Down Right
    # (sound None -> Sound Blaster), Enter. The title is reloaded for EGA at once, llama.inf is
    # stored, and the next start reads it; Esc leaves without storing.
    set(_prefs ${CMAKE_CURRENT_BINARY_DIR}/options-prefs)
    add_test(NAME llamatron_options_reset
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${_prefs})
    add_test(NAME llamatron_options_prefs
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_prefs})
    add_test(NAME llamatron_options_save
        COMMAND ${CMAKE_COMMAND} -E env LLAMATRON_PREF_DIR=${_prefs}
            $<TARGET_FILE:llamatron> --frames 12 --mute --tap 2:24:0,3:77:0,4:80:0,5:80:0,6:77:0,7:28:0)
    add_test(NAME llamatron_options_reload
        COMMAND ${CMAKE_COMMAND} -E env LLAMATRON_PREF_DIR=${_prefs}
            $<TARGET_FILE:llamatron> --frames 12 --mute --start-after 3 --seed 0)
    add_test(NAME llamatron_options_cancel
        COMMAND ${CMAKE_COMMAND} -E env LLAMATRON_PREF_DIR=${_prefs}
            $<TARGET_FILE:llamatron> --frames 12 --mute --tap 2:24:0,3:77:0,4:1:0)
    set_tests_properties(llamatron_options_reset PROPERTIES FIXTURES_SETUP llamatron_options)
    set_tests_properties(llamatron_options_prefs PROPERTIES FIXTURES_SETUP llamatron_options
        DEPENDS llamatron_options_reset)
    set_tests_properties(llamatron_options_save PROPERTIES FIXTURES_REQUIRED llamatron_options
        PASS_REGULAR_EXPRESSION "options saved E K S 0 N \\(stored\\)\ntitle 640x350")
    set_tests_properties(llamatron_options_reload PROPERTIES FIXTURES_REQUIRED llamatron_options
        DEPENDS llamatron_options_save PASS_REGULAR_EXPRESSION "settings E K S 0 N\n.*phase=game")
    set_tests_properties(llamatron_options_cancel PROPERTIES FIXTURES_REQUIRED llamatron_options
        DEPENDS llamatron_options_reload
        PASS_REGULAR_EXPRESSION "settings E K S 0 N\ntitle 640x350.*phase=title" FAIL_REGULAR_EXPRESSION "options saved")
endif()
