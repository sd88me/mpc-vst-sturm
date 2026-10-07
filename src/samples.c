/* Stand-in samples of this project's own and the WAV loader (see samples.h). Every stand-in is a small synthesis recipe chosen by
 * the slot's group (its position in the instrument's list) and refined by words in its name ("808", "Open", "Reverse"...), with
 * a fixed per-slot seed, so the same slot always sounds the same. They are meant to sit where the instrument's samples sit in a
 * sound (a kick where a kick was), not to imitate any recording. */
#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "samples.h"

#define SR 44100.0f
#define TWO_PI 6.2831853f
#define NMIP 8

struct tp_samples {
    tp_sample_t slot[TP_NSAMPLES];
    float *mip[TP_NSAMPLES];      /* wave slots: NMIP band-limited tables of TP_WLEN points */
    int user;
};

/* slot groups, by position in the list (manual p. 80-84) */
enum { G_OFF, G_NOISE, G_KICK, G_SNARE, G_HAT, G_CYM, G_TOM, G_CLAP, G_COW, G_PERC, G_WAVE };
static int group(int k) {
    if (k <= 0) return G_OFF;
    if (k <= 10) return G_NOISE;
    if (k <= 55) return G_KICK;
    if (k <= 128) return G_SNARE;
    if (k <= 195) return G_HAT;
    if (k <= 218) return G_CYM;
    if (k <= 254) return G_TOM;
    if (k <= 265) return G_CLAP;
    if (k <= 275) return G_COW;
    if (k <= 366) return G_PERC;
    return G_WAVE;
}
static int has(int k, const char *w) {
    const char *n = TP_SAMPLE_NAMES[k];
    size_t lw = strlen(w);
    for (; *n; n++) if (!strncasecmp(n, w, lw)) return 1;
    return 0;
}

/* ---------------- small DSP kit ---------------- */
typedef struct { uint32_t s; } rng_t;
static float frand(rng_t *r) { r->s = r->s * 1664525u + 1013904223u; return (float)(int32_t)r->s * (1.0f / 2147483648.0f); }
static float urand(rng_t *r) { return 0.5f * (frand(r) + 1); }
typedef struct { float b0, b1, b2, a1, a2, z1, z2; } bq_t;
static void bq_set(bq_t *f, int type, float fc, float q) {   /* RBJ: 0 LP, 1 HP, 2 BP */
    if (fc > 0.45f * SR) fc = 0.45f * SR;
    float w = TWO_PI * fc / SR, c = cosf(w), al = sinf(w) / (2 * q), a0 = 1 + al;
    if (type == 0) { f->b0 = (1 - c) / 2; f->b1 = 1 - c; f->b2 = (1 - c) / 2; }
    else if (type == 1) { f->b0 = (1 + c) / 2; f->b1 = -(1 + c); f->b2 = (1 + c) / 2; }
    else { f->b0 = al; f->b1 = 0; f->b2 = -al; }
    f->b0 /= a0; f->b1 /= a0; f->b2 /= a0; f->a1 = -2 * c / a0; f->a2 = (1 - al) / a0;
}
static float bq(bq_t *f, float x) {
    float y = f->b0 * x + f->z1;
    f->z1 = f->b1 * x - f->a1 * y + f->z2;
    f->z2 = f->b2 * x - f->a2 * y;
    return y;
}
static float sq(float ph) { return ph - floorf(ph) < 0.5f ? 1.0f : -1.0f; }
static float metal(float *ph, const float *f, int n, float scale) {
    float s = 0;
    for (int i = 0; i < n; i++) { ph[i] += f[i] * scale / SR; s += sq(ph[i]); }
    return s / n;
}
static const float HATF[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};   /* a square-wave cluster, the classic metal recipe */

