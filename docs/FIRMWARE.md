# What the Tempest's firmware and dumps hold

Findings from the public update files (main OS 1.5.0.2, voice OS 1.5, panel 1.3, SAM 1.1) and the factory sound and project
dumps (1.4), decoded offline on 2026-10-06 with `tools/fw/tp_fw.py` and `tools/fw/dspic_dis.py`. Nothing here was checked on a
running instrument. No firmware bytes, decoded images or tables are committed: the tools rebuild every number below from your
own files.

## 1. The update format

`F0 01 28 <target> <payload> F7`: DSI's id 01, product 0x28. The payload is the "packed MS bit" format restarted every 1171 MIDI
bytes (1024 decoded bytes), as on the Poly Evolver; each image ends with one extra byte.

| Target | File | Decoded | What it is |
|---|---|---|---|
| 0x71 | Main 1.5.0.2 | 524 289 | PIC32 (MIPS32, little endian), loaded at 0x9D000000: UI, sequencer, file system, every string |
| 0x72 | Voice 1.5 | 65 537 | dsPIC33 (24-bit words stored as 3 bytes): the voice controller (DCO timers, envelopes, LFOs, CVs) |
| 0x73 | Panel 1.3 | 65 537 | PIC32: the front panel |
| 0x74 | SAM 1.1 | 32 769 | code for the sample playback chip (not decoded; no sample data) |

**The samples are in none of these files.** The main OS shows "SAM Sample Bank Loading" for a separate sample-bank update that
is not public; the plugin plays stand-ins (README).

## 2. The main CPU's tables (`tp_fw.py params / samples`)

