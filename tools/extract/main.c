/* llamatron-extract: build-time extraction of everything the port needs from the user's own
 * copy of the game (docs/PLAN.md §2.2). Also usable as a CLI for RE/debugging. */
#include "common.h"
#include "extract.h"
#include "lzss.h"
#include "manifest.h"
#include "mz.h"
#include "outputs.h"
#include "sha256.h"
#include "unpack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const packed_executables[] = {"game86", "fs", "llama", "install"};

typedef struct {
    const char *original_dir;
    const char *manifest_path;
    const char *out_dir;
    bool allow_unverified;
} Options;

static void usage(void)
{
    fputs("usage:\n"
          "  llamatron-extract --verify --original DIR --manifest FILE [--allow-unverified]\n"
          "  llamatron-extract --all --original DIR --manifest FILE --out DIR [--allow-unverified]\n"
          "  llamatron-extract --unpack IN OUT      remove all LZEXE/EXEPACK layers\n"
          "  llamatron-extract --unlzexe IN OUT     remove one LZEXE 0.91 layer\n"
          "  llamatron-extract --unexepack IN OUT   remove one EXEPACK layer\n"
          "  llamatron-extract --load-image IN OUT  flat MZ load module relocated to 1000h\n"
          "  llamatron-extract --lzss IN OUT        decode one .cmp file\n"
          "  llamatron-extract --sha256 FILE...\n",
          stderr);
}

static int report_failure(const char *context)
{
    fprintf(stderr, "llamatron-extract: %s: %s\n", context, last_error());
    return 1;
}

static bool load_and_verify(const Options *opt, Manifest *manifest)
{
    if (!opt->original_dir || !opt->manifest_path)
        return fail("--original and --manifest are required");
    if (!manifest_load(opt->manifest_path, manifest))
        return false;
    printf("Verifying %s against %s\n", opt->original_dir, manifest->release);
    if (manifest_verify(manifest, opt->original_dir)) {
        printf("All required original files match.\n");
        return true;
    }
    if (opt->allow_unverified) {
        fprintf(stderr,
                "\n*** WARNING: original files do not match the supported release. Continuing\n"
                "*** because LLAMATRON_ALLOW_UNVERIFIED is set (RE work only). Extracted data\n"
                "*** and the port's behaviour may be wrong.\n\n");
        return true;
    }
    return fail("unsupported version of the original files; the port is derived from: %s",
                manifest->release);
}

/* Unpacks one shipped executable into exe/<base>.exe, exe/<base>.unpacked and
 * exe/<base>.layout.json (see tools/extract/README.md). */