/* ---------------- recipes (float, -1..1, n frames) ---------------- */
static void gen_noise(int k, float *x, int n, rng_t *r) {
    bq_t a = {0}, b = {0};
    float p0 = 0, p1 = 0, p2 = 0, hold = 0, ph = 0;
    if (k == 3) bq_set(&a, 2, 600, 0.45f);
    if (k == 4) bq_set(&a, 2, 4000, 8);
    if (k == 5) { bq_set(&a, 0, 1800, 0.7f); bq_set(&b, 0, 1800, 0.7f); }
    if (k == 6) bq_set(&a, 2, 6200, 3);
    if (k == 7) bq_set(&a, 0, 2500, 0.8f);
    if (k == 8) bq_set(&a, 2, 3300, 12);
    if (k == 10) { bq_set(&a, 1, 420, 0.7f); bq_set(&b, 1, 420, 0.7f); }
    for (int i = 0; i < n; i++) {
        float w = frand(r), y = w;
        switch (k) {
        case 2: p0 = 0.99765f * p0 + w * 0.0990460f; p1 = 0.96300f * p1 + w * 0.2965164f; p2 = 0.57000f * p2 + w * 1.0526913f;
                y = (p0 + p1 + p2 + w * 0.1848f) * 0.25f; break;
        case 3: case 4: case 10: y = bq(&a, w); if (k == 10) y = bq(&b, y); break;
        case 5: y = bq(&b, bq(&a, w)); break;
        case 6: y = bq(&a, w) * (0.55f + 0.45f * sinf(TWO_PI * 38 * i / SR)); break;
        case 7: ph += 110 / SR; y = bq(&a, (ph - floorf(ph) < 0.08f ? 1.0f : 0.0f) + 0.3f * w); break;
        case 8: y = bq(&a, w) + 0.3f * w; break;
        case 9: if (i % 23 == 0) hold = w; y = hold * (0.6f + 0.4f * sinf(TWO_PI * 13 * i / SR)); break;
        }
        x[i] = y;
    }
}
static float env_exp(int i, float t) { return expf(-(float)i / (t * SR)); }

