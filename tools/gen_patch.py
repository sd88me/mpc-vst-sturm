#!/usr/bin/env python3
"""The Tempest sound format as one table: the 127 fields a sound dump stores, in their stored order and bit widths.

    gen_patch.py --params     -> vst/params.json  (VST parameter list; index = position, append only)
    gen_patch.py --header     -> src/patch_tab.h  (the engine's table: key, bits, range, init value, display format)

Order, widths, ranges and init values are the main OS 1.5.0.2 descriptor table's (docs/FIRMWARE.md section 3; `tools/fw/tp_fw.py
check MAIN.syx` compares them). Names follow the manual (v1.4, p. 26-40). The sample select is stored as a number 0-127 plus a
bank 0-4 (sample = bank x 128 + number, 464 samples); the plugin adds one combined host parameter for it.
"""
import json
import os
import re
import sys

LFO_SHAPES = ["Triangle", "Rev Saw", "Sawtooth", "Square", "Random"]
GLIDE_MODES = ["FixRate", "FixRate A", "FixTime", "FixTime A"]
RESTART = ["Off", "Note", "Beat", "Play"]
KEY_ASSIGN = ["Low Note", "Low Retrig", "High Note", "High Retrig", "Last Note", "Last Retrig"]

# (key, label, bits, max, default, format)
def osc_a(n):
    return [("osc%d_freq" % n, "Osc%d Freq" % n, 7, 120, 36, "note"), ("osc%d_fine" % n, "Osc%d Fine" % n, 7, 100, 50, "cents"),
            ("osc%d_shape" % n, "Osc%d Shape" % n, 7, 103, 0, "ashape"), ("osc%d_glide" % n, "Osc%d Glide" % n, 7, 127, 0, "int"),
            ("osc%d_key" % n, "Osc%d KeyFollow" % n, 1, 1, 1, "onoff"), ("osc%d_reset" % n, "Osc%d WaveReset" % n, 1, 1, 0, "onoff")]

def osc_d(n):
    return [("osc%d_freq" % n, "Osc%d Pitch" % n, 7, 120, 64, "semi64"), ("osc%d_fine" % n, "Osc%d Fine" % n, 7, 100, 50, "cents"),
            ("osc%d_snum" % n, "Osc%d Smp Num" % n, 8, 127, 0, "int"), ("osc%d_sbank" % n, "Osc%d Smp Bank" % n, 4, 4, 0, "int"),
            ("osc%d_glide" % n, "Osc%d Glide" % n, 7, 127, 0, "int"), ("osc%d_key" % n, "Osc%d KeyFollow" % n, 1, 1, 1, "onoff")]

def lfo(n):
    return [("lfo%d_rate" % n, "LFO%d Rate" % n, 8, 178, 80, "lforate"), ("lfo%d_shape" % n, "LFO%d Shape" % n, 3, 4, 0, LFO_SHAPES),
            ("lfo%d_amt" % n, "LFO%d Amount" % n, 7, 127, 0, "int"), ("lfo%d_dest" % n, "LFO%d Dest" % n, 6, 57, 0, "dest"),
            ("lfo%d_sync" % n, "LFO%d Sync" % n, 1, 1, 0, "onoff")]

def env(k, label, dest_def=0, d=0, s=0, r=0, with_dest=True):
    out = []
    if with_dest:
        out += [("%s_dest" % k, "%s Dest" % label, 6, 57, dest_def, "dest"), ("%s_amt" % k, "%s Amount" % label, 8, 254, 127, "s127"),
                ("%s_vel" % k, "%s Vel Amt" % label, 7, 127, 0, "int")]
    out += [("%s_delay" % k, "%s Delay" % label, 7, 127, 0, "int"), ("%s_a" % k, "%s Attack" % label, 7, 127, 0, "int"),
            ("%s_d" % k, "%s Decay" % label, 7, 127, d, "int"), ("%s_s" % k, "%s Sustain" % label, 7, 127, s, "int"),
            ("%s_r" % k, "%s Release" % label, 7, 123 if k == "aenv" else 127, r, "int")]
    return out

def mod(n):
    return [("mod%d_src" % n, "Mod%d Source" % n, 5, 22, 0, "src"), ("mod%d_amt" % n, "Mod%d Amount" % n, 8, 254, 127, "s127"),
            ("mod%d_dest" % n, "Mod%d Dest" % n, 6, 57, 0, "dest")]

