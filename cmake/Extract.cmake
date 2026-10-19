# Build-time extraction (docs/PLAN.md §2.2).
#
# Configure: builds tools/extract for the host (also when cross-compiling to Emscripten) and runs
# `llamatron-extract --verify` against tools/manifest/original-files.json.
# Build: target `llamatron_generated` runs `llamatron-extract --all` into ${LLAMATRON_GENERATED_DIR}
# whenever the extractor, the manifest or an original file changes.
#
# Provides: LLAMATRON_EXTRACT (host executable), LLAMATRON_GENERATED_DIR, LLAMATRON_MANIFEST,
# LLAMATRON_ORIGINAL_FILES (resolved paths), LLAMATRON_GENERATED_DATA (files under generated/data),
# LLAMATRON_ASSETS_BLOB_C (generated/assets_blob.c: every data file as a C array),
# LLAMATRON_ASSET_INDEX_H (generated/asset_index.h: file names/sizes, format versions, counts;
# include it as "asset_index.h" with ${LLAMATRON_GENERATED_DIR} on the include path) and the target
# `llamatron_generated`.

set(LLAMATRON_MANIFEST "${PROJECT_SOURCE_DIR}/tools/manifest/original-files.json")
set(LLAMATRON_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(_extract_src "${PROJECT_SOURCE_DIR}/tools/extract")
set(_host_dir "${CMAKE_BINARY_DIR}/host-tools")
set(_host_bin "${_host_dir}/bin")

if(NOT LLAMATRON_ORIGINAL_DIR)
    message(FATAL_ERROR
        "LLAMATRON_ORIGINAL_DIR is not set. Point it at your own copy of Llamatron 2112:\n"
        "  cmake --preset macos-native -DLLAMATRON_ORIGINAL_DIR=/path/to/llamatron")
endif()
get_filename_component(LLAMATRON_ORIGINAL_DIR "${LLAMATRON_ORIGINAL_DIR}" ABSOLUTE)
if(NOT IS_DIRECTORY "${LLAMATRON_ORIGINAL_DIR}")
    message(FATAL_ERROR "LLAMATRON_ORIGINAL_DIR=${LLAMATRON_ORIGINAL_DIR} is not a directory.")
endif()

# Original files listed in the manifest, matched as written or in upper case like the extractor.
file(READ "${LLAMATRON_MANIFEST}" _manifest_json)
string(JSON _file_count LENGTH "${_manifest_json}" files)
math(EXPR _last "${_file_count} - 1")
set(LLAMATRON_ORIGINAL_FILES "")
foreach(_i RANGE ${_last})
    string(JSON _name GET "${_manifest_json}" files ${_i} name)
    string(TOUPPER "${_name}" _upper)
    foreach(_candidate "${_name}" "${_upper}")
        if(EXISTS "${LLAMATRON_ORIGINAL_DIR}/${_candidate}")
            list(APPEND LLAMATRON_ORIGINAL_FILES "${LLAMATRON_ORIGINAL_DIR}/${_candidate}")
            break()
        endif()
    endforeach()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${LLAMATRON_MANIFEST}")

# --- host build of the extractor -------------------------------------------------------------
# A separate CMake project so it always uses the host compiler, never the Emscripten toolchain.
get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
set(_host_args
    -S "${_extract_src}" -B "${_host_dir}" -G "${CMAKE_GENERATOR}"
    -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${_host_bin}"
    "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE=${_host_bin}")
if(CMAKE_MAKE_PROGRAM AND NOT _multi_config)
    list(APPEND _host_args "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
endif()
set(_host_env ${CMAKE_COMMAND} -E env --unset=CC --unset=CFLAGS --unset=LDFLAGS --unset=CMAKE_TOOLCHAIN_FILE)

execute_process(COMMAND ${_host_env} ${CMAKE_COMMAND} ${_host_args}
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _out)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Configuring the host extractor failed:\n${_out}")
endif()
execute_process(COMMAND ${_host_env} ${CMAKE_COMMAND} --build "${_host_dir}" --config Release
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _out)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Building the host extractor failed:\n${_out}")
endif()

set(LLAMATRON_EXTRACT "${_host_bin}/llamatron-extract${CMAKE_HOST_EXECUTABLE_SUFFIX}")

# Rebuilds the extractor on every build (a no-op when its sources are unchanged).
add_custom_target(llamatron_host_tools
    COMMAND ${_host_env} ${CMAKE_COMMAND} --build "${_host_dir}" --config Release
    BYPRODUCTS "${LLAMATRON_EXTRACT}"
    COMMENT "Building host extractor"
    VERBATIM)

# --- configure-time verification -------------------------------------------------------------
set(_verify_args --original "${LLAMATRON_ORIGINAL_DIR}" --manifest "${LLAMATRON_MANIFEST}")
if(LLAMATRON_ALLOW_UNVERIFIED)
    list(APPEND _verify_args --allow-unverified)
endif()
execute_process(COMMAND "${LLAMATRON_EXTRACT}" --verify ${_verify_args}
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
        "Original files in ${LLAMATRON_ORIGINAL_DIR} do not match the supported release:\n"
        "${_out}${_err}\n"
        "Only the release described in tools/manifest/original-files.json is supported. For RE work "
        "only, -DLLAMATRON_ALLOW_UNVERIFIED=ON skips this check.")
elseif(LLAMATRON_ALLOW_UNVERIFIED AND _err)
    message(WARNING "${_out}${_err}")
else()
    message(STATUS "Original files verified against the manifest (${LLAMATRON_ORIGINAL_DIR})")
endif()

# --- build-time extraction -------------------------------------------------------------------
set(LLAMATRON_GENERATED_OUTPUTS "")
foreach(_exe game86 fs llama install)
    list(APPEND LLAMATRON_GENERATED_OUTPUTS
        "${LLAMATRON_GENERATED_DIR}/exe/${_exe}.exe"
        "${LLAMATRON_GENERATED_DIR}/exe/${_exe}.unpacked"
        "${LLAMATRON_GENERATED_DIR}/exe/${_exe}.layout.json")
endforeach()

set(LLAMATRON_GENERATED_DATA "")
foreach(_data title_vga.idx8 title_vga.pal title_ega.idx4 title_ega.pal font.bin sine.bin
        sprites.bin sprite_refs.bin g3.bin samples.bin samples.idx
        palettes.dat default_scores.dat user_scores.dat config.dat strings.dat levels.dat)
    list(APPEND LLAMATRON_GENERATED_DATA "${LLAMATRON_GENERATED_DIR}/data/${_data}")
endforeach()
set(LLAMATRON_ASSETS_BLOB_C "${LLAMATRON_GENERATED_DIR}/assets_blob.c")
set(LLAMATRON_ASSET_INDEX_H "${LLAMATRON_GENERATED_DIR}/asset_index.h")
list(APPEND LLAMATRON_GENERATED_OUTPUTS ${LLAMATRON_GENERATED_DATA} "${LLAMATRON_ASSETS_BLOB_C}"
    "${LLAMATRON_ASSET_INDEX_H}")

file(GLOB _extract_sources CONFIGURE_DEPENDS "${_extract_src}/*.c" "${_extract_src}/*.h")
add_custom_command(
    OUTPUT "${LLAMATRON_GENERATED_DIR}/extract.stamp" ${LLAMATRON_GENERATED_OUTPUTS}
    COMMAND "${LLAMATRON_EXTRACT}" --all ${_verify_args} --out "${LLAMATRON_GENERATED_DIR}"
    DEPENDS "${LLAMATRON_EXTRACT}" ${_extract_sources} "${LLAMATRON_MANIFEST}" ${LLAMATRON_ORIGINAL_FILES}
    COMMENT "Extracting game data from ${LLAMATRON_ORIGINAL_DIR}"
    VERBATIM)
add_custom_target(llamatron_generated ALL DEPENDS "${LLAMATRON_GENERATED_DIR}/extract.stamp")
add_dependencies(llamatron_generated llamatron_host_tools)
