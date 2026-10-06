#!/usr/bin/env python3
"""Tempest firmware and sound-dump decoder (development tool; works on your own files, ships nothing).

    tp_fw.py info    FILE.syx...           what each update file is (target chip, decoded size)
    tp_fw.py unpack  FILE.syx -o OUT.bin   the decoded image (local use only: never commit it)
    tp_fw.py params  MAIN.syx              the main CPU's parameter descriptor table (type = bit width, max, default, names)
    tp_fw.py samples MAIN.syx              the sample names the main CPU shows for oscillators 3/4
    tp_fw.py tables  VOICE.syx             the voice CPU's tables, in physical units (5 kHz control rate, 40 MHz timers)
    tp_fw.py sounds  SOUNDS.syx...         decode sound dumps (F0 01 28 63 ...) to name + the 127 stored fields
    tp_fw.py check   MAIN.syx              the firmware's sound field widths/ranges against tools/gen_patch.py

Formats (found 2026-10-06, docs/FIRMWARE.md): F0 01 28 <target> <payload> F7, targets 0x71 main (PIC32, MIPS32 little endian,
loaded at 0x9D000000), 0x72 voice (dsPIC33, 3 bytes per 24-bit word), 0x73 panel (PIC32), 0x74 SAM (the sample chip's code).
The payload is DSI's "packed MS bit" format restarted every 1171 MIDI bytes (1024 decoded bytes), as on the Poly Evolver;
every image ends with one extra byte.
"""
import argparse
import math
import os
import re
import struct
import sys

TARGETS = {0x71: "main CPU (PIC32 / MIPS32 LE, base 0x9D000000)", 0x72: "voice CPU (dsPIC33, 24-bit words as 3 bytes)",
           0x73: "panel CPU (PIC32)", 0x74: "SAM (sample playback chip code)"}
BASE = 0x9D000000


def unpack7(d):
    out = bytearray()
    for i in range(0, len(d), 8):
        m = d[i]
        for j, b in enumerate(d[i + 1:i + 8]):
            out.append(b | (((m >> j) & 1) << 7))
    return bytes(out)


def unpack_fw(path):
    d = open(path, "rb").read()
    if len(d) < 6 or d[:3] != b"\xf0\x01\x28" or d[-1] != 0xF7:
        sys.exit("%s: not a Tempest firmware SysEx (F0 01 28 ... F7)" % path)
    body = d[4:-1]
    out = bytearray()
    for i in range(0, len(body), 1171):
        out += unpack7(body[i:i + 1171])
    return d[3], bytes(out)


# ---------------- main CPU image ----------------
class Main:
    def __init__(self, img):
        self.d = img

    def w(self, o):
        return struct.unpack_from("<I", self.d, o)[0]

    def s(self, a):
        o = a - BASE
        if not 0 <= o < len(self.d):
            return None
        e = self.d.find(b"\0", o)
        t = self.d[o:e]
        return t.decode("latin1") if t and all(32 <= c < 127 for c in t) else ("" if not t else None)

    def is_str(self, a):
        return BASE + 0x60000 <= a < BASE + 0x80000 and self.s(a) is not None

    def descriptors(self):
        """The parameter descriptor table: 20-byte records (type, max, default, name, short name). Found by content: the first
        oscillator's Frequency record (type 7, max 120, default 36, name 'Frequency')."""
        d = self.d
        for o in range(0, len(d) - 20, 4):
            if self.w(o) == 7 and self.w(o + 4) == 120 and self.w(o + 8) == 36 and self.s(self.w(o + 12)) == "Frequency":
                start = o
                break
        else:
            sys.exit("descriptor table not found")
        ok = lambda o: self.w(o) < 64 and self.w(o + 4) < 100000 and self.is_str(self.w(o + 12)) and self.is_str(self.w(o + 16))
        first = start
        while ok(first - 20):
            first -= 20
        recs = []
        o = first
        while ok(o):
            t, mx, df, a, b = (self.w(o + 4 * k) for k in range(5))
            recs.append(dict(off=o, type=t, max=mx, default=df - (1 << 32) if df >= 1 << 31 else df, name=self.s(a), short=self.s(b)))
            o += 20
        return recs, (start - first) // 20

    def sample_names(self):
        """The oscillator 3/4 list: a pointer table starting with 'Osc Off', 'White Noise'."""
        d = self.d
        for o in range(0, len(d) - 8, 4):
            if self.s(self.w(o)) == "Osc Off" and self.s(self.w(o + 4)) == "White Noise":
                out = []
                while self.is_str(self.w(o)) and self.s(self.w(o)):
                    out.append(self.s(self.w(o)))
                    o += 4
                return out
        sys.exit("sample list not found")