FIELDS = (osc_a(1) + osc_a(2) + osc_d(3) + osc_d(4) + [
    ("sync", "Sync 2>1", 1, 1, 0, "onoff"), ("glide_mode", "Glide Mode", 2, 3, 0, GLIDE_MODES), ("slop", "Osc Slop", 3, 5, 0, "int"),
    ("bend_range", "Bend Range", 4, 12, 2, "int"), ("osc_mix", "Osc 1/2 Mix", 7, 127, 64, "mix"), ("sub_osc", "Sub Osc", 7, 127, 0, "int"),
    ("osc3_level", "Osc3 Level", 7, 127, 120, "int"), ("osc4_level", "Osc4 Level", 7, 127, 120, "int"),
    ("prepost", "Pre/Post Filter", 7, 127, 0, "int"), ("feedback", "Feedback", 7, 127, 0, "int"),
    ("lpf_freq", "LP Freq", 8, 164, 164, "int"), ("lpf_res", "LP Resonance", 7, 127, 0, "int"), ("lpf_key", "LP Key Amt", 7, 127, 0, "int"),
    ("fenv_amt", "LP Env Amount", 8, 254, 127, "s127"), ("fenv_vel", "LP Env Vel Amt", 7, 127, 0, "int"),
    ("audio_mod", "Audio Mod", 7, 127, 0, "int"), ("poles", "4 Pole", 7, 1, 1, ["2 Pole", "4 Pole"]),
    ("hpf_freq", "HP Freq", 7, 127, 0, "int"), ("hpf_key", "HP Key Amt", 7, 127, 0, "int"),
    ("vca_level", "VCA Level", 7, 127, 0, "int"), ("aenv_amt", "Amp Env Amount", 7, 127, 15, "int"),
    ("aenv_vel", "Amp Env Vel Amt", 7, 127, 127, "int"), ("volume", "Volume", 7, 127, 127, "int")]
    + lfo(1) + lfo(2)
    + [("env_gate", "AD Mode", 1, 1, 0, ["On", "Off"])]     # stored as "Env Sustain": 0 = AD mode, 1 = sustain and release used
    + env("penv", "Pitch Env", dest_def=5)
    + env("fenv", "LP Env", d=50, with_dest=False)
    + env("aenv", "Amp Env", d=70, s=120, r=80, with_dest=False)
    + env("x1env", "Aux1 Env") + env("x2env", "Aux2 Env")
    + mod(1) + mod(2) + mod(3) + mod(4) + mod(5) + mod(6) + mod(7) + mod(8) + [
    ("penv_peak", "Pitch Env Peak", 7, 127, 0, "int"), ("fenv_peak", "LP Env Peak", 7, 127, 0, "int"),
    ("aenv_peak", "Amp Env Peak", 7, 127, 0, "int"), ("x1env_peak", "Aux1 Env Peak", 7, 127, 0, "int"),
    ("x2env_peak", "Aux2 Env Peak", 7, 127, 0, "int"),
    ("osc3_rev", "Osc3 Reverse", 1, 1, 0, "onoff"), ("osc4_rev", "Osc4 Reverse", 1, 1, 0, "onoff"),
    ("key_assign", "Key Assign", 4, 1, 0, "int"), ("f124", "Field 124", 5, 31, 3, "int"),
    ("lfo1_restart", "LFO1 Restart", 2, 3, 1, RESTART), ("lfo2_restart", "LFO2 Restart", 2, 3, 1, RESTART)])
assert len(FIELDS) == 127, len(FIELDS)
assert sum(f[2] for f in FIELDS) == 764, sum(f[2] for f in FIELDS)
HIDDEN = {"osc3_snum", "osc3_sbank", "osc4_snum", "osc4_sbank", "f124", "key_assign"}

def engine_names(var):
    """A name table of src/engine.c (the one place the destination and source names live)."""
    src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "engine.c")).read()
    body = src[src.index(var):]
    body = body[body.index("{") + 1:body.index("};")]
    return re.findall(r'"([^"]*)"', body)

