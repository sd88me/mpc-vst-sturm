/* The voice's analog half, modelled on its circuits (docs/ANALOG.md); the kernels are in mpc_analog.h (a copy of `analog/mpc_analog.h` in the Morpho-PE repo; update it with that repo's `analog/sync.sh`).
 *
 * DCOs (ma_dco_tick). The voice CPU runs each oscillator from a 32-bit timer at 40 MHz (period = the firmware's table entry) and, at the
 * end of every period, starts a second timer that holds a ~3 us reset pulse (voice OS 1.5: Timer 3/5 interrupts start Timer 1, PR1 = 120
 * cycles). So the core is a ramp (an integrator charged by a constant current, discharged by the reset); the shapes are taken from
 * that ramp: sawtooth = the ramp, triangle = the ramp rectified, pulse = a comparator against the pulse-width voltage (so 0 % and
 * 99 % sit outside the ramp and go flat), saw-tri = both. Hard sync is oscillator 2's reset pulse discharging oscillator 1, and the
 * sub oscillator is a flip-flop clocked by oscillator 1's resets (a square an octave down, locked to it). Every jump is
 * band-limited (2-point polyBLEP, one sample of latency).
 *
 * Low-pass (ma_ota2x). A Curtis 2/4-pole filter (the CEM3320 family): four transconductance one-pole cells in zero-delay feedback, each
 * limiting its own input, with the resonance fed back inverted from the 4th cell (4-pole) or the 2nd (2-pole, where it can't
 * self-oscillate), run at twice the sample rate. */
#pragma once
#include "mpc_analog.h"