/* `keep_image` (optional) receives the relocated load module. */
static bool extract_executable(const Options *opt, OutputSet *outputs, const char *base,
                               ByteBuf *keep_image)
{
    char name[64], path[4096], relative[128];
    snprintf(name, sizeof name, "%s.exe", base);
    if (!find_original(opt->original_dir, name, path, sizeof path))
        return fail("%s not found in %s", name, opt->original_dir);

    ByteBuf packed = {0}, unpacked = {0}, image = {0};
    MzHeader header;
    int layers = 0;
    bool ok = read_file(path, &packed) && unpack_all(packed.data, packed.size, &unpacked, &layers) &&
              mz_load_image(unpacked.data, unpacked.size, LOAD_SEGMENT, &image, &header);

    if (ok) {
        snprintf(relative, sizeof relative, "exe/%s.exe", base);
        ok = output_write(outputs, relative, unpacked.data, unpacked.size);
    }
    if (ok) {
        snprintf(relative, sizeof relative, "exe/%s.unpacked", base);
        ok = output_write(outputs, relative, image.data, image.size);
    }
    if (ok) {
        char layout[1024];
        snprintf(layout, sizeof layout,
                 "{\n"
                 "  \"name\": \"%s\",\n"
                 "  \"packer_layers_removed\": %d,\n"
                 "  \"load_segment\": %u,\n"
                 "  \"image_size\": %zu,\n"
                 "  \"header_size\": %u,\n"
                 "  \"min_alloc_paras\": %u,\n"
                 "  \"max_alloc_paras\": %u,\n"
                 "  \"reloc_count\": %u,\n"
                 "  \"entry_cs\": %u, \"entry_ip\": %u,\n"
                 "  \"stack_ss\": %u, \"stack_sp\": %u,\n"
                 "  \"loaded_cs\": %u, \"loaded_ss\": %u\n"
                 "}\n",
                 name, layers, LOAD_SEGMENT, image.size, header.header_paras * 16u,
                 header.min_alloc_paras, header.max_alloc_paras, header.reloc_count, header.cs,
                 header.ip, header.ss, header.sp, (header.cs + LOAD_SEGMENT) & 0xFFFFu,
                 (header.ss + LOAD_SEGMENT) & 0xFFFFu);
        snprintf(relative, sizeof relative, "exe/%s.layout.json", base);
        ok = output_write_text(outputs, relative, layout);
    }
    if (ok)
        printf("  %-12s %zu -> %zu bytes (%d layers, %u relocations)\n", name, packed.size,
               unpacked.size, layers, header.reloc_count);
    if (ok && keep_image) {
        bytebuf_free(keep_image);
        *keep_image = image;
        image = (ByteBuf){0};
    }
    bytebuf_free(&packed);
    bytebuf_free(&unpacked);
    bytebuf_free(&image);
    return ok;
}

const uint8_t *game86_at(const ExtractContext *ctx, uint16_t seg, uint16_t off, size_t size)
{
    if (seg < LOAD_SEGMENT) {
        fail("address %04x:%04x below the load segment", seg, off);
        return NULL;
    }
    size_t linear = (size_t)(seg - LOAD_SEGMENT) * 16 + off;
    if (linear + size > ctx->game86_image.size) {
        fail("game86 %04x:%04x+%zu is outside the image", seg, off, size);
        return NULL;
    }
    return ctx->game86_image.data + linear;
}

static int run_all(const Options *opt)
{
    Manifest manifest;
    if (!opt->out_dir) {
        fail("--out is required");
        return report_failure("--all");
    }
    if (!load_and_verify(opt, &manifest))
        return report_failure("verify");

    char exe_dir[4096];
    if (!path_join(exe_dir, sizeof exe_dir, opt->out_dir, "exe") || !make_dirs(exe_dir))
        return report_failure(opt->out_dir);

    OutputSet *outputs = calloc(1, sizeof *outputs);
    if (!outputs)
        return report_failure("out of memory");
    outputs->root = opt->out_dir;
    printf("Unpacking executables into %s\n", exe_dir);
    ExtractContext ctx = {.original_dir = opt->original_dir, .outputs = outputs};
    int status = 0;
    for (size_t i = 0; status == 0 && i < sizeof packed_executables / sizeof *packed_executables; i++) {
        bool is_game = strcmp(packed_executables[i], "game86") == 0;
        if (!extract_executable(opt, outputs, packed_executables[i], is_game ? &ctx.game86_image : NULL))
            status = report_failure(packed_executables[i]);
    }

    static const struct {
        const char *label;
        bool (*run)(ExtractContext *ctx);
    } steps[] = {
        {"title pictures", extract_title_pictures},
        {"font and sine tables", extract_cmp_data},
        {"sprites", extract_sprites},
    };
    for (size_t i = 0; status == 0 && i < sizeof steps / sizeof *steps; i++) {
        printf("Extracting %s\n", steps[i].label);
        if (!steps[i].run(&ctx))
            status = report_failure(steps[i].label);
    }
    if (status == 0 && !output_write_stamp(outputs))
        status = report_failure("extract.stamp");
    bytebuf_free(&ctx.game86_image);
    free(outputs);
    return status;
}

