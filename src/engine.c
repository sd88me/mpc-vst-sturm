/* Sturm: a six-voice instrument modelled on the DSI Tempest's voice (manual v1.4, main OS 1.5.0.2 and voice OS 1.5).
 * A sound is the instrument's own 127 stored fields, so its sound dumps (and the sounds inside its project dumps) load as they are.
 *
 * One voice: two analog-style oscillators (saw, triangle, saw-tri, pulse 0-99) mixed 1/2, a sub oscillator under osc 1, and two
 * sample oscillators (stand-ins or your WAVs, see samples.h) partly or fully past the filters (Pre/Post); a 2/4-pole lowpass with
 * audio mod from osc 1, a 2-pole highpass, the VCA, pan and volume; the left output fed back before the filter (Feedback).
 * Modulation as the voice CPU runs it, every CTL samples (about 5 kHz): five delay/attack/peak/decay/sustain/release envelopes
 * (pitch, lowpass, amp, aux 1, aux 2) with the firmware's exponential segments and AD mode, two LFOs, eight mod paths. */
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "engine.h"
#include "patch_tab.h"
#include "curves.h"
#include "samples.h"
#include "tsnd.h"
#include "presets.h"
#include "mpc_analog.h"

#define FS 44100.0f
#define MAXV 8
#define CTL 9
#define DT (CTL / FS)
#define MAXBANKS 64
#define BANK_SLOTS 22      /* BANKS page: tiles in the bank list and in the sound list (a page of the browsed bank) */
#define SOUND_SLOTS 32
#define PATHLEN 512

/* the instrument's modulation lists (manual p. 78-79; the main OS has the same lists) */
static const char *const DEST_NAMES[58] = {"Off", "Osc1 Freq", "Osc2 Freq", "Osc3 Freq", "Osc4 Freq", "OscAll Freq", "Osc1/2 Mix",
    "Osc3 Level", "Osc4 Level", "Osc1 PW", "Osc2 PW", "Osc1/2 PW", "Sub Osc", "Feedback", "Lowpass", "Resonance", "Filter FM",
    "Highpass", "VCA", "Pan", "LFO1 Freq", "LFO2 Freq", "LFOAll Freq", "LFO1 Amt", "LFO2 Amt", "LFOAll Amt", "PitchEnv Amt",
    "LPEnv Amt", "AmpEnv Amt", "Aux1Env Amt", "Aux2Env Amt", "AllEnv Amt", "PitchEnv Att", "LPEnv Att", "AmpEnv Att", "Aux1Env Att",
    "Aux2Env Att", "AllEnv Att", "PitchEnv Dec", "LPEnv Dec", "AmpEnv Dec", "Aux1Env Dec", "Aux2Env Dec", "AllEnv Dec",
    "PitchEnv Rel", "LPEnv Rel", "AmpEnv Rel", "Aux1Env Rel", "Aux2Env Rel", "AllEnv Rel", "Mod1 Amt", "Mod2 Amt", "Mod3 Amt",
    "Mod4 Amt", "Mod5 Amt", "Mod6 Amt", "Mod7 Amt", "Mod8 Amt"};
static const char *const SRC_NAMES[23] = {"Off", "Pitch Env", "LP Env", "Amp Env", "Aux1 Env", "Aux2 Env", "LFO 1", "LFO 2",
    "Velocity", "Note Number", "Noise", "Random", "Pad Pressure", "Slider1 Pos", "Slider2 Pos", "Slider1 Press", "Slider2 Press",
    "Foot Pedal 1", "Foot Pedal 2", "Pitch Bend", "Mod Wheel", "Breath", "Expression"};
enum { D_OSC1 = 1, D_OSC2, D_OSC3, D_OSC4, D_OSCALL, D_MIX, D_LVL3, D_LVL4, D_PW1, D_PW2, D_PWALL, D_SUB, D_FB, D_LP, D_RES, D_FM,
       D_HP, D_VCA, D_PAN, D_LFO1F, D_LFO2F, D_LFOALLF, D_LFO1A, D_LFO2A, D_LFOALLA, D_ENVAMT = 26, D_ENVATT = 32, D_ENVDEC = 38,
       D_ENVREL = 44, D_MODAMT = 50, NDEST = 58 };
/* LFO sync: quarter notes per cycle (manual p. 38) */
static const char *const SYNC_NAMES[16] = {"32 Qrtr", "16 Qrtr", "8 Qrtr", "6 Qrtr", "4 Qrtr", "3 Qrtr", "1/2 Note", "Qrtr Dot",
    "1 Qrtr", "Qrtr Trip", "8th", "8th Trip", "1/16", "16th Trip", "32nd", "64th"};
static const float SYNC_Q[16] = {32, 16, 8, 6, 4, 3, 2, 1.5f, 1, 2.0f / 3, 0.5f, 1.0f / 3, 0.25f, 1.0f / 6, 0.125f, 0.0625f};

enum { ST_IDLE, ST_DELAY, ST_ATT, ST_PEAK, ST_DEC, ST_SUS, ST_REL };
typedef struct { int st; float lvl, t; } env_t;
typedef struct { float ph, out, hold; } lfo_t;
enum { E_PITCH, E_LP, E_AMP, E_AUX1, E_AUX2 };

typedef struct {
    uint8_t f[NFIELD];             /* the sound this voice plays: the edited one, or its kit pad's */
    int pad, src_note;             /* kit pad (-1: the edited sound), the note that started it */
    float cgain;                   /* a choked voice fades out through this */
    int chok;
    int note, vel, gated, sounding;
    unsigned age;
    float key[4], tgt, from;       /* per oscillator glided key, the note it glides to, where it came from */
    ma_dco_t dco;                     /* osc 1, osc 2, sub */
    ma_ota2x_t lpf;
    double spos[2];                /* sample oscillators: position (frames, or cycles for waves) */
    int sdone[2];
    float slop[4], slopv[4];
    env_t env[5];
    lfo_t lfo[2];
    float rnd_note;
    float d[NDEST], dprev[NDEST];
    /* control results */
    float inc[2], sinc[2], lvl12[2], lvl34[2], sub, duty[2], fbk, prepost;
    int shape[2];
    float cut, cut_prev, res, am, vca, vca_prev, panl, panr, hpf_hz;
    /* audio state */
    float hp[2], last_l, hz_prev;
    uint32_t rng;
} voice_t;

/* a bank's sounds, read once when the folder is scanned, so changing or browsing banks reads no file */
typedef struct { uint8_t f[128][NFIELD]; char names[128][TSND_NAME]; uint8_t ch[128][2]; int n; } bcache_t;
typedef struct { char name[40]; char path[PATHLEN]; int first; bcache_t *c; } bankref_t;   /* first: index of its first sound in the file */

typedef struct {
    uint8_t f[NFIELD];
    char name[TSND_NAME];
    voice_t v[MAXV];
    int nv, mono, kit, kit_base, kit_page;
    unsigned age;
    struct { int note, vel; } held[16];
    int nheld, pedal, deferred[128];
    int root, pan, rr;
    float bend, wheel, breath, foot1, foot2, expr, press, cc_vol, sl[4];
    float t_wheel, t_breath, t_foot1, t_foot2, t_expr, t_press, t_sl[4];
    float host_bpm;
    int transport;
    tp_samples_t *smp;
    bankref_t banks[MAXBANKS];
    int nbanks, cur_bank, cur_prog;
    int bank_page;                              /* BANKS page: which 22 banks the bank list shows (follows the browsed bank unless stepped) */
    int browse_bank, browse_page, bcount;       /* BANKS page: the bank being looked at (loads nothing), its page, its sound count */
    char bnames[128][TSND_NAME];
    uint8_t bankdata[128][NFIELD];
    char banknames[128][TSND_NAME];
    uint8_t chk[128][3];            /* per bank sound: Choke 1, Choke 2 (a sound 1-32 of its beat, 0 none), Voice Assign (0 = any voice, 1-6) */
    char dir[PATHLEN];
    int display_rev;
    uint32_t rng;
} tp_t;

static float TAU_TAB[128], DLY_TAB[128], PEAK_TAB[128];

/* ---------------- helpers ---------------- */
static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static int clampi(int x, int a, int b) { return x < a ? a : x > b ? b : x; }
static float ftanh(float x) { x = clampf(x, -3, 3); return x * (27 + x * x) / (27 + 9 * x * x); }
static float rnd(uint32_t *s) { *s = *s * 1664525u + 1013904223u; return (float)(int32_t)*s * (1.0f / 2147483648.0f); }
static int sample_of_f(const uint8_t *f, int osc) {   /* osc 0 = Osc 3, 1 = Osc 4 */
    int k = f[osc ? P_OSC4_SBANK : P_OSC3_SBANK] * 128 + f[osc ? P_OSC4_SNUM : P_OSC3_SNUM];
    return k < TP_NSAMPLES ? k : 0;
}
static int sample_of(const tp_t *s, int osc) { return sample_of_f(s->f, osc); }
static int PV(const voice_t *v, int i) { return v->f[i]; }
static float S127V(const voice_t *v, int i) { return (float)v->f[i] - 127; }

static float HB_B[MA_HB_B_M];     /* 2x -> 1x half-band of the low-pass */
static void init_tables(void) {
    static int done;
    if (done) return;
    float hb_a[MA_HB_A_M];
    ma_hb_design_standard(hb_a, HB_B);
    for (int i = 0; i < 128; i++) { TAU_TAB[i] = tp_env_tau_s((float)i); DLY_TAB[i] = tp_env_delay_s((float)i); PEAK_TAB[i] = tp_env_peak_s((float)i); }
    done = 1;
}
static float tau_at(float v) {
    v = clampf(v, 0, 127);
    int i = (int)v;
    if (i >= 127) return TAU_TAB[127];
    return TAU_TAB[i] * powf(TAU_TAB[i + 1] / TAU_TAB[i], v - i);
}