static void gen_kick(int k, float *x, int n, rng_t *r) {
    int is808 = has(k, "808") || has(k, "Long") || has(k, "Boom"), is909 = has(k, "909") || has(k, "09") || has(k, "Punch");
    float fe = 42 + 22 * urand(r), fs = fe * (3 + 5 * urand(r)), ts = 0.012f + 0.04f * urand(r);
    float dec = is808 ? 0.55f + 0.4f * urand(r) : 0.12f + 0.3f * urand(r), click = is909 ? 0.5f : 0.15f * urand(r);
    float ph = 0, drive = (has(k, "Distort") || has(k, "Dirty") || has(k, "Crunch") || has(k, "Grain")) ? 4.0f : 1.2f;
    bq_t c = {0};
    bq_set(&c, 2, 3500, 1);
    for (int i = 0; i < n; i++) {
        float t = i / SR, f = fe + (fs - fe) * expf(-t / ts);
        ph += f / SR;
        float y = sinf(TWO_PI * ph) * env_exp(i, dec) + click * bq(&c, frand(r)) * env_exp(i, 0.004f);
        x[i] = tanhf(y * drive);
    }
}
static void gen_snare(int k, float *x, int n, rng_t *r) {
    bq_t nb = {0}, hp = {0};
    if (has(k, "Rim") || has(k, "Stick") || has(k, "Side") || has(k, "Edge")) {   /* rims and side sticks: short, high, woody */
        float f1 = 420 + 200 * urand(r), f2 = 1500 + 800 * urand(r), p1 = 0, p2 = 0;
        bq_set(&nb, 2, 2500, 2);
        for (int i = 0; i < n; i++) {
            p1 += f1 / SR; p2 += f2 / SR;
            x[i] = (0.6f * sinf(TWO_PI * p1) + 0.5f * sinf(TWO_PI * p2)) * env_exp(i, 0.018f) + 0.5f * bq(&nb, frand(r)) * env_exp(i, 0.006f);
        }
        return;
    }
    float f1 = 170 + 80 * urand(r), f2 = f1 * (1.55f + 0.3f * urand(r)), td = 0.06f + 0.12f * urand(r), nd = 0.1f + 0.25f * urand(r);
    float nmix = 0.5f + 0.4f * urand(r), p1 = 0, p2 = 0;
    bq_set(&nb, 2, 1800 + 3000 * urand(r), 0.7f);
    bq_set(&hp, 1, 600, 0.7f);
    for (int i = 0; i < n; i++) {
        float t = i / SR, bend = 1 + 0.3f * expf(-t / 0.01f);
        p1 += f1 * bend / SR; p2 += f2 * bend / SR;
        float tone = (sinf(TWO_PI * p1) + 0.6f * sinf(TWO_PI * p2)) * env_exp(i, td);
        float nz = bq(&hp, bq(&nb, frand(r))) * 2.5f * env_exp(i, nd);
        x[i] = tanhf((1 - nmix) * tone + nmix * nz);
    }
}
static void gen_metal(int k, float *x, int n, rng_t *r, float dec, float tone_hz) {
    float ph[6] = {0}, sc = 0.9f + 0.4f * urand(r);
    bq_t a = {0}, b = {0};
    bq_set(&a, 2, tone_hz * (0.8f + 0.4f * urand(r)), 0.9f);
    bq_set(&b, 1, 6000, 0.7f);
    for (int i = 0; i < n; i++) {
        float m = metal(ph, HATF, 6, sc) + 0.35f * frand(r);
        x[i] = bq(&b, bq(&a, m)) * 2.2f * env_exp(i, dec) * (1 - env_exp(i, 0.0006f) * 0.5f);
    }
    (void)k;
}
static void gen_hat(int k, float *x, int n, rng_t *r) {
    float dec = 0.035f + 0.04f * urand(r);
    if (has(k, "Pedal") || has(k, "Foot")) dec = 0.09f;
    if (has(k, "Half")) dec = 0.22f;
    if (has(k, "Open") || has(k, "Opn") || has(k, "Loose")) dec = 0.45f + 0.35f * urand(r);
    gen_metal(k, x, n, r, dec, 9000);
}
static void gen_cym(int k, float *x, int n, rng_t *r) {
    if (has(k, "Gong")) {
        float ph[5] = {0}, f[5] = {62, 111, 157, 236, 311};
        for (int i = 0; i < n; i++) {
            float s = 0;
            for (int j = 0; j < 5; j++) { ph[j] += f[j] * (1 + 0.002f * sinf(TWO_PI * 0.7f * i / SR)) / SR; s += sinf(TWO_PI * ph[j]) / (j + 1); }
            x[i] = s * env_exp(i, 1.6f) * (1 - env_exp(i, 0.02f));
        }
        return;
    }
    int ride = has(k, "Ride") || has(k, "Bell");
    gen_metal(k, x, n, r, ride ? 0.9f : 1.3f + 0.7f * urand(r), ride ? 5000 : 7500);
    if (ride) {
        float p[3] = {0}, f[3] = {720, 1230, 1990};
        for (int i = 0; i < n; i++) {
            float s = 0;
            for (int j = 0; j < 3; j++) { p[j] += f[j] / SR; s += sinf(TWO_PI * p[j]); }
            x[i] += 0.25f * s * env_exp(i, has(k, "Bell") ? 0.8f : 0.35f);
        }
    }
}
static void gen_tom(int k, float *x, int n, rng_t *r) {
    float f = has(k, "Hi") ? 190 + 80 * urand(r) : has(k, "Mid") ? 130 + 50 * urand(r) : 80 + 40 * urand(r);
    float dec = 0.25f + 0.3f * urand(r), ph = 0;
    bq_t nb = {0};
    bq_set(&nb, 2, 1500, 1);
    for (int i = 0; i < n; i++) {
        float t = i / SR;
        ph += f * (1 + 0.6f * expf(-t / 0.03f)) / SR;
        x[i] = sinf(TWO_PI * ph) * env_exp(i, dec) + 0.2f * bq(&nb, frand(r)) * env_exp(i, 0.01f);
    }
}
static void gen_clap(int k, float *x, int n, rng_t *r) {
    bq_t a = {0};
    bq_set(&a, 2, 1000 + 500 * urand(r), 1.4f);
    float gap = 0.008f + 0.005f * urand(r), tail = 0.12f + 0.15f * urand(r);
    for (int i = 0; i < n; i++) {
        float t = i / SR, e;
        if (t < 3 * gap) { float u = fmodf(t, gap); e = expf(-u / 0.004f); }
        else e = expf(-(t - 3 * gap) / tail);
        x[i] = bq(&a, frand(r)) * 3 * e;
    }
    (void)k;
}
static void gen_cow(int k, float *x, int n, rng_t *r) {
    float f1 = 540 * (0.85f + 0.3f * urand(r)), f2 = f1 * 1.48f, p1 = 0, p2 = 0;
    bq_t a = {0};
    bq_set(&a, 2, 900, 1.5f);
    for (int i = 0; i < n; i++) {
        p1 += f1 / SR; p2 += f2 / SR;
        float e = 0.6f * env_exp(i, 0.015f) + 0.4f * env_exp(i, 0.18f);
        x[i] = bq(&a, sq(p1) + sq(p2)) * 1.8f * e;
    }
    (void)k;
}
static void gen_perc(int k, float *x, int n, rng_t *r) {
    if (has(k, "Shaker") || has(k, "Cabasa") || has(k, "Maraca") || has(k, "Guiro") || has(k, "Fingers")) {
        bq_t a = {0};
        bq_set(&a, 1, 5000, 0.7f);
        int guiro = has(k, "Guiro");
        for (int i = 0; i < n; i++) {
            float t = i / SR, e = guiro ? (fmodf(t, 0.012f) < 0.004f ? 1.0f : 0.15f) * (t < 0.25f) : (1 - expf(-t / 0.01f)) * expf(-t / 0.06f);
            x[i] = bq(&a, frand(r)) * e * 2;
        }
        return;
    }
    if (has(k, "Tamb") || has(k, "Tambo")) { gen_metal(k, x, n, r, 0.25f, 8000); return; }
    if (has(k, "Conga") || has(k, "Tabla") || has(k, "Udu") || has(k, "Bounce") || has(k, "Drum")) {
        float f = 180 + 260 * urand(r), ph = 0, up = has(k, "Tabla") || has(k, "Udu") ? 1.0f : 0.0f;
        for (int i = 0; i < n; i++) {
            float t = i / SR;
            ph += f * (1 + 0.25f * expf(-t / 0.02f) + up * 0.15f * (1 - expf(-t / 0.08f))) / SR;
            x[i] = (sinf(TWO_PI * ph) + 0.3f * sinf(TWO_PI * 1.51f * ph)) * env_exp(i, 0.12f + 0.2f * up);
        }
        return;
    }
    if (has(k, "Tri") || has(k, "Bell") || has(k, "Metal") || has(k, "Clank") || has(k, "Flexi") || has(k, "Stick on")) {
        float f0 = 700 + 1800 * urand(r), p[4] = {0}, m[4] = {1, 2.76f, 5.40f, 8.93f};
        float dec = has(k, "Short") ? 0.15f : 0.5f + 0.8f * urand(r);
        for (int i = 0; i < n; i++) {
            float s = 0;
            for (int j = 0; j < 4; j++) { p[j] += f0 * m[j] / SR; s += sinf(TWO_PI * p[j]) / (1 + j); }
            x[i] = s * env_exp(i, dec);
        }
        return;
    }
    if (has(k, "Cross") || has(k, "Block") || has(k, "Clave") || has(k, "Wood") || has(k, "Keys") || has(k, "Castanet") ||
        has(k, "Click") || has(k, "Tap") || has(k, "stick") || has(k, "Lid") || has(k, "Box")) {
        float f = 700 + 1900 * urand(r), ph = 0, dec = 0.015f + 0.05f * urand(r);
        for (int i = 0; i < n; i++) { ph += f / SR; x[i] = sinf(TWO_PI * ph) * env_exp(i, dec) + 0.3f * frand(r) * env_exp(i, 0.002f); }
        return;
    }
    /* effects and the rest: a seeded FM sweep, a noise sweep or a zap */
    int kind = (int)(urand(r) * 3);
    float f0 = 80 + 1500 * urand(r), f1 = 50 + 3000 * urand(r), ratio = 0.5f + 3 * urand(r), dur = 0.15f + 0.6f * urand(r);
    float ph = 0, pm = 0;
    bq_t a = {0};
    for (int i = 0; i < n; i++) {
        float t = i / SR, u = fminf(t / dur, 1), f = f0 * powf(f1 / f0, u), e = env_exp(i, dur * 0.5f);
        if (kind == 0) { pm += f * ratio / SR; ph += f * (1 + 2 * sinf(TWO_PI * pm) * (1 - u)) / SR; x[i] = sinf(TWO_PI * ph) * e; }
        else if (kind == 1) { if (i % 64 == 0) bq_set(&a, 2, f * 2, 4); x[i] = bq(&a, frand(r)) * 3 * e; }
        else { ph += f0 * 4 * expf(-t / 0.03f) / SR + 40 / SR; x[i] = sq(ph) * 0.6f * e; }
    }
}

