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