# ---------------- sound dumps ----------------
def sound_fields(main):
    """Descriptor records of the 127 stored sound fields (the oscillator 1 Frequency record onwards, up to the LFO restarts)."""
    recs, k = main.descriptors()
    return recs[k:k + 127]


def messages(path):
    d = open(path, "rb").read()
    i = 0
    while True:
        a = d.find(b"\xf0", i)
        if a < 0:
            return
        b = d.find(b"\xf7", a)
        if b < 0:
            return
        yield d[a:b + 1]
        i = b + 1


def sounds(paths):
    """-> (file, path in the instrument, 128-byte body) for each sound message F0 01 28 63 <path length> <packed> F7."""
    for p in paths:
        for m in messages(p):
            if len(m) > 6 and m[1:4] == b"\x01\x28\x63":
                n = m[4]
                u = unpack7(m[5:-1])
                yield p, u[:n].rstrip(b"\0").decode("latin1"), u[n:]


WIDTHS_FALLBACK = None


def decode_sound(body, widths):
    """Bit-packed, LSB first: the 127 fields at their widths (764 bits), 116 reserved bits, then the name as 20 x 7-bit."""
    bits = int.from_bytes(body, "little")
    pos, out = 0, []
    for wdt in widths:
        out.append((bits >> pos) & ((1 << wdt) - 1))
        pos += wdt
    name = "".join(chr((bits >> (880 + 7 * k)) & 0x7F) for k in range(20)).rstrip(" \0")
    return out, name