/* ---------------- sounds and banks ---------------- */
static void sound_init(uint8_t *f, char *name) {
    for (int i = 0; i < NFIELD; i++) f[i] = (uint8_t)PTAB[i].def;
    if (name) strcpy(name, "Basic");
}
static void sound_clamp(uint8_t *f) {
    for (int i = 0; i < NFIELD; i++) if (f[i] > PTAB[i].max) f[i] = (uint8_t)PTAB[i].max;
    if (f[P_OSC3_SBANK] * 128 + f[P_OSC3_SNUM] >= TP_NSAMPLES) f[P_OSC3_SBANK] = f[P_OSC3_SNUM] = 0;
    if (f[P_OSC4_SBANK] * 128 + f[P_OSC4_SNUM] >= TP_NSAMPLES) f[P_OSC4_SBANK] = f[P_OSC4_SNUM] = 0;
}
static uint8_t *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > (16 << 20)) { fclose(f); return NULL; }
    uint8_t *b = malloc((size_t)n);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    *len = (size_t)n;
    return b;
}
typedef struct { tp_t *s; int skip, got; } loadctx_t;
static void count_cb(void *c, const uint8_t f[NFIELD], const char *name) { (void)f; (void)name; ((loadctx_t *)c)->got++; }
static void load_cb(void *c, const uint8_t f[NFIELD], const char *name, const uint8_t ch[2]) {
    loadctx_t *x = c;
    int k = x->got++ - x->skip;
    if (k < 0 || k >= 128) return;
    x->s->chk[k][0] = ch[0]; x->s->chk[k][1] = ch[1]; x->s->chk[k][2] = 0;
    memcpy(x->s->bankdata[k], f, NFIELD);
    sound_clamp(x->s->bankdata[k]);
    snprintf(x->s->banknames[k], TSND_NAME, "%s", name);
}
typedef struct { tp_t *s; int b0, b1, got; } cachectx_t;
static void cache_cb(void *c, const uint8_t f[NFIELD], const char *name, const uint8_t ch[2]) {
    cachectx_t *x = c;
    int bi = x->b0 + x->got / 128, k = x->got % 128;
    x->got++;
    if (bi >= x->b1 || !x->s->banks[bi].c) return;
    bcache_t *bc = x->s->banks[bi].c;
    memcpy(bc->f[k], f, NFIELD);
    snprintf(bc->names[k], TSND_NAME, "%s", name);
    bc->ch[k][0] = ch[0]; bc->ch[k][1] = ch[1];
    if (k + 1 > bc->n) bc->n = k + 1;
}
/* SYSEX/import.txt: what to leave out of sound dumps (project dumps are never filtered: their beats are the kit layout)
 *   skip_samples = off | drums | all   leave out sounds that play a PCM sample (we have no sample set): "drums" keeps the sounds that
 *                                      only use the noises (those have stand-ins), "all" drops any PCM sample (default off)
 *   dedupe = on | off                  leave out a sound identical in all 127 fields to one already loaded from an earlier file (default on) */
typedef struct { int skip, dedupe, n, cap; uint8_t (*seen)[NFIELD]; } import_t;
static void import_cfg(import_t *im, const char *dir) {
    char path[PATHLEN], line[160];
    snprintf(path, sizeof path, "%s/import.txt", dir);
    FILE *f = fopen(path, "r");
    while (f && fgets(line, sizeof line, f)) {
        char k[40] = "", v[40] = "";
        if (line[0] == '#' || sscanf(line, " %39[a-z_] = %39s", k, v) != 2) continue;
        if (!strcmp(k, "skip_samples")) im->skip = !strcmp(v, "all") ? 2 : !strcmp(v, "drums") ? 1 : 0;
        else if (!strcmp(k, "dedupe")) im->dedupe = strcmp(v, "off") != 0;
    }
    if (f) fclose(f);
}
static int uses_pcm(const uint8_t *f, int mode) {      /* an oscillator with level that plays a PCM slot (1-366; 1-10 are the noises) */
    for (int o = 0; o < 2; o++) {
        int k = f[o ? P_OSC4_SBANK : P_OSC3_SBANK] * 128 + f[o ? P_OSC4_SNUM : P_OSC3_SNUM];
        if (f[o ? P_OSC4_LEVEL : P_OSC3_LEVEL] && k >= (mode == 1 ? 11 : 1) && k <= 366) return 1;
    }
    return 0;
}
typedef struct { uint8_t (*f)[NFIELD]; char (*nm)[TSND_NAME]; uint8_t (*ch)[2]; int n, cap; } collect_t;
static void collect_cb(void *c, const uint8_t f[NFIELD], const char *name, const uint8_t ch[2]) {
    collect_t *x = c;
    if (x->n == x->cap) {
        x->cap = x->cap ? x->cap * 2 : 512;
        x->f = realloc(x->f, (size_t)x->cap * NFIELD); x->nm = realloc(x->nm, (size_t)x->cap * TSND_NAME); x->ch = realloc(x->ch, (size_t)x->cap * 2);
    }
    memcpy(x->f[x->n], f, NFIELD); snprintf(x->nm[x->n], TSND_NAME, "%s", name); memcpy(x->ch[x->n], ch, 2); x->n++;
}
static int cmpstr(const void *a, const void *b) { return strcasecmp(*(char *const *)a, *(char *const *)b); }
/* Every .syx with sounds becomes a bank (128 sounds each; a project of 512 sounds becomes four). */
static void scan_dir(tp_t *s, const char *dir, import_t *im) {
    import_cfg(im, dir);
    DIR *d = opendir(dir);
    if (!d) return;
    char *names[512];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < 512) {
        size_t l = strlen(e->d_name);
        if (l > 4 && !strcasecmp(e->d_name + l - 4, ".syx")) names[n++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof names[0], cmpstr);
    for (int i = 0; i < n; i++) {
        char path[PATHLEN];
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        size_t len;
        uint8_t *buf = read_file(path, &len);
        char base[40];
        snprintf(base, sizeof base, "%s", names[i]);
        char *dot = strrchr(base, '.');
        if (dot) *dot = 0;
        size_t p0 = 0;
        while (buf && p0 < len && buf[p0] != 0xF0) p0++;
        if (buf && p0 + 3 < len && buf[p0 + 3] == 0x63 && (im->skip || im->dedupe)) {   /* a sound dump: leave out what import.txt says */
            collect_t col = {0};
            tsnd_scan_ex(buf, len, collect_cb, &col);
            int kept = 0;
            for (int k = 0; k < col.n; k++) {
                int dup = 0;
                for (int j = 0; im->dedupe && !dup && j < im->n; j++) dup = !memcmp(im->seen[j], col.f[k], NFIELD);
                if (dup || (im->skip && uses_pcm(col.f[k], im->skip))) continue;
                if (im->n == im->cap) { im->cap = im->cap ? im->cap * 2 : 512; im->seen = realloc(im->seen, (size_t)im->cap * NFIELD); }
                memcpy(im->seen[im->n++], col.f[k], NFIELD);
                if (kept != k) { memcpy(col.f[kept], col.f[k], NFIELD); memcpy(col.nm[kept], col.nm[k], TSND_NAME); memcpy(col.ch[kept], col.ch[k], 2); }
                kept++;
            }
            for (int first = 0; first < kept && s->nbanks < MAXBANKS; first += 128) {
                bankref_t *r = &s->banks[s->nbanks++];
                r->c = calloc(1, sizeof(bcache_t));
                if (kept > 128) snprintf(r->name, sizeof r->name, "%.30s %d", base, first / 128 + 1);
                else snprintf(r->name, sizeof r->name, "%s", base);
                snprintf(r->path, sizeof r->path, "%s", path);
                r->first = first;
                for (int k = first; k < kept && k < first + 128; k++) {
                    int q = k - first;
                    memcpy(r->c->f[q], col.f[k], NFIELD); snprintf(r->c->names[q], TSND_NAME, "%s", col.nm[k]);
                    r->c->ch[q][0] = col.ch[k][0]; r->c->ch[q][1] = col.ch[k][1]; r->c->n = q + 1;
                }
            }
            free(col.f); free(col.nm); free(col.ch); free(buf); free(names[i]);
            continue;
        }
        loadctx_t lc = {s, 0, 0};
        if (buf) tsnd_scan(buf, len, count_cb, &lc);
        free(buf);
        int b0 = s->nbanks;
        for (int first = 0; first < lc.got && s->nbanks < MAXBANKS; first += 128) {
            bankref_t *r = &s->banks[s->nbanks++];
            r->c = calloc(1, sizeof(bcache_t));
            if (lc.got > 128) snprintf(r->name, sizeof r->name, "%.30s %d", base, first / 128 + 1);
            else snprintf(r->name, sizeof r->name, "%s", base);
            snprintf(r->path, sizeof r->path, "%s", path);
            r->first = first;
        }
        if (s->nbanks > b0 && (buf = read_file(path, &len))) {
            cachectx_t cc = {s, b0, s->nbanks, 0};
            tsnd_scan_ex(buf, len, cache_cb, &cc);
            free(buf);
        }
        free(names[i]);
    }
}
typedef struct { tp_t *s; int skip, got; } namectx_t;
static void name_cb(void *c, const uint8_t f[NFIELD], const char *name) {
    namectx_t *x = c;
    int k = x->got++ - x->skip;
    (void)f;
    if (k >= 0 && k < 128) snprintf(x->s->bnames[k], TSND_NAME, "%s", name);
}
/* The sound names of a bank without loading it, for the BANKS page. */
static void browse_load(tp_t *s, int bi) {
    bi = clampi(bi, 0, s->nbanks - 1);
    s->browse_bank = bi;
    s->bank_page = bi / BANK_SLOTS;
    s->bcount = 0;
    if (bi == 0) {
        uint8_t f[NFIELD];
        for (int k = 0; k < presets_count() && k < 128; k++, s->bcount++) presets_apply(k, f, s->bnames[k]);
    } else if (s->banks[bi].c) {
        s->bcount = s->banks[bi].c->n;
        for (int k = 0; k < s->bcount; k++) memcpy(s->bnames[k], s->banks[bi].c->names[k], TSND_NAME);
    } else {
        size_t len;
        uint8_t *buf = read_file(s->banks[bi].path, &len);
        if (buf) {
            namectx_t nc = {s, s->banks[bi].first, 0};
            tsnd_scan(buf, len, name_cb, &nc);
            s->bcount = clampi(nc.got - nc.skip, 0, 128);
            free(buf);
        }
    }
    s->browse_page = clampi(s->browse_page, 0, s->bcount ? (s->bcount - 1) / SOUND_SLOTS : 0);
    s->display_rev++;
}
/* The page follows the loaded sound: after any change of bank or sound the browsed bank and page are the current ones. */
static void browse_follow(tp_t *s) {
    if (s->browse_bank != s->cur_bank || !s->bcount) browse_load(s, s->cur_bank);
    s->browse_page = s->cur_prog / SOUND_SLOTS;
}
static void prepare_kit(tp_t *s);
static void load_bank(tp_t *s, int bi) {
    bi = clampi(bi, 0, s->nbanks - 1);
    memset(s->chk, 0, sizeof s->chk);
    for (int k = 0; k < 128; k++) { sound_init(s->bankdata[k], NULL); strcpy(s->banknames[k], "-"); }
    if (bi == 0) {
        for (int k = 0; k < presets_count() && k < 128; k++) presets_apply(k, s->bankdata[k], s->banknames[k]);
    } else if (s->banks[bi].c) {
        const bcache_t *bc = s->banks[bi].c;
        for (int k = 0; k < bc->n; k++) {
            memcpy(s->bankdata[k], bc->f[k], NFIELD);
            sound_clamp(s->bankdata[k]);
            snprintf(s->banknames[k], TSND_NAME, "%s", bc->names[k]);
            s->chk[k][0] = bc->ch[k][0]; s->chk[k][1] = bc->ch[k][1];
        }
    } else {
        size_t len;
        uint8_t *buf = read_file(s->banks[bi].path, &len);
        if (buf) {
            loadctx_t lc = {s, s->banks[bi].first, 0};
            tsnd_scan_ex(buf, len, load_cb, &lc);
            free(buf);
        }
    }
    s->cur_bank = bi;
    prepare_kit(s);
}
static void prepare_samples(tp_t *s) {   /* have this sound's stand-ins built (worker thread) before its first note */
    samples_request(s->smp, sample_of(s, 0));
    samples_request(s->smp, sample_of(s, 1));
}
static void prepare_kit(tp_t *s) {          /* the 16 pads' stand-ins, before they are played */
    if (!s->kit) return;
    for (int p = 16 * s->kit_page; p < 16 * s->kit_page + 16; p++) { samples_request(s->smp, sample_of_f(s->bankdata[p], 0)); samples_request(s->smp, sample_of_f(s->bankdata[p], 1)); }
}
static void select_sound(tp_t *s, int p) {
    s->cur_prog = clampi(p, 0, 127);
    memcpy(s->f, s->bankdata[s->cur_prog], NFIELD);
    snprintf(s->name, sizeof s->name, "%s", s->banknames[s->cur_prog]);
    prepare_samples(s);
    browse_follow(s);
    s->display_rev++;
}

