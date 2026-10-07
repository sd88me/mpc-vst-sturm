/* The instrument's sound format (docs/FIRMWARE.md section 5).
 *
 * Sound dump:   F0 01 28 63 <n> <packed data> F7    (n = length of the file path that starts the data, e.g. "/S/Kicks/Ouch Kick")
 * Project dump: F0 01 28 61 <n> <packed data> F7    (a project file; its 16 beats x 32 sounds are 128-byte sound records)
 * Packed data is DSI's "packed MS bit" format (8 MIDI bytes carry 7 bytes, the first holding their top bits).
 * A 128-byte sound record is a little-endian bit stream: the 127 fields at their widths (PTAB[].bits, 764 bits), 116 reserved
 * bits, then the 20-character name as 7-bit characters from bit 880. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "patch_tab.h"

#define TSND_BYTES 128
#define TSND_NAME 21

int tsnd_decode(const uint8_t rec[TSND_BYTES], uint8_t fields[NFIELD], char name[TSND_NAME]);   /* 1 if every field is in range */
void tsnd_encode(const uint8_t fields[NFIELD], const char *name, uint8_t rec[TSND_BYTES]);
int tsnd_unpack(const uint8_t *in, int n, uint8_t *out, int max);                                 /* packed MS bit -> bytes */

/* Calls fn for every sound found in a buffer of SysEx (sound dumps and project dumps). Returns the number of sounds. */
typedef void (*tsnd_fn)(void *ctx, const uint8_t fields[NFIELD], const char *name);
int tsnd_scan(const uint8_t *buf, size_t len, tsnd_fn fn, void *ctx);
/* The same with each sound's two Choke targets from its beat (0 = none; always 0 for a single sound dump). */
typedef void (*tsnd_fn_ex)(void *ctx, const uint8_t fields[NFIELD], const char *name, const uint8_t choke[2]);
int tsnd_scan_ex(const uint8_t *buf, size_t len, tsnd_fn_ex fn, void *ctx);
