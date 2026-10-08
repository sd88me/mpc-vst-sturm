#!/usr/bin/env python3
"""Writes vst/layout.conf and the signal-flow drawings vst/images/flow_*.svg.

The look takes its cues from the instrument's panel without copying it: an ultramarine plate, brighter rounded blue
sections, black pointer knobs, red LEDs, a grey LCD and amber signal-flow lines with arrows between the sections. The
tabs follow the panel's sections in signal order. Geometry is absolute (plugin area x 0-1280, y 92-720): a control sits
in a 140 px cell (MPC's value box under a knob is 130 px wide), a section row is 148 px tall. A page holds at most 16
Q-Link keys; several `qlinks` lines in a tab become sub-pages."""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
VST = os.path.join(HERE, "..", "vst")
OUT = []
CELL, ROW = 140, 150
FLOW = "#4aa8ff"

def emit(s): OUT.append(s)
def Y(i): return 92 + 154 * i     # section rows: 146 px sections, 8 px apart
def tab(name): emit("\n[tab %s]" % name)
def qlinks(name, keys):
    assert len(keys) <= 16, (name, len(keys))
    emit('qlinks "%s" = %s' % (name, ",".join(keys)))

# destination and source pickers: the options in columns (groups), a cell a little narrower than the field
DEST_GROUPS = [("Osc Freq", 6), ("Osc Mix", 6), ("Voice", 8), ("LFO", 6), ("Env Amt", 6), ("Env Att", 6), ("Env Dec", 6),
               ("Env Rel", 6), ("Mods", 8)]
SRC_GROUPS = [("Env", 6), ("LFO/Key", 6), ("Pad/Slider", 5), ("Pedal/Wheel", 6)]
def popup_extra(key):
    g = SRC_GROUPS if key.endswith("_src") else DEST_GROUPS if key.endswith("_dest") else None
    return ' groups="%s" cw=104' % ",".join("%s:%d" % t for t in g) if g else ""

def section(x, y, title, rows, w=None):
    """A frame whose rows are lists of (label, key); '~' marks a toggle (LED), '^' a popup, '' a knob, None an empty cell.
    With w, the frame is that wide and its cells spread evenly over it. Returns (keys in order, frame box)."""
    n = max(len(r) for r in rows)
    cell = CELL if w is None else w // n
    w, h = (CELL * n if w is None else w), ROW * len(rows) - 4
    emit('frame x=%d y=%d w=%d h=%d title="%s"' % (x, y, w, h, title))
    keys = []
    for ri, items in enumerate(rows):
        ry = y + ROW * ri
        for ci, it in enumerate(items):
            if it is None:
                continue
            label, key = it
            cx = x + cell * ci + cell // 2
            if key[0] == "~":
                emit('toggle cx=%d cy=%d label="%s" key=%s look=led' % (cx, ry + 64, label, key[1:]))
            elif key[0] == "^":
                emit('popup cx=%d cy=%d w=126 h=48 label="%s" key=%s%s' % (cx, ry + 96, label, key[1:], popup_extra(key[1:])))
            else:
                emit('knob cx=%d cy=%d r=24 label="%s" key=%s' % (cx, ry + 56, label, key))
            keys.append(key.lstrip("~^"))
    return keys, (x, y, w, h)

