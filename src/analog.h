/* The voice's analog half, modelled on its circuits (docs/ANALOG.md):
 *
 * DCOs. The voice CPU runs each oscillator from a 32-bit timer at 40 MHz (period = the firmware's table entry) and, at the end of
 * every period, starts a second timer that holds a ~3 us reset pulse (voice OS 1.5: Timer 3/5 interrupts start Timer 1, PR1 = 120
 * cycles). So the core is a ramp (an integrator charged by a constant current, discharged by the reset); the shapes are taken from
 * that ramp: sawtooth = the ramp, triangle = the ramp rectified, pulse = a comparator against the pulse-width voltage (so 0 % and
 * 99 % sit outside the ramp and go flat), saw-tri = both. Hard sync is oscillator 2's reset pulse discharging oscillator 1, and the
 * sub oscillator is a flip-flop clocked by oscillator 1's resets (a square an octave down, locked to it). Every jump is
 * band-limited (2-point polyBLEP, one sample of latency).
 *
 * Low-pass. A Curtis 2/4-pole filter (the CEM3320 family): four transconductance one-pole cells, each a tanh, with the resonance
 * fed back inverted from the 4th cell (4-pole) or the 2nd (2-pole, where it can't self-oscillate), run at twice the sample rate. */
#pragma once

typedef struct {
    float ph[2];          /* ramp positions 0..1 */
    int flip;             /* sub oscillator flip-flop */
    float y[3], r[3];     /* last naive outputs (osc 1, osc 2, sub) and the BLEP residual owed to them */
} dco_t;

typedef struct {
    int shape;            /* 0 off, 1 saw, 2 tri, 3 saw-tri, 4 pulse, 5 flat pulse */
    float duty, inc;
} dco_osc_t;

/* One sample: osc 1, osc 2, sub (one sample late, band-limited). */
void dco_tick(dco_t *d, const dco_osc_t *o1, const dco_osc_t *o2, int sync, float out[3]);

typedef struct { float s[4], t[4], fb_prev; } cem_t;
/* One output sample from one input sample at 44.1 kHz: cutoff in Hz, res 0..1, four = 4-pole. Oversampled 2x inside. */
float cem_tick(cem_t *f, float in, float hz_prev, float hz, float res, int four);