# host-side parameters after the sound (append only)
EXTRA = [
    {"key": "bank", "name": "Bank", "min": 0, "max": 63, "default": 0, "display": "int", "dynamic_display": True},
    {"key": "program", "name": "Sound", "min": 0, "max": 127, "default": 0, "display": "int", "dynamic_display": True},
    {"key": "patch_name", "name": "Name", "min": 0, "max": 1, "default": 0, "display": "string"},
    {"key": "bank_name", "name": "Bank Name", "min": 0, "max": 1, "default": 0, "display": "string"},
    {"key": "osc3_sample", "name": "Osc3 Sample", "min": 0, "max": 463, "default": 0, "display": "int", "dynamic_display": True, "nudge_pct": 10},
    {"key": "osc4_sample", "name": "Osc4 Sample", "min": 0, "max": 463, "default": 0, "display": "int", "dynamic_display": True, "nudge_pct": 10},
    {"key": "pan", "name": "Pan", "min": 0, "max": 127, "default": 64, "display": "int", "dynamic_display": True},
    {"key": "voices", "name": "Voices", "min": 1, "max": 8, "default": 6, "display": "int"},
    {"key": "mono_mode", "name": "Voice Mode", "options": ["Poly"] + KEY_ASSIGN, "default": 0},
    {"key": "root", "name": "Root Note", "min": 0, "max": 127, "default": 60, "display": "int", "dynamic_display": True},
    {"key": "status", "name": "Status", "min": 0, "max": 1, "default": 0, "display": "string"},
] + [{"key": "%s_%s" % (k, d), "name": "%s %s" % ("Sound" if k == "program" else "Bank", "<" if d == "prev" else ">"), "min": 0, "max": 1,
      "default": 0, "momentary": True, "type": "trigger", "step_of": k, "step_delta": -1 if d == "prev" else 1}
     for k in ("bank", "program") for d in ("prev", "next")] + [
    # kit mode (append only): 16 notes from Kit Notes play the bank's first 16 sounds, as the instrument's pads play a beat's sounds
    {"key": "kit", "name": "Kit Mode", "options": ["Off", "On", "On + Select"], "default": 0},
    {"key": "kit_base", "name": "Kit Notes", "min": 0, "max": 112, "default": 36, "display": "int", "dynamic_display": True},
    {"key": "kit_page", "name": "Kit Page", "options": ["Sounds %d-%d" % (16 * i + 1, 16 * i + 16) for i in range(8)], "default": 0},
    # the BANKS page (append only), browse a bank without loading it, tap a sound to load it
    {"key": "browse_bank", "name": "Browse Bank", "min": 0, "max": 63, "default": 0, "display": "int", "dynamic_display": True},
    {"key": "patch_page", "name": "Sound Page", "min": 0, "max": 3, "default": 0, "display": "int", "dynamic_display": True},
    {"key": "bank_range", "name": "Bank Range", "min": 0, "max": 1, "default": 0, "display": "string"},
] + [{"key": "%s_%s" % (k, d), "name": "%s %s" % ("Browse" if k == "browse_bank" else "Page", "<" if d == "prev" else ">"), "min": 0, "max": 1,
      "default": 0, "momentary": True, "type": "trigger", "step_of": k, "step_delta": -1 if d == "prev" else 1}
     for k in ("browse_bank", "patch_page") for d in ("prev", "next")] + [
    {"key": "bank_slot_%d" % (i + 1), "name": "Bank %d" % (i + 1), "min": 0, "max": 1, "default": 0, "display": "string"} for i in range(22)
] + [
    {"key": "patch_slot_%d" % (i + 1), "name": "Sound %d" % (i + 1), "min": 0, "max": 1, "default": 0, "display": "string"} for i in range(32)
] + [
    # the instrument's beat settings of the edited sound (append only): the two sounds of its beat it chokes (1-32, 0 none) and Voice Assign
    {"key": "choke1", "name": "Choke 1", "min": 0, "max": 32, "default": 0, "display": "int", "dynamic_display": True},
    {"key": "choke2", "name": "Choke 2", "min": 0, "max": 32, "default": 0, "display": "int", "dynamic_display": True},
    {"key": "voice_assign", "name": "Voice Assign", "min": 0, "max": 6, "default": 0, "display": "int", "dynamic_display": True},
    # the 33rd tile of the sound list (3 columns of 11): always blank, so a page of 32 sounds fills the grid
    {"key": "patch_slot_33", "name": "Sound 33", "min": 0, "max": 1, "default": 0, "display": "string"},
    # the bank list's page (22 banks a page; up to 64 banks) with its two step triggers
    {"key": "bank_page", "name": "Bank Page", "min": 0, "max": 2, "default": 0, "display": "int", "dynamic_display": True},
    # leaves out the sounds of the sound dumps that play PCM samples (we have none): a stand-in plays for the others
    {"key": "sample_filter", "name": "Sample Sounds", "options": ["All sounds", "No drum samples", "No samples"], "default": 0},
] + [{"key": "bank_page_%s" % d, "name": "Bank Page %s" % ("<" if d == "prev" else ">"), "min": 0, "max": 1, "default": 0, "momentary": True,
      "type": "trigger", "step_of": "bank_page", "step_delta": -1 if d == "prev" else 1} for d in ("prev", "next")] + [
    # the output section (append only): the panel's Distortion and Compress knobs, the compressor envelope, and the mixer's delay
    # (delayed notes: Delay Send is how loud they are, Delay On switches them, Repeats and Time shape them)
    {"key": "out_dist", "name": "Distortion", "min": 0, "max": 127, "default": 0, "display": "int"},
    {"key": "out_comp", "name": "Compress", "min": 0, "max": 127, "default": 0, "display": "int"},
    {"key": "comp_attack", "name": "Comp Attack", "min": 0, "max": 127, "default": 20, "display": "int"},
    {"key": "comp_peak", "name": "Comp Peak Hold", "min": 0, "max": 127, "default": 0, "display": "int"},
    {"key": "comp_decay", "name": "Comp Decay", "min": 0, "max": 127, "default": 64, "display": "int"},
    {"key": "comp_amount", "name": "Comp Env Amount", "min": 0, "max": 127, "default": 0, "display": "int"},
    {"key": "delay_send", "name": "Delay Send", "min": 0, "max": 127, "default": 100, "display": "int"},
    {"key": "delay_on", "name": "Delay On", "options": ["Off", "On"], "default": 0},
    {"key": "delay_repeats", "name": "Delay Repeats", "min": 0, "max": 16, "default": 3, "display": "int"},
    {"key": "delay_time", "name": "Delay Time", "options": engine_names("SYNC_NAMES"), "default": 10},
]

