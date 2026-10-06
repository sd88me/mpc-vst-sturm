#include <math.h>
#include "analog.h"

#define FS 44100.0f

static float ftanh(float x) { x = x < -3 ? -3 : x > 3 ? 3 : x; return x * (27 + x * x) / (27 + 9 * x * x); }

/* ---------------- DCOs ---------------- */
/* tri from the ramp: 1 at the reset, -1 half way (continuous at a natural reset) */
static float tri(float p) { return 2 * fabsf(2 * p - 1) - 1; }
static float shape_out(const dco_osc_t *o, float p) {
    switch (o->shape) {
    case 1: return 2 * p - 1;
    case 2: return tri(p);
    case 3: return 0.5f * (2 * p - 1 + tri(p));
    case 4: return p < o->duty ? 1.0f : -1.0f;
    default: return 0;
    }
}
static void step_at(float h, float t, float *rp, float *rn) {
    /* polyBLEP: the sample before the step gets +h/2 (1-s)^2 with s its distance to the step (s = 1 - t), the sample after it
     * gets -h/2 (1-t)^2 */
    *rp += h * 0.5f * t * t;
    *rn -= h * 0.5f * (1 - t) * (1 - t);
}

void dco_tick(dco_t *d, const dco_osc_t *o1, const dco_osc_t *o2, int sync, float out[3]) {
    float rn[3] = {0, 0, 0};
    /* osc 2 first: its reset may discharge osc 1 */
    float p2 = d->ph[1] + o2->inc, t2 = -1;
    if (o2->shape == 4 && d->ph[1] < o2->duty && p2 >= o2->duty) step_at(-2, (p2 - o2->duty) / o2->inc, &d->r[1], &rn[1]);
    if (p2 >= 1) {
        p2 -= 1;
        t2 = p2 / o2->inc;          /* samples since the reset */
        float before = shape_out(o2, 1.0f - 1e-7f), after = shape_out(o2, 0);
        if (after != before) step_at(after - before, t2, &d->r[1], &rn[1]);
        if (o2->shape == 4 && p2 >= o2->duty) step_at(-2, (p2 - o2->duty) / o2->inc, &d->r[1], &rn[1]);
    }
    d->ph[1] = p2;
    /* osc 1: its own reset, or osc 2's */
    float p1 = d->ph[0] + o1->inc;
    if (sync && t2 >= 0) {
        float at = p1 - o1->inc * t2;              /* where osc 1 was when osc 2 reset */
        if (at >= 1) {                             /* it reset on its own first */
            at -= 1;
            float tn = (p1 - 1) / o1->inc;
            float jb = shape_out(o1, 0) - shape_out(o1, 1.0f - 1e-7f);
            if (jb != 0) step_at(jb, tn, &d->r[0], &rn[0]);
            step_at(d->flip ? -2.0f : 2.0f, tn, &d->r[2], &rn[2]);
            d->flip ^= 1;
        } else if (o1->shape == 4 && d->ph[0] < o1->duty && at >= o1->duty)
            step_at(-2, t2 + (at - o1->duty) / o1->inc, &d->r[0], &rn[0]);
        float j = shape_out(o1, 0) - shape_out(o1, at);
        if (j != 0) step_at(j, t2, &d->r[0], &rn[0]);
        step_at(d->flip ? -2.0f : 2.0f, t2, &d->r[2], &rn[2]);
        d->flip ^= 1;
        p1 = o1->inc * t2;
        if (o1->shape == 4 && p1 >= o1->duty) step_at(-2, (p1 - o1->duty) / o1->inc, &d->r[0], &rn[0]);
    } else {
        if (o1->shape == 4 && d->ph[0] < o1->duty && p1 >= o1->duty) step_at(-2, (p1 - o1->duty) / o1->inc, &d->r[0], &rn[0]);
        if (p1 >= 1) {
            p1 -= 1;
            float t1 = p1 / o1->inc;
            float jb = shape_out(o1, 0) - shape_out(o1, 1.0f - 1e-7f);
            if (jb != 0) step_at(jb, t1, &d->r[0], &rn[0]);
            step_at(d->flip ? -2.0f : 2.0f, t1, &d->r[2], &rn[2]);
            d->flip ^= 1;
            if (o1->shape == 4 && p1 >= o1->duty) step_at(-2, (p1 - o1->duty) / o1->inc, &d->r[0], &rn[0]);
        }
    }
    d->ph[0] = p1;
    /* emit the previous sample with its corrections, keep this one */
    float now[3] = {shape_out(o1, p1), shape_out(o2, p2), d->flip ? 1.0f : -1.0f};
    for (int k = 0; k < 3; k++) {
        out[k] = d->y[k] + d->r[k];
        d->y[k] = now[k];
        d->r[k] = rn[k];
    }
}

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
