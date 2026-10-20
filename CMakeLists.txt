# Llamatron 2112 port (docs/PLAN.md). Configure with a preset:
#   cmake --preset macos-native [-DLLAMATRON_ORIGINAL_DIR=/path/to/llamatron]
#   emcmake cmake --preset web-emscripten [-DLLAMATRON_ORIGINAL_DIR=...]
# The game files are read from -DLLAMATRON_ORIGINAL_DIR, else $LLAMATRON_ORIGINAL_DIR, else
# <repo>/original (cmake/Extract.cmake).
cmake_minimum_required(VERSION 3.20)
project(llamatron VERSION 0.1.0 LANGUAGES C)
# CMake 3.29+: a link line names each static library once, so Apple's ld stops warning
# "ignoring duplicate libraries" (the game, testlib and app targets share libraries). The
# default covers subdirectories whose own cmake_minimum_required(3.20) resets the policy
# (tests/, which builds the testlib).
if(POLICY CMP0156)
    cmake_policy(SET CMP0156 NEW)
    set(CMAKE_POLICY_DEFAULT_CMP0156 NEW)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(LLAMATRON_ORIGINAL_DIR "" CACHE PATH
    "Your own copy of the original game, read at build time (empty: the environment variable of that name, else <repo>/original)")
option(LLAMATRON_ALLOW_UNVERIFIED "RE work only: continue when original files do not match the manifest" OFF)
set(LLAMATRON_TARGET "native" CACHE STRING "Target platform: native or web")
set_property(CACHE LLAMATRON_TARGET PROPERTY STRINGS native web)

if(LLAMATRON_TARGET STREQUAL "web" AND NOT EMSCRIPTEN)
    message(FATAL_ERROR
        "The web-emscripten preset needs the Emscripten toolchain. Install emsdk (or "
        "`brew install emscripten`) and configure through emcmake:\n"
        "  emcmake cmake --preset web-emscripten")
endif()
if(EMSCRIPTEN AND NOT LLAMATRON_TARGET STREQUAL "web")
    set(LLAMATRON_TARGET "web" CACHE STRING "Target platform: native or web" FORCE)
endif()

include(cmake/Warnings.cmake)
include(cmake/Extract.cmake)

enable_testing()
include(cmake/ExtractTests.cmake)
include(cmake/SourceChecks.cmake)

if(EXISTS "${PROJECT_SOURCE_DIR}/src/CMakeLists.txt")
    add_subdirectory(src)
endif()
# W1 data loaders; needs llama_engine (gfx.h types) and the asset store from src/.
if(TARGET llama_engine AND NOT TARGET llamatron_assets)
    add_subdirectory(src/assets)
endif()
# W1 options screen (PLAN M7); host-side, on top of the game's text engine.
if(TARGET llamatron_game AND TARGET llama_assets_files)
    add_subdirectory(src/options)
endif()
if(EXISTS "${PROJECT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()