# ---- signal-flow drawings (one SVG per tab, drawn into the page background under the sections)
class Flow:
    def __init__(self, name):
        self.name, self.parts = name, []
    def line(self, *pts, arrow=True, dot=True):
        d = "M" + " L".join("%d,%d" % (x, y - 92) for x, y in pts)
        self.parts.append('<path d="%s" fill="none" stroke="%s" stroke-width="3" stroke-linejoin="round"/>' % (d, FLOW))
        if dot:
            x, y = pts[0]
            self.parts.append('<circle cx="%d" cy="%d" r="5.5" fill="%s"/>' % (x, y - 92, FLOW))
        if arrow:
            (x0, y0), (x1, y1) = pts[-2], pts[-1]
            y0, y1 = y0 - 92, y1 - 92
            if x1 != x0:
                s = 1 if x1 > x0 else -1
                p = "%d,%d %d,%d %d,%d" % (x1, y1, x1 - 13 * s, y1 - 7, x1 - 13 * s, y1 + 7)
            else:
                s = 1 if y1 > y0 else -1
                p = "%d,%d %d,%d %d,%d" % (x1, y1, x1 - 7, y1 - 13 * s, x1 + 7, y1 - 13 * s)
            self.parts.append('<polygon points="%s" fill="%s"/>' % (p, FLOW))
    def label(self, x, y, text, anchor="start"):
        self.parts.append('<text x="%d" y="%d" font-family="Titillium Web, sans-serif" font-weight="700" font-size="15" '
                          'letter-spacing="2" fill="%s" text-anchor="%s">%s</text>' % (x, y - 92, FLOW, anchor, text))
    def write(self):
        os.makedirs(os.path.join(VST, "images"), exist_ok=True)
        path = os.path.join(VST, "images", "flow_%s.svg" % self.name)
        with open(path, "w") as f:
            f.write('<svg xmlns="http://www.w3.org/2000/svg" width="1280" height="628" viewBox="0 0 1280 628">\n%s\n</svg>\n'
                    % "\n".join(self.parts))
        emit('art file=images/flow_%s.svg x=0 y=92 w=1280 h=628' % self.name)

def wordmark(x, y, w=300, h=96):
    emit('art file=images/wordmark.svg x=%d y=%d w=%d h=%d' % (x, y, w, h))

HEADER = """# Sturm skin: cues from the instrument without copying it: a charcoal plate between walnut cheeks, thin grey section outlines
# with pale blue titles, black knobs with silver caps, blue LEDs and a blue LCD. Our own drawing (no logos, no traced panel art).
# Written by tools/gen_layout.py: do not edit.
theme_bg=2a2c30
theme_panel=2f3236
theme_line=8d939c
theme_box=1d1f22
theme_ink=e9ecef
theme_ink_dim=c3c8cf
theme_ink_faint=8d939c
theme_accent=4aa8ff
theme_accent_hi=4aa8ff
theme_knob_face=141517
theme_knob_ring=0a0a0b
theme_knob_dot=ffffff
theme_seg_active=4aa8ff
theme_seg_active_tx=0b1020
theme_seg_inactive=3a3d42
theme_btn_bg=d7dade
theme_btn_text=15161a
theme_btn_text_plain=15161a
theme_display_bg=1f3f9e
theme_display_cell=2448ad
theme_display_ink=f2f6ff
theme_display_off=2a4fb5
theme_display_bezel=0b0c0e
theme_lcd=1f3f9e
knob_look=moog
art_css=skin.css"""

M = 30                 # left and right margin: the walnut cheeks are 14 px wide (vst/skin.css); 16 px of plate between
FULL = 1280 - 2 * M

