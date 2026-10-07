#include <math.h>
#include "analog.h"

#define FS 44100.0f

static float ftanh(float x) { x = x < -3 ? -3 : x > 3 ? 3 : x; return x * (27 + x * x) / (27 + 9 * x * x); }

/* ---------------- Curtis low-pass ---------------- */
/* one cell: y += g (tanh(x) - tanh(y)), the tanh of each state kept from the last step */
float cem_tick(cem_t *f, float in, float hz_prev, float hz, float res, int four) {
    float outsum = 0;
    for (int k = 0; k < 2; k++) {
        float fc = k ? hz : 0.5f * (hz_prev + hz);
        if (fc > 0.42f * 2 * FS) fc = 0.42f * 2 * FS;
        if (fc < 5) fc = 5;
        /* tuning and resonance compensation for this cell cascade (Huovilainen, DAFx 2004), at the oversampled rate */
        float x1 = 6.2831853f * fc / (2 * FS);
        float g = x1 * (0.9892f + x1 * (-0.4342f + x1 * (0.1381f - 0.0202f * x1)));
        float rc = 1.0029f + x1 * (0.0526f + x1 * (-0.0926f + 0.0218f * x1));
        if (g > 0.99f) g = 0.99f;
        float fb, x;
        if (four) {
            fb = 0.5f * (f->s[3] + f->fb_prev);    /* half-sample delay compensation keeps the self-oscillation in tune */
            f->fb_prev = f->s[3];
            x = in * (1 + 0.6f * res) - 4.6f * rc * res * fb;
        } else {
            fb = 0.5f * (f->s[1] + f->fb_prev);
            f->fb_prev = f->s[1];
            x = in * (1 + 0.3f * res) - 1.7f * rc * res * fb;
        }
        float tx = ftanh(x);
        int n = four ? 4 : 2;
        for (int i = 0; i < n; i++) {
            f->s[i] += g * (tx - f->t[i]);
            f->t[i] = ftanh(f->s[i]);
            tx = f->t[i];
        }
        if (!four) { f->s[2] = f->s[3] = f->t[2] = f->t[3] = 0; }
        outsum += four ? f->s[3] : f->s[1];
    }
    return 0.5f * outsum;
}