/* ---------------- envelopes and LFOs (voice OS 1.5, docs/FIRMWARE.md section 4) ---------------- */
/* per-tick fraction for a time constant at this control rate */
static float coef(float tau) { return 1 - expf(-DT / fmaxf(tau, 1e-5f)); }
static void env_gate(env_t *e, int on, int adsr) {
    if (on) { e->st = ST_DELAY; e->t = 0; }
    else if (adsr && e->st != ST_IDLE) e->st = ST_REL;   /* in AD mode release has no effect: the decay runs on */
}
/* a, d, r: time values with their modulation (0..127); sus 0..1; adsr: sustain/release used (AD mode off) */
static float env_tick(env_t *e, float delay_s, float a, float peak_s, float d, float sus, float r, int adsr) {
    switch (e->st) {
    case ST_DELAY:
        e->t += DT;
        if (e->t < delay_s) break;
        e->t = 0;
        e->st = ST_ATT;
        /* fall through */
    case ST_ATT:
        if (a <= 0) { e->lvl = 1; e->st = ST_PEAK; e->t = 0; break; }
        e->lvl += (1 - e->lvl) * coef(tau_at(a));
        if (e->lvl > 0.98828f) { e->lvl = 1; e->st = ST_PEAK; e->t = 0; }
        break;
    case ST_PEAK:
        e->t += DT;
        if (e->t >= peak_s) e->st = ST_DEC;
        break;
    case ST_DEC: {
        float tgt = adsr ? sus : 0;
        e->lvl -= (e->lvl - tgt) * coef(tau_at(d));
        if (e->lvl < tgt + 2.3e-5f) {
            if (adsr) { e->lvl = tgt; e->st = ST_SUS; }
            else { e->lvl = 0; e->st = ST_IDLE; }
        }
        break;
    }
    case ST_SUS: e->lvl = sus; break;
    case ST_REL:
        e->lvl -= e->lvl * coef(2 * tau_at(r));
        if (e->lvl < 1.5e-5f) { e->lvl = 0; e->st = ST_IDLE; }
        break;
    default: e->lvl = 0;
    }
    return e->lvl;
}
static float lfo_tick(lfo_t *l, float hz, int shape, uint32_t *rng) {
    l->ph += hz * DT;
    if (l->ph >= 1) { l->ph -= floorf(l->ph); l->hold = rnd(rng); }
    float p = l->ph;
    switch (shape) {
    case 0: l->out = p < 0.5f ? 4 * p - 1 : 3 - 4 * p; break;
    case 1: l->out = 1 - 2 * p; break;
    case 2: l->out = 2 * p - 1; break;
    case 3: l->out = p < 0.5f ? 1 : -1; break;
    default: l->out = l->hold;
    }
    return l->out;
}

/* ---------------- notes ---------------- */
/* The edited sound follows edits live; a kit pad plays its bank slot (the edited one when it is the current sound). */
static void voice_load(tp_t *s, voice_t *v) {
    memcpy(v->f, v->pad < 0 || v->pad == s->cur_prog ? s->f : s->bankdata[v->pad], NFIELD);
}
static void voice_trigger(tp_t *s, voice_t *v, int on) {
    int adsr = PV(v, P_ENV_GATE);
    for (int e = 0; e < 5; e++) env_gate(&v->env[e], on, adsr);
    if (!on) return;
    v->sounding = 1;
    v->chok = 0;
    v->cgain = 1;
    static const int rs[2] = {P_LFO1_RESTART, P_LFO2_RESTART};
    for (int l = 0; l < 2; l++) if (PV(v, rs[l]) == 1) { v->lfo[l].ph = 0; v->lfo[l].hold = rnd(&v->rng); }
    if (PV(v, P_OSC1_RESET)) { v->dco.ph[0] = 0; v->dco.flip = 0; }
    if (PV(v, P_OSC2_RESET)) v->dco.ph[1] = 0;
    for (int o = 0; o < 2; o++) {   /* samples start from their beginning (reverse: the end), waves keep running */
        const tp_sample_t *sm = samples_peek(s->smp, sample_of_f(v->f, o));
        int rev = PV(v, o ? P_OSC4_REV : P_OSC3_REV);
        if (!sm->loop) v->spos[o] = rev ? sm->len - 1 : 0;
        v->sdone[o] = 0;
    }
    v->rnd_note = rnd(&v->rng);
    v->lpf.st[0] += 0.02f;             /* the gate's control-voltage step leaks into the filter: a resonant filter starts at once */
}
static void voice_note(tp_t *s, voice_t *v, int note, int vel, int legato) {
    voice_load(s, v);
    int mode = PV(v, P_GLIDE_MODE);
    v->from = v->sounding ? v->tgt : (float)note;
    v->note = note;
    v->vel = vel;
    v->tgt = (float)note;
    if (!v->sounding || ((mode & 1) && !legato)) for (int o = 0; o < 4; o++) v->key[o] = (float)note;
    v->gated = 1;
    v->age = ++s->age;
    if (!legato || !(s->mono == 2 || s->mono == 4 || s->mono == 6) || v->env[E_AMP].st == ST_IDLE) voice_trigger(s, v, 1);
}
static void voice_release(tp_t *s, voice_t *v) { v->gated = 0; voice_trigger(s, v, 0); }

