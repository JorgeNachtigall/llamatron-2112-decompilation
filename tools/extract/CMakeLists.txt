# The extractor always runs on the build host. It is built either as part of a native build
# (add_subdirectory) or as a standalone host project from cmake/Extract.cmake when the main
# build cross-compiles (Emscripten).
cmake_minimum_required(VERSION 3.20)
project(llamatron_extract LANGUAGES C)

add_executable(llamatron-extract
    blob.c
    cmpdata.c
    common.c
    ilbm.c
    lzss.c
    main.c
    manifest.c
    mz.c
    outputs.c
    samples.c
    sha256.c
    sprites.c
    title.c
    unexepack.c
    unlzexe.c
)
set_target_properties(llamatron-extract PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(llamatron-extract PRIVATE /W4)
else()
    target_compile_options(llamatron-extract PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()
