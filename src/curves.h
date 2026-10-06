/* Parameter-to-physical curves. Each one is a formula or a short breakpoint list fitted to the Tempest voice OS 1.5 tables
 * (docs/FIRMWARE.md section 4 has the measurements; tools/fw/tp_fw.py tables re-derives them). No firmware data is copied. */
#pragma once
#define TP_CTL_HZ 5000.0f                /* the voice CPU's control loop: LFOs and envelopes step at 5 kHz */
float tp_note_hz(float note);            /* DCO: 0 = C0 = 8.1758 Hz (MIDI note 0), semitone steps */
float tp_lfo_hz(float v);                /* LFO rate 0..162 (unsynced): 30 s at 0, 8.18 Hz at 90, semitones to 523 Hz at 162 */
float tp_env_delay_s(float v);           /* envelope delay 0..127 */
float tp_env_peak_s(float v);            /* peak hold 0..127 */
float tp_env_tau_s(float v);             /* attack/decay time constant 0..127 (release: x2) */
float tp_glide_semis_s(float v);         /* glide 1..127: semitones per second (FixRate) */
float tp_lpf_hz(float v);                /* lowpass cutoff 0..164 (semitones from 8.18 Hz, model) */
float tp_hpf_hz(float v);                /* highpass cutoff 1..127 (model) */