static int key_pick(const tp_t *s) {
    int pri = (s->mono - 1) / 2, best = -1;    /* 0 low, 1 high, 2 last */
    for (int i = 0; i < s->nheld; i++) {
        if (best < 0) { best = i; continue; }
        if (pri == 0 && s->held[i].note < s->held[best].note) best = i;
        if (pri == 1 && s->held[i].note > s->held[best].note) best = i;
        if (pri == 2) best = i;
    }
    return best;
}
static void mono_update(tp_t *s) {
    voice_t *v = &s->v[0];
    int k = key_pick(s);
    if (k < 0) { if (v->gated) voice_release(s, v); return; }
    int note = s->held[k].note;
    if (v->gated && v->note == note) return;
    voice_note(s, v, note, s->held[k].vel, v->gated);
}
static int kit_pad(const tp_t *s, int note) {
    int p = note - s->kit_base;
    return s->kit && p >= 0 && p < 16 ? 16 * s->kit_page + p : -1;   /* the bank slot it plays */
}
/* Each sound of a beat can choke two others of the beat (a closed hat cuts the open one; a sound that chokes itself cuts its own
 * last note): the pads' sounding voices fade out in a few milliseconds (manual p. 42). */
static void choke_pads(tp_t *s, int pad) {
    int base = pad / 32 * 32;
    for (int c = 0; c < 2; c++) {
        int t = s->chk[pad][c];
        if (t < 1 || t > 32) continue;
        for (int i = 0; i < s->nv; i++) {
            voice_t *v = &s->v[i];
            if (v->sounding && !v->chok && v->pad == base + t - 1) { v->chok = 1; if (v->gated) voice_release(s, v); }
        }
    }
}
static void note_on(tp_t *s, int note, int vel) {
    s->deferred[note & 127] = 0;
    int pad = kit_pad(s, note);
    if (s->kit && pad < 0) return;                 /* kit mode: only the 16 pad notes play */
    if (pad >= 0) {                                /* a pad: its sound at its own pitch, on the next free (else oldest) voice */
        if (s->kit == 2 && pad != s->cur_prog) { s->cur_prog = pad; memcpy(s->f, s->bankdata[pad], NFIELD);
            snprintf(s->name, sizeof s->name, "%s", s->banknames[pad]); s->display_rev++; }
        choke_pads(s, pad);
        voice_t *best = NULL;
        if (s->chk[pad][2]) best = &s->v[(s->chk[pad][2] - 1) % s->nv];     /* Voice Assign: always this voice (stealing it) */
        for (int i = 0; !best && i < s->nv; i++) { voice_t *v = &s->v[(s->rr + i) % s->nv]; if (!v->sounding) { best = v; break; } }
        if (!best) { best = &s->v[0]; for (int i = 1; i < s->nv; i++) if (s->v[i].age < best->age) best = &s->v[i]; }
        s->rr = (int)(best - s->v) + 1;
        best->pad = pad;
        best->src_note = note;
        voice_note(s, best, s->root, vel, 0);
        return;
    }
    for (int i = 0; i < s->nheld; i++) if (s->held[i].note == note) { memmove(&s->held[i], &s->held[i + 1], sizeof s->held[0] * (size_t)(--s->nheld - i)); break; }
    if (s->nheld < 16) { s->held[s->nheld].note = note; s->held[s->nheld].vel = vel; s->nheld++; }
    if (s->mono) { s->v[0].pad = -1; mono_update(s); return; }
    voice_t *best = NULL;
    if (s->chk[s->cur_prog][2]) best = &s->v[(s->chk[s->cur_prog][2] - 1) % s->nv];
    for (int i = 0; !best && i < s->nv; i++) {             /* the next voice that is silent, else the oldest */
        voice_t *v = &s->v[(s->rr + i) % s->nv];
        if (!v->sounding) { best = v; break; }
    }
    if (!best) { best = &s->v[0]; for (int i = 1; i < s->nv; i++) if (s->v[i].age < best->age) best = &s->v[i]; }
    s->rr = (int)(best - s->v) + 1;
    best->pad = -1;
    best->src_note = note;
    voice_note(s, best, note, vel, 0);
}
static void note_off(tp_t *s, int note) {
    if (s->pedal) { s->deferred[note & 127] = 1; return; }
    if (kit_pad(s, note) >= 0) {
        for (int i = 0; i < s->nv; i++) if (s->v[i].gated && s->v[i].pad >= 0 && s->v[i].src_note == note) voice_release(s, &s->v[i]);
        return;
    }
    for (int i = 0; i < s->nheld; i++) if (s->held[i].note == note) { memmove(&s->held[i], &s->held[i + 1], sizeof s->held[0] * (size_t)(--s->nheld - i)); break; }
    if (s->mono) { mono_update(s); return; }
    for (int i = 0; i < s->nv; i++) if (s->v[i].gated && s->v[i].note == note) voice_release(s, &s->v[i]);
}
static void all_off(tp_t *s) {
    s->nheld = 0;
    memset(s->deferred, 0, sizeof s->deferred);
    for (int i = 0; i < MAXV; i++) if (s->v[i].gated) voice_release(s, &s->v[i]);
}

