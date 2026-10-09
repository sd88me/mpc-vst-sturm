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
- Plugin: 214 parameters, an eight-tab skin (BANKS: bank and sound lists with Q-Link and data wheel control) in the browser renderer (`"art": "html"`; cues from the instrument's panel: charcoal plate,
  walnut cheeks, black knurled knobs with metal caps, blue LEDs and LCD; a signal-path drawing on FILTER), state chunk "TP2" (sound,
  host settings and the kit page's 16 sounds; "TP1" still loads).
- Kit mode (2026-10-06): 16 notes play 16 sounds of the bank (a project's beat), each at its own pitch, sharing the voices; edits
  write through to the bank; tested offline with a factory project.
- Tests: `test/test_engine.c` passes under ASan (pitch, curves, decay timing, sound record and state round trips, every sample
  slot, the factory folder). `tools/test_port.sh` passes every check but one (below). armhf build OK (glibc 2.27).

## Device (2026-10-07, a Force)
- Installed and registered; plays and the skin draws (the user's first listen).
- Bench (mpc-vst-plugins docs/BENCH.md): voices about 20 % mean, p99 25 % of a block (idle 1.3 %); Q-Link sweep p99 34.7 %, worst
  block 40.5 % -> WARN (a later run: 35.9 % / 39.5 %). Before the fix below the sweep was p99 735 %, worst 3361 % (FAIL): picking a sample synthesised its stand-in
  on the calling thread (up to about 100 ms on the device) and bank changes read files.
- Fix (output bit-identical over all 416 factory sounds): stand-ins are built by a worker thread (`samples_request`; the audio path
  uses `samples_peek`, which never blocks; a note struck before its stand-in is ready plays it silent), and every bank's sounds are
  read once when the folders are scanned. Remaining cost is the voice DSP.
- Tried and dropped (2026-10-07): a selectable Quality "Eco" mode with polynomial exp2/tan for the cutoff (89 dB below the signal
  against Exact). On the Force it gained nothing (voices 20.7 % vs 20.4 % mean, sweep p99 35.8 % vs 35.9 %): the library calls are
  not the cost. Removed. The final build (md5 21446420...) is deployed.


- **VS wave ROM (2026-10-07, offline):** `src/vsrom.c` reads the user's own VS program ROM chips (decoded in the Morpho-PE repo, its
  docs/FIRMWARE.md section 14) into the 96 wave slots. The slot map is a guess (ROM order, `vsmap.txt` overrides): only Sine, Sawtooth
  and Square (slots 0-2) are checked against the ROM; the manual lists names and no wave definitions, and the main OS holds no sample
  table (no address list in the image). A hardware wave dump or a recording of slots 3+ would settle it.
  Second-hand confirmation of the base (2026-10-08, a forum post quoting a DSI engineer's e-mail): "the VS waveforms start on sample Sine (sample number 369) and continue to the end of the list". Number 369 is our 0-based slot 368, so the 96 slots 368-463 are the VS block as assumed; the order inside it is still ROM order. DSI treat the VS waves as a bonus, not made for this instrument. Checked against the ROM's spectra (2026-10-08, offline): the top of the block lines up with ROM wave n-1, not n ("Bell Partials 1/2" at 82 and 94 are ROM waves 81 and 93, the nine unnamed "VS 84-92" are the nine plain-tone waves 83-91, "VS 95" is the noise wave 94; the two "Pinch" and the "Pulse2/3" pairs also match best shifted). So the default map is now slots 0-4 = ROM 0-4, slot 5 none (stand-in), slots 6-95 = ROM n-1. Where the extra slot sits (somewhere in 3-14) is a guess; `vsmap.txt` overrides. Arturia's `waverom.bin` is read directly too. Tested with a synthetic image only.
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
- Silent at C3 (2026-10-07, 416 factory sounds, 8 notes on a fresh instance each): one, Alien Loop (both oscillators are flat pulses
  and the filter's own ring sits at 9 Hz; it plays only with pad pressure). Several more are quiet or silent on some notes because their
  loudness depends on free-running LFOs, Random or velocity (Bit Snare, Sizzle Tubes, Rezzy Beacon, Pad~Funk'Up, ReEntry Delayed,
  8bit Console): that is by their design, not checked on a unit. Horror Toned is a filter ring at resonance 106 and is faint; whether the real
  filter self-oscillates that low is open.
- An envelope's modulation (Env Amt destinations) is added after its velocity amount, not before: with the other order, a negative
  Velocity -> Amp Env Amt path (Plucked Lead, Pressure Wubs) closes the VCA at normal velocity. The order is inferred, not read from
  the firmware; the voice CPU only sums modulation, so the main CPU probably applies velocity first.
- Envelope time modulation follows the manual's direction; the firmware's arithmetic suggests the opposite (docs/FIRMWARE.md).
- `test_port.sh`: "six data wheel clicks step six" fails on Osc1 Freq (0-120): the wrapper rounds a 1.2-step click to 2 (the
  same wrapper limit Morpho-PE records).
- Not done: the sequencer (out of scope), Beat-wide parameters, NRPN, writing sounds
  back as SysEx. Played on a Force once (2026-10-07).

## Next steps
1. Device: install, bench (mpc-vst-plugins docs/BENCH.md), play, save/reload a project.
2. Listen against the hardware (or recordings) and fit the analog guesses.
3. Kit mode on a device: MPC's pad notes, per-pad pan. Choke 1/2 are done (read from a project's beats, section 5 of docs/FIRMWARE.md); Voice Assign is by hand only (its byte in the beat was not found).
4. README screenshots, catalog entry (`release.py --repo sd88me/mpc-vst-sturm --license MIT --id sturm --extra standins:SAMPLES`).

## 2026-10-09: Sturm-TP, output section
- Renamed Sturm-TP (display name, plugin and skin folder `sd88me - VST - Sturm-TP`; uid and `.so` name unchanged).
- OUTPUT tab, ten new host parameters appended (not part of a sound): **Distortion** (tanh stage with first-order antiderivative antialiasing, a little
  even-harmonic bias, DC blocker, a rolloff that drops with drive, level-matched to the clean input so the knob adds sound, not volume; aliasing
  -78 dB against -54 dB for a plain tanh at full drive), **Compress** with the **compressor envelope** (Attack, Peak Hold, Decay, Amount: a note
  trigger runs an attack/hold/decay envelope the detector also hears, so a hit ducks the mix like a side-chain; stereo-linked peak detector,
  soft-knee gain computer in dB, 3 ms attack and 150 ms release on the reduction, half-makeup), and the **delay**: as the manual says (p. 44) it plays
  repeated *notes* rather than delayed audio: Repeats copies of each note, one Time apart (the 16 tempo-synced rates the LFO uses), each quieter,
  the first at Send/127 of the velocity; the note-off repeats too. Not from the firmware: the manual has no numbers for these, so the curves are
  ours. Not done: the compressor's trigger-sound choice and a negative Amount, per-sound Send/On (Send and On are global), Beat-wide sources.
- SOUND page: one stepper (number and name), as Morpho-PE's PROGRAM page. BANKS page: thinner pickers and page controls, clear of the panel
  titles and edges (checked on the Force). Skin: darker plate with lighter grey filled panels, knobs restyled after the panel (no cap, light rim).
- Tests: distortion keeps the level, compress narrows loud against soft, the delay plays a repeat, the output state round-trips.
