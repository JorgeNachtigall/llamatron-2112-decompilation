/* MS-DOS MZ executable header: parsing, rebuilding after unpacking, and producing a flat
 * relocated load image. */
#ifndef EXTRACT_MZ_H
#define EXTRACT_MZ_H

#include "common.h"

#define MZ_HEADER_SIZE 0x1C

typedef struct {
    uint16_t last_page_bytes; /* bytes used in the last 512-byte page (0 = full) */
    uint16_t page_count;
    uint16_t reloc_count;
    uint16_t header_paras;
    uint16_t min_alloc_paras;
    uint16_t max_alloc_paras;
    uint16_t ss, sp;
    uint16_t checksum;
    uint16_t ip, cs;
    uint16_t reloc_table_offset;
    uint16_t overlay;
} MzHeader;

/* A far-pointer fixup: the word at segment:offset gets the load segment added to it. */
typedef struct {
    uint16_t offset;
    uint16_t segment;
} MzReloc;

typedef struct {
    MzReloc *items;
    size_t count;
    size_t capacity;
} MzRelocList;

bool mz_reloc_push(MzRelocList *list, uint16_t offset, uint16_t segment);
void mz_reloc_free(MzRelocList *list);

bool mz_parse_header(const uint8_t *data, size_t size, MzHeader *out);

/* What an unpacker recovered: entry registers, memory needs, fixups and the raw image. */
typedef struct {
    uint16_t ss, sp, ip, cs;
    uint16_t min_alloc_paras, max_alloc_paras;
    MzRelocList relocs;
    ByteBuf image;
} MzProgram;

void mz_program_free(MzProgram *program);

/* Writes a fresh MZ file: 0x1C-byte header, relocation table right after it, header padded
 * to a 512-byte multiple, then the image. Checksum and overlay number are zero. */
bool mz_write(const MzProgram *program, ByteBuf *out);

/* The load module of an MZ file (everything after the header) with every relocation fixed up
 * for `load_segment`. Also returns the header so callers can report entry points. */
bool mz_load_image(const uint8_t *data, size_t size, uint16_t load_segment, ByteBuf *image,
                   MzHeader *header);

#endif
