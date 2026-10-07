# Status (2026-10-06)

## Done (offline)
- Firmware decoded: update format and chips, the main CPU's parameter descriptor table (field widths, ranges, defaults),
  sample names, modulation lists; the sound and project dump format; the voice CPU's control loop and tables (docs/FIRMWARE.md;
  `tools/fw/tp_fw.py`, `tools/fw/dspic_dis.py`).
- Engine (`src/`): the 127-field sound, 6 voices (1-8), poly or mono with the six key priorities, glide (four modes, per oscillator),
  slop; DCO models with band-limited sync and sub (docs/ANALOG.md), two sample oscillators with reverse, Pre/Post and stand-in
  samples; a Curtis-style 2/4-pole low-pass that self-oscillates in tune, audio mod, key tracking; 2-pole high-pass; inverted
  feedback loop; VCA; five envelopes with the firmware's delay/peak tables, exponential segments and AD mode; two LFOs with the
  firmware's rates, sync and restart; eight mod paths with the firmware's scaling; pitch bend, wheel, breath, foot, expression,
  pressure, sliders (CC 16-19).
- Banks: every `.syx` in the plugin folder or `SYSEX/` (sound dumps and project dumps; 128 sounds per bank). All 416 factory
  sounds (1.4) decode and load; 411 make sound at C3.
- Plugin: 152 parameters, a six-tab skin in the browser renderer (`"art": "html"`; cues from the instrument's panel: charcoal plate,
  walnut cheeks, black knurled knobs with metal caps, blue LEDs and LCD; a signal-path drawing on FILTER), state chunk "TP2" (sound,
  host settings and the kit page's 16 sounds; "TP1" still loads).
- Kit mode (2026-10-06): 16 notes play 16 sounds of the bank (a project's beat), each at its own pitch, sharing the voices; edits
  write through to the bank; tested offline with a factory project.
- Tests: `test/test_engine.c` passes under ASan (pitch, curves, decay timing, sound record and state round trips, every sample
  slot, the factory folder). `tools/test_port.sh` passes every check but one (below). armhf build OK (glibc 2.27).

## Known limits
- **No real samples.** The instrument's sample bank is not in any public file; oscillators 3/4 play stand-ins of our own unless
  you put WAVs in `SAMPLES/`. The plain-signal stand-ins (ten noises, the sine, the 96 waves) also ship as WAVs (`standins/`,
  `tools/render_samples.c`, checked by the test); the drum and percussion ones are generated in code only. How much depends on
  it (counted with the sample, level and bank fields, 2026-10-07): in the 1.4 factory sounds 119 of 416 use a PCM sample (111 of
  them a noise, which is covered) and about 20 a recognisable drum hit; 32 more use only the VS waves. In the 1.0 factory sounds
  324 of 452 use a PCM sample (about 200 distinct, mostly drum machine hits, hats and cymbals), so those sound least like the
  original. The 1.0 and 1.4 sample lists are the same (the names land on sensible sounds).
- The 1.0 factory sounds decode, but `key_assign` runs to 4 in 231 of 452 (1.5 firmware max 1): its meaning changed between
  versions and is not mapped yet; `aenv_r` is 127 against a maximum of 123 in 3.
- Analog parts are modelled from their circuits, not measured (docs/ANALOG.md): cutoff scale, high-pass law, feedback gain,
  resonance range.
- Silent at C3: Dat Kick, Alien Loop and three others: they depend on modulation of pulse widths at 0/99 or on loop gains not
  yet matched.
- Envelope time modulation follows the manual's direction; the firmware's arithmetic suggests the opposite (docs/FIRMWARE.md).
- `test_port.sh`: "six data wheel clicks step six" fails on Osc1 Freq (0-120): the wrapper rounds a 1.2-step click to 2 (the
  same wrapper limit Morpho-PE records).
- Not done: the sequencer (out of scope), Beat-wide parameters, the mixer's delay/distortion/compressor, NRPN, writing sounds
  back as SysEx. Not run on a device.

## Next steps
1. Device: install, bench (mpc-vst-plugins docs/BENCH.md), play, save/reload a project.
2. Listen against the hardware (or recordings) and fit the analog guesses.
3. Kit mode on a device: MPC's pad notes, choke groups (the instrument's Choke 1/2 are beat settings, not in the sound), per-pad pan.
4. README screenshots, catalog entry (`release.py --repo sd88me/mpc-vst-sturm --license MIT --id sturm --extra standins:SAMPLES`).
