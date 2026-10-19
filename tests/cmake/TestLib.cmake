# llamatron_game_testlib: a SHARED build of the game logic, loaded by the oracle tests
# through ctypes (tests/adapters/cport.py).  Test-only; the shipped binaries link the
# static llamatron_game.
#
# It is assembled from the llamatron_game target itself (same sources, include dirs,
# definitions, options), so src/game owns a single source list.  Built when
# BUILD_TESTING is ON (or undefined), only for native host builds.
# Sets LLAMATRON_TESTLIB_ENV for the test environment.

set(LLAMATRON_TESTLIB_ENV "")
if(NOT TARGET llamatron_game)
  message(STATUS "llamatron_game_testlib: no llamatron_game target yet, C oracle tests skipped")
  return()
endif()
if(DEFINED BUILD_TESTING AND NOT BUILD_TESTING)
  return()
endif()
if(CMAKE_CROSSCOMPILING OR EMSCRIPTEN)
  return()
endif()

get_target_property(_game_srcs llamatron_game SOURCES)
get_target_property(_game_dir llamatron_game SOURCE_DIR)
set(_testlib_srcs "")
foreach(_src IN LISTS _game_srcs)
  if(_src MATCHES "^\\$<" OR IS_ABSOLUTE "${_src}")
    list(APPEND _testlib_srcs "${_src}")
  else()
    list(APPEND _testlib_srcs "${_game_dir}/${_src}")
  endif()
endforeach()
# GameState <-> DGROUP adapter (tests/adapters/dgroup_map.h), only in the test build
list(APPEND _testlib_srcs "${CMAKE_CURRENT_LIST_DIR}/../adapters/dgroup_map.c")

add_library(llamatron_game_testlib SHARED ${_testlib_srcs})
set(_game "llamatron_game")
target_include_directories(llamatron_game_testlib PRIVATE
  "$<TARGET_PROPERTY:${_game},INCLUDE_DIRECTORIES>")
target_compile_definitions(llamatron_game_testlib PRIVATE
  "$<TARGET_PROPERTY:${_game},COMPILE_DEFINITIONS>" LLAMATRON_TESTLIB=1)
target_compile_options(llamatron_game_testlib PRIVATE
  "$<TARGET_PROPERTY:${_game},COMPILE_OPTIONS>")
target_link_libraries(llamatron_game_testlib PRIVATE
  "$<TARGET_PROPERTY:${_game},LINK_LIBRARIES>")
target_include_directories(llamatron_game_testlib PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../adapters")
# dgroup_map resolves DS pointers ([0x39EB], [0x06C7]) through the extracted data
if(TARGET llamatron_assets AND TARGET llama_assets_files)
  target_link_libraries(llamatron_game_testlib PRIVATE llamatron_assets llama_assets_files)
endif()
set_target_properties(llamatron_game_testlib PROPERTIES
  C_VISIBILITY_PRESET default           # ctypes needs every game function exported
  POSITION_INDEPENDENT_CODE ON)
if(TARGET llamatron_generated)           # generated headers must exist before compiling
  add_dependencies(llamatron_game_testlib llamatron_generated)
endif()

set(LLAMATRON_TESTLIB_ENV "LLAMATRON_GAME_TESTLIB=$<TARGET_FILE:llamatron_game_testlib>")

# Headless replay driver: the port on a PROBE.KEY script, state dumped as DGROUP images for
# comparison with the DOSBox-X reference runs (tests/replay/).
set(_replay_script_c "${LLAMATRON_REPO_DIR}/src/platform/apps/input_script.c")
if(TARGET llamatron_assets AND TARGET llama_assets_files AND EXISTS "${_replay_script_c}")
  add_executable(llamatron_replay
    "${CMAKE_CURRENT_LIST_DIR}/../replay/replay_driver.c"
    "${CMAKE_CURRENT_LIST_DIR}/../adapters/dgroup_map.c"
    "${_replay_script_c}")
  target_include_directories(llamatron_replay PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../adapters" "${LLAMATRON_REPO_DIR}/src/platform/apps"
    "${LLAMATRON_REPO_DIR}/src/platform")
  target_link_libraries(llamatron_replay PRIVATE llamatron_game llamatron_assets llama_assets_files)
  set(LLAMATRON_REPLAY_ENV "LLAMATRON_REPLAY=$<TARGET_FILE:llamatron_replay>")
endif()