/* ---------------- the control tick ---------------- */
/* An envelope amount with its velocity amount: the amount is the minimum, velocity adds toward full (manual p. 36). */
static float env_amount(float amt, int velamt, int vel) {
    float a = amt + (amt < 0 ? -1.0f : 1.0f) * velamt * vel / 127.0f;
    return clampf(a, -127, 127);
}
static void voice_control(tp_t *s, voice_t *v) {
    voice_load(s, v);
    float *d = v->d;
    memcpy(v->dprev, d, sizeof v->dprev);
    memset(d, 0, sizeof v->d);
    const float *dp = v->dprev;
    int adsr = PV(v, P_ENV_GATE), vel = v->vel;

    /* envelopes: time modulation in value steps (positive = longer, as the manual's figures show) */
    static const int ep[5][6] = {{P_PENV_DELAY, P_PENV_A, P_PENV_PEAK, P_PENV_D, P_PENV_S, P_PENV_R},
        {P_FENV_DELAY, P_FENV_A, P_FENV_PEAK, P_FENV_D, P_FENV_S, P_FENV_R}, {P_AENV_DELAY, P_AENV_A, P_AENV_PEAK, P_AENV_D, P_AENV_S, P_AENV_R},
        {P_X1ENV_DELAY, P_X1ENV_A, P_X1ENV_PEAK, P_X1ENV_D, P_X1ENV_S, P_X1ENV_R}, {P_X2ENV_DELAY, P_X2ENV_A, P_X2ENV_PEAK, P_X2ENV_D, P_X2ENV_S, P_X2ENV_R}};
    float env[5];
    for (int e = 0; e < 5; e++) {
        float a = PV(v, ep[e][1]) > 0 ? clampf(PV(v, ep[e][1]) + dp[D_ENVATT + e] + dp[D_ENVATT + 5], 0.5f, 127) : 0;
        env[e] = env_tick(&v->env[e], DLY_TAB[PV(v, ep[e][0])], a, PEAK_TAB[PV(v, ep[e][2])],
                          PV(v, ep[e][3]) + dp[D_ENVDEC + e] + dp[D_ENVDEC + 5], PV(v, ep[e][4]) / 127.0f,
                          PV(v, ep[e][5]) + dp[D_ENVREL + e] + dp[D_ENVREL + 5], adsr);
    }

    /* LFOs */
    static const int lp[2][6] = {{P_LFO1_RATE, P_LFO1_SHAPE, P_LFO1_AMT, P_LFO1_DEST, P_LFO1_SYNC, P_LFO1_RESTART},
        {P_LFO2_RATE, P_LFO2_SHAPE, P_LFO2_AMT, P_LFO2_DEST, P_LFO2_SYNC, P_LFO2_RESTART}};
    float lfo[2];
    for (int l = 0; l < 2; l++) {
        int rv = PV(v, lp[l][0]);
        float hz;
        if (PV(v, lp[l][4]) || rv > 162) {
            int si = rv > 162 ? rv - 163 : rv * 16 / 163;
            float bpm = s->host_bpm > 0 ? s->host_bpm : 120;
            hz = bpm / 60.0f / SYNC_Q[clampi(si, 0, 15)];
        } else hz = tp_lfo_hz(clampf(rv + dp[D_LFO1F + l] + dp[D_LFOALLF], 0, 162));
        lfo[l] = lfo_tick(&v->lfo[l], hz, PV(v, lp[l][1]), &v->rng);
        float amt = clampf(PV(v, lp[l][2]) + dp[D_LFO1A + l] + dp[D_LFOALLA], 0, 127);
        int dst = PV(v, lp[l][3]);
        if (dst > 0 && dst < NDEST) d[dst] += lfo[l] * amt;
    }

    /* envelopes to their destinations: pitch and aux are assignable, LP and amp are wired */
    static const int ed[5][3] = {{P_PENV_DEST, P_PENV_AMT, P_PENV_VEL}, {-1, P_FENV_AMT, P_FENV_VEL}, {-1, P_AENV_AMT, P_AENV_VEL},
        {P_X1ENV_DEST, P_X1ENV_AMT, P_X1ENV_VEL}, {P_X2ENV_DEST, P_X2ENV_AMT, P_X2ENV_VEL}};
    float eamt[5];
    for (int e = 0; e < 5; e++) {
        float a = e == E_AMP ? (float)PV(v, ed[e][1]) : S127V(v, ed[e][1]);
        a = env_amount(a, PV(v, ed[e][2]), vel) + dp[D_ENVAMT + e] + dp[D_ENVAMT + 5];
        eamt[e] = e == E_AMP ? clampf(a, 0, 127) : a;
        if (ed[e][0] >= 0) { int dst = PV(v, ed[e][0]); if (dst > 0 && dst < NDEST) d[dst] += env[e] * eamt[e]; }
    }

    /* mod paths: a full-scale source moves the destination by the amount in its own steps */
    float src[23];
    src[0] = 0;
    for (int e = 0; e < 5; e++) src[1 + e] = env[e];
    src[6] = lfo[0]; src[7] = lfo[1];
    src[8] = vel / 127.0f; src[9] = v->note / 127.0f; src[10] = rnd(&v->rng); src[11] = v->rnd_note;
    src[12] = s->press; src[13] = s->sl[0]; src[14] = s->sl[1]; src[15] = s->sl[2]; src[16] = s->sl[3];
    src[17] = s->foot1; src[18] = s->foot2; src[19] = s->bend; src[20] = s->wheel; src[21] = s->breath; src[22] = s->expr;
    static const int mp[8][3] = {{P_MOD1_SRC, P_MOD1_AMT, P_MOD1_DEST}, {P_MOD2_SRC, P_MOD2_AMT, P_MOD2_DEST}, {P_MOD3_SRC, P_MOD3_AMT, P_MOD3_DEST},
        {P_MOD4_SRC, P_MOD4_AMT, P_MOD4_DEST}, {P_MOD5_SRC, P_MOD5_AMT, P_MOD5_DEST}, {P_MOD6_SRC, P_MOD6_AMT, P_MOD6_DEST},
        {P_MOD7_SRC, P_MOD7_AMT, P_MOD7_DEST}, {P_MOD8_SRC, P_MOD8_AMT, P_MOD8_DEST}};
    for (int m = 0; m < 8; m++) {
        int sr = PV(v, mp[m][0]), dst = PV(v, mp[m][2]);
        if (!sr || !dst || sr > 22 || dst >= NDEST) continue;
        float amt = clampf(S127V(v, mp[m][1]) + dp[D_MODAMT + m], -127, 127);
        if (sr == 9 && dst >= D_OSC1 && dst <= D_OSCALL) d[dst] += (v->note - s->root) * amt / 127.0f;   /* key tracks in semitones */
        else d[dst] += src[sr] * amt;
    }

    /* pitch: frequency + fine + key (glided) + bend + modulation + slop */
    int gm = PV(v, P_GLIDE_MODE), legato = s->nheld > 1 || s->mono;
    static const int of[4][4] = {{P_OSC1_FREQ, P_OSC1_FINE, P_OSC1_GLIDE, P_OSC1_KEY}, {P_OSC2_FREQ, P_OSC2_FINE, P_OSC2_GLIDE, P_OSC2_KEY},
        {P_OSC3_FREQ, P_OSC3_FINE, P_OSC3_GLIDE, P_OSC3_KEY}, {P_OSC4_FREQ, P_OSC4_FINE, P_OSC4_GLIDE, P_OSC4_KEY}};
    float slop = PV(v, P_SLOP) * 0.04f, bend = s->bend * PV(v, P_BEND_RANGE);
    float semis[4];
    for (int o = 0; o < 4; o++) {
        int g = PV(v, of[o][2]);
        if (g > 0 && !((gm & 1) && !legato)) {
            float rate = tp_glide_semis_s((float)g);
            if (gm >= 2) rate *= fmaxf(fabsf(v->tgt - v->from), 1) / 12.0f;      /* FixTime: the same time for any interval */
            float dk = v->tgt - v->key[o], st = rate * DT;
            v->key[o] += dk > st ? st : dk < -st ? -st : dk;
        } else v->key[o] = v->tgt;
        if ((v->rng & 0x3ff) < 4) v->slopv[o] = rnd(&v->rng) * slop;
        v->slop[o] += (v->slopv[o] - v->slop[o]) * 0.001f;
        float key = PV(v, of[o][3]) ? v->key[o] - s->root : 0;
        float base = o < 2 ? (float)PV(v, of[o][0]) : (float)PV(v, of[o][0]) - 64;
        semis[o] = base + (PV(v, of[o][1]) - 50) / 100.0f + key + bend + d[D_OSC1 + o] + d[D_OSCALL] + v->slop[o];
    }
    for (int o = 0; o < 2; o++) v->inc[o] = fminf(tp_note_hz(semis[o]) / FS, 0.45f);
    for (int o = 0; o < 2; o++) {
        int smp = sample_of_f(v->f, o);
        const tp_sample_t *sm = samples_peek(s->smp, smp);
        float ratio = sm->fixed ? 1.0f : exp2f(semis[2 + o] / 12.0f);
        v->sinc[o] = sm->rate * ratio;
    }
    /* oscillator levels and shapes */
    float mix = clampf(PV(v, P_OSC_MIX) + d[D_MIX], 0, 127) / 127.0f;
    v->lvl12[0] = 1 - mix; v->lvl12[1] = mix;
    v->lvl34[0] = clampf(PV(v, P_OSC3_LEVEL) + d[D_LVL3], 0, 127) / 127.0f;
    v->lvl34[1] = clampf(PV(v, P_OSC4_LEVEL) + d[D_LVL4], 0, 127) / 127.0f;
    if (!PV(v, P_OSC3_LEVEL)) v->lvl34[0] = 0;   /* level 0 is off; modulation does not open it (manual p. 30) */
    if (!PV(v, P_OSC4_LEVEL)) v->lvl34[1] = 0;
    v->sub = clampf(PV(v, P_SUB_OSC) + d[D_SUB], 0, 127) / 127.0f;
    for (int c = 0; c < 2; c++) {
        int sh = PV(v, c ? P_OSC2_SHAPE : P_OSC1_SHAPE);
        v->shape[c] = sh < 4 ? sh : 4;
        float pw = clampf(sh - 4 + d[D_PW1 + c] + d[D_PWALL], 0, 99);
        v->duty[c] = sh < 4 ? 0.5f : pw / 100.0f;
        if (sh >= 4 && (pw <= 0 || pw >= 99)) v->shape[c] = 5;   /* the pulse goes flat at the extremes */
    }
    v->prepost = PV(v, P_PREPOST) / 127.0f;
    float fbv = clampf(PV(v, P_FEEDBACK) + d[D_FB], 0, 127) / 127.0f;
    v->fbk = 7.0f * fbv * sqrtf(fbv);    /* loop gain: a mild fuzz low down, the factory kicks (72-115) ring on it alone */

    /* lowpass: base + envelope + key tracking (64 = a semitone per note) + modulation */
    float cut = PV(v, P_LPF_FREQ) + env[E_LP] * eamt[E_LP] + (v->key[0] - s->root) * PV(v, P_LPF_KEY) / 64.0f + d[D_LP];
    v->cut_prev = v->cut;
    v->cut = clampf(cut, -40, 200);
    float r = clampf(PV(v, P_LPF_RES) + d[D_RES], 0, 127) / 127.0f;
    v->res = r;
    v->am = clampf(PV(v, P_AUDIO_MOD) + d[D_FM], 0, 127) * 0.38f;
    int hpv = PV(v, P_HPF_FREQ);
    float hv = hpv + (v->key[0] - s->root) * PV(v, P_HPF_KEY) / 64.0f + d[D_HP];
    v->hpf_hz = (hpv > 0 || d[D_HP] > 0) && hv > 0 ? fminf(tp_hpf_hz(hv), 0.45f * FS) : 0;

    /* VCA: level + amp envelope x amount */
    v->vca_prev = v->vca;
    v->vca = clampf((PV(v, P_VCA_LEVEL) + env[E_AMP] * eamt[E_AMP] + d[D_VCA]) / 127.0f, 0, 1) * v->cgain;
    /* pan (the mixer's, a host parameter here) */
    float pos = clampf(s->pan + d[D_PAN], 0, 127) / 127.0f;
    v->panl = cosf(pos * 1.5707963f) * 1.41421356f;
    v->panr = sinf(pos * 1.5707963f) * 1.41421356f;
}

/* ---------------- audio ---------------- */
static float sample_osc(tp_t *s, voice_t *v, int o) {
    int k = sample_of_f(v->f, o);
    if (!k || v->lvl34[o] <= 0 || v->sdone[o]) return 0;
    const tp_sample_t *sm = samples_peek(s->smp, k);
    float inc = v->sinc[o];
    const float *wt = samples_wave(s->smp, k, inc / TP_WLEN);
    if (wt) {          /* a single-cycle wave: spos is the phase in points */
        double p = v->spos[o];
        int i = (int)p;
        float f = (float)(p - i);
        float y = wt[i & (TP_WLEN - 1)] + f * (wt[(i + 1) & (TP_WLEN - 1)] - wt[i & (TP_WLEN - 1)]);
        p += PV(v, o ? P_OSC4_REV : P_OSC3_REV) ? -inc : inc;
        while (p >= TP_WLEN) p -= TP_WLEN;
        while (p < 0) p += TP_WLEN;
        v->spos[o] = p;
        return y;
    }
    if (!sm->data || sm->len < 2) return 0;
    double p = v->spos[o];
    int rev = PV(v, o ? P_OSC4_REV : P_OSC3_REV);
    if (p < 0 || p >= sm->len - 1) {
        if (!sm->loop) { v->sdone[o] = 1; return 0; }
        p = fmod(p, sm->len - 1);
        if (p < 0) p += sm->len - 1;
    }
    int i = (int)p;
    float f = (float)(p - i);
    float y = (sm->data[i] + f * (sm->data[i + 1] - sm->data[i])) * (1.0f / 32768);
    v->spos[o] = p + (rev ? -inc : inc);
    return y;
}