def params_json():
    out = []
    for key, name, bits, mx, d, fmt in FIELDS:
        e = {"key": key, "name": name}
        if fmt in ("dest", "src"):      # pickers: an option per value (the value stays the same number)
            e["options"] = engine_names({"dest": "DEST_NAMES", "src": "SRC_NAMES"}[fmt])[:mx + 1]
            e["default"] = d
        elif isinstance(fmt, list):
            e["options"] = fmt[:mx + 1]
            e["default"] = d
        else:
            e.update({"min": 0, "max": mx, "default": d, "display": "int"})
            if fmt != "int":
                e["dynamic_display"] = True
            if key.endswith("_freq") and key.startswith("osc"):
                e["nudge_pct"] = 10     # a data wheel click is 1/100 of the 0-120 range: one semitone, not two
        out.append(e)
    out += EXTRA
    return {"name": "Sturm-TP", "params": out}

def header():
    L = ["/* generated by tools/gen_patch.py: do not edit */", "#pragma once",
         "enum { NFIELD = %d, NSOUND_BITS = 764 };" % len(FIELDS),
         "typedef struct { const char *key; unsigned char bits; short max, def; const char *fmt; } ptab_t;",
         "static const ptab_t PTAB[NFIELD] = {"]
    for key, name, bits, mx, d, fmt in FIELDS:
        f = "opt" if isinstance(fmt, list) else fmt
        L.append('    {"%s", %d, %d, %d, "%s"},' % (key, bits, mx, d, f))
    L.append("};")
    for key, name, bits, mx, d, fmt in FIELDS:
        if isinstance(fmt, list):
            L.append("static const char *const OPT_%s[] = {%s};" % (key.upper(), ", ".join('"%s"' % o for o in fmt)))
    L.append("enum {")
    for i, f in enumerate(FIELDS):
        L.append("    P_%s = %d," % (f[0].upper(), i))
    L.append("};")
    return "\n".join(L) + "\n"

if __name__ == "__main__":
    if "--params" in sys.argv: print(json.dumps(params_json(), indent=1))
    elif "--header" in sys.argv: sys.stdout.write(header())
    else: sys.exit(__doc__)
