# Game-facing services, pure C11 without SDL (testable headless).
add_library(llama_engine STATIC
    gfx.c
    input.c
    timing.c
    title.c
)
target_include_directories(llama_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(llama_engine PRIVATE
    $<$<C_COMPILER_ID:Clang,AppleClang,GNU>:-Wall -Wextra -Wpedantic>)

add_executable(engine_selftest engine_selftest.c)
target_link_libraries(engine_selftest PRIVATE llama_engine)
add_test(NAME engine_selftest COMMAND engine_selftest)
