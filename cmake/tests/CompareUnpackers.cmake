# cmake -P script: unpacks ORIGINAL_DIR/NAME.exe with the Python reference tools and with the C
# extractor, and fails unless both stages (LZEXE, then EXEPACK) and the full unpack agree byte
# for byte. Inputs: EXTRACT, PYTHON, TOOLS_DIR, ORIGINAL_DIR, NAME, WORK_DIR.

set(_in "${ORIGINAL_DIR}/${NAME}.exe")
if(NOT EXISTS "${_in}")
    string(TOUPPER "${NAME}.exe" _upper)
    set(_in "${ORIGINAL_DIR}/${_upper}")
endif()
set(_dir "${WORK_DIR}/${NAME}")
file(REMOVE_RECURSE "${_dir}")
file(MAKE_DIRECTORY "${_dir}")

function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _out)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "command failed (${_rc}): ${ARGN}\n${_out}")
    endif()
endfunction()

function(expect_same a b what)
    execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files "${a}" "${b}" RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        file(SHA256 "${a}" _ha)
        file(SHA256 "${b}" _hb)
        message(FATAL_ERROR "${NAME}: ${what} differs\n  ${a} ${_ha}\n  ${b} ${_hb}")
    endif()
endfunction()

run("${PYTHON}" -I "${TOOLS_DIR}/unlzexe.py" "${_in}" "${_dir}/py-stage1.exe")
run("${PYTHON}" -I "${TOOLS_DIR}/unexepack.py" "${_dir}/py-stage1.exe" "${_dir}/py-stage2.exe")

run("${EXTRACT}" --unlzexe "${_in}" "${_dir}/c-stage1.exe")
run("${EXTRACT}" --unexepack "${_dir}/c-stage1.exe" "${_dir}/c-stage2.exe")
run("${EXTRACT}" --unpack "${_in}" "${_dir}/c-unpack.exe")

expect_same("${_dir}/py-stage1.exe" "${_dir}/c-stage1.exe" "unlzexe output")
expect_same("${_dir}/py-stage2.exe" "${_dir}/c-stage2.exe" "unexepack output")
expect_same("${_dir}/py-stage2.exe" "${_dir}/c-unpack.exe" "--unpack output")
message(STATUS "${NAME}: C unpackers match the Python reference byte for byte")
