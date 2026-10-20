# bundle_dylibs.cmake - copy the non-system dylibs an app's executable needs into
# Contents/Frameworks, so the app runs without Homebrew (cmake -P, from packaging.cmake).
#
#   cmake -DAPP=<path>/Llamatron.app -DEXE=Llamatron [-DCODESIGN=<codesign>] -P bundle_dylibs.cmake
#
# Every load command naming a library outside /usr/lib and /System is pointed at
# @executable_path/../Frameworks/<name>, and the copy gets that install name; copies are scanned
# the same way. LC_RPATH entries of a copy that point outside the app are removed.
#
# Homebrew's SDL2 is sdl2-compat, which does not link SDL3 but loads it at run time, trying
# @loader_path/libSDL3.dylib first: when a copy names that path, libSDL3.dylib is taken from
# the copy's own rpath directories and put beside it.
#
# The tools are Xcode's otool and install_name_tool. install_name_tool would break the files'
# signatures (and warn so): with CODESIGN each file is unsigned before it is changed and each
# copy is signed ad hoc again afterwards (the app itself is the caller's).

if(NOT APP OR NOT EXE)
    message(FATAL_ERROR "bundle_dylibs.cmake: APP and EXE are required")
endif()
find_program(OTOOL otool REQUIRED)
find_program(INSTALL_NAME_TOOL install_name_tool REQUIRED)

set(frameworks ${APP}/Contents/Frameworks)
file(MAKE_DIRECTORY ${frameworks})

macro(unsign file)
    if(CODESIGN)
        execute_process(COMMAND ${CODESIGN} --remove-signature ${file} COMMAND_ERROR_IS_FATAL ANY)
    endif()
endmacro()

function(is_system path out)
    if(path MATCHES "^(/usr/lib/|/System/)" OR path MATCHES "^@(executable|loader)_path/")
        set(${out} TRUE PARENT_SCOPE)
    else()
        set(${out} FALSE PARENT_SCOPE)
    endif()
endfunction()

# The libraries a Mach-O file loads (otool -L, minus its own install name).
function(load_commands file out)
    execute_process(COMMAND ${OTOOL} -L ${file} OUTPUT_VARIABLE text COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND ${OTOOL} -D ${file} OUTPUT_VARIABLE id_text COMMAND_ERROR_IS_FATAL ANY)
    string(REGEX REPLACE "^[^\n]*\n" "" id_text "${id_text}")
    string(STRIP "${id_text}" id)
    string(REGEX MATCHALL "\n\t[^\n]+ \\(compatibility" lines "${text}")
    set(libs "")
    foreach(line IN LISTS lines)
        string(REGEX REPLACE "^\n\t(.+) \\(compatibility$" "\\1" lib "${line}")
        if(NOT lib STREQUAL id)
            list(APPEND libs ${lib})
        endif()
    endforeach()
    set(${out} ${libs} PARENT_SCOPE)
endfunction()

# The LC_RPATH entries of a Mach-O file.
function(rpaths file out)
    execute_process(COMMAND ${OTOOL} -l ${file} OUTPUT_VARIABLE text COMMAND_ERROR_IS_FATAL ANY)
    string(REGEX MATCHALL "cmd LC_RPATH\n[^\n]*\n +path [^\n]+ \\(offset" entries "${text}")
    set(paths "")
    foreach(entry IN LISTS entries)
        string(REGEX REPLACE ".* +path (.+) \\(offset$" "\\1" path "${entry}")
        list(APPEND paths ${path})
    endforeach()
    set(${out} ${paths} PARENT_SCOPE)
endfunction()

# Copy `source` into Frameworks as `name` (once) and queue it for scanning.
macro(take source name)
    if(NOT EXISTS ${frameworks}/${name})
        file(REAL_PATH ${source} real)
        file(COPY_FILE ${real} ${frameworks}/${name})
        file(CHMOD ${frameworks}/${name} PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
        unsign(${frameworks}/${name})
        execute_process(COMMAND ${INSTALL_NAME_TOOL} -id @executable_path/../Frameworks/${name}
                        ${frameworks}/${name} COMMAND_ERROR_IS_FATAL ANY)
        list(APPEND queue "${frameworks}/${name}|${real}")
        list(APPEND copies ${frameworks}/${name})
        message(STATUS "Llamatron.app: bundled ${name} (${real})")
    endif()
endmacro()

set(copies "")
unsign(${APP}/Contents/MacOS/${EXE})
set(queue "${APP}/Contents/MacOS/${EXE}|")
while(queue)
    list(POP_FRONT queue item)
    string(REPLACE "|" ";" item "${item}")
    list(GET item 0 file)
    list(LENGTH item n)
    set(original "")
    if(n GREATER 1)
        list(GET item 1 original)
    endif()

    load_commands(${file} libs)
    foreach(lib IN LISTS libs)
        is_system(${lib} system)
        if(system)
            continue()
        endif()
        get_filename_component(name ${lib} NAME)
        take(${lib} ${name})
        execute_process(COMMAND ${INSTALL_NAME_TOOL} -change ${lib}
                        @executable_path/../Frameworks/${name} ${file} COMMAND_ERROR_IS_FATAL ANY)
    endforeach()

    # Rpaths that lead out of the app go (the executable's build rpath, e.g. /opt/homebrew/lib);
    # a copy's are kept as places to look for what it loads at run time.
    rpaths(${file} paths)
    set(search "")
    foreach(path IN LISTS paths)
        if(original)
            get_filename_component(original_dir ${original} DIRECTORY)
            string(REPLACE "@loader_path" "${original_dir}" dir "${path}")
            list(APPEND search ${dir})
        elseif(path MATCHES "^@executable_path(/|$)")
            continue()
        endif()
        execute_process(COMMAND ${INSTALL_NAME_TOOL} -delete_rpath ${path} ${file}
                        COMMAND_ERROR_IS_FATAL ANY)
    endforeach()
    if(original)
        # sdl2-compat: SDL3, loaded at run time from beside the library.
        file(STRINGS ${file} wants_sdl3 REGEX "^@loader_path/libSDL3\\.dylib$" LIMIT_COUNT 1)
        if(wants_sdl3)
            find_file(sdl3 libSDL3.dylib PATHS ${search} NO_DEFAULT_PATH NO_CACHE)
            if(NOT sdl3)
                message(FATAL_ERROR "${file} loads SDL3 at run time, but libSDL3.dylib is not in "
                    "its rpaths (${search})")
            endif()
            take(${sdl3} libSDL3.dylib)
        endif()
    endif()
endwhile()

if(CODESIGN)
    foreach(copy IN LISTS copies)
        execute_process(COMMAND ${CODESIGN} --force --sign - ${copy} COMMAND_ERROR_IS_FATAL ANY)
    endforeach()
endif()
