#include <stdlib.h>
#include <string.h>
#include "tsnd.h"

static unsigned getbits(const uint8_t *b, int pos, int n) {
    unsigned v = 0;
    for (int k = 0; k < n; k++, pos++) v |= (unsigned)((b[pos >> 3] >> (pos & 7)) & 1) << k;
    return v;
}
static void putbits(uint8_t *b, int pos, int n, unsigned v) {
    for (int k = 0; k < n; k++, pos++) {
        if ((v >> k) & 1) b[pos >> 3] |= (uint8_t)(1 << (pos & 7));
        else b[pos >> 3] &= (uint8_t)~(1 << (pos & 7));
    }
}

int tsnd_decode(const uint8_t rec[TSND_BYTES], uint8_t f[NFIELD], char name[TSND_NAME]) {
    int pos = 0, ok = 1;
    for (int i = 0; i < NFIELD; i++) {
        unsigned v = getbits(rec, pos, PTAB[i].bits);
        pos += PTAB[i].bits;
        if ((int)v > PTAB[i].max) { ok = 0; v = (unsigned)PTAB[i].max; }
        f[i] = (uint8_t)v;
    }
    int n = 0;
    for (int k = 0; k < 20; k++) {
        unsigned c = getbits(rec, 880 + 7 * k, 7);
        name[n++] = (c >= 32 && c < 127) ? (char)c : ' ';
    }
    name[n] = 0;
    while (n > 0 && name[n - 1] == ' ') name[--n] = 0;
    return ok;
}

void tsnd_encode(const uint8_t f[NFIELD], const char *name, uint8_t rec[TSND_BYTES]) {
    memset(rec, 0, TSND_BYTES);
    int pos = 0;
    for (int i = 0; i < NFIELD; i++) { putbits(rec, pos, PTAB[i].bits, f[i]); pos += PTAB[i].bits; }
    size_t nl = strlen(name);
    for (int k = 0; k < 20; k++) putbits(rec, 880 + 7 * k, 7, k < (int)nl ? (unsigned)(name[k] & 0x7F) : ' ');
}

int tsnd_unpack(const uint8_t *in, int n, uint8_t *out, int max) {
    int o = 0;
    for (int i = 0; i < n; i += 8) {
        uint8_t m = in[i];
        for (int j = 1; j < 8 && i + j < n && o < max; j++) out[o++] = (uint8_t)(in[i + j] | (((m >> (j - 1)) & 1) << 7));
    }
    return o;
}

/* A plausible sound record inside a project: every field in range, the 116 reserved bits zero (as in every factory sound), all
 * 20 name characters printable, and a name that starts with a letter or digit. */
static int plausible(const uint8_t *rec, uint8_t f[NFIELD], char name[TSND_NAME]) {
    for (int b = NSOUND_BITS; b < 880; b++) if ((rec[b >> 3] >> (b & 7)) & 1) return 0;
    for (int k = 0; k < 20; k++) { unsigned c = getbits(rec, 880 + 7 * k, 7); if (c < 32 || c > 126) return 0; }
    if (!tsnd_decode(rec, f, name)) return 0;
    size_t n = strlen(name);
    if (n < 2) return 0;
    char c = name[0];
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

/* A beat's per-sound settings are 32 records of 30 bytes before its 32 sounds (docs/FIRMWARE.md section 5): bytes 0-5 are FF FF 00 00 00 00,
 * byte 6 volume, 7 pan, 12 and 13 the two Choke targets (a sound number 1-32 in the beat, 0 none). Returns the offset of the last such
 * block that ends before `end`, or -1. */
static int find_block(const uint8_t *u, int end) {
    for (int i = end - 960; i >= 0 && i > end - 16000; i--) {
        if (i >= 30 && !memcmp(u + i - 30, "\xff\xff\0\0\0\0", 6)) continue;
        int ok = 1;
        for (int k = 0; k < 32 && ok; k++) ok = !memcmp(u + i + 30 * k, "\xff\xff\0\0\0\0", 6);
        if (ok) return i;
    }
    return -1;
}
typedef struct { tsnd_fn fn; void *ctx; } plain_t;
static void plain_cb(void *c, const uint8_t f[NFIELD], const char *name, const uint8_t ch[2]) { plain_t *p = c; (void)ch; p->fn(p->ctx, f, name); }
int tsnd_scan(const uint8_t *buf, size_t len, tsnd_fn fn, void *ctx) {
    plain_t p = {fn, ctx};
    return tsnd_scan_ex(buf, len, plain_cb, &p);
}
int tsnd_scan_ex(const uint8_t *buf, size_t len, tsnd_fn_ex fn, void *ctx) {
    int count = 0;
    for (size_t i = 0; i + 6 < len; i++) {
        if (buf[i] != 0xF0 || buf[i + 1] != 0x01 || buf[i + 2] != 0x28 || (buf[i + 3] != 0x63 && buf[i + 3] != 0x61)) continue;
        size_t e = i + 5;
        while (e < len && buf[e] != 0xF7) e++;
        if (e >= len) break;
        int pathlen = buf[i + 4], n = (int)(e - (i + 5)), max = n / 8 * 7 + 7;
        uint8_t *u = malloc((size_t)max);
        if (!u) break;
        int got = tsnd_unpack(buf + i + 5, n, u, max);
        uint8_t f[NFIELD];
        char name[TSND_NAME];
        if (buf[i + 3] == 0x63) {
            if (got >= pathlen + TSND_BYTES - 4) {
                uint8_t rec[TSND_BYTES] = {0};
                int avail = got - pathlen;
                memcpy(rec, u + pathlen, (size_t)(avail < TSND_BYTES ? avail : TSND_BYTES));
                tsnd_decode(rec, f, name);
                if (!name[0]) {   /* fall back to the last part of the file path */
                    const char *p = (const char *)u, *slash = NULL;
                    for (int k = 0; k < pathlen && p[k]; k++) if (p[k] == '/') slash = p + k;
                    strncpy(name, slash ? slash + 1 : "Sound", TSND_NAME - 1);
                    name[TSND_NAME - 1] = 0;
                }
                const uint8_t none[2] = {0, 0};
                fn(ctx, f, name, none);
                count++;
            }
        } else {
            int run = 0, blk = -1;      /* run: position in a beat's 32 consecutive sounds */
            for (int o = pathlen; o + TSND_BYTES <= got;) {
                if (plausible(u + o, f, name)) {
                    if (run == 0) blk = find_block(u, o);
                    uint8_t ch[2] = {0, 0};
                    if (blk >= 0 && run < 32) { ch[0] = u[blk + 30 * run + 12]; ch[1] = u[blk + 30 * run + 13]; if (ch[0] > 32) ch[0] = 0; if (ch[1] > 32) ch[1] = 0; }
                    fn(ctx, f, name, ch);
                    count++;
                    run = (run + 1) % 32;
                    o += TSND_BYTES;
                } else { o++; run = 0; }
            }
        }
        free(u);
        i = e;
    }
    return count;
}
