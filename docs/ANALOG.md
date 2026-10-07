# The analog half: what is modelled and why

Tempest's voice: two DCOs, a Curtis 2/4-pole low-pass filter, a 2-pole high-pass filter and an analog VCA per voice (DSI's
spec sheet and manual). The digital oscillators are converted to analog before the filters. What follows is how `src/analog.c`
and the voice loop in `src/engine.c` model each part (2026-10-06, offline; no instrument measured yet).

## DCOs (`ma_dco_tick`)

From the voice firmware: each oscillator's period comes from a 32-bit timer at 40 MHz; the timer's interrupt starts a short
second timer (120 cycles, 3 us) that is the reset pulse. That is a reset integrator: a capacitor charged by a constant current
(the ramp), discharged at the end of each period. The model:

- **Ramp** = sawtooth. The 3 us reset is 0.13 of a sample at 44.1 kHz, so it is treated as an instant step, band-limited with a
  2-point polyBLEP (one sample of latency).
- **Triangle** = the ramp rectified (continuous at the reset, so no step).
- **Pulse** = a comparator between the ramp and the pulse-width voltage; at 0 % and 99 % the threshold sits outside the ramp and
  the output goes flat, which is what the manual describes.
- **Saw-tri** = both.
- **Hard sync** = oscillator 2's reset pulse discharging oscillator 1: oscillator 1 jumps from wherever it is to the bottom of
  its ramp, a band-limited step of the right size at the exact sub-sample moment.
- **Sub oscillator** = a flip-flop clocked by oscillator 1's resets (natural or synced): a square an octave down, phase-locked.
- **Pitch** is exact equal temperament (the firmware's period table); slop adds a slow random detune (0-5).

## Low-pass (`ma_ota2x`)

A Curtis filter (the CEM3320 family): four one-pole transconductance cells in series, each limiting its own input (a tanh), in
zero-delay feedback (the cascade is solved exactly, no half-sample delay to compensate), with the resonance fed back inverted from the
last cell, or the second in 2-pole mode. It runs at 2x with the cutoff interpolated across the sample and a half-band decimator.
Resonance 0..1 maps to feedback 4.6 res (4-pole, self-oscillates near the top) and 1.2 res (2-pole). Replaced Huovilainen's cascade
(2026-10-07) when the kernels moved into mpc-analog; the old filter's table, for reference, was 60/263/1033/4400/17600 Hz. The stages'
limiting lowers the pitch a little, so the cutoff is raised in proportion to res^4 (measured correction, 1.15 semitones at low cutoffs):

| Setting | Self-oscillation | Expected |
|---|---|---|
| 36 | 64 Hz | 65 Hz |
| 60 | 259 Hz | 262 Hz |
| 84 | 1035 Hz | 1047 Hz |
| 108 | 4.16 kHz | 4.2 kHz |
| 130 | 14.8 kHz | 14.9 kHz |

- **4-pole**: resonance reaches self-oscillation near the top of the range (needed: 13 factory kicks are only a ringing filter,
  all oscillators off, resonance 127).
- **2-pole**: the output and feedback are taken after the second cell, which can't self-oscillate ("in 2-pole mode resonance is
  much more subtle", manual p. 32).
- **Cutoff scale**: semitones from 8.18 Hz (the oscillators' scale). An inference, not a measurement: it puts those factory kicks
  at 46-150 Hz, and key amount 64 then tracks one semitone per note.
- **Noise floor and gate leak**: a tiny noise at the input and a small step into the first cell at each gate, as a real voice has
  from its circuit noise and CV feedthrough, so a fully resonant filter starts ringing at once.
- **Audio mod**: oscillator 1 sweeps the cutoff exponentially.

## High-pass, feedback, VCA

- **High-pass**: a 2-pole state-variable filter (Q 0.71); 0 bypasses it. Its cutoff curve is a model; the firmware's CV curve
  (0x132A) is known but not the CV-to-Hz law.
- **Feedback**: the left output back into the filter input, inverted (in phase it damps the resonance, and five factory kicks are
  oscillators-off sounds made by this loop alone, so the loop must add to the resonance). Its gain follows 7 x v^1.5 (v = level
  0-1): a mild fuzz at low settings, the factory kicks' 72-115 ring on it alone, high settings squeal.
- **VCA**: soft saturation (tanh) when driven hard.

## What would make it closer

Recordings of one Tempest voice: a filter sweep at resonance 0/64/127 in both pole modes with a saw input, self-oscillation pitch
against the cutoff value, oscillator waveforms at a few pitches, a feedback sweep. Each one replaces a guess above with a fit.
