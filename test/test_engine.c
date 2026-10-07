#define _GNU_SOURCE
/* Engine checks: every built-in sound plays, pitch is right, envelope timing follows the firmware's curves, the sound format
 * round-trips, state restores; with a folder argument, every sound in its .syx files decodes, loads and plays.
 *   gcc -O1 -g -fsanitize=address,undefined -Isrc -I../mpc-vst-plugins/wrapper -o /tmp/tp_test test/test_engine.c  src/[a-z]*.c -lm -lpthread
 *   /tmp/tp_test [folder with .syx [folder with a project .syx]]   (TP_WAV=dir also writes one WAV per sound there) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "engine.h"
#include "patch_tab.h"
#include "presets.h"
#include "samples.h"
#include "tsnd.h"
#include "curves.h"

static int fails;
#define CHECK(c, ...) do { int ok_ = (c); printf(ok_ ? "ok   " : "FAIL "); printf(__VA_ARGS__); printf("\n"); fails += !ok_; } while (0)

static const mpc_engine_t *E;
static void setp(void *h, const char *k, int v) { char b[16]; snprintf(b, sizeof b, "%d", v); E->set_param(h, k, b); }
static int getp(void *h, const char *k) { char b[64]; return E->get_param(h, k, b, sizeof b) > 0 ? atoi(b) : -1; }
/* stand-ins are built on a worker thread: before a note, wait until the sound's are ready, as a player's first note after a change would be */
static void settle(void *h) { for (int i = 0; i < 4000 && getp(h, "samples_busy") > 0; i++) { struct timespec t = {0, 1000000}; nanosleep(&t, NULL); } }
static void midi3(void *h, int a, int b, int c) { uint8_t m[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c}; if ((a & 0xF0) == 0x90 && c) settle(h); E->midi(h, m, 3); }
static double render(void *h, int blocks, float *peak, int16_t *keep) {
    int16_t o[256];
    double r = 0;
    *peak = 0;
    for (int k = 0; k < blocks; k++) {
        E->render(h, o, 128);
        for (int i = 0; i < 128; i++) {
            r += (double)o[2 * i] * o[2 * i];
            if (abs(o[2 * i]) > *peak) *peak = (float)abs(o[2 * i]);
            if (keep) keep[k * 128 + i] = o[2 * i];
        }
    }
    return sqrt(r / (blocks * 128.0)) / 32768;
}
static double zc_freq(const int16_t *x, int n) {
    int c = 0, first = -1, last = -1;
    for (int i = 1; i < n; i++)
        if (x[i - 1] < 0 && x[i] >= 0) { if (first < 0) first = i; last = i; c++; }
    return c > 1 ? (c - 1) * 44100.0 / (last - first) : 0;
}
static void wav_write(const char *path, const int16_t *x, int n) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    unsigned hdr[11] = {0x46464952, 36 + 2u * n, 0x45564157, 0x20746d66, 16, 0x00010001, 44100, 88200, 0x00100002, 0x61746164, 2u * n};
    fwrite(hdr, 4, 11, f);
    fwrite(x, 2, (size_t)n, f);
    fclose(f);
}
static void set_all(void *h, const char *kv) {   /* "key=value ..." */
    char key[32];
    int val, n;
    while (sscanf(kv, " %31[^=]=%d%n", key, &val, &n) == 2) { setp(h, key, val); kv += n; }
}
static void init_sound(void *h) { for (int i = 0; i < NFIELD; i++) setp(h, PTAB[i].key, PTAB[i].def); }

