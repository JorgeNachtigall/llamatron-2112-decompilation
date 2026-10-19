# Reimplemented game86 logic (docs/PLAN.md §2.1), pure C11 without SDL.
# W4 owns this file; W5 (entities/) and W6 (draw_*.c, text.c, hiscore.c) add their sources here.
# tests/cmake/TestLib.cmake builds the ctypes test library llamatron_game_testlib from this
# target's sources and settings, so keep a single source list and keep it PIC.
add_library(llamatron_game STATIC
    game.c
    score.c
    script.c
    entity_loop.c
    frame.c
    sound.c
    rng.c
    draw_page.c
    draw_rect.c
    draw_target.c
    text.c
    draw_sprite.c
    hud.c
    draw_scroller.c
    hiscore.c
    entities/anim.c
    entities/enemy_init.c
    entities/hit.c
    entities/kill_actions.c
    entities/pickups.c
    entities/spawn.c
    entities/movement.c
    entities/think_basic.c
    entities/think_spawners.c
    entities/think_homing.c
    entities/think_chargers.c
    entities/think_herders.c
    entities/think_chasers.c
    entities/think_orbiters.c
    entities/think_table.c
)
target_include_directories(llamatron_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/assets)   # W6 hud.c: HudGlyphs type from formats.h (header only)
target_link_libraries(llamatron_game PUBLIC llama_engine)   # W6 draw_*.c use engine/gfx.h
# Game data loaders (game_data.h); the executable still picks the asset store
# (llama_assets_embedded or llama_assets_files).
target_link_libraries(llamatron_game PUBLIC llamatron_assets)
set_target_properties(llamatron_game PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(llamatron_game PRIVATE
    $<$<C_COMPILER_ID:Clang,AppleClang,GNU>:-Wall -Wextra -Wpedantic>)