/* ---------------- single-cycle waves: a spectrum per name, then band-limited tables ---------------- */
static void wave_spectrum(int k, float *amp, float *phs, int nh, rng_t *r) {
    for (int h = 1; h <= nh; h++) { amp[h] = 0; phs[h] = 0; }
    const char *n = TP_SAMPLE_NAMES[k];
    if (!strcmp(n, "Sine") || has(k, "Sine 130")) { amp[1] = 1; return; }
    if (!strcmp(n, "Sawtooth")) { for (int h = 1; h <= nh; h++) amp[h] = 1.0f / h; return; }
    if (!strcmp(n, "Square")) { for (int h = 1; h <= nh; h += 2) amp[h] = 1.0f / h; return; }
    if (has(k, "Pulse")) { float d = has(k, "Pulse3") ? 0.125f : 0.25f; for (int h = 1; h <= nh; h++) amp[h] = fabsf(sinf(3.14159f * h * d)) / h; return; }
    if (has(k, "TriPlus")) { for (int h = 1; h <= nh; h += 2) amp[h] = 1.0f / (h * h); amp[2] = 0.2f; amp[3] += 0.15f; return; }
    float base = TP_WAVE_HZ;
    struct { const char *w; float f1, f2, f3; } FMT[] = {{"Ooh", 300, 870, 2240}, {"Eeh", 270, 2300, 3000}, {"Ahh", 730, 1090, 2440},
        {"AOh", 570, 840, 2410}, {"Vox", 500, 1500, 2500}, {"Vocal", 450, 1200, 2600}, {"Harmony", 600, 1000, 2500}};
    for (size_t j = 0; j < sizeof FMT / sizeof FMT[0]; j++)
        if (has(k, FMT[j].w)) {
            for (int h = 1; h <= nh; h++) {
                float f = h * base, a = 0;
                float fs[3] = {FMT[j].f1, FMT[j].f2, FMT[j].f3};
                for (int q = 0; q < 3; q++) a += expf(-(f - fs[q]) * (f - fs[q]) / (2 * 120.0f * 120.0f)) / (q + 1);
                amp[h] = a + 0.08f / h;
            }
            return;
        }
    if (has(k, "Reed") || has(k, "Oboe") || has(k, "Clarinet")) {
        for (int h = 1; h <= nh; h++) amp[h] = (h & 1 ? 1.0f : 0.25f) / powf((float)h, 0.9f) * (has(k, "Oboe") && h > 2 && h < 7 ? 2.0f : 1.0f);
        return;
    }
    if (has(k, "Org") || has(k, "Pipe")) { int d[] = {1, 2, 3, 4, 6, 8}; float a[] = {1, 0.8f, 0.6f, 0.5f, 0.35f, 0.3f}; for (int j = 0; j < 6; j++) if (d[j] <= nh) amp[d[j]] = a[j]; return; }
    if (has(k, "Flute") || has(k, "Whistl") || has(k, "Pure")) { amp[1] = 1; amp[2] = 0.2f; amp[3] = 0.12f; amp[4] = 0.04f; return; }
    if (has(k, "Tuba") || has(k, "Corn")) { for (int h = 1; h <= 14 && h <= nh; h++) amp[h] = 1.0f / powf((float)h, 1.1f); return; }
    if (has(k, "Bell") || has(k, "Bel") || has(k, "Tine") || has(k, "Partials")) {
        int p[] = {1, 2, 3, 5, 7, 9, 11, 14, 17};
        for (int j = 0; j < 9; j++) if (p[j] <= nh) amp[p[j]] = (0.4f + 0.6f * urand(r)) / (1 + 0.4f * j);
        return;
    }
    if (has(k, "Piano") || has(k, "Pno") || has(k, "Harp") || has(k, "Banjo") || has(k, "Guitar") || has(k, "Harm")) {
        for (int h = 1; h <= nh; h++) amp[h] = powf((float)h, -1.4f) * (0.6f + 0.4f * fabsf(sinf(h * 1.3f + urand(r))));
        return;
    }
    if (has(k, "Bass")) { for (int h = 1; h <= nh; h++) amp[h] = (h < 6 ? 1.0f : 0.3f) / h; return; }
    if (has(k, "NoFund")) { for (int h = 2; h <= nh; h++) amp[h] = 1.0f / h; return; }
    /* the rest ("VS nn", Hack, Pinch, Clustr, Feedback, ...): seeded spectra */
    float tilt = 0.6f + 1.2f * urand(r);
    for (int h = 1; h <= nh; h++) { amp[h] = urand(r) * urand(r) / powf((float)h, tilt); phs[h] = TWO_PI * urand(r); }
    amp[1] += 0.3f;
}
/* NMIP tables: level l keeps harmonics up to 128 >> l (256 points carry 127 harmonics). */
static void build_mips(float *mip, const float *amp, const float *phs) {
    float m0 = 0;
    for (int l = 0; l < NMIP; l++) {
        int hmax = 127 >> l;
        float *t = mip + l * TP_WLEN, m = 0;
        for (int i = 0; i < TP_WLEN; i++) {
            float s = 0;
            for (int h = 1; h <= hmax; h++) if (amp[h] != 0) s += amp[h] * sinf(TWO_PI * h * i / TP_WLEN + phs[h]);
            t[i] = s;
        }
        for (int i = 0; i < TP_WLEN; i++) m = fmaxf(m, fabsf(t[i]));
        if (l == 0) m0 = m > 0 ? m : 1;
        for (int i = 0; i < TP_WLEN; i++) t[i] /= m0;
    }
}