/* One voice, n samples (n <= CTL), added into out[] (stereo float). */
static void voice_audio(tp_t *s, voice_t *v, float *out, int n) {
    int four = PV(v, P_POLES), sync = PV(v, P_SYNC);
    float vol = PV(v, P_VOLUME) / 127.0f;
    float hg = 0, ha = 0;
    if (v->hpf_hz > 0) { hg = tanf(3.14159265f * v->hpf_hz / FS); ha = 1 / (1 + hg * (hg + 1.4142f)); }
    for (int i = 0; i < n; i++) {
        float t = (i + 1) / (float)n;
        /* the DCOs (analog.h): osc 2's reset discharges osc 1 when synced; the sub is clocked by osc 1 */
        ma_dco_osc_t d1 = {v->shape[0], v->duty[0], v->inc[0]}, d2 = {v->shape[1], v->duty[1], v->inc[1]};
        float dco[3];
        ma_dco_tick(&v->dco, &d1, &d2, sync, dco);
        float o1 = dco[0], o2 = dco[1], sub = dco[2] * v->sub;
        float s3 = sample_osc(s, v, 0) * v->lvl34[0], s4 = sample_osc(s, v, 1) * v->lvl34[1];
        float digi = s3 + s4;
        float in = o1 * v->lvl12[0] + o2 * v->lvl12[1] + sub + digi * (1 - v->prepost);
        in -= ftanh(v->last_l * v->fbk);     /* the left output fed back before the filter, inverted (it adds to the resonance) */
        in += rnd(&v->rng) * 2e-4f;                  /* the circuit's own noise: lets a fully resonant filter start ringing */
        /* the Curtis low-pass (analog.h); audio mod is osc 1 sweeping the cutoff */
        float semis = v->cut_prev + (v->cut - v->cut_prev) * t + v->am * o1;
        float hz = tp_lpf_hz(semis);
        float y = ma_ota2x(&v->lpf, HB_B, in * 0.7f, hz, v->res, four, FS) * 1.4f;
        v->hz_prev = hz;
        if (!(fabsf(y) < 64)) {         /* a runaway or NaN (an extreme resonance and modulation) must not outlive the note: it would silence the voice for good */
            memset(&v->lpf, 0, sizeof v->lpf); v->hp[0] = v->hp[1] = 0; v->last_l = 0; y = 0;
        }
        /* 2-pole highpass (state variable, Butterworth) */
        if (hg > 0) {
            float hp = (y - (1.4142f + hg) * v->hp[0] - v->hp[1]) * ha;
            float bp = hg * hp + v->hp[0];
            float lp = hg * bp + v->hp[1];
            v->hp[0] = hg * hp + bp;
            v->hp[1] = hg * bp + lp;
            y = hp;
        }
        /* VCA, the part of osc 3/4 that bypasses the filters, pan */
        float gv = v->vca_prev + (v->vca - v->vca_prev) * t;
        float a = ftanh((y + digi * v->prepost * 0.5f) * gv * 1.1f) * 0.91f;   /* the VCA saturates gently when driven */
        float L = a * v->panl, R = a * v->panr;
        v->last_l = L;
        out[2 * i] += L * vol;
        out[2 * i + 1] += R * vol;
    }
}

/* ---------------- engine API ---------------- */
static void *tp_create(const char *dir) {
    init_tables();
    tp_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->smp = samples_open();
    if (!s->smp) { free(s); return NULL; }
    s->nv = 6;
    s->cc_vol = 1;
    s->root = 60;
    s->kit_base = 36;
    s->pan = 64;
    s->rng = 0x13579BDFu;
    for (int i = 0; i < MAXV; i++) {
        s->v[i].rng = 0x2468ACE1u + 7919u * (uint32_t)i;
        s->v[i].note = 60;
        for (int o = 0; o < 4; o++) s->v[i].key[o] = 60;
        for (int o = 0; o < 2; o++) s->v[i].dco.ph[o] = (float)((i * 37 + o * 11) % 100) / 100.0f;
        s->v[i].tgt = 60;
        s->v[i].pad = -1;
    }
    strcpy(s->banks[0].name, "Sturm");
    s->nbanks = 1;
    if (dir && *dir) {
        snprintf(s->dir, sizeof s->dir, "%s", dir);
        char sub[PATHLEN];
        snprintf(sub, sizeof sub, "%s/SYSEX", dir);
        mkdir(sub, 0755);
        char cfg[PATHLEN];
        snprintf(cfg, sizeof cfg, "%s/import.txt", sub);
        if (access(cfg, F_OK) != 0) {          /* a commented template, so the options can be found */
            FILE *cf = fopen(cfg, "w");
            if (cf) {
                fputs("# What to leave out when the sound dumps in this folder are read (project dumps are never filtered).\n"
                      "# skip_samples: off | drums (leave out sounds that play drum/percussion samples; noises are kept) | all (any PCM sample)\n"
                      "# dedupe: on | off (leave out a sound identical to one already loaded from an earlier file)\n"
                      "skip_samples = off\ndedupe = on\n", cf);
                fclose(cf);
            }
        }
        import_t im = {0, 1, 0, 0, NULL};
        scan_dir(s, dir, &im);
        scan_dir(s, sub, &im);
        free(im.seen);
        snprintf(sub, sizeof sub, "%s/SAMPLES", dir);
        mkdir(sub, 0755);
        samples_load_dir(s->smp, sub);
    }
    load_bank(s, 0);
    select_sound(s, 0);       /* also points the BANKS page at it */
    return s;
}
static void tp_destroy(void *h) {
    tp_t *s = h;
    if (!s) return;
    samples_close(s->smp);
    for (int i = 0; i < s->nbanks; i++) free(s->banks[i].c);
    free(s);
}

static void set_field(tp_t *s, int idx, int val) {
    if (idx < 0 || idx >= NFIELD) return;
    s->f[idx] = (uint8_t)clampi(val, 0, PTAB[idx].max);
    s->bankdata[s->cur_prog][idx] = s->f[idx];     /* edits stay with the sound (a kit keeps them across pads) */
    if (idx == P_OSC3_SNUM || idx == P_OSC3_SBANK || idx == P_OSC4_SNUM || idx == P_OSC4_SBANK) { sound_clamp(s->f); prepare_samples(s); }
}
static void set_sample(tp_t *s, int osc, int k) {
    k = clampi(k, 0, TP_NSAMPLES - 1);
    s->f[osc ? P_OSC4_SBANK : P_OSC3_SBANK] = (uint8_t)(k / 128);
    s->f[osc ? P_OSC4_SNUM : P_OSC3_SNUM] = (uint8_t)(k % 128);
    memcpy(s->bankdata[s->cur_prog], s->f, NFIELD);
    prepare_samples(s);
}

static void tp_midi(void *h, const uint8_t *m, int len) {
    tp_t *s = h;
    if (len < 2 || m[0] >= 0xF0) return;
    int st = m[0] & 0xF0, d1 = m[1] & 127, d2 = len > 2 ? m[2] & 127 : 0;
    switch (st) {
    case 0x90: if (d2) { note_on(s, d1, d2); break; } /* fall through */
    case 0x80: note_off(s, d1); break;
    case 0xA0: s->t_press = d2 / 127.0f; break;
    case 0xD0: s->t_press = d1 / 127.0f; break;
    case 0xE0: s->bend = ((d1 | (d2 << 7)) - 8192) / 8192.0f; break;
    case 0xC0: select_sound(s, d1); break;
    case 0xB0:
        switch (d1) {
        case 1: s->t_wheel = d2 / 127.0f; break;
        case 2: s->t_breath = d2 / 127.0f; break;
        case 4: s->t_foot1 = d2 / 127.0f; break;
        case 12: s->t_foot2 = d2 / 127.0f; break;
        case 7: s->cc_vol = d2 / 127.0f; break;
        case 11: s->t_expr = d2 / 127.0f; break;
        case 16: case 17: case 18: case 19: s->t_sl[d1 - 16] = d2 / 127.0f; break;
        case 32: if (d2 < s->nbanks) { load_bank(s, d2); browse_follow(s); s->display_rev++; } break;
        case 64:
            s->pedal = d2 >= 64;
            if (!s->pedal) for (int n = 0; n < 128; n++) if (s->deferred[n]) { s->deferred[n] = 0; note_off(s, n); }
            break;
        case 120: case 123: case 125:
            all_off(s);
            if (d1 == 123) { s->t_wheel = s->t_breath = s->t_foot1 = s->t_foot2 = s->t_press = 0; s->bend = 0; s->cc_vol = 1; }
            break;
        }
        break;
    }
}

static void tp_render(void *h, int16_t *out, int frames) {
    tp_t *s = h;
    float buf[2 * CTL];
    for (int f = 0; f < frames; f += CTL) {
        int n = frames - f < CTL ? frames - f : CTL;
        const float k = 0.05f;
        s->wheel += (s->t_wheel - s->wheel) * k; s->press += (s->t_press - s->press) * k;
        s->breath += (s->t_breath - s->breath) * k; s->foot1 += (s->t_foot1 - s->foot1) * k; s->foot2 += (s->t_foot2 - s->foot2) * k;
        s->expr += (s->t_expr - s->expr) * k;
        for (int j = 0; j < 4; j++) s->sl[j] += (s->t_sl[j] - s->sl[j]) * k;
        memset(buf, 0, sizeof buf);
        for (int i = 0; i < MAXV; i++) {
            voice_t *v = &s->v[i];
            if (!v->sounding) continue;
            if (v->chok) v->cgain *= 0.8f;        /* a choked voice: gone in about 6 ms */
            voice_control(s, v);
            voice_audio(s, v, buf, n);
            /* a voice is free once its amp envelope has finished (AD mode: even with the key held) and nothing else sounds */
            if ((v->chok && v->cgain < 0.003f) || (v->env[E_AMP].st == ST_IDLE && PV(v, P_VCA_LEVEL) == 0 && fabsf(v->last_l) < 1e-4f)) {
                v->sounding = 0;
                v->chok = 0;
                memset(&v->lpf, 0, sizeof v->lpf);
                memset(v->hp, 0, sizeof v->hp);
                v->last_l = 0;
            }
        }
        float g = 0.224f * s->cc_vol;   /* 7 dB below the first build, which clipped on chords: bench RMS on the Force, level-matched with Profit-8, Clementine-XT and Maze Voice */
        for (int i = 0; i < 2 * n; i++) {
            float x = ma_tanh(buf[i] * g);   /* soft limiter instead of a hard clip */
            out[2 * f + i] = (int16_t)lrintf(x * 32767);
        }
    }
}

