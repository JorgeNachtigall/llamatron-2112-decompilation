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

# Title screen regression: hashes of the extracted pictures as shown (pixels + palette).
add_executable(title_hash title_hash.c)
target_link_libraries(title_hash PRIVATE llama_engine)
if(LLAMATRON_GENERATED_DIR AND NOT EMSCRIPTEN)
    add_test(NAME title_hash_vga COMMAND title_hash --data "${LLAMATRON_GENERATED_DIR}/data" --expect c7ab74c4)
    add_test(NAME title_hash_ega COMMAND title_hash --data "${LLAMATRON_GENERATED_DIR}/data" --ega --expect d0a1684d)
endif()