/* ---------------- slots ---------------- */
static float slot_len(int g, int k) {
    switch (g) {
    case G_NOISE: return 1.0f;
    case G_KICK: return has(k, "808") || has(k, "Long") || has(k, "Boom") ? 1.4f : 0.8f;
    case G_SNARE: return 0.6f;
    case G_HAT: return has(k, "Open") || has(k, "Opn") || has(k, "Loose") || has(k, "Half") ? 1.3f : 0.4f;
    case G_CYM: return 3.0f;
    case G_TOM: return 1.0f;
    case G_CLAP: return 0.6f;
    case G_COW: return 0.8f;
    default: return 1.5f;
    }
}
static void finish(tp_sample_t *s, float *x, int n, int rev) {
    float m = 0;
    for (int i = 0; i < n; i++) m = fmaxf(m, fabsf(x[i]));
    float g = m > 0 ? 0.9f / m : 0;
    int fade = n < 2000 ? n / 4 : 500;
    s->data = malloc(sizeof(int16_t) * (size_t)n);
    if (!s->data) { s->len = 0; return; }
    for (int i = 0; i < n; i++) {
        float y = x[rev ? n - 1 - i : i] * g;
        if (!s->loop && !rev && i >= n - fade) y *= (float)(n - i) / fade;
        if (rev && i < fade) y *= (float)i / fade;
        s->data[i] = (int16_t)lrintf(fmaxf(-1, fminf(1, y)) * 32767);
    }
    s->len = n;
}
static void synth_slot(tp_samples_t *ss, int k) {
    tp_sample_t *s = &ss->slot[k];
    int g = group(k);
    rng_t r = {0x7E3A5000u + 7919u * (uint32_t)k};
    s->rate = 1;
    if (g == G_OFF) { s->ready = 1; return; }
    if (g == G_WAVE || k == 367) {
        float amp[129], phs[129];
        wave_spectrum(k, amp, phs, 128, &r);
        ss->mip[k] = malloc(sizeof(float) * NMIP * TP_WLEN);
        if (ss->mip[k]) build_mips(ss->mip[k], amp, phs);
        s->loop = 1;
        s->rate = TP_WAVE_HZ * TP_WLEN / SR;
        s->len = TP_WLEN;
        s->ready = 1;
        return;
    }
    int n = (int)(slot_len(g, k) * SR);
    float *x = calloc((size_t)n, sizeof(float));
    if (!x) { s->ready = 1; return; }
    switch (g) {
    case G_NOISE: gen_noise(k, x, n, &r); s->loop = 1; s->fixed = 1; break;
    case G_KICK: gen_kick(k, x, n, &r); break;
    case G_SNARE: gen_snare(k, x, n, &r); break;
    case G_HAT: gen_hat(k, x, n, &r); break;
    case G_CYM: gen_cym(k, x, n, &r); break;
    case G_TOM: gen_tom(k, x, n, &r); break;
    case G_CLAP: gen_clap(k, x, n, &r); break;
    case G_COW: gen_cow(k, x, n, &r); break;
    default: gen_perc(k, x, n, &r);
    }
    finish(s, x, n, has(k, "Reverse"));
    free(x);
    s->ready = 1;
}

