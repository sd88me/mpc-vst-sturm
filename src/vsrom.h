/* The Prophet VS's 95 ROM waves from the user's own program ROM chips (never shipped; found by structure, see docs/FIRMWARE.md and
 * the Morpho-PE repo's section 14): the two 27256 chips (high byte and low byte, 16 or 32 KB each, in either order) or one 64 KB
 * interleaved image. Read as one byte stream from the chips' upper half, a table of 7264 rising 16-bit words ends at 0xFFFF and 95
 * waves of 192 bytes follow: 128 signed high bytes, then 64 bytes of low nibbles (the even sample in the high nibble). */
#pragma once
#include <stddef.h>
#include <stdint.h>
#define VSROM_WAVES 95
#define VSROM_WLEN 128
/* b may be NULL for an interleaved image. Returns 95 and fills out (-1..1) when the files are what they claim, else 0. */
int vsrom_decode(const uint8_t *a, size_t na, const uint8_t *b, size_t nb, float out[VSROM_WAVES][VSROM_WLEN]);
