/*
 * scores_store.c - the host's high-score storage for tests (tests/oracle/test_hiscore_save.py):
 *
 *   scores_store save TABLE   a 160-byte scores.yak image through the port's save path, the
 *                             one the shell's GameLaunch.save_scores hook takes (game86
 *                             1000:5e89): scores_parse_yak, scores_format_yak (as the game turns
 *                             its table into bytes), plat_scores_save
 *   scores_store load OUT     plat_scores_load (game86 1000:60aa) into OUT
 *
 * Storage goes where the game keeps it: LLAMATRON_PREF_DIR, else the user's preference dir.
 * Prints "saved: yes|no" / "loaded: yes|no"; a failed save is not an error (the game goes on).
 */
#include "formats.h"
#include "settings.h"

#include <stdio.h>
#include <string.h>

static int save(const char *path)
{
    uint8_t yak[SCORES_YAK_SIZE + 1];
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(yak, 1, sizeof yak, f) : 0;
    if (f)
        fclose(f);
    HighScoreTable table;
    uint8_t out[SCORES_YAK_SIZE];
    if (n != SCORES_YAK_SIZE || !scores_parse_yak(yak, n, &table) || !scores_format_yak(&table, out)) {
        fprintf(stderr, "scores_store: %s is not a table the game would write\n", path);
        return 2;
    }
    printf("saved: %s\n", plat_scores_save(out) ? "yes" : "no");
    return 0;
}

static int load(const char *path)
{
    uint8_t yak[PLAT_SCORES_SIZE];
    bool ok = plat_scores_load(yak);
    printf("loaded: %s\n", ok ? "yes" : "no");
    if (!ok)
        return 0;
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(yak, 1, sizeof yak, f) != sizeof yak || fclose(f) != 0) {
        fprintf(stderr, "scores_store: cannot write %s\n", path);
        return 2;
    }
    return 0;
}

int main(int argc, char **argv)
{
    _Static_assert(PLAT_SCORES_SIZE == SCORES_YAK_SIZE, "scores.yak sizes disagree");
    if (argc == 3 && strcmp(argv[1], "save") == 0)
        return save(argv[2]);
    if (argc == 3 && strcmp(argv[1], "load") == 0)
        return load(argv[2]);
    fprintf(stderr, "usage: scores_store save TABLE | load OUT\n");
    return 2;
}
