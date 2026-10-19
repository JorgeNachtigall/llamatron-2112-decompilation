# Llamatron 2112 port (docs/PLAN.md). Configure with a preset:
#   cmake --preset macos-native [-DLLAMATRON_ORIGINAL_DIR=/path/to/llamatron]
#   emcmake cmake --preset web-emscripten [-DLLAMATRON_ORIGINAL_DIR=...]
cmake_minimum_required(VERSION 3.20)
project(llamatron VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(LLAMATRON_ORIGINAL_DIR "" CACHE PATH
    "Your own copy of the original game (required; hash-verified, read at build time only)")
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

include(cmake/Extract.cmake)

enable_testing()
include(cmake/ExtractTests.cmake)

if(EXISTS "${PROJECT_SOURCE_DIR}/src/CMakeLists.txt")
    add_subdirectory(src)
endif()
# W1 data loaders; needs llama_engine (gfx.h types) and the asset store from src/.
if(TARGET llama_engine AND NOT TARGET llamatron_assets)
    add_subdirectory(src/assets)
endif()
if(EXISTS "${PROJECT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()
