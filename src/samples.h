/* Oscillator 3/4 samples: 463 slots in the instrument's order (0 = off).
 *
 * The instrument's sample audio is not in its OS files (it comes in a separate sample-bank update that is not public), so every
 * slot starts as a stand-in of this project's own, synthesised on first use from the slot's group and name: the noises, drum
 * and percussion one-shots, and the 96 single-cycle wave slots (368-463, Prophet VS wave n at slot 368 + n).
 * Your own audio replaces slots, read from the plugin's SAMPLES folder at load:
 *   - "<number> anything.wav" (e.g. "037 my 909.wav") or "<sample name>.wav" (e.g. "909ish.wav"): that slot, as a one-shot
 *     (a looping slot loops the whole file);
 *   - "367 anything.wav" (the sine slot): the whole file is one cycle, looped at C3;
 *   - the Prophet VS's program ROM chips (two 16/32 KB .bin or .rom files, or one 64 KB image): the 95 ROM waves replace the wave
 *     slots, ROM waves 0-4 at slots 368-372, wave j (5-94) at slot 369 + j. Only the first three (sine, saw, square) are checked against the instrument's list:
 *     the rest of the map is a guess (docs/STATUS.md), and "vsmap.txt" ("<wave slot 0-95> <rom wave 0-94 or -1>" per line) overrides it;
 *   - a WAV of single cycles (cue points between cycles, or 128-sample cycles) whose name contains "cycles": the wave slots in order.
 * Audio is 16-bit at 44.1 kHz inside; WAVs are resampled (linear) and mixed to mono. */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define TP_NSAMPLES 464
#define TP_FIRST_WAVE 368
#define TP_WLEN 256                     /* single cycles are kept as 256 points */
#define TP_WAVE_HZ 130.8128f            /* a wave slot at unity pitch plays C3, like the "Sine 130.81 Hz" sample */

extern const char *const TP_SAMPLE_NAMES[TP_NSAMPLES];

typedef struct {
    int16_t *data;
    int len;            /* frames */
    float rate;         /* playback increment at unity pitch (source rate / 44100, or the cycle rate for waves) */
    uint8_t loop;       /* loops the whole buffer (noises, waves) */
    uint8_t fixed;      /* ignores pitch (the noises) */
    uint8_t user;       /* loaded from a WAV */
    uint8_t ready;
} tp_sample_t;

typedef struct tp_samples tp_samples_t;
tp_samples_t *samples_open(void);
void samples_close(tp_samples_t *s);
/* The slot, synthesised on the calling thread if it is not built yet (blocking: not for the audio thread). */
const tp_sample_t *samples_get(tp_samples_t *s, int slot);
/* For the audio thread: the slot if it is built, else an empty one (silence) after asking the worker thread to build it. Never blocks. */
const tp_sample_t *samples_peek(tp_samples_t *s, int slot);
/* Asks the worker thread to build a slot (the latest request goes first). Never blocks. */
void samples_request(tp_samples_t *s, int slot);
/* Slots queued or being built. */
int samples_pending(tp_samples_t *s);
/* Scans a folder for WAVs as described above; returns slots replaced. */
int samples_load_dir(tp_samples_t *s, const char *dir);
/* The band-limited table (TP_WLEN points) of a wave slot for a phase increment of inc cycles per sample; NULL if the slot is
 * a one-shot or loop of audio, or not built yet. */
const float *samples_wave(tp_samples_t *s, int slot, float inc);
int samples_user_count(const tp_samples_t *s);