/* ---------------- parameters ---------------- */
static int find_key(const char *k) {
    for (int i = 0; i < NFIELD; i++) if (!strcmp(PTAB[i].key, k)) return i;
    return -1;
}
static const char *NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static int format_value(const tp_t *s, int i, char *b, int n) {
    int v = s->f[i];
    const char *f = PTAB[i].fmt;
    size_t z = (size_t)n;
    if (!strcmp(f, "note")) snprintf(b, z, "%s%d", NOTE[v % 12], v / 12);     /* the instrument's names: 0 = C0 (8.18 Hz) */
    else if (!strcmp(f, "cents")) snprintf(b, z, "%+d", v - 50);
    else if (!strcmp(f, "semi64")) snprintf(b, z, "%+d", v - 64);
    else if (!strcmp(f, "ashape")) {
        if (v == 0) snprintf(b, z, "Osc Off");
        else if (v < 4) snprintf(b, z, "%s", v == 1 ? "Sawtooth" : v == 2 ? "Triangle" : "Saw-Tri");
        else snprintf(b, z, "Pulse %d", v - 4);
    } else if (!strcmp(f, "mix")) snprintf(b, z, "%d/%d", (127 - v) * 100 / 127, v * 100 / 127);
    else if (!strcmp(f, "s127")) snprintf(b, z, "%+d", v - 127);
    else if (!strcmp(f, "dest")) snprintf(b, z, "%s", DEST_NAMES[clampi(v, 0, NDEST - 1)]);
    else if (!strcmp(f, "src")) snprintf(b, z, "%s", SRC_NAMES[clampi(v, 0, 22)]);
    else if (!strcmp(f, "lforate")) {
        int sync = s->f[i == P_LFO1_RATE ? P_LFO1_SYNC : P_LFO2_SYNC];
        if (v > 162 || sync) snprintf(b, z, "%s", SYNC_NAMES[clampi(v > 162 ? v - 163 : v * 16 / 163, 0, 15)]);
        else { float hz = tp_lfo_hz((float)v); snprintf(b, z, hz < 10 ? "%.2f Hz" : "%.1f Hz", hz); }
    } else if (!strcmp(f, "onoff")) snprintf(b, z, v ? "On" : "Off");
    else snprintf(b, z, "%d", v);
    return (int)strlen(b) + 1;
}

/* Plugin state (the wrapper's chunk, 8 KB):
 *   TP2 <bank> <sound> <kit> <kit notes> <pan> <voices> <voice mode> <root> <kit page>
 *   <254 hex digits> <name>                      the edited sound
 *   16 more such lines when kit mode is on      the kit page's sounds, with their edits
 * TP1 (one line: "TP1 <bank> <sound> <hex> <name>") still loads. */
static int read_sound(const char *p, uint8_t *f, char *name, const char **end) {
    uint8_t tmp[NFIELD];
    for (int i = 0; i < NFIELD; i++) {
        unsigned x;
        if (sscanf(p + 2 * i, "%2x", &x) != 1) return 0;
        tmp[i] = (uint8_t)x;
    }
    p += 2 * NFIELD;
    if (*p == ' ') p++;
    const char *nl = strchr(p, '\n');
    size_t l = nl ? (size_t)(nl - p) : strlen(p);
    if (name) snprintf(name, TSND_NAME, "%.*s", (int)(l < TSND_NAME - 1 ? l : TSND_NAME - 1), p);
    memcpy(f, tmp, NFIELD);
    sound_clamp(f);
    if (end) *end = nl ? nl + 1 : p + l;
    return 1;
}
static void state_set(tp_t *s, const char *val) {
    int b = 0, p = 0, pos = 0, kit = 0, kb = 36, pan = 64, nv = 6, mono = 0, root = 60, kp = 0;
    const char *q;
    if (sscanf(val, "TP1 %d %d %n", &b, &p, &pos) >= 2 && pos > 0) q = val + pos;
    else if (sscanf(val, "TP2 %d %d %d %d %d %d %d %d %d%n", &b, &p, &kit, &kb, &pan, &nv, &mono, &root, &kp, &pos) == 9 && pos > 0) {
        q = val + pos;
        while (*q == ' ' || *q == '\n') q++;
    } else return;
    all_off(s);
    if (b >= 0 && b < s->nbanks && b != s->cur_bank) load_bank(s, b);
    s->kit = clampi(kit, 0, 2); s->kit_base = clampi(kb, 0, 112); s->pan = clampi(pan, 0, 127);
    s->nv = clampi(nv, 1, MAXV); s->mono = clampi(mono, 0, 6); s->root = clampi(root, 0, 127); s->kit_page = clampi(kp, 0, 7);
    s->cur_prog = clampi(p, 0, 127);
    if (!read_sound(q, s->f, s->name, &q)) return;
    memcpy(s->bankdata[s->cur_prog], s->f, NFIELD);
    snprintf(s->banknames[s->cur_prog], TSND_NAME, "%s", s->name);
    for (int k = 16 * s->kit_page; k < 16 * s->kit_page + 16 && s->kit && *q; k++)
        if (!read_sound(q, s->bankdata[k], s->banknames[k], &q)) break;
    if (s->kit) memcpy(s->f, s->bankdata[s->cur_prog], NFIELD);
    if (!strncmp(q, "TC", 2)) {
        unsigned a, c2, v;
        const char *t = q + 2;
        int off;
        for (int k = 16 * s->kit_page; k < 16 * s->kit_page + 17 && sscanf(t, " %2x%2x%2x%n", &a, &c2, &v, &off) == 3; k++, t += off) {
            int idx = k < 16 * s->kit_page + 16 ? k : s->cur_prog;
            s->chk[idx][0] = (uint8_t)clampi((int)a, 0, 32); s->chk[idx][1] = (uint8_t)clampi((int)c2, 0, 32); s->chk[idx][2] = (uint8_t)clampi((int)v, 0, 6);
        }
    }
    prepare_samples(s);
    prepare_kit(s);
    browse_follow(s);
    s->display_rev++;
}
static int put_sound(char *b, int o, int n, const uint8_t *f, const char *name) {
    for (int i = 0; i < NFIELD && o + 3 < n; i++) o += snprintf(b + o, (size_t)(n - o), "%02x", f[i]);
    if (o < n) o += snprintf(b + o, (size_t)(n - o), " %s\n", name);
    return o < n ? o : n - 1;
}
static int state_get(tp_t *s, char *b, int n) {
    int o = snprintf(b, (size_t)n, "TP2 %d %d %d %d %d %d %d %d %d\n", s->cur_bank, s->cur_prog, s->kit, s->kit_base, s->pan, s->nv,
                     s->mono, s->root, s->kit_page);
    o = put_sound(b, o, n, s->f, s->name);
    for (int k = 16 * s->kit_page; k < 16 * s->kit_page + 16 && s->kit; k++) o = put_sound(b, o, n, s->bankdata[k], s->banknames[k]);
    int any = 0;       /* choke and voice assign of the kit page's pads and of the edited sound, only when set */
    for (int k = 0; k < 128; k++) if (s->chk[k][0] || s->chk[k][1] || s->chk[k][2]) any = 1;
    if (any && o + 16 < n) {
        o += snprintf(b + o, (size_t)(n - o), "TC");
        for (int k = 16 * s->kit_page; k < 16 * s->kit_page + 16; k++) o += snprintf(b + o, (size_t)(n - o), " %02x%02x%02x", s->chk[k][0], s->chk[k][1], s->chk[k][2]);
        o += snprintf(b + o, (size_t)(n - o), " %02x%02x%02x\n", s->chk[s->cur_prog][0], s->chk[s->cur_prog][1], s->chk[s->cur_prog][2]);
    }
    return o < n ? o + 1 : n;
}

