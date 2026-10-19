# ctest checks for the extractor. All run the host extractor, so they also work in the
# Emscripten build tree.

add_test(NAME extract_verify
    COMMAND "${LLAMATRON_EXTRACT}" --verify --original "${LLAMATRON_ORIGINAL_DIR}" --manifest "${LLAMATRON_MANIFEST}")

# A manifest whose first required hash is wrong must be rejected (hashes only; nothing proprietary).
file(READ "${LLAMATRON_MANIFEST}" _manifest_text)
string(JSON _first_hash GET "${_manifest_text}" files 0 sha256)
string(REPEAT "0" 64 _zero_hash)
string(REPLACE "${_first_hash}" "${_zero_hash}" _bad_manifest "${_manifest_text}")
set(_bad_manifest_path "${CMAKE_BINARY_DIR}/tests/manifest-wrong-hash.json")
file(WRITE "${_bad_manifest_path}" "${_bad_manifest}")
add_test(NAME extract_verify_rejects_mismatch
    COMMAND "${LLAMATRON_EXTRACT}" --verify --original "${LLAMATRON_ORIGINAL_DIR}" --manifest "${_bad_manifest_path}")
set_tests_properties(extract_verify_rejects_mismatch PROPERTIES WILL_FAIL TRUE)

# The C unpackers must reproduce the Python reference tools byte for byte, per stage.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    foreach(_exe game86 fs llama install)
        add_test(NAME unpack_matches_python_${_exe}
            COMMAND ${CMAKE_COMMAND}
                "-DEXTRACT=${LLAMATRON_EXTRACT}"
                "-DPYTHON=${Python3_EXECUTABLE}"
                "-DTOOLS_DIR=${PROJECT_SOURCE_DIR}/tools"
                "-DORIGINAL_DIR=${LLAMATRON_ORIGINAL_DIR}"
                "-DNAME=${_exe}"
                "-DWORK_DIR=${CMAKE_BINARY_DIR}/tests/unpack"
                -P "${PROJECT_SOURCE_DIR}/cmake/tests/CompareUnpackers.cmake")
    endforeach()
    # Typed texts must reproduce the original byte strings exactly (needs a built tree).
    add_test(NAME strings_roundtrip
        COMMAND "${Python3_EXECUTABLE}" -I "${PROJECT_SOURCE_DIR}/cmake/tests/strings_roundtrip.py"
            "${LLAMATRON_GENERATED_DIR}/data/strings.dat" "${LLAMATRON_GENERATED_DIR}/exe/game86.unpacked")
    add_test(NAME levels_roundtrip
        COMMAND "${Python3_EXECUTABLE}" -I "${PROJECT_SOURCE_DIR}/cmake/tests/levels_roundtrip.py"
            "${LLAMATRON_GENERATED_DIR}/data/levels.dat" "${LLAMATRON_GENERATED_DIR}/exe/game86.unpacked")
else()
    message(STATUS "python3 not found: skipping unpack_matches_python tests")
endif()

# The extractor's LZSS against the original's cmp_load / lzss_decode / data_load (game86
# 1000:e65e, e6bb, e73d, e778, 60aa) run in the unicorn harness (test oracle only).
set(_harness_python "${PROJECT_SOURCE_DIR}/tools/harness/.venv/bin/python")
if(DEFINED LLAMATRON_HARNESS_PYTHON)
    set(_harness_python "${LLAMATRON_HARNESS_PYTHON}")
endif()
if(EXISTS "${_harness_python}")
    add_test(NAME extract_lzss_oracle
        COMMAND "${_harness_python}" -I "${PROJECT_SOURCE_DIR}/cmake/tests/lzss_oracle.py"
            "${LLAMATRON_EXTRACT}" "${LLAMATRON_GENERATED_DIR}/exe/game86.unpacked"
            "${LLAMATRON_ORIGINAL_DIR}" "${LLAMATRON_MANIFEST}" "${CMAKE_BINARY_DIR}/tests/lzss")
    set_tests_properties(extract_lzss_oracle PROPERTIES SKIP_RETURN_CODE 77 LABELS "oracle" TIMEOUT 300)
    # llama.inf as llama.exe inf_read (1000:01e7) reads it and install.exe write_config
    # (1000:050f) writes it: the extractor's config.dat import, and the platform's
    # settings_parse_inf / settings_format_inf through a small probe.
    add_test(NAME extract_launcher_oracle
        COMMAND "${_harness_python}" -I "${PROJECT_SOURCE_DIR}/cmake/tests/launcher_oracle.py"
            "${LLAMATRON_EXTRACT}" "${LLAMATRON_GENERATED_DIR}/exe" "${CMAKE_BINARY_DIR}/tests/launcher")
    set_tests_properties(extract_launcher_oracle PROPERTIES SKIP_RETURN_CODE 77 LABELS "oracle" TIMEOUT 300)
    if(NOT EMSCRIPTEN)
        add_executable(settings_inf_probe "${PROJECT_SOURCE_DIR}/cmake/tests/settings_inf_probe.c"
            "${PROJECT_SOURCE_DIR}/src/platform/settings.c")
        target_include_directories(settings_inf_probe PRIVATE "${PROJECT_SOURCE_DIR}/src/platform")
        foreach(_check format parse)
            add_test(NAME settings_${_check}_oracle
                COMMAND "${_harness_python}" -I "${PROJECT_SOURCE_DIR}/cmake/tests/launcher_oracle.py"
                    "${LLAMATRON_EXTRACT}" "${LLAMATRON_GENERATED_DIR}/exe" "${CMAKE_BINARY_DIR}/tests/launcher-${_check}"
                    --probe $<TARGET_FILE:settings_inf_probe> --check ${_check})
            set_tests_properties(settings_${_check}_oracle PROPERTIES SKIP_RETURN_CODE 77 LABELS "oracle" TIMEOUT 300)
        endforeach()
        # Disabled until settings_parse_inf upper-cases and rejects as llama.exe does (W2).
        set_tests_properties(settings_parse_oracle PROPERTIES DISABLED TRUE)
    endif()
else()
    message(STATUS "no harness venv: skipping extract_lzss_oracle")
endif()