- **Parameter descriptor table**: 20-byte records `(type, max, default, name ptr, short name ptr)`, found by content (the first
  oscillator's Frequency: type 7, max 120, default 36). `type` is the field's **bit width** in a sound record. Records 77-203
  are the 127 stored sound fields; 204-223 the name (20 x 7-bit, default "Basic"); after them display-only fields (the 14-bit
  sample select 0-463 and others).
- **Sample list**: a pointer table of 464 names starting "Osc Off", "White Noise" (the manual's list, p. 80-84). Sound value v
  is name v: 0 = off. Checked against the factory sounds' names ("909 Kick 01" uses 37 = "909ish", "Tight Low Kick" 368 =
  "Sine"). Slots 368-463 are the Prophet VS waves in VS order ("VS 23" at 391).
- **Enum and modulation lists**: LFO shapes, sync rates (16), glide modes, restart modes (Off/Note/Beat/Play), key assign
  modes, the 22 mod sources and 57 destinations (the manual's lists, in the same order).

## 3. The sound format (`tp_fw.py sounds`)

- **Sound dump**: `F0 01 28 63 <n> <packed> F7`, n = length of the file path that opens the data (`/S/Kicks/Ouch Kick`), then a
  128-byte record. **Project dump**: `F0 01 28 61 <n> <packed> F7`, the project file; its 16 beats x 32 sounds are 128-byte
  records in a row (512 per project).
- **Record**: a little-endian bit stream: the 127 fields at their descriptor widths (764 bits), 116 reserved bits (zero in every
  factory sound), the name as 20 x 7-bit characters from bit 880. All 416 factory sounds (1.4) decode with every field in range
  but four (Amp Release 124-127 against a maximum of 123; Key Assign 3 against 1): the engine clamps.
- Notable fields: digital oscillator pitch is value - 64 (the factory sounds stay in 40-88, the manual's -24..+24); "Env
  Sustain" (field 57) is the inverse of AD mode; the sample select is a number 0-127 plus a bank 0-4; field 124 is always 2
  (unknown).

## 4. The voice CPU (`tp_fw.py tables`, `dspic_dis.py`)

The voice receives the sound as a byte array in descriptor order (0x181A + field) and the modulation sums as 16-bit words
(0x119A + 2 x destination). Its main loop writes the CVs through a multiplexed DAC (port bits on 0x2D4) and calls the LFOs and
envelopes once per pass.

| Voice 1.5 address | What | Evidence | Engine |
|---|---|---|---|
| 0x0326 | 128 DCO periods, 32-bit, for a 40 MHz timer: note 0 = 8.1758 Hz, exact equal temperament | Written to PR2/PR3 and PR4/PR5 (Timer 2/3 and 4/5 in 32-bit mode) | `tp_note_hz`: value = MIDI note, 0 = C0 |
| | Timer 3/5 interrupts start Timer 1 (PR1 = 120 cycles = 3 us) and set port bits: the reset pulse of each oscillator | ISRs at 0x64BA, 0x64CA | `analog.c`: the DCO is a reset integrator |
| 0x0526 | Envelope **delay**: ticks (loaded doubled), 10 ... 15 000 | State 2 of the envelope routine 0x1BA6 | `tp_env_delay_s`: 4 ms ... 6 s |
| 0x0626 | **Peak hold**: ticks, 5 ... 15 000 | State 7 | `tp_env_peak_s`: 1 ms ... 3 s |
| 0x0726 | Attack/decay/release **rate**: per tick the level moves r x distance / 2^21 (32x32 multiply at 0x6F78); attack aims at full scale and ends at 0x7E800000 (98.8 %), decay at sustain + 0xC000 (AD mode: then 0 and the envelope stops), release at half the decay rate | States 3, 4, 6 | `tp_env_tau_s` (26 breakpoints, within 3 %) |
| 0x0926 | **LFO** phase increments for rates 0-162; the phase wraps at 2^27 | LFO routine 0x1610 | `tp_lfo_hz`: 5 kHz loop gives 0.0333 Hz (30 s) at 0, round decimals to 7.7 Hz at 89, semitones from 8.18 Hz at 90 to 523 Hz at 162, as the manual says |
| 0x0F26 | **Glide**: G/2 per tick in 1/256 semitone (value 1 about 6800 semitones/s, 127 about 20) | 0x4AF4 | `tp_glide_semis_s` |
| 0x0200 | Semitone ratios over two octaves | | |
| 0x1060 | A small random walk (+-5) | | slop (`P_SLOP`) |
| 0x142A | An S-curve indexed by the mixer's pan (field 214 of the voice's array, not a sound field) | 0x44A4 | pan is a host parameter |
| 0x132A | High-pass CV curve indexed by HP Freq | 0x5116 | `tp_hpf_hz` (model) |
| 0x122A | A 127-entry curve 1200-16000 near the low-pass code | 0x4F9C | not identified |

The **control rate is 5 kHz**: only that rate makes the LFO table's increments round decimal frequencies (8.1758 x 2^27 /
increment[90] = 5000). The engine runs its control tick every 9 samples (4.9 kHz) with per-tick coefficients from the real
time constants.

**Modulation scale**: every routine that reads a modulation sum shifts it right 8 bits before adding it to the parameter
(LFO rate index, envelope time index, pitch in 1/256 semitone), so a full-scale source at amount 127 moves a destination by 127
of its own steps. The engine uses that rule for every destination.

**One disagreement**: the envelope code subtracts the time modulation from the time value (positive modulation = shorter);
the manual's figures (p. 37) show a positive amount making the decay longer. The engine follows the manual; worth checking on
an instrument.

## 5. What it would take to go further

- **The sample bank.** The single biggest gap: with a dump of the SAM sample bank update (or recordings of the samples), the
  engine would play the real samples (`SAMPLES/` already takes WAVs by number or name).
- **Read more of the voice code** with `dspic_dis.py`: per-destination modulation scaling in the CV writer, the shape-dependent
  level compensation (0x120E/0x140E tables, written by the calibration routine), the 0x122A curve, Key Assign.
- **Measurements of an instrument** for the analog half (docs/ANALOG.md): filter sweeps at several resonances, oscillator
  recordings, the feedback loop.
