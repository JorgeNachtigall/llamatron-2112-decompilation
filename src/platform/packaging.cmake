# Personal-use packaging (included from src/platform/CMakeLists.txt).
#
# PERSONAL USE ONLY: both outputs contain assets extracted from the owner's original game
# files. They must never be distributed, uploaded or published (docs/PLAN.md §0, goal 3).
#
#   native macOS, -DLLAMATRON_MACOS_BUNDLE=ON: target llamatron_macos_app builds
#     <build>/Llamatron.app (assets embedded, generic placeholder icon, ad-hoc signed only:
#     no Apple account, no notarisation).
#   web: target web-dist gathers the page into <build-web>/dist/ (index.html, js, wasm, data).
#
# LLAMATRON_PACKAGE_TARGET picks the executable target to package. The calls run deferred (end
# of the top-level configure) so the target may be defined in any directory.

if(EXISTS ${PROJECT_SOURCE_DIR}/src/game/CMakeLists.txt)
    set(_llama_package_default llamatron)
else()
    set(_llama_package_default llamatron_title)
endif()
set(LLAMATRON_PACKAGE_TARGET ${_llama_package_default} CACHE STRING
    "Executable target packaged by llamatron_macos_app / web-dist (personal use only)")
option(LLAMATRON_MACOS_BUNDLE "Build Llamatron.app (macOS, personal use only, never distribute)" OFF)

# Cache (internal) so the deferred calls, which run in the top-level directory, see them.
set(LLAMA_PACKAGING_DIR ${CMAKE_CURRENT_LIST_DIR} CACHE INTERNAL "")
set(LLAMA_PERSONAL_USE_NOTE
    "PERSONAL USE ONLY. This build contains assets extracted from the owner's original Llamatron 2112 files. Do not distribute, upload or publish it." CACHE INTERNAL "")

function(llama_add_macos_bundle)
    set(target ${LLAMATRON_PACKAGE_TARGET})
    if(NOT TARGET ${target})
        message(FATAL_ERROR "LLAMATRON_MACOS_BUNDLE: no target '${target}' (LLAMATRON_PACKAGE_TARGET)")
    endif()
    get_target_property(libs ${target} LINK_LIBRARIES)
    if(NOT "llama_assets_embedded" IN_LIST libs)
        message(FATAL_ERROR "LLAMATRON_MACOS_BUNDLE: '${target}' must link llama_assets_embedded "
            "(LLAMATRON_EMBED_ASSETS=ON and the generated assets blob)")
    endif()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    find_program(LLAMA_ICONUTIL iconutil REQUIRED)
    find_program(LLAMA_CODESIGN codesign REQUIRED)

    set(app ${CMAKE_BINARY_DIR}/Llamatron.app)
    set(work ${CMAKE_BINARY_DIR}/llamatron_app.dir)
    configure_file(${LLAMA_PACKAGING_DIR}/macos/Info.plist.in ${work}/Info.plist @ONLY)
    file(WRITE ${work}/PERSONAL-USE-ONLY.txt "${LLAMA_PERSONAL_USE_NOTE}\n")

    add_custom_command(OUTPUT ${work}/Llamatron.icns
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${work}/Llamatron.iconset
        COMMAND ${Python3_EXECUTABLE} ${LLAMA_PACKAGING_DIR}/tools/make_icon.py ${work}/Llamatron.iconset
        COMMAND ${LLAMA_ICONUTIL} -c icns ${work}/Llamatron.iconset -o ${work}/Llamatron.icns
        DEPENDS ${LLAMA_PACKAGING_DIR}/tools/make_icon.py
        COMMENT "Placeholder icon for Llamatron.app")
    add_custom_command(OUTPUT ${work}/bundle.stamp
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${app}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${app}/Contents/MacOS ${app}/Contents/Resources
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${target}> ${app}/Contents/MacOS/Llamatron
        COMMAND ${CMAKE_COMMAND} -E copy ${work}/Info.plist ${app}/Contents/Info.plist
        COMMAND ${CMAKE_COMMAND} -E copy ${work}/Llamatron.icns ${app}/Contents/Resources/Llamatron.icns
        COMMAND ${CMAKE_COMMAND} -E copy ${work}/PERSONAL-USE-ONLY.txt ${app}/Contents/Resources/PERSONAL-USE-ONLY.txt
        COMMAND ${LLAMA_CODESIGN} --force --sign - ${app}
        COMMAND ${CMAKE_COMMAND} -E touch ${work}/bundle.stamp
        DEPENDS ${target} ${work}/Info.plist ${work}/Llamatron.icns ${work}/PERSONAL-USE-ONLY.txt
        COMMENT "Llamatron.app from ${target} (personal use only, ad-hoc signed)")
    add_custom_target(llamatron_macos_app ALL DEPENDS ${work}/bundle.stamp)
    if(target STREQUAL "llamatron" OR target STREQUAL "llamatron_title")
        # The bundled binary runs from anywhere: no data directory, assets embedded.
        add_test(NAME macos_app_title COMMAND ${app}/Contents/MacOS/Llamatron --frames 2 --vga --mute
            WORKING_DIRECTORY /)
        set_tests_properties(macos_app_title PROPERTIES
            ENVIRONMENT "LLAMATRON_PREF_DIR=${work}/test-prefs"
            PASS_REGULAR_EXPRESSION "title 320x400 hash=c7ab74c4")
    endif()
endfunction()

function(llama_add_web_dist)
    set(target ${LLAMATRON_PACKAGE_TARGET})
    if(NOT TARGET ${target})
        message(WARNING "web-dist: no target '${target}' (LLAMATRON_PACKAGE_TARGET), not added")
        return()
    endif()
    add_custom_target(web-dist
        COMMAND ${CMAKE_COMMAND} -DSRC_DIR=$<TARGET_FILE_DIR:${target}>
            -DBASE=$<TARGET_FILE_BASE_NAME:${target}> -DDEST=${CMAKE_BINARY_DIR}/dist
            "-DNOTE=${LLAMA_PERSONAL_USE_NOTE}" -P ${LLAMA_PACKAGING_DIR}/web_dist.cmake
        DEPENDS ${target}
        COMMENT "Web build of ${target} into ${CMAKE_BINARY_DIR}/dist (personal use only)"
        VERBATIM)
endfunction()

if(EMSCRIPTEN)
    cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL llama_add_web_dist)
elseif(LLAMATRON_MACOS_BUNDLE)
    if(NOT APPLE)
        message(FATAL_ERROR "LLAMATRON_MACOS_BUNDLE is for macOS builds only")
    endif()
    cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL llama_add_macos_bundle)
endif()
