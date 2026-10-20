# ctest checks on the source tree itself; they need neither the original files nor a build.

# Game code names what it uses (PLAN §0.1): no lookups by the original's DS offset in src/game
# outside the allow-list in NoDsOffsets.cmake. The fixtures prove the check still bites.
add_test(NAME source_no_ds_offsets
    COMMAND ${CMAKE_COMMAND} "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}/src/game" "-DROOT=${PROJECT_SOURCE_DIR}"
        -P "${PROJECT_SOURCE_DIR}/cmake/tests/NoDsOffsets.cmake")
foreach(_case good bad)
    set(_fixture "${PROJECT_SOURCE_DIR}/cmake/tests/ds_offsets/${_case}")
    add_test(NAME source_no_ds_offsets_fixture_${_case}
        COMMAND ${CMAKE_COMMAND} "-DSOURCE_DIR=${_fixture}/src/game" "-DROOT=${_fixture}"
            -P "${PROJECT_SOURCE_DIR}/cmake/tests/NoDsOffsets.cmake")
endforeach()
# Passes when all three calls of the bad fixture are reported (exit status not used).
set_tests_properties(source_no_ds_offsets_fixture_bad PROPERTIES
    PASS_REGULAR_EXPRESSION "bad.c: asset_palette_at\\(0x2175\\).*bad.c: asset_text_at\\(.*bad.c: game_sprite_at\\(0x245a\\)")
set_tests_properties(source_no_ds_offsets source_no_ds_offsets_fixture_good source_no_ds_offsets_fixture_bad
    PROPERTIES LABELS guard)
