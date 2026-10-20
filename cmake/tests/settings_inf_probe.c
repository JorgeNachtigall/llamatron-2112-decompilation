/* Test probe for launcher_oracle.py (src/platform/settings.c), one request per stdin line:
 *   <hex bytes>                      settings_parse_inf -> "accepted vga joystick sound cpu flash"
 *   F vga joystick sound cpu flash   settings_format_inf -> the llama.inf bytes as hex
 * (numbers in decimal). */
#include "assets.h"
#include "settings.h"

#include <stdio.h>
#include <string.h>

/* settings_defaults() reads config.dat through the asset store; the probe never calls it. */
const uint8_t *asset_get(const char *name, size_t *size)
{
    (void)name;
    *size = 0;
    return NULL;
}

int main(void)
{
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        int vga, joystick, sound, cpu, flash;
        /* The lines come from launcher_oracle.py (small decimal and hex numbers), so sscanf's
         * missing overflow report does not matter here. */
        /* NOLINTNEXTLINE(bugprone-unchecked-string-to-number-conversion) */
        if (sscanf(line, "F %d %d %d %d %d", &vga, &joystick, &sound, &cpu, &flash) == 5) {
            Settings s = {vga != 0, joystick != 0, (SettingsSound)sound, (uint8_t)cpu, (SettingsFlash)flash};
            uint8_t inf[SETTINGS_INF_SIZE];
            settings_format_inf(&s, inf);
            for (int i = 0; i < SETTINGS_INF_SIZE; i++)
                printf("%02x", inf[i]);
            printf("\n");
            continue;
        }
        uint8_t inf[64];
        size_t size = 0;
        unsigned byte;
        /* NOLINTNEXTLINE(bugprone-unchecked-string-to-number-conversion) */
        for (const char *p = line; size < sizeof inf && sscanf(p, "%2x", &byte) == 1; p += 2)
            inf[size++] = (uint8_t)byte;
        Settings s = settings_shipped_defaults();
        bool accepted = settings_parse_inf(inf, size, &s);
        printf("%d %d %d %d %d %d\n", accepted, s.vga, s.joystick, (int)s.sound, s.cpu, (int)s.flash);
    }
    return 0;
}
