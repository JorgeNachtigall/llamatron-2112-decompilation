# Compiler warnings for every C target of the project (the code-health sweep, 2026-10-19).
# The set below builds clean, natively and with emcc; a new warning is meant to be fixed, not
# silenced. LLAMATRON_WERROR (the ci-* presets) turns warnings into errors; the everyday
# presets leave it off. The extractor is a separate host build and repeats this set in
# tools/extract/CMakeLists.txt.
option(LLAMATRON_WERROR "Treat compiler warnings as errors (the ci-* presets)" OFF)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        -Wstrict-prototypes -Wmissing-prototypes -Wimplicit-fallthrough -Wformat=2 -Wundef -Wvla
        -Wcast-qual)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        add_compile_options(-Wcomma -Wmissing-variable-declarations)
    endif()
    if(LLAMATRON_WERROR)
        add_compile_options(-Werror)
    endif()
elseif(MSVC)
    add_compile_options(/W4)
    if(LLAMATRON_WERROR)
        add_compile_options(/WX)
    endif()
endif()