static void tp_set_param(void *h, const char *k, const char *val) {
    tp_t *s = h;
    if (!strcmp(k, "state")) { state_set(s, val); return; }
    int i = find_key(k);
    if (i >= 0) { set_field(s, i, atoi(val)); return; }
    int x = atoi(val);
    if (!strcmp(k, "bank")) { if (x != s->cur_bank && x < s->nbanks) { load_bank(s, x); select_sound(s, 0); } }
    else if (!strcmp(k, "program")) { if (x != s->cur_prog) select_sound(s, x); }
    else if (!strcmp(k, "browse_bank")) { if (x != s->browse_bank && x >= 0 && x < s->nbanks) { s->browse_page = 0; browse_load(s, x); } }
    else if (!strcmp(k, "bank_page")) { s->bank_page = clampi(x, 0, s->nbanks ? (s->nbanks - 1) / BANK_SLOTS : 0); s->display_rev++; }
    else if (!strcmp(k, "patch_page")) { s->browse_page = clampi(x, 0, s->bcount ? (s->bcount - 1) / SOUND_SLOTS : 0); s->display_rev++; }
    else if (!strncmp(k, "bank_slot_", 10)) {      /* a tap on a bank tile: browse it (loads nothing) */
        int b = s->bank_page * BANK_SLOTS + atoi(k + 10) - 1;
        if (b >= 0 && b < s->nbanks) { s->browse_page = 0; browse_load(s, b); }
    }
    else if (!strncmp(k, "patch_slot_", 11)) {      /* a tap on a sound tile: load that bank and sound */
        int idx = s->browse_page * SOUND_SLOTS + atoi(k + 11) - 1;
        if (atoi(k + 11) <= SOUND_SLOTS && idx >= 0 && idx < s->bcount) {
            if (s->browse_bank != s->cur_bank) load_bank(s, s->browse_bank);
            select_sound(s, idx);
        }
    }
    else if (!strcmp(k, "choke1")) s->chk[s->cur_prog][0] = (uint8_t)clampi(x, 0, 32);
    else if (!strcmp(k, "choke2")) s->chk[s->cur_prog][1] = (uint8_t)clampi(x, 0, 32);
    else if (!strcmp(k, "voice_assign")) s->chk[s->cur_prog][2] = (uint8_t)clampi(x, 0, 6);
    else if (!strcmp(k, "osc3_sample")) set_sample(s, 0, x);
    else if (!strcmp(k, "osc4_sample")) set_sample(s, 1, x);
    else if (!strcmp(k, "pan")) s->pan = clampi(x, 0, 127);
    else if (!strcmp(k, "voices")) { int nv = clampi(x, 1, MAXV); if (nv != s->nv) { all_off(s); s->nv = nv; } }
    else if (!strcmp(k, "mono_mode")) { int m = clampi(x, 0, 6); if (m != s->mono) { all_off(s); s->mono = m; } }
    else if (!strcmp(k, "root")) s->root = clampi(x, 0, 127);
    else if (!strcmp(k, "kit")) { int m = clampi(x, 0, 2); if (m != s->kit) { all_off(s); s->kit = m; prepare_kit(s); } }
    else if (!strcmp(k, "kit_base")) { all_off(s); s->kit_base = clampi(x, 0, 112); }
    else if (!strcmp(k, "kit_page")) { all_off(s); s->kit_page = clampi(x, 0, 7); prepare_kit(s); }
    else if (!strcmp(k, "lfo_bpm")) s->host_bpm = (float)atof(val);
    else if (!strcmp(k, "transport")) {
        if (x && !s->transport)       /* LFOs set to restart on Beat or Play start over with the host's transport */
            for (int v = 0; v < MAXV; v++) for (int l = 0; l < 2; l++) if (s->v[v].f[l ? P_LFO2_RESTART : P_LFO1_RESTART] >= 2) s->v[v].lfo[l].ph = 0;
        s->transport = x;
    }
}

static int tp_get_param(void *h, const char *k, char *b, int n) {
    tp_t *s = h;
    size_t z = (size_t)n;
    if (!strcmp(k, "state")) return state_get(s, b, n);
    if (!strcmp(k, "display_rev")) return snprintf(b, z, "%d", s->display_rev) + 1;
    size_t kl = strlen(k);
    if (kl > 8 && !strcmp(k + kl - 8, "_display")) {
        char base[64];
        snprintf(base, sizeof base, "%.*s", (int)(kl - 8), k);
        int i = find_key(base);
        if (i >= 0) return format_value(s, i, b, n);
        if (!strcmp(base, "bank")) return snprintf(b, z, "%d %s", s->cur_bank + 1, s->banks[s->cur_bank].name) + 1;
        if (!strcmp(base, "choke1") || !strcmp(base, "choke2")) {
            int c = s->chk[s->cur_prog][base[5] == '2'], t = s->cur_prog / 32 * 32 + c - 1;
            if (!c) return snprintf(b, z, "Off") + 1;
            return snprintf(b, z, "%d %s", c, t < 128 ? s->banknames[t] : "") + 1;
        }
        if (!strcmp(base, "voice_assign")) {
            int v = s->chk[s->cur_prog][2];
            return v ? snprintf(b, z, "Voice %d", v) + 1 : snprintf(b, z, "Any voice") + 1;
        }
        if (!strcmp(base, "browse_bank")) return snprintf(b, z, "%d %s", s->browse_bank + 1, s->banks[s->browse_bank].name) + 1;
        if (!strcmp(base, "patch_page")) return snprintf(b, z, "Page %d/%d", s->browse_page + 1, s->bcount ? (s->bcount - 1) / SOUND_SLOTS + 1 : 1) + 1;
        if (!strcmp(base, "program")) return snprintf(b, z, "%03d %s", s->cur_prog + 1, s->banknames[s->cur_prog]) + 1;
        if (!strcmp(base, "osc3_sample") || !strcmp(base, "osc4_sample")) {
            int o = base[3] == '4', sm = sample_of(s, o);
            return snprintf(b, z, "%s", TP_SAMPLE_NAMES[sm]) + 1;
        }
        if (!strcmp(base, "pan")) {
            if (s->pan == 64) return snprintf(b, z, "C") + 1;
            return snprintf(b, z, "%s%d", s->pan < 64 ? "L" : "R", abs(s->pan - 64)) + 1;
        }
        if (!strcmp(base, "root")) return snprintf(b, z, "%s%d", NOTE[s->root % 12], s->root / 12 - 2) + 1;
        if (!strcmp(base, "kit_page")) return snprintf(b, z, "Sounds %d-%d", 16 * s->kit_page + 1, 16 * s->kit_page + 16) + 1;
        if (!strcmp(base, "kit_base")) return snprintf(b, z, "%s%d-%s%d", NOTE[s->kit_base % 12], s->kit_base / 12 - 2,
                                                       NOTE[(s->kit_base + 15) % 12], (s->kit_base + 15) / 12 - 2) + 1;
        return 0;
    }
    int i = find_key(k);
    if (i >= 0) return snprintf(b, z, "%d", s->f[i]) + 1;
    if (!strcmp(k, "bank")) return snprintf(b, z, "%d", s->cur_bank) + 1;
    if (!strcmp(k, "program")) return snprintf(b, z, "%d", s->cur_prog) + 1;
    if (!strcmp(k, "patch_name")) return snprintf(b, z, "%s", s->name) + 1;
    if (!strcmp(k, "choke1")) return snprintf(b, z, "%d", s->chk[s->cur_prog][0]) + 1;
    if (!strcmp(k, "choke2")) return snprintf(b, z, "%d", s->chk[s->cur_prog][1]) + 1;
    if (!strcmp(k, "voice_assign")) return snprintf(b, z, "%d", s->chk[s->cur_prog][2]) + 1;
    if (!strcmp(k, "browse_bank")) return snprintf(b, z, "%d", s->browse_bank) + 1;
    if (!strcmp(k, "patch_page")) return snprintf(b, z, "%d", s->browse_page) + 1;
    if (!strcmp(k, "bank_page")) return snprintf(b, z, "%d", s->bank_page) + 1;
    if (!strcmp(k, "bank_range")) {
        int a = s->bank_page * BANK_SLOTS;
        return snprintf(b, z, "Banks %d-%d of %d", a + 1, a + BANK_SLOTS < s->nbanks ? a + BANK_SLOTS : s->nbanks, s->nbanks) + 1;
    }
    {
        size_t kl2 = strlen(k);
        int on = kl2 > 3 && !strcmp(k + kl2 - 3, "_on");
        if (!strncmp(k, "bank_slot_", 10)) {
            int bi = s->bank_page * BANK_SLOTS + atoi(k + 10) - 1;
            if (on) return snprintf(b, z, "%d", bi == s->browse_bank) + 1;
            if (bi < 0 || bi >= s->nbanks) { b[0] = 0; return 1; }
            return snprintf(b, z, "%d %s", bi + 1, s->banks[bi].name) + 1;
        }
        if (!strncmp(k, "patch_slot_", 11)) {
            int idx = s->browse_page * SOUND_SLOTS + atoi(k + 11) - 1;
            if (on) return snprintf(b, z, "%d", s->browse_bank == s->cur_bank && idx == s->cur_prog) + 1;
            if (atoi(k + 11) > SOUND_SLOTS || idx < 0 || idx >= s->bcount) { b[0] = 0; return 1; }
            return snprintf(b, z, "%03d %s", idx + 1, s->bnames[idx]) + 1;
        }
    }
    if (!strcmp(k, "bank_name")) return snprintf(b, z, "%s", s->banks[s->cur_bank].name) + 1;
    if (!strcmp(k, "osc3_sample")) return snprintf(b, z, "%d", sample_of(s, 0)) + 1;
    if (!strcmp(k, "osc4_sample")) return snprintf(b, z, "%d", sample_of(s, 1)) + 1;
    if (!strcmp(k, "pan")) return snprintf(b, z, "%d", s->pan) + 1;
    if (!strcmp(k, "voices")) return snprintf(b, z, "%d", s->nv) + 1;
    if (!strcmp(k, "mono_mode")) return snprintf(b, z, "%d", s->mono) + 1;
    if (!strcmp(k, "root")) return snprintf(b, z, "%d", s->root) + 1;
    if (!strcmp(k, "kit")) return snprintf(b, z, "%d", s->kit) + 1;
    if (!strcmp(k, "kit_base")) return snprintf(b, z, "%d", s->kit_base) + 1;
    if (!strcmp(k, "kit_page")) return snprintf(b, z, "%d", s->kit_page) + 1;
    if (!strcmp(k, "samples_busy")) return snprintf(b, z, "%d", samples_pending(s->smp)) + 1;   /* stand-ins still being built (tests wait on it) */
    if (!strcmp(k, "status")) {
        int u = samples_user_count(s->smp);
        return (u ? snprintf(b, z, "%d banks, %d samples of yours", s->nbanks, u) : snprintf(b, z, "%d banks, stand-in samples", s->nbanks)) + 1;
    }
    return 0;
}

static const mpc_engine_t ENGINE = {tp_create, tp_destroy, tp_midi, tp_set_param, tp_get_param, tp_render, NULL};
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