int main(int argc, char **argv) {
    E = mpc_engine();
    void *h = E->create(NULL);
    float pk;
    static int16_t buf[128 * 1400];
    static char b[9000];
    const char *wavdir = getenv("TP_WAV");

    /* every built-in sound plays without overloading */
    for (int p = 0; p < presets_count(); p++) {
        setp(h, "program", p);
        E->get_param(h, "patch_name", b, sizeof b);
        char name[64];
        snprintf(name, sizeof name, "%.60s", b);
        midi3(h, 0x90, 60, 110);
        double r = render(h, 260, &pk, buf);
        midi3(h, 0x80, 60, 0);
        render(h, 140, &pk, buf + 260 * 128);
        CHECK(r > 0.003 && pk < 32767, "sound %d \"%s\" plays (rms %.3f)", p + 1, name, r);
        if (wavdir) { char path[512]; snprintf(path, sizeof path, "%s/preset_%02d_%s.wav", wavdir, p + 1, name); wav_write(path, buf, 400 * 128); }
    }

    E->destroy(h);
    h = E->create(NULL);
    /* pitch: a sawtooth on osc 1 at C3 (36) with the key at the root plays 65.41 Hz; an octave up from the key doubles it */
    init_sound(h);
    set_all(h, "osc1_shape=1 osc1_freq=36 osc_mix=0 lpf_freq=164 env_gate=1 aenv_s=127 aenv_amt=127");
    midi3(h, 0x90, 60, 127);
    render(h, 40, &pk, NULL);
    render(h, 200, &pk, buf);
    double f1 = zc_freq(buf, 200 * 128);
    midi3(h, 0x80, 60, 0);
    midi3(h, 0xB0, 123, 0);
    render(h, 1000, &pk, NULL);
    midi3(h, 0x90, 72, 127);
    render(h, 40, &pk, NULL);
    render(h, 200, &pk, buf);
    double f2 = zc_freq(buf, 200 * 128);
    midi3(h, 0x80, 72, 0);
    render(h, 100, &pk, NULL);
    CHECK(fabs(f1 - 65.406) < 0.3, "osc 1 at 36 plays %.2f Hz (65.41)", f1);
    CHECK(fabs(f2 - 130.81) < 0.6, "an octave up plays %.2f Hz (130.81)", f2);

    /* curves */
    CHECK(fabsf(tp_lfo_hz(0) - 0.0333f) < 1e-3f && fabsf(tp_lfo_hz(90) - 8.1758f) < 1e-3f && fabsf(tp_lfo_hz(162) - 523.25f) < 0.1f,
          "LFO rate 0 / 90 / 162 = %.4f / %.3f / %.1f Hz", tp_lfo_hz(0), tp_lfo_hz(90), tp_lfo_hz(162));
    CHECK(fabsf(tp_env_delay_s(127) - 6.0f) < 1e-3f && fabsf(tp_env_peak_s(127) - 3.0f) < 1e-3f, "delay / peak hold at 127: %.2f / %.2f s",
          tp_env_delay_s(127), tp_env_peak_s(127));

    E->destroy(h);
    h = E->create(NULL);
    /* decay timing: AD mode, decay 70: time constant from the firmware rate table, so the level falls to 1/e in tau */
    init_sound(h);
    set_all(h, "osc1_shape=54 osc1_freq=60 lpf_freq=164 env_gate=0 aenv_d=70 aenv_amt=127 aenv_vel=0 osc_mix=0");
    midi3(h, 0x90, 60, 127);
    render(h, 345, &pk, buf);         /* 1 s */
    midi3(h, 0x80, 60, 0);
    float tau = tp_env_tau_s(70);
    int i_tau = (int)(tau * 44100), win = 400;
    double a0 = 0, a1 = 0;
    for (int i = 2000; i < 2000 + win; i++) a0 += abs(buf[i]);
    for (int i = 2000 + i_tau; i < 2000 + i_tau + win; i++) a1 += abs(buf[i]);
    CHECK(fabs(a1 / a0 - exp(-1.0)) < 0.08, "amp decay 70 falls to %.2f (1/e = 0.37) in tau = %.3f s", a1 / a0, tau);
    render(h, 400, &pk, NULL);

    E->destroy(h);
    h = E->create(NULL);
    /* the sound format round-trips */
    uint8_t f[NFIELD], g[NFIELD], rec[TSND_BYTES];
    char nm[TSND_NAME], nm2[TSND_NAME];
    for (int i = 0; i < NFIELD; i++) f[i] = (uint8_t)((i * 37 + 11) % (PTAB[i].max + 1));
    tsnd_encode(f, "Round Trip", rec);
    CHECK(tsnd_decode(rec, g, nm2) && !memcmp(f, g, NFIELD) && !strcmp(nm2, "Round Trip"), "sound record round trip");
    (void)nm;

    /* state */
    setp(h, "program", 3);
    setp(h, "lpf_freq", 77);
    E->get_param(h, "state", b, sizeof b);
    setp(h, "program", 1);
    E->set_param(h, "state", b);
    CHECK(getp(h, "lpf_freq") == 77 && getp(h, "program") == 3, "state restores (lpf %d, sound %d)", getp(h, "lpf_freq"), getp(h, "program"));

    /* samples: every slot synthesises, and a held note on each sample group sounds */
    tp_samples_t *sm = samples_open();
    int bad = 0;
    for (int k = 1; k < TP_NSAMPLES; k++) {
        const tp_sample_t *s = samples_get(sm, k);
        if (!s->ready || (s->len < 2 && !samples_wave(sm, k, 0.01f))) bad++;
    }
    CHECK(bad == 0, "all %d sample slots synthesise (%d bad)", TP_NSAMPLES - 1, bad);
    /* the shipped stand-in WAVs (standins/, run from the repo root) load into the same slots and sound like the built-ins */
    tp_samples_t *fl = samples_open();
    int nl = samples_load_dir(fl, "standins");
    if (nl) {
        int diff = 0;
        for (int k = 1; k <= 10; k++) {
            const tp_sample_t *a = samples_get(sm, k), *b = samples_get(fl, k);
            if (!b->user || a->len != b->len || !b->loop || !b->fixed) { diff++; continue; }
            for (int i = 0; i < a->len; i++) if (abs(a->data[i] - b->data[i]) > 2) { diff++; break; }
        }
        float wd = 0;
        for (int k = TP_FIRST_WAVE - 1; k < TP_NSAMPLES; k++) {
            const float *a = samples_wave(sm, k, 0), *b = samples_wave(fl, k, 0);
            if (!a || !b || !samples_get(fl, k)->user) { diff++; continue; }
            for (int i = 0; i < TP_WLEN; i++) wd = fmaxf(wd, fabsf(a[i] - b[i]));
        }
        CHECK(nl == 107 && diff == 0 && wd < 0.02f, "shipped stand-in WAVs: %d slots loaded (want 107), %d differ, wave diff %.4f", nl, diff, wd);
    }
    samples_close(fl);
    /* a Prophet VS program ROM image (built here: a rising table, then 95 waves of 192 bytes) fills the wave slots; vsmap.txt re-maps one */
    {
        char dir[] = "/tmp/tp_vsrom_XXXXXX";
        if (mkdtemp(dir)) {
            static uint8_t img[65536];
            uint8_t *st = img + 32768;
            for (int k = 0; k < 7264; k++) { int w = k == 7263 ? 0xFFFF : 0x028C + (int)((0xFFFF - 0x028C) * (long)k / 7263); st[2 * k] = (uint8_t)(w >> 8); st[2 * k + 1] = (uint8_t)w; }
            for (int j = 0; j < 95; j++)
                for (int k = 0; k < 128; k++) {
                    int v = (int)lrintf(2000.0f * sinf(6.2831853f * (float)(j + 1) * (float)k / 128)), hb = v >> 4, nib = v & 15;
                    st[14528 + 192 * j + k] = (uint8_t)hb;
                    st[14528 + 192 * j + 128 + k / 2] |= (uint8_t)((k & 1) ? nib : nib << 4);
                }
            char path[256];
            snprintf(path, sizeof path, "%s/vs.bin", dir);
            FILE *f = fopen(path, "wb");
            fwrite(img, 1, sizeof img, f);
            fclose(f);
            snprintf(path, sizeof path, "%s/vsmap.txt", dir);
            f = fopen(path, "w");
            fprintf(f, "# wave slot, rom wave\n5 0\n");
            fclose(f);
            tp_samples_t *vs = samples_open();
            int n = samples_load_dir(vs, dir);
            /* slot 3 is ROM wave 3 = four cycles of sine; slot 5 was mapped to ROM wave 0 = one cycle */
            const float *w3 = samples_wave(vs, TP_FIRST_WAVE + 3, 0), *w5 = samples_wave(vs, TP_FIRST_WAVE + 5, 0);
            int c3 = 0, c5 = 0;
            for (int i = 0; w3 && w5 && i < TP_WLEN; i++) { int q = (i + TP_WLEN - 1) % TP_WLEN; c3 += w3[q] < 0 && w3[i] >= 0; c5 += w5[q] < 0 && w5[i] >= 0; }
            CHECK(n == 95 && w3 && w5 && c3 == 4 && c5 == 1 && !samples_get(vs, TP_FIRST_WAVE + 95)->user, "VS ROM image: %d slots loaded, slot 3 has %d cycles (4), mapped slot 5 has %d (1)", n, c3, c5);
            samples_close(vs);
            snprintf(path, sizeof path, "%s/vs.bin", dir); remove(path);
            snprintf(path, sizeof path, "%s/vsmap.txt", dir); remove(path);
            rmdir(dir);
        }
    }
    samples_close(sm);
    /* the BANKS page on the built-in bank: tiles show the sounds, a tap loads one, the page follows */
    {
        char t[64];
        setp(h, "program", 0);
        E->get_param(h, "patch_slot_1", t, sizeof t);
        CHECK(!strncmp(t, "001 ", 4) && getp(h, "patch_slot_1_on") == 1, "BANKS: first tile reads '%s' and is the loaded sound", t);
        setp(h, "patch_slot_4", 1);
        E->get_param(h, "patch_slot_4", t, sizeof t);
        CHECK(getp(h, "program") == 3 && getp(h, "patch_slot_4_on") == 1 && getp(h, "patch_slot_1_on") == 0, "BANKS: a tap on tile 4 loads sound 4 (program %d)", getp(h, "program"));
        E->get_param(h, "patch_slot_30", t, sizeof t);
        CHECK(t[0] == 0, "BANKS: a tile past the last sound is empty ('%s')", t);
        E->get_param(h, "bank_slot_1", t, sizeof t);
        CHECK(!strncmp(t, "1 Sturm", 7) && getp(h, "bank_slot_1_on") == 1, "BANKS: bank tile 1 reads '%s' and is browsed", t);
    }
    E->destroy(h);

    /* a folder of the instrument's sound / project dumps: all decode in range, all load and play */
    if (argc > 1) {
        h = E->create(argv[1]);
        int nb = getp(h, "bank");
        (void)nb;
        char st[256];
        E->get_param(h, "status", st, sizeof st);
        printf("     %s\n", st);
        int banks = 0, silent_total = 0;
        for (int bi = 1; bi < 64; bi++) {
            setp(h, "bank", bi);
            if (getp(h, "bank") != bi) break;
            banks++;
            E->get_param(h, "bank_name", b, sizeof b);
            int played = 0, silent = 0;
            for (int p = 0; p < 128; p++) {
                setp(h, "program", p);
                char pn[64];
                E->get_param(h, "patch_name", pn, sizeof pn);
                if (!strcmp(pn, "-")) break;
                midi3(h, 0x90, 60, 110);
                double r = render(h, 200, &pk, buf);
                midi3(h, 0x80, 60, 0);
                render(h, 150, &pk, buf + 200 * 128);
                for (int k = 0; k < 8; k++) render(h, 100, &pk, NULL);  /* let long tails die before the next one */
                midi3(h, 0xB0, 123, 0);
                played++;
                if (r < 0.0005) {       /* free-running LFOs and Random sources make a note's loudness depend on the voice it lands on: judge on fresh instances, 8 notes */
                    void *g = E->create(argv[1]);
                    setp(g, "bank", bi);
                    setp(g, "program", p);
                    double best = 0;
                    for (int k = 0; k < 8 && best < 0.0005; k++) {
                        midi3(g, 0x90, 60, 110);
                        double q = render(g, 200, &pk, NULL);
                        if (q > best) best = q;
                        midi3(g, 0x80, 60, 0);
                        render(g, 300, &pk, NULL);
                    }
                    E->destroy(g);
                    if (best < 0.0005) { silent++; silent_total++; printf("     silent: %s\n", pn); }
                }
                if (wavdir) { char path[2200]; snprintf(path, sizeof path, "%s/%s_%03d_%s.wav", wavdir, b, p + 1, pn); for (char *c = path + strlen(wavdir) + 1; *c; c++) if (*c == '/' || *c == ' ') *c = '_'; wav_write(path, buf, 350 * 128); }
            }
            printf("     bank %d \"%s\": %d sounds, %d silent at C3\n", bi, b, played, silent);
        }
        CHECK(banks > 0, "%d banks from %s", banks, argv[1]);
        CHECK(silent_total <= 2, "factory sounds silent at C3 over 8 notes: %d (known: modulation-dependent ones, docs/STATUS.md)", silent_total);
        if (banks > 2) {      /* browsing a bank loads nothing; a tapped sound loads its bank */
            char t[64], tb[64];
            setp(h, "bank", 0);
            setp(h, "program", 2);
            setp(h, "browse_bank", 2);
            E->get_param(h, "patch_slot_1", t, sizeof t);
            CHECK(getp(h, "bank") == 0 && getp(h, "program") == 2 && getp(h, "browse_bank") == 2 && t[0], "BANKS: browsing bank 3 leaves bank %d sound %d loaded; tile 1 '%s'",
                  getp(h, "bank"), getp(h, "program"), t);
            setp(h, "patch_page", 1);
            E->get_param(h, "patch_slot_1", tb, sizeof tb);
            CHECK(!strncmp(tb, "033 ", 4) || tb[0] == 0, "BANKS: page 2 starts at sound 33 ('%s')", tb);
            setp(h, "patch_page", 0);
            setp(h, "patch_slot_2", 1);
            CHECK(getp(h, "bank") == 2 && getp(h, "program") == 1 && getp(h, "browse_bank") == 2, "BANKS: tapping tile 2 loads bank %d sound %d", getp(h, "bank"), getp(h, "program"));
            E->get_param(h, "bank_range", t, sizeof t);
            CHECK(!strncmp(t, "Banks 1-", 8), "BANKS: range '%s'", t);
        }
        E->destroy(h);
    }
    /* kit mode: with a project bank, 16 notes play 16 different sounds; pad edits survive the state */
    if (argc > 2) {
        h = E->create(argv[2]);
        setp(h, "bank", 1);
        setp(h, "kit", 1);
        double r[3];
        int16_t a[128 * 60], c[128 * 60];
        midi3(h, 0x90, 36, 110); r[0] = render(h, 60, &pk, a); midi3(h, 0x80, 36, 0); midi3(h, 0xB0, 123, 0); render(h, 600, &pk, NULL);
        midi3(h, 0x90, 37, 110); r[1] = render(h, 60, &pk, c); midi3(h, 0x80, 37, 0); midi3(h, 0xB0, 123, 0); render(h, 600, &pk, NULL);
        midi3(h, 0x90, 60, 110); r[2] = render(h, 60, &pk, NULL); midi3(h, 0x80, 60, 0); render(h, 300, &pk, NULL);
        CHECK(r[0] > 0.003 && r[1] > 0.003 && memcmp(a, c, sizeof a), "kit: pads 1 and 2 play different sounds (rms %.3f, %.3f)", r[0], r[1]);
        CHECK(r[2] < 1e-4, "kit: a note outside the 16 pads is silent (rms %.5f)", r[2]);
        int nch = 0, closed_ok = 0;     /* a project's beats carry Choke targets: a hat that chokes the open hat */
        for (int bi = 1; bi <= 4; bi++) {
            setp(h, "bank", bi);
            if (getp(h, "bank") != bi) break;
            for (int p = 0; p < 128; p++) {
                setp(h, "program", p);
                int c1 = getp(h, "choke1"), c2 = getp(h, "choke2");
                nch += (c1 > 0) + (c2 > 0);
                for (int c = 0; c < 2; c++) {
                    int t = c ? c2 : c1;
                    char pn[64], tn[64];
                    E->get_param(h, "patch_name", pn, sizeof pn);
                    int cur = p / 32 * 32 + t - 1;
                    if (t > 0 && cur < 128 && (strcasestr(pn, "hat") || strcasestr(pn, "hh")) && (strcasestr(pn, "cl") || strcasestr(pn, "~"))) {
                        setp(h, "program", cur);
                        E->get_param(h, "patch_name", tn, sizeof tn);
                        setp(h, "program", p);
                        if (strcasestr(tn, "op")) closed_ok++;
                    }
                }
            }
        }
        CHECK(nch > 0 && closed_ok > 0, "choke targets read from the project: %d set, %d closed hats choke an open hat", nch, closed_ok);
        setp(h, "program", 2);
        setp(h, "lpf_freq", 33);
        setp(h, "program", 5);
        setp(h, "pan", 20);
        E->get_param(h, "state", b, sizeof b);
        E->destroy(h);
        h = E->create(argv[2]);
        E->set_param(h, "state", b);
        int pan = getp(h, "pan"), kit = getp(h, "kit");
        setp(h, "program", 2);
        CHECK(kit == 1 && pan == 20 && getp(h, "lpf_freq") == 33, "kit state restores (kit %d, pan %d, pad 3 lpf %d; %d bytes)", kit, pan,
              getp(h, "lpf_freq"), (int)strlen(b));
        E->destroy(h);
    }
    /* choke: a sound that chokes another cuts its sounding voice (a kit on the built-in bank; set here, no project needed) */
    {
        h = E->create(NULL);
        setp(h, "kit", 1);
        setp(h, "program", 1);                      /* pad 2: a sustained tone */
        setp(h, "osc1_shape", 1); setp(h, "osc2_shape", 0); setp(h, "vca_level", 100); setp(h, "aenv_amt", 0); setp(h, "lpf_freq", 100); setp(h, "env_gate", 1);
        setp(h, "osc3_level", 0); setp(h, "osc4_level", 0); setp(h, "sub_osc", 0);
        setp(h, "program", 0);                      /* pad 1: silent, chokes pad 2 */
        setp(h, "osc1_shape", 0); setp(h, "osc2_shape", 0); setp(h, "vca_level", 0); setp(h, "aenv_amt", 0);
        double r[3];
        midi3(h, 0x90, 37, 100); render(h, 40, &pk, NULL);
        r[0] = render(h, 20, &pk, NULL);            /* pad 2 sounding alone */
        midi3(h, 0x90, 36, 100);
        render(h, 20, &pk, NULL);
        r[1] = render(h, 20, &pk, NULL);            /* pad 1 struck, no choke set: pad 2 goes on */
        setp(h, "choke1", 2);
        CHECK(getp(h, "choke1") == 2 && getp(h, "choke2") == 0, "choke: Choke 1 of pad 1 reads %d", getp(h, "choke1"));
        midi3(h, 0x90, 36, 100);
        render(h, 20, &pk, NULL);
        r[2] = render(h, 20, &pk, NULL);            /* now pad 1 chokes pad 2 */
        CHECK(r[0] > 0.02 && r[1] > 0.02 && r[2] < r[0] * 0.02, "choke: pad 2 sounds (%.3f, %.3f) and is cut by pad 1's Choke 1 (%.5f)", r[0], r[1], r[2]);
        setp(h, "voice_assign", 3);
        char st[8192];
        E->get_param(h, "state", st, sizeof st);
        void *h2 = E->create(NULL);
        E->set_param(h2, "state", st);
        CHECK(getp(h2, "choke1") == 2 && getp(h2, "voice_assign") == 3, "choke: Choke 1 and Voice Assign survive the saved state (%d, %d; %d bytes)", getp(h2, "choke1"), getp(h2, "voice_assign"), (int)strlen(st));
        E->destroy(h2);
        E->destroy(h);
    }
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
