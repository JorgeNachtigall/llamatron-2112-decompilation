/* Title pictures as fs.exe shows them (docs/re-notes-fs-install.md §1.3, §3):
 *
 *   data/title_vga.idx8  320x400, 1 byte/pixel: rows 0-199 newtit.lbm, rows 200-399 newtit2.lbm
 *   data/title_vga.pal   256 x 3 bytes, 6-bit DAC levels: newtit.lbm's CMAP >> 2 (newtit2's
 *                        own CMAP is ignored, as in fs.exe)
 *   data/title_ega.idx4  640x350, 1 byte/pixel, colour indices 0..15
 *   data/title_ega.pal   17 EGA palette register values (rgbRGB), entry 16 = overscan = 0
 */
#include "extract.h"
#include "ilbm.h"

#include <stdio.h>
#include <string.h>

enum {
    VGA_TITLE_WIDTH = 320,
    VGA_TITLE_PART_HEIGHT = 200,
    EGA_TITLE_WIDTH = 640,
    EGA_TITLE_HEIGHT = 350,
    EGA_PALETTE_REGISTERS = 17,
};

static bool load_picture(const char *original_dir, const char *name, uint16_t width,
                         uint16_t height, IlbmImage *image)
{
    char path[4096];
    ByteBuf file = {0};
    if (!find_original(original_dir, name, path, sizeof path))
        return fail("%s not found", name);
    bool ok = read_file(path, &file) && ilbm_decode(file.data, file.size, image);
    bytebuf_free(&file);
    if (ok && (image->width != width || image->height != height))
        ok = fail("%s is %ux%u, expected %ux%u", name, image->width, image->height, width, height);
    if (!ok) {
        ilbm_free(image);
        return fail("%s: %s", name, last_error());
    }
    return true;
}

/* fs 1000:01c9 VGA path, 1000:043a CMAP >> 2 into the DAC */
static bool extract_vga_title(const char *original_dir, OutputSet *outputs)
{
    IlbmImage top, bottom;
    if (!load_picture(original_dir, "newtit.lbm", VGA_TITLE_WIDTH, VGA_TITLE_PART_HEIGHT, &top))
        return false;
    if (!load_picture(original_dir, "newtit2.lbm", VGA_TITLE_WIDTH, VGA_TITLE_PART_HEIGHT, &bottom)) {
        ilbm_free(&top);
        return false;
    }

    size_t part = (size_t)VGA_TITLE_WIDTH * VGA_TITLE_PART_HEIGHT;
    ByteBuf pixels = {0};
    bool ok = bytebuf_append(&pixels, top.pixels, part) && bytebuf_append(&pixels, bottom.pixels, part);

    uint8_t dac[ILBM_MAX_COLORS][3]; /* fs.exe always programs all 256 DAC entries */
    for (int i = 0; i < ILBM_MAX_COLORS; i++)
        for (int c = 0; c < 3; c++)
            dac[i][c] = (uint8_t)(top.cmap[i][c] >> 2);

    ok = ok && output_write(outputs, "data/title_vga.idx8", pixels.data, pixels.size) &&
         output_write(outputs, "data/title_vga.pal", &dac[0][0], sizeof dac);
    bytebuf_free(&pixels);
    ilbm_free(&top);
    ilbm_free(&bottom);
    return ok;
}

/* EGA register value for an 8-bit RGB colour: the top two bits of each component select
 * off / secondary (1/3) / primary (2/3) / both. fs 1000:043a with tables DS:0122/0126/012a */
static uint8_t ega_register_for(const uint8_t rgb[3])
{
    static const uint8_t primary[3] = {0x04, 0x02, 0x01};   /* R G B */
    static const uint8_t secondary[3] = {0x20, 0x10, 0x08}; /* r g b */
    uint8_t value = 0;
    for (int c = 0; c < 3; c++) {
        int level = rgb[c] >> 6;
        if (level & 1)
            value |= secondary[c];
        if (level & 2)
            value |= primary[c];
    }
    return value;
}

/* fs 1000:0216 EGA path */
static bool extract_ega_title(const char *original_dir, OutputSet *outputs)
{
    IlbmImage image;
    if (!load_picture(original_dir, "tronega.lbm", EGA_TITLE_WIDTH, EGA_TITLE_HEIGHT, &image))
        return false;

    uint8_t registers[EGA_PALETTE_REGISTERS] = {0};
    int colors = image.color_count < 16 ? image.color_count : 16;
    for (int i = 0; i < colors; i++)
        registers[i] = ega_register_for(image.cmap[i]);

    bool ok = true;
    size_t count = (size_t)EGA_TITLE_WIDTH * EGA_TITLE_HEIGHT;
    for (size_t i = 0; i < count; i++)
        if (image.pixels[i] > 15)
            ok = fail("tronega.lbm has a colour index above 15");
    ok = ok && output_write(outputs, "data/title_ega.idx4", image.pixels, count) &&
         output_write(outputs, "data/title_ega.pal", registers, sizeof registers);
    ilbm_free(&image);
    return ok;
}

bool extract_title_pictures(const char *original_dir, OutputSet *outputs)
{
    return extract_vga_title(original_dir, outputs) && extract_ega_title(original_dir, outputs);
}
