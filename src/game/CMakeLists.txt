# Reimplemented game86 logic (docs/PLAN.md §2.1), pure C11 without SDL.
# W4 owns this file; W5 (entities/) and W6 (draw_*.c, text.c, hiscore.c) add their sources here.
# tests/cmake/TestLib.cmake builds the ctypes test library llamatron_game_testlib from this
# target's sources and settings, so keep a single source list and keep it PIC.
add_library(llamatron_game STATIC
    game.c
    score.c
    rng.c
    draw_page.c
    draw_rect.c
    draw_target.c
    text.c
    draw_sprite.c
    hud.c
    draw_scroller.c
    hiscore.c
)
target_include_directories(llamatron_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/assets)   # W6 hud.c: HudGlyphs type from formats.h (header only)
target_link_libraries(llamatron_game PUBLIC llama_engine)   # W6 draw_*.c use engine/gfx.h
set_target_properties(llamatron_game PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(llamatron_game PRIVATE
    $<$<C_COMPILER_ID:Clang,AppleClang,GNU>:-Wall -Wextra -Wpedantic>)
