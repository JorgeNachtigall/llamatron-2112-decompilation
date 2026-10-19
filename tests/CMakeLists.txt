# Differential tests against the original code (docs/PLAN.md §3.2).
#
# Works both standalone (cmake -S tests -B build/tests && ctest --test-dir build/tests)
# and as add_subdirectory(tests) from the top-level project.  Each tests/oracle/test_*.py
# becomes one ctest entry running pytest in the harness venv (tools/harness/.venv, created
# by tools/harness/setup_venv.sh).  The harness is a test oracle only: nothing here is
# linked into src/.
cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(llamatron_tests NONE)
  enable_testing()
endif()

get_filename_component(LLAMATRON_REPO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(LLAMATRON_HARNESS_DIR "${LLAMATRON_REPO_DIR}/tools/harness")
set(LLAMATRON_HARNESS_PYTHON "${LLAMATRON_HARNESS_DIR}/.venv/bin/python" CACHE FILEPATH
    "Python interpreter with unicorn + pytest (see tools/harness/setup_venv.sh)")
option(LLAMATRON_HARNESS_SETUP "Create the harness venv at configure time if missing" OFF)

# The image under test: the extractor's flat load module if the top-level build
# defines LLAMATRON_GENERATED_DIR, else the harness picks its own default
# (build/generated/exe/game86.unpacked, then unpacked/game86.exe).
set(_harness_env "PYTHONDONTWRITEBYTECODE=1")
if(DEFINED LLAMATRON_GENERATED_DIR)
  list(APPEND _harness_env "LLAMATRON_GAME86_IMAGE=${LLAMATRON_GENERATED_DIR}/exe/game86.unpacked")
endif()

# Shared build of src/game for the C side of the comparisons (ctypes).
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/TestLib.cmake")
if(LLAMATRON_TESTLIB_ENV)
  list(APPEND _harness_env "${LLAMATRON_TESTLIB_ENV}")
endif()

# DOSBox-X reference-run smoke tests (PLAN §3.3): the original launch chain runs
# headless on a copy of the original folder. Needs dosbox-x and the original files;
# label "dosbox" (about 25 s each), exclude with ctest -LE dosbox.
find_program(LLAMATRON_DOSBOX_X dosbox-x)
find_program(LLAMATRON_PYTHON3 python3)
if(DEFINED LLAMATRON_ORIGINAL_DIR AND NOT LLAMATRON_ORIGINAL_DIR STREQUAL "")
  set(_original_dir "${LLAMATRON_ORIGINAL_DIR}")
else()
  set(_original_dir "${LLAMATRON_REPO_DIR}/original")
endif()
if(LLAMATRON_DOSBOX_X AND LLAMATRON_PYTHON3 AND EXISTS "${_original_dir}/llama.exe")
  foreach(_machine vga ega)
    add_test(NAME dosbox.smoke_${_machine}
             COMMAND "${LLAMATRON_PYTHON3}" "${LLAMATRON_HARNESS_DIR}/dosbox/refrun.py" smoke
                     --machine ${_machine} --original "${_original_dir}"
                     --workdir "${CMAKE_CURRENT_BINARY_DIR}/dosbox/smoke-${_machine}"
                     --time-limit 60)
    set_tests_properties(dosbox.smoke_${_machine} PROPERTIES LABELS "dosbox" TIMEOUT 120)
  endforeach()
else()
  message(STATUS "DOSBox-X smoke tests disabled (need dosbox-x on PATH and the original files)")
endif()

if(NOT EXISTS "${LLAMATRON_HARNESS_PYTHON}" AND LLAMATRON_HARNESS_SETUP)
  execute_process(COMMAND sh "${LLAMATRON_HARNESS_DIR}/setup_venv.sh"
                  RESULT_VARIABLE _venv_rc)
  if(NOT _venv_rc EQUAL 0)
    message(WARNING "tools/harness/setup_venv.sh failed (${_venv_rc})")
  endif()
endif()

if(NOT EXISTS "${LLAMATRON_HARNESS_PYTHON}")
  message(STATUS "Oracle tests disabled: no harness venv at ${LLAMATRON_HARNESS_PYTHON} "
                 "(run tools/harness/setup_venv.sh or configure with -DLLAMATRON_HARNESS_SETUP=ON)")
  return()
endif()

file(GLOB _oracle_tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/oracle/test_*.py")
foreach(_test IN LISTS _oracle_tests)
  get_filename_component(_name "${_test}" NAME_WE)
  string(REGEX REPLACE "^test_" "" _name "${_name}")
  add_test(NAME oracle.${_name}
           COMMAND "${LLAMATRON_HARNESS_PYTHON}" -m pytest -q -p no:cacheprovider
                   --rootdir "${CMAKE_CURRENT_SOURCE_DIR}" "${_test}"
           WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  set_tests_properties(oracle.${_name} PROPERTIES
    ENVIRONMENT "${_harness_env}"
    LABELS "oracle"
    # Report as skipped (not passed) when every case was skipped, e.g. no game image.
    SKIP_REGULAR_EXPRESSION "\n[0-9]+ skipped in ")
endforeach()
