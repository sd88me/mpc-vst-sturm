#include "vsrom.h"

/* byte i of the upper half as one stream: hi/lo are the two chips' upper 16 KB, or (lo NULL) an interleaved 32 KB */
static int vs_byte(const uint8_t *hi, const uint8_t *lo, int i) { return lo ? (i & 1 ? lo : hi)[i >> 1] : hi[i]; }
static int vs_ok(const uint8_t *hi, const uint8_t *lo) {
    int prev = -1, rises = 0;
    for (int k = 0; k < 7264; k++) {
        int w = vs_byte(hi, lo, 2 * k) << 8 | vs_byte(hi, lo, 2 * k + 1);
        if (w < prev) return 0;
        rises += w > prev;
        prev = w;
    }
    /* the real table rises on most steps; one chip given twice (or the wrong pair) rises only when its high byte does */
    return prev == 0xFFFF && rises > 4000 && (vs_byte(hi, lo, 0) << 8 | vs_byte(hi, lo, 1)) < 0x1000;
}
static const uint8_t *vs_upper(const uint8_t *p, size_t n) { return n == 32768 ? p + 16384 : n == 16384 ? p : NULL; }

int vsrom_decode(const uint8_t *a, size_t na, const uint8_t *b, size_t nb, float out[VSROM_WAVES][VSROM_WLEN]) {
    const uint8_t *hi = NULL, *lo = NULL;
    if (!b) {
        if (na != 65536 && na != 32768) return 0;
        hi = a + na - 32768;
        if (!vs_ok(hi, NULL)) return 0;
    } else {
        const uint8_t *ua = vs_upper(a, na), *ub = vs_upper(b, nb);
        if (!ua || !ub) return 0;
        if (vs_ok(ua, ub)) { hi = ua; lo = ub; }
        else if (vs_ok(ub, ua)) { hi = ub; lo = ua; }
        else return 0;
    }
    for (int w = 0; w < VSROM_WAVES; w++) {
        int base = 14528 + 192 * w;
        for (int k = 0; k < VSROM_WLEN; k++) {
            int h = (int8_t)vs_byte(hi, lo, base + k);
            int nb2 = vs_byte(hi, lo, base + 128 + k / 2);
            int n = (k & 1) ? (nb2 & 15) : (nb2 >> 4);
            out[w][k] = (h * 16 + n) / 2048.0f;
        }
    }
    return VSROM_WAVES;
}
