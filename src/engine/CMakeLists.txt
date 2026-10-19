# Game-facing services, pure C11 without SDL (testable headless).
add_library(llama_engine STATIC
    gfx.c
    input.c
    timing.c
    title.c
    snd.c
)
# The ctypes test library (tests/cmake/TestLib.cmake) links this into a shared object.
set_target_properties(llama_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(llama_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(llama_engine PUBLIC llama_assets_api)
target_compile_options(llama_engine PRIVATE
    $<$<C_COMPILER_ID:Clang,AppleClang,GNU>:-Wall -Wextra -Wpedantic>)

add_executable(engine_selftest engine_selftest.c)
target_link_libraries(engine_selftest PRIVATE llama_engine)
add_test(NAME engine_selftest COMMAND engine_selftest)

add_executable(snd_selftest snd_selftest.c)
target_link_libraries(snd_selftest PRIVATE llama_engine llama_assets_files)
add_test(NAME snd_selftest COMMAND snd_selftest)

# Title screen regression: hashes of the extracted pictures as shown (pixels + palette).
add_executable(title_hash title_hash.c)
target_link_libraries(title_hash PRIVATE llama_engine llama_assets_files)
if(LLAMATRON_GENERATED_DIR AND NOT EMSCRIPTEN)
    add_test(NAME title_hash_vga COMMAND title_hash --data "${LLAMATRON_GENERATED_DIR}/data" --expect c7ab74c4)
    add_test(NAME title_hash_ega COMMAND title_hash --data "${LLAMATRON_GENERATED_DIR}/data" --ega --expect d0a1684d)
    # Title sound on the real bank (sample 1Bh, 100 ticks, 22050 Hz).
    add_executable(snd_hash snd_hash.c)
    target_link_libraries(snd_hash PRIVATE llama_engine llama_assets_files)
    add_test(NAME snd_hash_title COMMAND snd_hash --data "${LLAMATRON_GENERATED_DIR}/data" --expect 16d7fc03)
    if(TARGET llama_assets_embedded)
        # Same picture through the embedded blob: proves the blob matches the data files.
        add_executable(title_hash_embedded title_hash.c)
        target_link_libraries(title_hash_embedded PRIVATE llama_engine llama_assets_embedded)
        add_test(NAME title_hash_vga_embedded COMMAND title_hash_embedded --expect c7ab74c4)
        add_test(NAME title_hash_ega_embedded COMMAND title_hash_embedded --ega --expect d0a1684d)
    endif()
endif()
