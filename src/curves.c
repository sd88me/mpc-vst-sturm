#include <math.h>
#include "curves.h"

typedef struct { float x, y; } bp_t;

static float lin(const bp_t *b, int n, float x) {
    if (x <= b[0].x) return b[0].y;
    for (int i = 1; i < n; i++)
        if (x <= b[i].x) return b[i - 1].y + (b[i].y - b[i - 1].y) * (x - b[i - 1].x) / (b[i].x - b[i - 1].x);
    return b[n - 1].y;
}
static float loglin(const bp_t *b, int n, float x) {
    if (x <= b[0].x) return b[0].y;
    for (int i = 1; i < n; i++)
        if (x <= b[i].x) return b[i - 1].y * powf(b[i].y / b[i - 1].y, (x - b[i - 1].x) / (b[i].x - b[i - 1].x));
    return b[n - 1].y;
}

/* The DCO period table is exact equal temperament from MIDI note 0 on a 40 MHz timer. */
float tp_note_hz(float note) { return 8.1757989f * exp2f(note / 12.0f); }

/* LFO: round decimal rates up to 89 (the firmware's phase increments give these to 4 digits), then semitones from 8.18 Hz. */
static const bp_t LFO[] = {{0, 0.0333f}, {1, 0.04f}, {13, 0.16f}, {14, 0.18f}, {15, 0.2f}, {16, 0.23f}, {17, 0.26f}, {18, 0.3f},
    {30, 0.9f}, {31, 1.0f}, {38, 1.35f}, {39, 1.45f}, {42, 1.6f}, {76, 5.0f}, {86, 7.0f}, {87, 7.3f}, {88, 7.6f}, {89, 7.7f}};
float tp_lfo_hz(float v) {
    if (v >= 90) return tp_note_hz(v - 90);
    return lin(LFO, sizeof LFO / sizeof LFO[0], v);
}

/* Delay: a tick count (loaded doubled) in round steps; peak hold: the same shape, finer at the bottom. */
static const bp_t DLY[] = {{0, 10}, {49, 500}, {54, 700}, {59, 1000}, {66, 1700}, {67, 1900}, {72, 2400}, {73, 2400}, {74, 2600},
    {79, 3100}, {114, 10100}, {116, 10700}, {117, 11200}, {118, 11400}, {120, 12000}, {125, 14000}, {127, 15000}};
static const bp_t PEAK[] = {{0, 5}, {9, 50}, {39, 350}, {42, 410}, {43, 420}, {47, 500}, {51, 620}, {53, 700}, {57, 900}, {58, 970},
    {59, 1020}, {60, 1100}, {66, 1700}, {67, 1900}, {72, 2400}, {73, 2400}, {74, 2600}, {79, 3100}, {114, 10100}, {116, 10700},
    {117, 11200}, {118, 11400}, {120, 12000}, {125, 14000}, {127, 15000}};
float tp_env_delay_s(float v) { return 2 * lin(DLY, sizeof DLY / sizeof DLY[0], v) / TP_CTL_HZ; }
float tp_env_peak_s(float v) { return lin(PEAK, sizeof PEAK / sizeof PEAK[0], v) / TP_CTL_HZ; }

/* Attack and decay close a fraction r/2^21 of the remaining distance every tick; this is 1/(r/2^21 * 5000), within 3 %. */
static const bp_t TAU[] = {{0, 0.0001667f}, {1, 0.0003937f}, {2, 0.0005155f}, {3, 0.0007463f}, {5, 0.001316f}, {9, 0.002778f},
    {12, 0.004167f}, {16, 0.00625f}, {19, 0.00999f}, {25, 0.01781f}, {39, 0.04312f}, {43, 0.0512f}, {55, 0.08192f}, {61, 0.113f},
    {69, 0.156f}, {73, 0.1638f}, {88, 0.2621f}, {97, 0.4096f}, {102, 0.5699f}, {110, 0.8738f}, {115, 1.311f}, {118, 1.872f},
    {119, 2.185f}, {123, 3.277f}, {126, 5.243f}, {127, 6.554f}};
float tp_env_tau_s(float v) { return loglin(TAU, sizeof TAU / sizeof TAU[0], v); }

/* Glide: a per-tick step of G/2 in 1/256 semitone. */
static const bp_t GL[] = {{1, 700}, {2, 600}, {3, 500}, {8, 350}, {27, 160}, {32, 128}, {35, 114}, {39, 98}, {47, 82}, {127, 2}};
float tp_glide_semis_s(float v) { return lin(GL, sizeof GL / sizeof GL[0], v) * 0.5f / 256.0f * TP_CTL_HZ; }

/* The analog filters are circuits: these are models, not measurements. Lowpass: semitones on the oscillators' scale (0 = 8.18 Hz):
 * the factory kicks that are a self-oscillating filter (all oscillators off, resonance 127, cutoff 30-52) then ring at 46-150 Hz,
 * and key amount 64 tracks a semitone per note (manual p. 32). Highpass: 0 passes all, 127 near 20 kHz. */
float tp_lpf_hz(float v) { return 8.1757989f * exp2f(v / 12.0f); }
float tp_hpf_hz(float v) { return 12.0f * exp2f(v * (10.7f / 127.0f)); }
