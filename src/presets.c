#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "presets.h"
#include "tsnd.h"

/* Each preset: a name and "key=value" overrides of the init sound, values as stored (bipolar amounts: 127 = 0). */
static const struct { const char *name, *set; } PRESETS[] = {
    {"Basic", "osc1_shape=1 osc2_shape=1 osc2_fine=56 env_gate=1 aenv_s=127"},
    {"Analog Kick", "osc1_freq=24 osc1_shape=2 osc1_key=0 osc1_reset=1 osc_mix=0 lpf_freq=110 penv_amt=175 penv_d=38 "
                    "aenv_amt=40 aenv_d=62 fenv_amt=127 sub_osc=40"},
    {"Noise Snare", "osc1_freq=46 osc1_shape=2 osc1_reset=1 osc_mix=0 osc3_snum=1 osc3_level=110 osc3_key=0 lpf_freq=150 "
                    "lpf_res=20 hpf_freq=30 penv_amt=150 penv_d=30 aenv_d=55 aenv_amt=30"},
    {"Closed Hat", "osc1_shape=0 osc2_shape=0 osc3_snum=1 osc3_level=120 osc4_snum=4 osc4_level=60 lpf_freq=164 hpf_freq=100 "
                   "aenv_d=32 aenv_amt=25"},
    {"Open Hat", "osc1_shape=0 osc2_shape=0 osc3_snum=1 osc3_level=120 osc4_snum=4 osc4_level=60 lpf_freq=164 hpf_freq=96 "
                 "aenv_d=72 aenv_amt=25"},
    {"Tom", "osc1_freq=38 osc1_shape=2 osc1_reset=1 osc_mix=0 osc3_snum=2 osc3_level=25 lpf_freq=120 penv_amt=139 penv_d=50 "
            "aenv_d=66 aenv_amt=30"},
    {"Clap", "osc1_shape=0 osc2_shape=0 osc3_snum=1 osc3_level=120 lpf_freq=150 lpf_res=40 poles=0 hpf_freq=60 aenv_d=50 "
             "aenv_amt=30 mod1_src=6 mod1_amt=180 mod1_dest=18 lfo1_rate=125 lfo1_shape=1 lfo1_amt=0"},
    {"Cowbell", "osc1_freq=73 osc2_freq=79 osc1_shape=54 osc2_shape=54 osc1_reset=1 osc2_reset=1 lpf_freq=130 lpf_res=50 poles=0 "
                "hpf_freq=60 aenv_d=50 aenv_amt=30"},
    {"Zap", "osc1_freq=60 osc1_shape=1 osc_mix=0 penv_amt=230 penv_d=40 lpf_freq=90 lpf_res=90 fenv_amt=200 fenv_d=45 "
            "aenv_d=55 aenv_amt=30"},
    {"Bass", "osc1_freq=36 osc2_freq=36 osc2_fine=56 osc1_shape=1 osc2_shape=54 osc_mix=64 sub_osc=60 lpf_freq=60 lpf_res=40 "
             "fenv_amt=200 fenv_d=60 fenv_s=20 env_gate=1 aenv_s=110 aenv_d=60 aenv_r=40 aenv_amt=40 glide_mode=1 osc1_glide=40 "
             "osc2_glide=40"},
    {"Lead", "osc1_freq=48 osc2_freq=48 osc2_fine=58 osc1_shape=1 osc2_shape=1 osc_mix=64 lpf_freq=95 lpf_res=55 fenv_amt=180 "
             "fenv_d=70 fenv_s=60 env_gate=1 aenv_a=10 aenv_s=120 aenv_r=60 aenv_amt=40 lfo1_rate=60 lfo1_dest=5 lfo1_amt=0 "
             "mod1_src=20 mod1_amt=135 mod1_dest=5"},
    {"Pluck", "osc1_freq=48 osc1_shape=3 osc2_freq=60 osc2_shape=2 osc_mix=40 lpf_freq=50 lpf_res=70 fenv_amt=215 fenv_d=58 "
              "aenv_d=72 aenv_amt=35"},
    {"Pad", "osc1_freq=48 osc2_freq=48 osc2_fine=60 osc1_shape=1 osc2_shape=1 osc3_snum=1 osc3_sbank=3 osc3_level=50 "
            "lpf_freq=80 lpf_res=30 fenv_amt=160 fenv_a=95 fenv_d=100 fenv_s=60 env_gate=1 aenv_a=95 aenv_s=127 aenv_r=100 "
            "aenv_amt=40 slop=3 lfo1_rate=40 lfo1_dest=14 lfo1_amt=15"},
    {"VS Bell", "osc1_shape=0 osc2_shape=0 osc3_snum=115 osc3_sbank=2 osc3_level=127 osc4_snum=41 osc4_sbank=3 osc4_level=80 "
                "osc4_fine=53 lpf_freq=164 aenv_d=95 aenv_amt=35"},
};

int presets_count(void) { return (int)(sizeof PRESETS / sizeof PRESETS[0]); }

void presets_apply(int k, uint8_t f[NFIELD], char *name) {
    for (int i = 0; i < NFIELD; i++) f[i] = (uint8_t)PTAB[i].def;
    if (k < 0 || k >= presets_count()) return;
    if (name) snprintf(name, TSND_NAME, "%s", PRESETS[k].name);
    const char *p = PRESETS[k].set;
    char key[32];
    int val, n;
    while (sscanf(p, " %31[^=]=%d%n", key, &val, &n) == 2) {
        for (int i = 0; i < NFIELD; i++)
            if (!strcmp(PTAB[i].key, key)) { f[i] = (uint8_t)(val < 0 ? 0 : val > PTAB[i].max ? PTAB[i].max : val); break; }
        p += n;
    }
}