tp_samples_t *samples_open(void) { return calloc(1, sizeof(tp_samples_t)); }
void samples_close(tp_samples_t *s) {
    if (!s) return;
    for (int k = 0; k < TP_NSAMPLES; k++) { free(s->slot[k].data); free(s->mip[k]); }
    free(s);
}
const tp_sample_t *samples_get(tp_samples_t *s, int k) {
    if (k <= 0 || k >= TP_NSAMPLES) return &s->slot[0];
    if (!s->slot[k].ready) synth_slot(s, k);
    return &s->slot[k];
}
const float *samples_wave(tp_samples_t *s, int k, float inc) {
    if (k <= 0 || k >= TP_NSAMPLES || !s->mip[k]) return NULL;
    int l = 0;
    float hmax = 127;
    while (l < NMIP - 1 && hmax * inc > 0.45f) { l++; hmax = (float)(127 >> l); }
    return s->mip[k] + l * TP_WLEN;
}
int samples_user_count(const tp_samples_t *s) { return s->user; }

/* ---------------- WAV ---------------- */
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
typedef struct { const uint8_t *data; uint32_t frames, rate, ncue, cue[257]; int fmt, ch, bits; } wav_t;
static int wav_parse(const uint8_t *buf, size_t len, wav_t *w) {
    memset(w, 0, sizeof *w);
    if (len < 12 || memcmp(buf, "RIFF", 4) || memcmp(buf + 8, "WAVE", 4)) return 0;
    uint32_t dlen = 0;
    for (size_t i = 12; i + 8 <= len;) {
        uint32_t sz = rd32(buf + i + 4);
        const uint8_t *c = buf + i + 8;
        if (sz > len - i - 8) sz = (uint32_t)(len - i - 8);
        if (!memcmp(buf + i, "fmt ", 4) && sz >= 16) {
            w->fmt = c[0] | c[1] << 8; w->ch = c[2] | c[3] << 8; w->rate = rd32(c + 4); w->bits = c[14] | c[15] << 8;
            if (w->fmt == 0xFFFE && sz >= 26) w->fmt = c[24] | c[25] << 8;
        } else if (!memcmp(buf + i, "data", 4)) { w->data = c; dlen = sz; }
        else if (!memcmp(buf + i, "cue ", 4) && sz >= 4) {
            uint32_t n = rd32(c);
            for (uint32_t k = 0; k < n && w->ncue < 256 && 4 + 24 * k + 24 <= sz; k++) w->cue[w->ncue++] = rd32(c + 4 + 24 * k + 20);
        }
        i += 8 + sz + (sz & 1);
    }
    if (!w->data || w->ch < 1 || !w->rate || !((w->fmt == 1 && (w->bits == 16 || w->bits == 24 || w->bits == 32 || w->bits == 8)) || (w->fmt == 3 && w->bits == 32))) return 0;
    w->frames = dlen / (uint32_t)(w->ch * w->bits / 8);
    return w->frames > 0;
}
static float wav_at(const wav_t *w, uint32_t f) {
    float s = 0;
    for (int c = 0; c < w->ch; c++) {
        const uint8_t *p = w->data + ((size_t)f * w->ch + c) * (w->bits / 8);
        if (w->fmt == 3) { float v; memcpy(&v, p, 4); s += v; }
        else if (w->bits == 8) s += (p[0] - 128) / 128.0f;
        else if (w->bits == 16) s += (int16_t)(p[0] | p[1] << 8) / 32768.0f;
        else if (w->bits == 24) s += (float)((int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8) / 8388608.0f;
        else s += (float)(int32_t)rd32(p) / 2147483648.0f;
    }
    return s / w->ch;
}
static void load_oneshot(tp_samples_t *ss, int k, const wav_t *w) {
    tp_sample_t *s = &ss->slot[k];
    free(ss->mip[k]);
    ss->mip[k] = NULL;
    int g = group(k);
    double step = (double)w->rate / SR;
    int n = (int)(w->frames / step);
    if (n < 2 || n > (int)(SR * 30)) return;
    float *x = malloc(sizeof(float) * (size_t)n);
    if (!x) return;
    for (int i = 0; i < n; i++) {
        double p = i * step;
        uint32_t i0 = (uint32_t)p, i1 = i0 + 1 < w->frames ? i0 + 1 : i0;
        float a = wav_at(w, i0), b = wav_at(w, i1);
        x[i] = a + (float)(p - i0) * (b - a);
    }
    free(s->data);
    memset(s, 0, sizeof *s);
    s->loop = g == G_NOISE;
    s->fixed = g == G_NOISE;
    s->rate = 1;
    finish(s, x, n, 0);
    free(x);
    s->user = s->ready = 1;
    ss->user++;
}
/* One cycle (L frames from a) becomes wave slot `slot`: resampled to TP_WLEN points, then the same band-limited tables as the stand-ins. */
static void set_cycle(tp_samples_t *ss, const wav_t *w, uint32_t a, uint32_t L, int slot) {
    float cyc[TP_WLEN], amp[129], phs[129], mean = 0;
    for (int j = 0; j < TP_WLEN; j++) {
        float p = (float)j * L / TP_WLEN;
        uint32_t i0 = (uint32_t)p, i1 = i0 + 1 < L ? i0 + 1 : i0;
        float x0 = wav_at(w, a + i0), x1 = wav_at(w, a + i1);
        cyc[j] = x0 + (p - i0) * (x1 - x0);
        mean += cyc[j];
    }
    mean /= TP_WLEN;
    for (int h = 1; h <= 128; h++) {
        float re = 0, im = 0;
        for (int j = 0; j < TP_WLEN; j++) { float ph = TWO_PI * h * j / TP_WLEN; re += (cyc[j] - mean) * cosf(ph); im += (cyc[j] - mean) * sinf(ph); }
        amp[h] = sqrtf(re * re + im * im) * 2 / TP_WLEN;
        phs[h] = atan2f(re, im);
    }
    tp_sample_t *s = &ss->slot[slot];
    free(s->data);
    free(ss->mip[slot]);
    memset(s, 0, sizeof *s);
    ss->mip[slot] = malloc(sizeof(float) * NMIP * TP_WLEN);
    if (ss->mip[slot]) build_mips(ss->mip[slot], amp, phs);
    s->loop = 1;
    s->rate = TP_WAVE_HZ * TP_WLEN / SR;
    s->len = TP_WLEN;
    s->user = s->ready = 1;
    ss->user++;
}
static void load_cycles(tp_samples_t *ss, const wav_t *w) {
    uint32_t edges[258];
    int ne = 0;
    if (w->ncue) {
        uint32_t c[257];
        memcpy(c, w->cue, sizeof(uint32_t) * w->ncue);
        for (uint32_t i = 1; i < w->ncue; i++) for (uint32_t j = i; j > 0 && c[j - 1] > c[j]; j--) { uint32_t t = c[j]; c[j] = c[j - 1]; c[j - 1] = t; }
        if (c[0] > 0) edges[ne++] = 0;
        for (uint32_t i = 0; i < w->ncue && ne < 257; i++) if (c[i] <= w->frames && (!ne || c[i] > edges[ne - 1])) edges[ne++] = c[i];
        if (edges[ne - 1] < w->frames && w->frames - edges[ne - 1] >= 16) edges[ne++] = w->frames;
    } else {
        if (w->frames % 128) return;
        for (uint32_t i = 0; i <= w->frames / 128 && ne < 258; i++) edges[ne++] = i * 128;
    }
    int slot = TP_FIRST_WAVE;
    for (int e = 0; e + 1 < ne && slot < TP_NSAMPLES; e++) {
        uint32_t a = edges[e], L = edges[e + 1] - a;
        if (L < 16) continue;
        set_cycle(ss, w, a, L, slot);
        slot++;
    }
}
static int slot_for_file(const char *fn) {
    if (isdigit((unsigned char)fn[0])) {
        int k = atoi(fn);
        return k > 0 && k < TP_NSAMPLES ? k : -1;
    }
    size_t l = strlen(fn);
    if (l < 5) return -1;
    for (int k = 1; k < TP_NSAMPLES; k++)
        if (strlen(TP_SAMPLE_NAMES[k]) == l - 4 && !strncasecmp(fn, TP_SAMPLE_NAMES[k], l - 4)) return k;
    return -1;
}
int samples_load_dir(tp_samples_t *ss, const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    int before = ss->user;
    struct dirent *e;
    char path[1024];
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l < 5 || strcasecmp(e->d_name + l - 4, ".wav")) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t *buf = (n > 44 && n < (64L << 20)) ? malloc((size_t)n) : NULL;
        if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) {
            wav_t w;
            if (wav_parse(buf, (size_t)n, &w)) {
                char low[256];
                snprintf(low, sizeof low, "%s", e->d_name);
                for (char *p = low; *p; p++) *p = (char)tolower((unsigned char)*p);
                if (strstr(low, "cycles")) load_cycles(ss, &w);
                else {
                    int k = slot_for_file(e->d_name);
                    if (k == TP_FIRST_WAVE - 1) set_cycle(ss, &w, 0, w.frames, k);      /* the sine slot: the whole file is one cycle */
                    else if (k > 0) load_oneshot(ss, k, &w);
                }
            }
        }
        free(buf);
        fclose(f);
    }
    closedir(d);
    return ss->user - before;
}
