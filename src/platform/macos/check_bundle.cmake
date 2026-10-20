# check_bundle.cmake - ctest macos_app_self_contained: Llamatron.app needs nothing outside
# itself and the system (cmake -DAPP=<path>/Llamatron.app -P check_bundle.cmake).
#
# Every Mach-O file in Contents/MacOS and Contents/Frameworks may load only /usr/lib, /System
# and @executable_path/../Frameworks/<name> with <name> present; no LC_RPATH may point outside
# the app; and the bundle's ad-hoc signature verifies (codesign --verify --deep --strict).

find_program(OTOOL otool REQUIRED)
find_program(CODESIGN codesign REQUIRED)

file(GLOB files ${APP}/Contents/MacOS/* ${APP}/Contents/Frameworks/*)
set(problems "")
foreach(file IN LISTS files)
    execute_process(COMMAND ${OTOOL} -L ${file} OUTPUT_VARIABLE text COMMAND_ERROR_IS_FATAL ANY)
    string(REGEX MATCHALL "\n\t[^\n]+ \\(compatibility" lines "${text}")
    foreach(line IN LISTS lines)
        string(REGEX REPLACE "^\n\t(.+) \\(compatibility$" "\\1" lib "${line}")
        if(lib MATCHES "^(/usr/lib/|/System/)")
            continue()
        elseif(lib MATCHES "^@executable_path/\\.\\./Frameworks/(.+)$")
            if(NOT EXISTS ${APP}/Contents/Frameworks/${CMAKE_MATCH_1})
                list(APPEND problems "${file}: ${lib} is not in the app")
            endif()
        else()
            list(APPEND problems "${file}: loads ${lib}")
        endif()
    endforeach()
    execute_process(COMMAND ${OTOOL} -l ${file} OUTPUT_VARIABLE text COMMAND_ERROR_IS_FATAL ANY)
    string(REGEX MATCHALL "cmd LC_RPATH\n[^\n]*\n +path [^\n]+ \\(offset" entries "${text}")
    foreach(entry IN LISTS entries)
        string(REGEX REPLACE ".* +path (.+) \\(offset$" "\\1" path "${entry}")
        if(NOT path MATCHES "^@(executable|loader)_path(/|$)" OR path MATCHES "\\.\\./\\.\\./\\.\\.")
            list(APPEND problems "${file}: rpath ${path}")
        endif()
    endforeach()
endforeach()
execute_process(COMMAND ${CODESIGN} --verify --deep --strict ${APP} RESULT_VARIABLE signed
                ERROR_VARIABLE sign_text)
if(NOT signed EQUAL 0)
    list(APPEND problems "codesign --verify: ${sign_text}")
endif()
if(problems)
    list(JOIN problems "\n  " text)
    message(FATAL_ERROR "Llamatron.app is not self-contained:\n  ${text}")
endif()
list(LENGTH files n)
message(STATUS "Llamatron.app self-contained: ${n} Mach-O files, signature ok")
