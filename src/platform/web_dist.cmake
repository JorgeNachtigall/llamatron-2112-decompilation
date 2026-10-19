# web_dist.cmake (cmake -P, from the web-dist target in packaging.cmake): copy one Emscripten
# page into DEST as index.html plus its .js/.wasm/.data, and add the personal-use note.
# Inputs: SRC_DIR, BASE (file name without .html), DEST, NOTE.
foreach(var SRC_DIR BASE DEST NOTE)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "web_dist.cmake: ${var} not set")
    endif()
endforeach()

file(REMOVE_RECURSE ${DEST})
file(MAKE_DIRECTORY ${DEST})
file(COPY_FILE ${SRC_DIR}/${BASE}.html ${DEST}/index.html)
foreach(ext js wasm data)
    if(EXISTS ${SRC_DIR}/${BASE}.${ext})
        file(COPY_FILE ${SRC_DIR}/${BASE}.${ext} ${DEST}/${BASE}.${ext})
    elseif(NOT ext STREQUAL "data")
        message(FATAL_ERROR "web_dist.cmake: ${SRC_DIR}/${BASE}.${ext} missing")
    endif()
endforeach()
file(WRITE ${DEST}/PERSONAL-USE-ONLY.txt "${NOTE}\n")
message(STATUS "web-dist: ${DEST} (personal use only, do not distribute)")
