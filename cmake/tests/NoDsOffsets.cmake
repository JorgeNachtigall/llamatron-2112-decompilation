# Fails if game code looks things up by the original's DS offset instead of a named ID
# (PLAN §0.1: a real reimplementation, no raw memory offsets in game code).
#
#   cmake -DSOURCE_DIR=<dir> [-DROOT=<dir paths are shown relative to>] -P NoDsOffsets.cmake
#
# Flagged, outside comments, in every .c / .h under SOURCE_DIR:
#   - a *_at( call whose first argument is a hex number of 3-4 digits (a DS or bank offset:
#     foo_at(0x2175); coordinates such as draw_char_at(8, y) pass);
#   - any call of the assets' DS-offset accessors (asset_palette_at, asset_flash_list_at,
#     asset_text_at, asset_text_raw, asset_dos_message_at, asset_sprite_index_at). Those are
#     for tests/adapters that map original memory; game code uses asset_palette(PaletteId),
#     asset_text(TextId), asset_sprite_id_gfx(SpriteId), asset_title_shape(TitleShapeId), ...
#
# ALLOWED lists the known leftovers as "<path>|<call>", each with its owner and the planned
# replacement. Remove an entry once its owner has switched; a stale entry is only reported.

set(ALLOWED
    # W4 Keel: hiscore_art markers -> asset_sprite_id_gfx(SPRITE_ID_HISCORE_MARK_1 / _2)
    "src/game/main_sm.c|game_sprite_at(0x245A)"
    "src/game/main_sm.c|game_sprite_at(0x4268)"
    "src/game/main_sm.c|asset_sprite_index_at("
    # W5a/W6: Entity.sprite_ref holds the frame's 218C bank offset (enemy_init 00 copies it from
    # the ref table); storing the frame or ref index instead removes this lookup
    "src/game/entities/anim.c|asset_sprite_index_at("
)

if(NOT SOURCE_DIR)
    message(FATAL_ERROR "NoDsOffsets.cmake: pass -DSOURCE_DIR=<dir>")
endif()
if(NOT ROOT)
    get_filename_component(ROOT "${SOURCE_DIR}/../.." ABSOLUTE)
endif()

set(_hex "[0-9A-Fa-f]")
set(_offset_call "[A-Za-z0-9_]*_at\\([ \t\n]*0[xX]${_hex}${_hex}${_hex}${_hex}?[uUlL]*[ \t\n]*[,)]")
set(_accessor_call "asset_(palette_at|flash_list_at|text_at|text_raw|dos_message_at|sprite_index_at)\\(")

file(GLOB_RECURSE _files "${SOURCE_DIR}/*.c" "${SOURCE_DIR}/*.h")
list(SORT _files)
set(_found "")
set(_used "")
foreach(_file IN LISTS _files)
    file(RELATIVE_PATH _rel "${ROOT}" "${_file}")
    file(READ "${_file}" _code)
    # Comments out (block, then line), strings and character literals emptied.
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" " " _code "${_code}")
    string(REGEX REPLACE "//[^\n]*" "" _code "${_code}")
    string(REGEX REPLACE "\"([^\"\\\\\n]|\\\\.)*\"" "\"\"" _code "${_code}")
    string(REGEX REPLACE "'([^'\\\\\n]|\\\\.)*'" "' '" _code "${_code}")

    string(REGEX MATCHALL "(^|[^A-Za-z0-9_])(${_offset_call}|${_accessor_call})" _matches "${_code}")
    foreach(_match IN LISTS _matches)
        string(REGEX REPLACE "^[^A-Za-z_]" "" _match "${_match}")
        string(REGEX REPLACE "[ \t\n]*[,)]$" ")" _call "${_match}")
        string(REGEX REPLACE "\\([ \t\n]*" "(" _call "${_call}")
        if(_call MATCHES "^${_accessor_call}$")
            string(REGEX REPLACE "\\(.*" "(" _call "${_call}")
        endif()
        set(_entry "${_rel}|${_call}")
        list(FIND ALLOWED "${_entry}" _index)
        if(_index LESS 0)
            list(APPEND _found "${_entry}")
        else()
            list(APPEND _used "${_entry}")
        endif()
    endforeach()
endforeach()

foreach(_entry IN LISTS ALLOWED)
    list(FIND _used "${_entry}" _index)
    string(FIND "${_entry}" "|" _bar)
    string(SUBSTRING "${_entry}" 0 ${_bar} _path)
    if(_index LESS 0 AND EXISTS "${ROOT}/${_path}")
        message(STATUS "allow-list entry no longer needed (remove it): ${_entry}")
    endif()
endforeach()

list(REMOVE_DUPLICATES _found)
if(_found)
    foreach(_entry IN LISTS _found)
        string(REPLACE "|" ": " _shown "${_entry}")
        message("DS offset in game code: ${_shown}")
    endforeach()
    message(FATAL_ERROR "Use the named ID (formats.h SpriteId, TitleShapeId, PaletteId, FlashListId, "
        "TextId, MessageId, SampleId) and its accessor in game_data.h instead of the original's offset.")
endif()
list(LENGTH _files _count)
list(REMOVE_DUPLICATES _used)
list(LENGTH _used _allowed)
message(STATUS "no new DS-offset lookups in ${_count} files (${SOURCE_DIR}); ${_allowed} allow-listed")