# ---------------- voice CPU image ----------------
class Voice:
    def __init__(self, img):
        d = img[:65536]
        self.W = [d[i] | d[i + 1] << 8 | d[i + 2] << 16 for i in range(0, len(d) - 2, 3)]

    def lo(self, pa):
        return self.W[pa // 2] & 0xFFFF

    def seq(self, pa, n):
        return [self.lo(pa + 2 * i) for i in range(n)]

    def seq32(self, pa, n):
        return [self.lo(pa + 4 * i) | self.lo(pa + 4 * i + 2) << 16 for i in range(n)]

    def find_psv(self, first_words):
        """Program address of a table whose first 16-bit words are first_words."""
        n = len(first_words)
        for i in range(0, len(self.W) - n):
            if all(self.W[i + k] == first_words[k] for k in range(n)):
                return 2 * i
        return None


def voice_tables(v):
    """Locate the voice tables by content (Voice 1.5 addresses in brackets)."""
    t = {}
    # [0x326] 128 DCO timer periods, 32 bit, 40 MHz: starts 4892488 (= 8.1758 Hz)
    for i in range(len(v.W) - 1):
        if v.W[i] & 0xFFFF == 4892488 & 0xFFFF and v.W[i + 1] & 0xFFFF == 4892488 >> 16:
            t["period"] = 2 * i
            break
    p = t["period"]
    t["delay"] = p + 512          # [0x526] 128 x 16 bit
    t["peak"] = p + 768           # [0x626]
    t["rate"] = p + 1024          # [0x726] 128 x 32 bit
    t["lfo"] = p + 1536           # [0x926] 163 x 32 bit
    t["glide"] = v.find_psv([0, 700, 600, 500, 470])   # [0xF26]
    return t


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["info", "unpack", "params", "samples", "tables", "sounds", "check"])
    ap.add_argument("files", nargs="+")
    ap.add_argument("-o", "--out")
    ap.add_argument("--main", help="main OS file (for the field widths when decoding sounds)")
    a = ap.parse_args()
    if a.cmd == "info":
        for f in a.files:
            t, img = unpack_fw(f)
            print("%s: target 0x%02x %s, %d bytes decoded" % (f, t, TARGETS.get(t, "?"), len(img)))
        return
    if a.cmd == "sounds":
        if not a.main:
            sys.exit("sounds needs --main MAIN.syx (the field widths come from its descriptor table)")
        _, img = unpack_fw(a.main)
        fields = sound_fields(Main(img))
        widths = [r["type"] for r in fields]
        bad = 0
        for f, path, body in sounds(a.files):
            vals, name = decode_sound(body, widths)
            over = [i for i, (x, r) in enumerate(zip(vals, fields)) if x > r["max"]]
            bad += bool(over)
            print("%-40s %-20s %s%s" % (path, name, " ".join(map(str, vals)), "  OVER %s" % over if over else ""))
        print("# %d sounds with out-of-range fields" % bad, file=sys.stderr)
        return
    t, img = unpack_fw(a.files[0])
    if a.cmd == "unpack":
        open(a.out or os.path.basename(a.files[0]) + ".bin", "wb").write(img)
        return
    if a.cmd in ("params", "samples", "check"):
        if t != 0x71:
            sys.exit("%s is not the main OS (target 0x%02x)" % (a.files[0], t))
        m = Main(img)
        if a.cmd == "samples":
            for i, n in enumerate(m.sample_names()):
                print(i, n)
            return
        recs, k = m.descriptors()
        if a.cmd == "params":
            for i, r in enumerate(recs):
                tag = "field %3d" % (i - k) if k <= i < k + 127 else "         "
                print("%3d %s  bits/type %2d  max %4d  default %4d  %-20s %s" % (i, tag, r["type"], r["max"], r["default"], r["name"], r["short"]))
            return
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
        import gen_patch
        fields = recs[k:k + 127]
        bad = 0
        for i, (r, f) in enumerate(zip(fields, gen_patch.FIELDS)):
            if (r["type"], r["max"]) != (f[2], f[3]):
                bad += 1
                print("field %d %s: firmware bits %d max %d, gen_patch bits %d max %d" % (i, f[0], r["type"], r["max"], f[2], f[3]))
        print("sound fields: %d of 127 agree with tools/gen_patch.py" % (127 - bad))
        return
    if a.cmd == "tables":
        if t != 0x72:
            sys.exit("%s is not the voice OS (target 0x%02x)" % (a.files[0], t))
        v = Voice(img)
        tb = voice_tables(v)
        per = v.seq32(tb["period"], 128)
        print("DCO periods @0x%x: 40 MHz / period: note 0 = %.4f Hz, note 127 = %.2f Hz, semitone ratio %.6f" % (
            tb["period"], 40e6 / per[0], 40e6 / per[127], per[0] / per[1]))
        dl = v.seq(tb["delay"], 128)
        print("env delay @0x%x (ticks, counter = 2x, 5 kHz):" % tb["delay"], " ".join("%d:%g" % (i, 2 * x / 5) for i, x in enumerate(dl)), "ms")
        pk = v.seq(tb["peak"], 128)
        print("env peak hold @0x%x (ticks at 5 kHz):" % tb["peak"], " ".join("%d:%g" % (i, x / 5) for i, x in enumerate(pk)), "ms")
        rt = v.seq32(tb["rate"], 128)
        print("env rate @0x%x (per-tick fraction x/2^21; time constant at 5 kHz):" % tb["rate"],
              " ".join("%d:%.4g" % (i, 2 ** 21 / (x * 5000)) for i, x in enumerate(rt)), "s")
        lf = v.seq32(tb["lfo"], 163)
        print("LFO @0x%x (phase wraps at 2^27, 5 kHz):" % tb["lfo"], " ".join("%d:%.4g" % (i, x * 5000 / 2 ** 27) for i, x in enumerate(lf)), "Hz")
        if tb["glide"] is not None:
            g = v.seq(tb["glide"], 128)
            print("glide @0x%x (G/2 per tick in 1/256 semitone):" % tb["glide"],
                  " ".join("%d:%.4g" % (i, (x >> 1) / 256 * 5000) for i, x in enumerate(g)), "semitones/s")


if __name__ == "__main__":
    main()
