#include "mz.h"

#include <stdlib.h>
#include <string.h>

bool mz_reloc_push(MzRelocList *list, uint16_t offset, uint16_t segment)
{
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2 : 256;
        MzReloc *items = realloc(list->items, capacity * sizeof *items);
        if (!items)
            return fail("out of memory (relocation table)");
        list->items = items;
        list->capacity = capacity;
    }
    list->items[list->count++] = (MzReloc){offset, segment};
    return true;
}

void mz_reloc_free(MzRelocList *list)
{
    free(list->items);
    *list = (MzRelocList){0};
}

void mz_program_free(MzProgram *program)
{
    mz_reloc_free(&program->relocs);
    bytebuf_free(&program->image);
}

bool mz_parse_header(const uint8_t *data, size_t size, MzHeader *out)
{
    if (size < MZ_HEADER_SIZE || data[0] != 'M' || data[1] != 'Z')
        return fail("not an MZ executable");
    uint16_t words[13];
    for (int i = 0; i < 13; i++)
        words[i] = read_u16le(data + 2 + i * 2);
    *out = (MzHeader){
        .last_page_bytes = words[0],
        .page_count = words[1],
        .reloc_count = words[2],
        .header_paras = words[3],
        .min_alloc_paras = words[4],
        .max_alloc_paras = words[5],
        .ss = words[6],
        .sp = words[7],
        .checksum = words[8],
        .ip = words[9],
        .cs = words[10],
        .reloc_table_offset = words[11],
        .overlay = words[12],
    };
    return true;
}

bool mz_write(const MzProgram *program, ByteBuf *out)
{
    size_t table_size = program->relocs.count * 4;
    size_t header_size = (MZ_HEADER_SIZE + table_size + 0x1FF) & ~(size_t)0x1FF;
    size_t total = header_size + program->image.size;
    size_t pages = (total + 511) / 512;
    if (program->relocs.count > 0xFFFF || header_size / 16 > 0xFFFF || pages > 0xFFFF)
        return fail("unpacked program too large for an MZ header");

    out->size = 0;
    if (!bytebuf_reserve(out, total))
        return false;
    memset(out->data, 0, header_size);
    out->size = header_size;

    uint8_t *h = out->data;
    h[0] = 'M';
    h[1] = 'Z';
    write_u16le(h + 0x02, (uint16_t)(total % 512));
    write_u16le(h + 0x04, (uint16_t)pages);
    write_u16le(h + 0x06, (uint16_t)program->relocs.count);
    write_u16le(h + 0x08, (uint16_t)(header_size / 16));
    write_u16le(h + 0x0A, program->min_alloc_paras);
    write_u16le(h + 0x0C, program->max_alloc_paras);
    write_u16le(h + 0x0E, program->ss);
    write_u16le(h + 0x10, program->sp);
    write_u16le(h + 0x14, program->ip);
    write_u16le(h + 0x16, program->cs);
    write_u16le(h + 0x18, MZ_HEADER_SIZE);
    for (size_t i = 0; i < program->relocs.count; i++) {
        write_u16le(h + MZ_HEADER_SIZE + i * 4, program->relocs.items[i].offset);
        write_u16le(h + MZ_HEADER_SIZE + i * 4 + 2, program->relocs.items[i].segment);
    }
    return bytebuf_append(out, program->image.data, program->image.size);
}

bool mz_load_image(const uint8_t *data, size_t size, uint16_t load_segment, ByteBuf *image,
                   MzHeader *header)
{
    if (!mz_parse_header(data, size, header))
        return false;
    size_t header_size = (size_t)header->header_paras * 16;
    size_t file_size = (size_t)header->page_count * 512;
    if (header->last_page_bytes)
        file_size -= 512 - header->last_page_bytes;
    if (file_size > size || header_size > file_size)
        return fail("MZ header describes %zu bytes but the file has %zu", file_size, size);
    size_t table_end = (size_t)header->reloc_table_offset + (size_t)header->reloc_count * 4;
    if (table_end > header_size)
        return fail("MZ relocation table extends past the header");

    image->size = 0;
    if (!bytebuf_append(image, data + header_size, file_size - header_size))
        return false;
    for (uint16_t i = 0; i < header->reloc_count; i++) {
        const uint8_t *entry = data + header->reloc_table_offset + i * 4;
        size_t at = (size_t)read_u16le(entry + 2) * 16 + read_u16le(entry);
        if (at + 2 > image->size)
            return fail("relocation %u points outside the image (0x%zx)", i, at);
        write_u16le(image->data + at, (uint16_t)(read_u16le(image->data + at) + load_segment));
    }
    return true;
}