def voice_diagram(x, y, w, h):
    """The voice's signal path as a small drawing of our own (after the manual's architecture page): oscillators and samples into
    the low pass, high pass, VCA and pan; Pre/Post lets the samples skip the filters; the left output feeds back."""
    B, INK, DIM = "#4aa8ff", "#e9ecef", "#a9c4e8"
    def box(bx, by, bw, bh, label):
        return ('<rect x="%d" y="%d" width="%d" height="%d" rx="5" fill="#1d1f22" stroke="%s" stroke-width="1.5"/>'
                '<text x="%d" y="%d" font-family="Titillium Web, sans-serif" font-size="14" font-weight="600" fill="%s" '
                'text-anchor="middle">%s</text>' % (bx, by, bw, bh, DIM, bx + bw // 2, by + bh // 2 + 5, INK, label))
    def arrow(pts, dashed=False):
        d = "M" + " L".join("%d,%d" % p for p in pts)
        (x0, y0), (x1, y1) = pts[-2], pts[-1]
        if x1 != x0:
            s = 1 if x1 > x0 else -1
            head = "%d,%d %d,%d %d,%d" % (x1, y1, x1 - 9 * s, y1 - 5, x1 - 9 * s, y1 + 5)
        else:
            s = 1 if y1 > y0 else -1
            head = "%d,%d %d,%d %d,%d" % (x1, y1, x1 - 5, y1 - 9 * s, x1 + 5, y1 - 9 * s)
        return ('<path d="%s" fill="none" stroke="%s" stroke-width="2"%s/><polygon points="%s" fill="%s"/>'
                % (d, B, ' stroke-dasharray="5 4"' if dashed else "", head, B))
    def label(lx, ly, text, anchor="middle"):
        return ('<text x="%d" y="%d" font-family="Titillium Web, sans-serif" font-size="12" font-style="italic" fill="%s" '
                'text-anchor="%s">%s</text>' % (lx, ly, DIM, anchor, text))
    p = ['<text x="10" y="22" font-family="Titillium Web, sans-serif" font-size="14" font-weight="600" font-style="italic" '
         'letter-spacing="1.5" fill="%s">VOICE</text>' % DIM]
    p.append(box(10, 50, 92, 40, "OSC 1/2"))
    p.append(box(10, 120, 92, 40, "OSC 3/4"))
    p.append(box(140, 85, 70, 40, "LP"))
    p.append(box(240, 85, 70, 40, "HP"))
    p.append(box(340, 85, 60, 40, "VCA"))
    p.append(box(340, 175, 60, 40, "PAN"))
    p.append(arrow([(102, 70), (122, 70), (122, 98), (140, 98)]))
    p.append(arrow([(102, 140), (122, 140), (122, 112), (140, 112)]))
    p.append(arrow([(210, 98), (240, 98)]))
    p.append(arrow([(310, 98), (340, 98)]))
    p.append(arrow([(370, 125), (370, 175)]))
    p.append(arrow([(400, 195), (448, 195)]))
    p.append(label(448, 220, "L R", "end"))
    p.append(arrow([(56, 160), (56, 240), (325, 240), (325, 117), (340, 117)], dashed=True))
    p.append(label(188, 258, "PRE/POST: samples past the filters"))
    p.append(arrow([(340, 195), (175, 195), (175, 125)], dashed=True))
    p.append(label(258, 188, "FEEDBACK (left output)"))
    os.makedirs(os.path.join(VST, "images"), exist_ok=True)
    with open(os.path.join(VST, "images", "voice.svg"), "w") as f:
        f.write('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">\n%s\n</svg>\n'
                % (w, h, w, h, "\n".join(p)))
    emit('art file=images/voice.svg x=%d y=%d w=%d h=%d' % (x, y, w, h))

def env_row(y, title, k, dest=True, extra=()):
    items = []
    if dest:
        items += [("DESTINATION", "^%s_dest" % k), ("AMOUNT", "%s_amt" % k), ("VELOCITY", "%s_vel" % k)]
    items += [("DELAY", "%s_delay" % k), ("ATTACK", "%s_a" % k), ("PEAK", "%s_peak" % k), ("DECAY", "%s_d" % k),
              ("SUSTAIN", "%s_s" % k), ("RELEASE", "%s_r" % k)] + list(extra)
    return section(M, y, title, [items], FULL)

def main():
    emit(HEADER)

    # ---- SOUND: the sound and status line, the voice settings, the panel's quick controls, the kit and its beat settings
    tab("SOUND")
    emit('stepper style=dotmatrix cx=%d cy=124 w=280 h=48 label="" key=program' % (M + 140))
    emit('readout style=dotmatrix cx=%d cy=124 w=340 h=48 label="" key=patch_name' % (M + 280 + 10 + 170))
    emit('readout style=dotmatrix cx=%d cy=124 w=570 h=48 label="" key=status' % (M + FULL - 285))
    vo, _ = section(M, 164, "VOICE", [[("VOICES", "voices"), ("VOICE MODE", "^mono_mode"), ("ROOT NOTE", "root"), ("PAN", "pan"),
                     ("VOLUME", "volume"), ("BEND RANGE", "bend_range"), ("GLIDE MODE", "^glide_mode"), ("OSC SLOP", "slop"),
                     ("AD MODE", "^env_gate")]], FULL)
    qk, _ = section(M, 318, "PANEL", [[("LP FREQ", "lpf_freq"), ("RESONANCE", "lpf_res"), ("AUDIO MOD", "audio_mod"),
                     ("LP ENV", "fenv_amt"), ("HP FREQ", "hpf_freq"), ("FEEDBACK", "feedback"), ("ATTACK", "aenv_a"),
                     ("DECAY", "aenv_d"), ("PITCH ENV", "penv_amt")]], FULL)
    # kit mode: 16 pads play 16 sounds; the last six knobs are the edited sound's beat settings (what it chokes, which voice it uses)
    kt, _ = section(M, 472, "KIT  16 PADS, 16 SOUNDS  (CHOKE AND VOICE: THE SELECTED PAD)",
                    [[("KIT MODE", "^kit"), ("KIT NOTES", "kit_base"), ("KIT PAGE", "kit_page"), ("CHOKE 1", "choke1"),
                      ("CHOKE 2", "choke2"), ("VOICE ASSIGN", "voice_assign")]], 6 * 140)
    wordmark(M + 6 * 140 + 20, 482, 360, 100)
    qlinks("Sound", ["program", "bank", "volume", "pan", "voices", "mono_mode", "root", "bend_range"] + qk[:8])
    qlinks("Panel", qk + ["glide_mode", "slop", "env_gate", "aenv_amt"])
    qlinks("Kit", kt + ["program"])

    # ---- BANKS: pick a bank and a sound from lists; the Q-Links are bank, sound, page back, page on (the wheel steps what is selected)
    tab("BANKS")
    # two panels, each with its picker and name on top, its table, and its page control under the table; the sample selector sits
    # under the sound table beside the sound page control. Everything in a panel is as wide as, and lined up with, its table.
    BW = 500                                       # the banks panel; the sounds panel takes the rest
    SX = M + BW + 16
    SW = FULL - BW - 16
    emit('frame x=%d y=96 w=%d h=604 title="BANKS"' % (M, BW))
    emit('stepper style=dotmatrix cx=%d cy=150 w=%d h=44 label="" key=browse_bank' % (M + BW // 2, BW - 24))
    emit('list x=%d y=180 w=%d h=456 cols=2 rows=11 gap=6 th=36 key=bank_slot order=cols' % (M + 12, BW - 24))
    emit('stepper style=dotmatrix cx=%d cy=670 w=%d h=42 label="" key=bank_page' % (M + BW // 2, BW - 24))
    emit('frame x=%d y=96 w=%d h=604 title="SOUNDS"' % (SX, SW))
    emit('stepper style=dotmatrix cx=%d cy=150 w=%d h=44 label="" key=program' % (SX + SW // 2, SW - 24))
    emit('list x=%d y=180 w=%d h=462 cols=2 rows=16 gap=2 th=27 key=patch_slot order=cols' % (SX + 12, SW - 24))
    half = (SW - 24 - 8) // 2
    emit('stepper style=dotmatrix cx=%d cy=670 w=%d h=42 label="" key=patch_page' % (SX + 12 + half // 2, half))
    emit('popup cx=%d cy=670 w=%d h=42 label="" key=sample_filter' % (SX + 12 + half + 8 + half // 2, half))
    qlinks("Banks", ["browse_bank", "bank_page", "program", "patch_page", "bank_page_prev", "bank_page_next", "patch_page_prev", "patch_page_next", "sample_filter"])

    # ---- OSC: two analog oscillators and two sample oscillators
    tab("OSC")
    o1, _ = section(M, Y(0), "OSC 1  ANALOG", [[("FREQUENCY", "osc1_freq"), ("FINE", "osc1_fine"), ("SHAPE", "osc1_shape"),
                     ("GLIDE", "osc1_glide"), ("KEY FOLLOW", "~osc1_key"), ("WAVE RESET", "~osc1_reset"), ("SYNC 2>1", "~sync"),
                     ("SUB OSC", "sub_osc")]], FULL)
    o2, _ = section(M, Y(1), "OSC 2  ANALOG", [[("FREQUENCY", "osc2_freq"), ("FINE", "osc2_fine"), ("SHAPE", "osc2_shape"),
                     ("GLIDE", "osc2_glide"), ("KEY FOLLOW", "~osc2_key"), ("WAVE RESET", "~osc2_reset"), ("OSC SLOP", "slop"),
                     ("1/2 MIX", "osc_mix")]], FULL)
    o3, _ = section(M, Y(2), "OSC 3  SAMPLE", [[("SAMPLE", "osc3_sample"), ("PITCH", "osc3_freq"), ("FINE", "osc3_fine"),
                     ("LEVEL", "osc3_level"), ("GLIDE", "osc3_glide"), ("KEY FOLLOW", "~osc3_key"), ("REVERSE", "~osc3_rev"),
                     ("PRE/POST", "prepost")]], FULL)
    o4, _ = section(M, Y(3), "OSC 4  SAMPLE", [[("SAMPLE", "osc4_sample"), ("PITCH", "osc4_freq"), ("FINE", "osc4_fine"),
                     ("LEVEL", "osc4_level"), ("GLIDE", "osc4_glide"), ("KEY FOLLOW", "~osc4_key"), ("REVERSE", "~osc4_rev"),
                     ("GLIDE MODE", "^glide_mode")]], FULL)
    qlinks("Analog", o1 + o2)
    qlinks("Samples", o3 + o4)

    # ---- FILTER: low pass with its envelope, high pass and feedback, amp
    tab("FILTER")
    lp, _ = section(M, Y(0), "LOW PASS", [[("4 POLE", "~poles"), ("FREQUENCY", "lpf_freq"), ("RESONANCE", "lpf_res"),
                     ("ENV AMOUNT", "fenv_amt"), ("VELOCITY", "fenv_vel"), ("KEY AMOUNT", "lpf_key"), ("AUDIO MOD", "audio_mod")]], FULL)
    le, _ = env_row(Y(1), "LOW PASS ENVELOPE", "fenv", dest=False)
    hp, _ = section(M, Y(2), "HIGH PASS", [[("FREQUENCY", "hpf_freq"), ("KEY AMOUNT", "hpf_key")]], 2 * 155)
    fb, _ = section(M + 2 * 155 + 10, Y(2), "FEEDBACK", [[("LEVEL", "feedback")]], 155)
    va, _ = section(M, Y(3), "AMP", [[("VCA LEVEL", "vca_level"), ("ENV AMOUNT", "aenv_amt"), ("VELOCITY", "aenv_vel"),
                     ("VOLUME", "volume"), ("PAN", "pan")]], 5 * 150)
    voice_diagram(790, Y(2), 460, 300)
    qlinks("Low Pass", lp + le + ["hpf_freq", "hpf_key", "feedback"])
    qlinks("HP / Amp", hp + fb + va)

    # ---- ENVELOPES: amp, pitch, aux 1, aux 2
    tab("ENV")
    ae, _ = env_row(Y(0), "AMP ENVELOPE", "aenv", dest=False, extra=[("AMOUNT", "aenv_amt"), ("VELOCITY", "aenv_vel")])
    pe, _ = env_row(Y(1), "PITCH ENVELOPE", "penv")
    x1, _ = env_row(Y(2), "AUX 1 ENVELOPE", "x1env")
    x2, _ = env_row(Y(3), "AUX 2 ENVELOPE", "x2env")
    qlinks("Amp/Pitch", ae + pe[:8])
    qlinks("Pitch/Aux", pe[8:] + x1 + x2[:6])
    qlinks("Aux 2", x2)

    # ---- LFO: the two LFOs; MODS: the eight paths
    tab("LFO")
    l1, _ = section(M, Y(0), "LFO 1", [[("RATE", "lfo1_rate"), ("SHAPE", "^lfo1_shape"), ("AMOUNT", "lfo1_amt"),
                     ("DESTINATION", "^lfo1_dest"), ("SYNC", "~lfo1_sync"), ("RESTART", "^lfo1_restart")]], FULL)
    l2, _ = section(M, Y(1), "LFO 2", [[("RATE", "lfo2_rate"), ("SHAPE", "^lfo2_shape"), ("AMOUNT", "lfo2_amt"),
                     ("DESTINATION", "^lfo2_dest"), ("SYNC", "~lfo2_sync"), ("RESTART", "^lfo2_restart")]], FULL)
    wordmark(FULL + M - 360, Y(3) + 20, 360, 100)
    qlinks("LFOs", l1 + l2)

    tab("MODS")
    mods = []
    for n in range(8):
        k, _ = section(M + 618 * (n // 4), Y(n % 4), "MOD %d" % (n + 1), [[("SOURCE", "^mod%d_src" % (n + 1)),
                       ("AMOUNT", "mod%d_amt" % (n + 1)), ("DESTINATION", "^mod%d_dest" % (n + 1))]], 602)
        mods += k
    qlinks("Mods 1-4", mods[:12])
    qlinks("Mods 5-8", mods[12:])

    with open(os.path.join(VST, "layout.conf"), "w") as f:
        f.write("\n".join(OUT) + "\n")

if __name__ == "__main__":
    main()