/* Single-file conversions for debugging: IN -> OUT through `convert`. */
typedef bool (*ConvertFn)(const uint8_t *data, size_t size, ByteBuf *out);

static bool convert_lzexe(const uint8_t *data, size_t size, ByteBuf *out)
{
    MzProgram program = {0};
    bool ok = lzexe_unpack(data, size, &program) && mz_write(&program, out);
    mz_program_free(&program);
    return ok;
}

static bool convert_exepack(const uint8_t *data, size_t size, ByteBuf *out)
{
    MzProgram program = {0};
    bool ok = exepack_unpack(data, size, &program) && mz_write(&program, out);
    mz_program_free(&program);
    return ok;
}

static bool convert_unpack(const uint8_t *data, size_t size, ByteBuf *out)
{
    return unpack_all(data, size, out, NULL);
}

static bool convert_lzss(const uint8_t *data, size_t size, ByteBuf *out)
{
    return lzss_decode(data, size, out);
}

static bool convert_load_image(const uint8_t *data, size_t size, ByteBuf *out)
{
    MzHeader header;
    return mz_load_image(data, size, LOAD_SEGMENT, out, &header);
}

static int run_convert(ConvertFn convert, const char *in, const char *out)
{
    ByteBuf input = {0}, output = {0};
    bool ok = read_file(in, &input) && convert(input.data, input.size, &output) &&
              write_file(out, output.data, output.size);
    if (ok)
        printf("%s: %zu -> %zu bytes\n", in, input.size, output.size);
    bytebuf_free(&input);
    bytebuf_free(&output);
    return ok ? 0 : report_failure(in);
}

static int run_sha256(int count, char **paths)
{
    for (int i = 0; i < count; i++) {
        ByteBuf data = {0};
        if (!read_file(paths[i], &data))
            return report_failure(paths[i]);
        char hash[SHA256_HEX_SIZE];
        sha256_hex(data.data, data.size, hash);
        printf("%s  %s\n", hash, paths[i]);
        bytebuf_free(&data);
    }
    return 0;
}

int main(int argc, char **argv)
{
    static const struct {
        const char *flag;
        ConvertFn convert;
    } conversions[] = {
        {"--unpack", convert_unpack},
        {"--unlzexe", convert_lzexe},
        {"--unexepack", convert_exepack},
        {"--load-image", convert_load_image},
        {"--lzss", convert_lzss},
    };
    if (argc < 2) {
        usage();
        return 2;
    }
    for (size_t i = 0; i < sizeof conversions / sizeof *conversions; i++) {
        if (strcmp(argv[1], conversions[i].flag) == 0) {
            if (argc != 4) {
                usage();
                return 2;
            }
            return run_convert(conversions[i].convert, argv[2], argv[3]);
        }
    }
    if (strcmp(argv[1], "--sha256") == 0)
        return run_sha256(argc - 2, argv + 2);

    const char *mode = argv[1];
    Options opt = {0};
    for (int i = 2; i < argc; i++) {
        const char *arg = argv[i];
        bool has_value = i + 1 < argc;
        if (strcmp(arg, "--original") == 0 && has_value)
            opt.original_dir = argv[++i];
        else if (strcmp(arg, "--manifest") == 0 && has_value)
            opt.manifest_path = argv[++i];
        else if (strcmp(arg, "--out") == 0 && has_value)
            opt.out_dir = argv[++i];
        else if (strcmp(arg, "--allow-unverified") == 0)
            opt.allow_unverified = true;
        else {
            fprintf(stderr, "llamatron-extract: unexpected argument '%s'\n", arg);
            usage();
            return 2;
        }
    }

    if (strcmp(mode, "--verify") == 0) {
        Manifest manifest;
        return load_and_verify(&opt, &manifest) ? 0 : report_failure("verify");
    }
    if (strcmp(mode, "--all") == 0)
        return run_all(&opt);
    usage();
    return 2;
}
