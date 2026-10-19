/*
 * input_script.h - keyboard input tied to timer tick numbers, in the file format of the
 * DOSBox-X reference runs (tools/harness/dosbox/probe.asm, PROBE.KEY), so one script drives
 * both the original and the port.
 *
 * File: records of 6 bytes {u32 little-endian tick number, u8 set-1 scan byte (break = make
 * | 80h), u8 0}, sorted by tick number. Ticks are numbered from 1, counting from the game's
 * start (the first timer IRQ after game86 installed its handler); a byte for tick n reaches
 * the keyboard handler right before tick n runs (tools/harness/README.md, replay contract).
 */
#ifndef LLAMATRON_INPUT_SCRIPT_H
#define LLAMATRON_INPUT_SCRIPT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct InputScriptEvent {
    uint32_t tick;
    uint8_t scan_code;
} InputScriptEvent;

typedef struct InputScript {
    InputScriptEvent *events;
    size_t count, capacity;
    size_t next;                    /* replay position */
} InputScript;

enum { INPUT_SCRIPT_RECORD_SIZE = 6 };

/* Read a script; false (with a message) if the file is missing or malformed. */
bool input_script_load(InputScript *script, const char *path);
/* Append one event (recording); ticks must not decrease. */
bool input_script_add(InputScript *script, uint32_t tick, uint8_t scan_code);
bool input_script_save(const InputScript *script, const char *path);
/* Replay: the next event due at or before `tick`, or NULL. */
const InputScriptEvent *input_script_take_due(InputScript *script, uint32_t tick);
void input_script_free(InputScript *script);

#endif /* LLAMATRON_INPUT_SCRIPT_H */
