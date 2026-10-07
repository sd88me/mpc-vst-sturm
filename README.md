# Sturm

A six-voice analog-style drum and synth voice for Akai MPC OS standalone devices (Force, MPC Live / One / X / Key), built as a
native VST2 instrument with its own screen skin and Q-Link pages. It plays the way the DSI Tempest's voice does: two analog
oscillators (saw, triangle, saw-tri, pulse) with sync and a sub oscillator, two sample oscillators that can bypass the filters,
a Curtis-style 2/4-pole low-pass that rings on its own, a 2-pole high-pass, feedback, five envelopes with delay and peak hold,
two LFOs and eight modulation paths. A sound is the instrument's own 127 fields, so Tempest sound and project dumps load as
they are.

*Sturm (German for storm) is an independent project, not affiliated with or endorsed by Dave Smith Instruments or Sequential.*

**Status: development build, offline only.** See [docs/STATUS.md](docs/STATUS.md).

## How it relates to the original

This is **not an emulation of the instrument's firmware**. It is a new engine built on the instrument's data model and on what
its firmware and manual say:

- **The sound format is the original's**: the 127 fields in their stored order and bit widths, read from the main OS's own
  descriptor table ([docs/FIRMWARE.md](docs/FIRMWARE.md)).
- **The control side follows the voice firmware**: the DCO pitch table, the envelopes' delay, peak and rate tables and their
  exponential segments, the LFO rates, glide and the modulation scaling, all at the voice CPU's 5 kHz control rate. The engine
  uses formulas and breakpoints fitted to them; no firmware data is in this repository or the plugin.
- **The analog half is modelled on its circuits** ([docs/ANALOG.md](docs/ANALOG.md)): reset-integrator DCOs with band-limited
  sync and sub, a CEM3320-style zero-delay-feedback cell cascade with in-tune self-oscillation (kernels shared with Morpho-PE via Morpho-PE's `analog/` header), high-pass, feedback and VCA. Not yet measured
  against an instrument.

## Your own sounds and samples

The plugin makes `SYSEX` and `SAMPLES` folders inside its own folder on first load.

- **Sounds**: put Tempest `.syx` files in `SYSEX` (or next to the plugin): sound dumps and project dumps (a project's sounds become
  banks of 128). Without files, a bank of this project's own sounds plays.
- **Samples**: the instrument's samples are not in its OS files. Oscillators 3/4 start with stand-ins of this project's own (noises,
  drum and percussion one-shots, single-cycle waves), chosen by each slot's group and name. Replace any slot with a WAV in
  `SAMPLES`: `037 anything.wav` (slot number) or `909ish.wav` (slot name); a WAV with "cycles" in its name and one cycle between cue
  points fills the 96 wave slots (Prophet VS order) in turn.
  The release ships the stand-ins that are plain signals as WAVs in `SAMPLES` (the ten noises, the sine and the 96 waves; the
  `standins/` folder, made by `tools/render_samples.c`) so you can open, edit or overwrite them. The plugin sounds the same without them.

## Using it

Six tabs: **SOUND** (sound and bank, the voice settings, a panel row of the most used controls, kit mode), **OSC** (oscillators
1-4), **FILTER** (low pass and its envelope, high pass, feedback, amp, a drawing of the voice's signal path), **ENV** (amp, pitch,
aux 1, aux 2 envelopes), **LFO**, **MODS** (eight paths). A note at the root note (C3 by default) plays the sound at its own pitch.

**Kit mode** plays 16 sounds from one instance, as the instrument's pads play a beat's sounds: the 16 notes from Kit Notes (C1 =
MPC's first pad by default) play the bank's sounds of the Kit Page (1-16, 17-32, ...), each at its own pitch, sharing the voices.
Load a project dump and its beats' kits are the bank's pages (32 sounds a beat: A1-A16, then B1-B16). "On + Select" also makes the
last struck pad the sound you edit. Edits stay with their sound, and the saved project keeps the kit page's 16 sounds.

MIDI: notes, velocity, pitch bend, mod wheel (1), breath (2), foot pedal 1 (4) and 2 (12), volume (7), expression (11), sliders
1/2 position and pressure (16-19), sustain (64), channel and poly pressure, program change, bank select (32).

## Building

Needs a checkout of [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) next to this repo, Python 3 and Docker.

```
tools/make_layout.sh                                         # params.json, src/patch_tab.h, the skin layout
../mpc-vst-plugins/tools/test_port.sh vst/vst.json           # offline host test (ASan)
../mpc-vst-plugins/tools/build_port.sh vst/vst.json          # armhf .so + skin
```

Engine tests: see the first lines of `test/test_engine.c`. Firmware tools: `tools/fw/tp_fw.py` (works on your own files).
