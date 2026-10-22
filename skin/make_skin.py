#!/usr/bin/env python3
"""Profit-5's MPC skin, drawn from scratch for the MPC's 1280 x 628 plugin area, after the instrument's Rev 4 panel layout.

    python3 skin/make_skin.py vst/      # -> vst/art/*.png, vst/layout.conf, vst/preview/*.png

Every material is procedural (no photo, no traced art, no logo): the black face, walnut cheeks, white silkscreen, knurled black
knobs with a white pointer, aluminium caps, square switch caps with red LEDs, the red seven-segment glass. Each tab is drawn once
at 2x without its controls; every control is then a filmstrip (knobs, 128 frames over 300 degrees, matching the printed 0-10 scale)
or an off/on pair cut from that tab with the control drawn in place, so each frame sits exactly over its spot.
Process: mpc-vst-plugins .claude/skills/panel-skin/SKILL.md. Needs Pillow and GNU FreeFont (apt-get install fonts-freefont-ttf)."""
import json
import math
import os
import random
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

SS = 2                      # drawing scale
W, H = 1280, 628            # MPC plugin area
Y_OFF = 86                  # layout y = screen y + 86 (Force Shadow coordinates)
CHEEK = 26                  # walnut cheeks, left and right
FRAMES = 128               # one picture per MPC knob step (MPC resamples a strip to 128 steps)
SWEEP = 150                 # knob travel: -150 .. +150 degrees, 0 to 10 on the scale
FACE = (21, 21, 23)
INK = (234, 234, 228)
LED_ON, LED_OFF = (255, 46, 38), (78, 16, 14)
STYLE = os.environ.get("P5_STYLE", "rev2")   # rev2: the 1978-80 panel (OSC 1 / 2, lighter wood); rev4: the reissue's naming and darker walnut
FONT_DIR = os.environ.get("P5_FONT_DIR", "/usr/share/fonts/truetype/freefont")   # GNU FreeFont (FreeSans), font exception: rendered art is ours

PLAQUE_FONT = os.environ.get("P5_PLAQUE_FONT", os.path.join(os.path.dirname(os.path.abspath(__file__)), "fonts", "UncialAntiqua-Regular.ttf"))   # Uncial Antiqua (SIL OFL 1.1), the nameplate lettering; falls back to FreeSans Bold

_fonts = {}


def font(px, bold=True, cond=True):
    name = "FreeSansBold.ttf" if bold else "FreeSans.ttf"
    k = (name, px)
    if k not in _fonts:
        _fonts[k] = ImageFont.truetype(os.path.join(FONT_DIR, name), int(round(px * SS)))
    return _fonts[k]


def S(v):
    return int(round(v * SS))


def polar(cx, cy, r, deg):
    a = math.radians(deg)
    return cx + r * math.sin(a), cy - r * math.cos(a)


# ---------------------------------------------------------------- materials

def face(w, h, seed=1):
    """The panel's black face: flat paint with a faint grain."""
    rnd = random.Random(seed)
    im = Image.new("RGB", (w, h), FACE)
    noise = Image.effect_noise((w // 4 + 1, h // 4 + 1), 9).resize((w, h), Image.BILINEAR)
    im = Image.blend(im, Image.merge("RGB", [noise.point(lambda v: 14 + v // 18)] * 3), 0.35)
    _ = rnd
    return im


def walnut(w, h, seed=3):
    """Oiled walnut: vertical grain (several sine streaks with noise), darker toward the panel edge."""
    rnd = random.Random(seed)
    im = Image.new("RGB", (w, h))
    px = im.load()
    waves = [(rnd.uniform(0.004, 0.02), rnd.uniform(0, 6.28), rnd.uniform(3, 9)) for _ in range(5)]
    for x in range(w):
        for y in range(h):
            g = sum(math.sin(y * f + p + x * 0.09 * a) for f, p, a in waves) / len(waves)
            g += 0.35 * math.sin(x * 0.55 + 3 * math.sin(y * 0.003))
            t = 0.5 + 0.5 * g
            r, gg, b = (74 + 46 * t, 42 + 28 * t, 26 + 16 * t) if STYLE == "rev4" else (128 + 52 * t, 76 + 34 * t, 36 + 18 * t)
            px[x, y] = (int(r), int(gg), int(b))
    noise = Image.effect_noise((w, h), 24).convert("RGB")
    im = Image.blend(im, Image.composite(im, noise, Image.new("L", (w, h), 220)), 0.5)
    return im.filter(ImageFilter.GaussianBlur(0.6))


def knob_sprite(r, silver=False):
    """A knob seen from above at 2x, pointing up (rotated per frame): knurled skirt, cap, white pointer (dark on aluminium)."""
    R = S(r)
    im = Image.new("RGBA", (2 * R + 4, 2 * R + 4), (0, 0, 0, 0))
    c = R + 2
    d = ImageDraw.Draw(im)
    if silver:
        for i in range(R, 0, -1):
            t = i / R
            v = int(150 + 80 * (1 - t) ** 0.6)
            d.ellipse((c - i, c - i, c + i, c + i), fill=(v, v, v + 4))
        for k in range(36):                                     # brushed rings
            rr = R * (0.2 + 0.8 * k / 36)
            d.ellipse((c - rr, c - rr, c + rr, c + rr), outline=(200, 200, 204, 40), width=1)
        d.line((c, c - R * 0.15, c, c - R * 0.92), fill=(30, 30, 30), width=max(2, S(2.2)))
        d.ellipse((c - R, c - R, c + R, c + R), outline=(90, 90, 94), width=S(1))
        return im
    d.ellipse((c - R, c - R, c + R, c + R), fill=(12, 12, 13))
    n = 28                                                      # knurled skirt
    for k in range(n):
        a = 2 * math.pi * k / n
        x0, y0 = c + R * 0.74 * math.sin(a), c - R * 0.74 * math.cos(a)
        x1, y1 = c + R * 0.98 * math.sin(a), c - R * 0.98 * math.cos(a)
        lit = 0.5 + 0.5 * math.cos(a + 0.8)                    # light from the top left
        v = int(30 + 40 * lit)
        d.line((x0, y0, x1, y1), fill=(v, v, v + 2), width=max(1, S(1.4)))
    rc = R * 0.72                                               # the cap
    for i in range(int(rc), 0, -1):
        t = i / rc
        v = int(26 + 22 * (1 - t))
        d.ellipse((c - i, c - i, c + i, c + i), fill=(v, v, v + 2))
    d.ellipse((c - rc, c - rc, c + rc, c + rc), outline=(70, 70, 74), width=max(1, S(1)))
    d.line((c, c - R * 0.12, c, c - R * 0.96), fill=(240, 240, 236), width=max(2, S(2.6)))
    return im


def switch_sprite(w, lit, color="black"):
    """A square push-button with its LED (2x): bevelled cap, LED near the top."""
    Wd = S(w)
    im = Image.new("RGBA", (Wd, Wd), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    base = {"black": (16, 16, 17), "grey": (128, 128, 130), "red": (196, 30, 26)}[color]
    hi = tuple(min(255, v + 46) for v in base)
    lo = tuple(max(0, v - 12) for v in base)
    d.rounded_rectangle((0, 0, Wd - 1, Wd - 1), radius=S(3), fill=lo)
    d.rounded_rectangle((0, 0, Wd - S(2), Wd - S(2)), radius=S(3), fill=hi)
    d.rounded_rectangle((S(2), S(2), Wd - S(3), Wd - S(3)), radius=S(2), fill=base)
    if color != "red":
        cx, cy, rr = Wd / 2, Wd * 0.36, S(3.2)
        if lit:
            glow = Image.new("RGBA", im.size, (0, 0, 0, 0))
            ImageDraw.Draw(glow).ellipse((cx - rr * 3, cy - rr * 3, cx + rr * 3, cy + rr * 3), fill=(255, 40, 30, 120))
            im.alpha_composite(glow.filter(ImageFilter.GaussianBlur(S(3))))
            d = ImageDraw.Draw(im)
        d.ellipse((cx - rr, cy - rr, cx + rr, cy + rr), fill=LED_ON if lit else LED_OFF)
        if lit:
            d.ellipse((cx - rr * 0.4, cy - rr * 0.6, cx + rr * 0.1, cy - rr * 0.1), fill=(255, 200, 190))
    return im


SEG = {"0": "abcdef", "1": "bc", "2": "abdeg", "3": "abcdg", "4": "bcfg", "5": "acdfg", "6": "acdefg", "7": "abc", "8": "abcdefg", "9": "abcdfg"}


def seven_seg(d, x, y, h, ch, on=(255, 36, 30), off=(54, 8, 8)):
    """One seven-segment digit, slanted, at 2x coordinates (x, y top left, h tall)."""
    w, t = h * 0.52, h * 0.11
    sl = h * 0.08
    def sk(px, py):
        return x + px + sl * (1 - py / h), y + py
    segs = {"a": ((t, 0), (w - t, 0)), "b": ((w, t), (w, h / 2 - t / 2)), "c": ((w, h / 2 + t / 2), (w, h - t)),
            "d": ((t, h), (w - t, h)), "e": ((0, h / 2 + t / 2), (0, h - t)), "f": ((0, t), (0, h / 2 - t / 2)), "g": ((t, h / 2), (w - t, h / 2))}
    for s, (p0, p1) in segs.items():
        d.line((*sk(*p0), *sk(*p1)), fill=on if s in SEG.get(ch, "") else off, width=int(t))


REV2_NAMES = {"OSCILLATOR A": "OSCILLATOR 1", "OSCILLATOR B": "OSCILLATOR 2", "FREQ A": "FREQ 1", "FREQ B": "FREQ 2", "PW A": "PW 1",
              "PW B": "PW 2", "OSC A": "OSC 1", "OSC B": "OSC 2", "FILT ENV": "FIL. ENV.", "GLIDE RATE": "GLIDE", "INITIAL AMOUNT": "INIT AMT *"}


def rename(s):
    return REV2_NAMES.get(s, s) if STYLE == "rev2" else s


# ---------------------------------------------------------------- a tab

class Tab:
    def __init__(self, name, title):
        self.name, self.title = name, title
        self.hi = Image.new("RGB", (W * SS, H * SS))
        self.hi.paste(face(W * SS, H * SS, hash(name) & 255), (0, 0))
        wl = walnut(S(CHEEK), H * SS, 3)
        self.hi.paste(wl, (0, 0))
        self.hi.paste(walnut(S(CHEEK), H * SS, 7), (W * SS - S(CHEEK), 0))
        d = ImageDraw.Draw(self.hi)
        for x in (S(CHEEK), W * SS - S(CHEEK)):                     # the face's edge against the wood
            d.line((x, 0, x, H * SS), fill=(6, 6, 6), width=S(2))
        self.controls, self.qlinks = [], []

    @property
    def d(self):
        return ImageDraw.Draw(self.hi)

    def text(self, x, y, s, px=11, fill=INK, anchor="mm", bold=True):
        s = rename(s)
        self.d.text((S(x), S(y)), s, font=font(px, bold), fill=fill, anchor=anchor)

    def box(self, x0, y0, x1, y1, title=None):
        """A silkscreen section outline, its title breaking the top line between two short rules."""
        d = self.d
        d.rounded_rectangle((S(x0), S(y0), S(x1), S(y1)), radius=S(9), outline=INK, width=S(1.6))
        if title:
            f = font(13)
            tw = d.textlength(title, font=f) / SS
            cx = (x0 + x1) / 2
            d.rectangle((S(cx - tw / 2 - 10), S(y0 - 3), S(cx + tw / 2 + 10), S(y0 + 3)), fill=FACE)
            self.text(cx, y0, title, 13)

    def bracket(self, x0, x1, y, label):
        """A legend under a group with rules each side ('—— SOURCE AMOUNT ——')."""
        d = self.d
        f = font(10)
        tw = d.textlength(label, font=f) / SS
        cx = (x0 + x1) / 2
        d.line((S(x0), S(y), S(cx - tw / 2 - 6), S(y)), fill=INK, width=S(1))
        d.line((S(cx + tw / 2 + 6), S(y), S(x1), S(y)), fill=INK, width=S(1))
        self.text(cx, y, label, 10)

    def scale(self, cx, cy, r):
        """The 0-10 numerals round a knob, as printed (-150 .. +150 degrees)."""
        for i in range(11):
            deg = -SWEEP + 2 * SWEEP * i / 10
            x, y = polar(cx, cy, r + 13, deg)
            self.text(x, y, str(i), 9.5 if i < 10 else 9, bold=False)
            x0, y0 = polar(cx, cy, r + 3, deg)
            x1, y1 = polar(cx, cy, r + 6.5, deg)
            self.d.line((S(x0), S(y0), S(x1), S(y1)), fill=INK, width=S(1))

    def knob(self, key, cx, cy, r=27, label=None, silver=False, scale=True, vs=14, hide_value=False):
        if scale:
            self.scale(cx, cy, r)
        sh = Image.new("RGBA", self.hi.size, (0, 0, 0, 0))               # contact shadow, fixed (it does not turn)
        ImageDraw.Draw(sh).ellipse((S(cx - r + 2), S(cy - r + 4), S(cx + r + 2), S(cy + r + 6)), fill=(0, 0, 0, 170))
        self.hi.paste(Image.alpha_composite(self.hi.convert("RGBA"), sh.filter(ImageFilter.GaussianBlur(S(3)))).convert("RGB"))
        if label:
            self.text(cx, cy + r + 24, label, 10.5)
        self.controls.append(dict(kind="knob", key=key, cx=cx, cy=cy, r=r, silver=silver, vs=vs, hide=hide_value))

    def switch(self, key, cx, cy, w=34, label=None, color="black", above=None):
        if label:
            self.text(cx, cy + w / 2 + 12, label, 10)
        if above:
            self.text(cx, cy - w / 2 - 10, above, 10)
        self.controls.append(dict(kind="switch", key=key, cx=cx, cy=cy, w=w, color=color))

    def wave(self, cx, cy, shape):
        """The waveform symbols printed over the shape switches."""
        d = self.d
        w, h = 7, 6
        pts = {"saw": [(-w, h), (w, -h), (w, h)], "tri": [(-w, h), (0, -h), (w, h)],
               "pulse": [(-w, h), (-w * 0.45, h), (-w * 0.45, -h), (w * 0.45, -h), (w * 0.45, h), (w, h)]}[shape]
        d.line([(S(cx + x), S(cy + y)) for x, y in pts], fill=INK, width=S(1.6), joint="curve")

    def plaque(self, cx, cy, text, w=250, h=74):
        """Our own wordmark on a bevelled, brushed-silver nameplate with screws and engraved lettering (not the instrument's)."""
        W, H = S(w), S(h)
        rnd = random.Random(5)
        # brushed aluminium: horizontal streaks over a soft vertical gradient
        base = Image.new("L", (W, H))
        px = base.load()
        streak = [rnd.randint(-14, 14) for _ in range(H)]
        for y in range(H):
            g = 206 + int(26 * math.cos((y / H - 0.35) * math.pi)) + streak[y]
            for x in range(W):
                px[x, y] = max(0, min(255, g))
        base = base.filter(ImageFilter.GaussianBlur(0.6)).filter(ImageFilter.BoxBlur(1))
        n = Image.effect_noise((W, H), 10).filter(ImageFilter.GaussianBlur(S(14)))
        base = ImageChops.add(base, n.point(lambda v: (v - 128) // 6 if v > 128 else 0))
        pl = Image.merge("RGB", [base.point(lambda v, c=c: min(255, int(v * c))) for c in (0.99, 1.0, 1.03)]).convert("RGBA")
        d = ImageDraw.Draw(pl)
        r = S(9)
        mask = Image.new("L", (W, H), 0)
        ImageDraw.Draw(mask).rounded_rectangle((0, 0, W - 1, H - 1), radius=r, fill=255)
        # bevel: light top-left edge, dark bottom-right edge, then an inset face line
        d.line((r, 1, W - r, 1), fill=(255, 255, 255, 255), width=S(2))
        d.line((1, r, 1, H - r), fill=(255, 255, 255, 230), width=S(2))
        d.line((r, H - 2, W - r, H - 2), fill=(70, 72, 78, 255), width=S(2))
        d.line((W - 2, r, W - 2, H - r), fill=(86, 88, 94, 255), width=S(2))
        d.rounded_rectangle((S(7), S(7), W - S(8), H - S(8)), radius=S(5), outline=(120, 122, 128, 255), width=S(1))
        d.rounded_rectangle((S(8), S(8), W - S(7), H - S(7)), radius=S(5), outline=(250, 250, 252, 160), width=S(1))
        # screws
        for sx in (S(16), W - S(16)):
            sy = H // 2
            sr = S(7)
            d.ellipse((sx - sr, sy - sr, sx + sr, sy + sr), fill=(150, 152, 158, 255), outline=(60, 62, 66, 255), width=S(1))
            d.ellipse((sx - sr + S(1), sy - sr + S(1), sx + sr - S(2), sy + sr - S(2)), outline=(232, 234, 238, 255), width=S(1))
            d.line((sx - S(4.5), sy + S(1.5), sx + S(4.5), sy - S(1.5)), fill=(40, 40, 44, 255), width=S(1.6))
        # engraved lettering: dark ink, a light lip below-right and a shadow above-left
        try:
            f = ImageFont.truetype(PLAQUE_FONT, S(45))
            try:
                f.set_variation_by_axes([850])
            except Exception:
                pass
        except OSError:
            f = font(60, True)
        tx, ty = W // 2, H // 2 + S(1)
        lt = Image.new("L", (W, H), 0)
        ImageDraw.Draw(lt).text((tx, ty), text, font=f, fill=255, anchor="mm")
        lip = ImageChops.offset(lt, S(1.4), S(1.6))
        sh = ImageChops.offset(lt, -S(1), -S(1.2))
        pl.paste(Image.new("RGBA", (W, H), (255, 255, 255, 255)), (0, 0), lip.point(lambda v: v * 200 // 255))
        pl.paste(Image.new("RGBA", (W, H), (60, 60, 66, 255)), (0, 0), sh.filter(ImageFilter.GaussianBlur(S(0.7))).point(lambda v: v * 150 // 255))
        pl.paste(Image.new("RGBA", (W, H), (28, 28, 32, 255)), (0, 0), lt)
        # drop shadow of the plate on the panel
        x0, y0 = S(cx - w / 2), S(cy - h / 2)
        shd = Image.new("RGBA", self.hi.size, (0, 0, 0, 0))
        ImageDraw.Draw(shd).rounded_rectangle((x0 + S(2), y0 + S(5), x0 + W + S(3), y0 + H + S(7)), radius=r, fill=(0, 0, 0, 190))
        self.hi.paste(Image.alpha_composite(self.hi.convert("RGBA"), shd.filter(ImageFilter.GaussianBlur(S(4)))).convert("RGB"))
        self.hi.paste(pl.convert("RGB"), (x0, y0), mask)

    def display(self, cx, cy, digits):
        w, h = 74, 42
        d = self.d
        d.rectangle((S(cx - w / 2), S(cy - h / 2), S(cx + w / 2), S(cy + h / 2)), fill=(24, 4, 4), outline=(60, 60, 62), width=S(1.5))
        for i, ch in enumerate(digits):
            seven_seg(d, S(cx - w / 2 + 12 + i * 28), S(cy - h / 2 + 7), S(28), ch)


# ---------------------------------------------------------------- the three tabs (screen px, after the Rev 4 panel)

def tab_osc():
    t = Tab("OSC", "OSCILLATORS")
    # row 1: POLY-MOD, OSCILLATOR A
    t.box(42, 26, 424, 205, "POLY-MOD")
    t.knob("pm_fenv", 104, 108, label="FILT ENV", hide_value=True); t.knob("pm_oscb", 206, 108, label="OSC B", hide_value=True)
    t.bracket(62, 248, 186, "SOURCE AMOUNT")
    for k, x, s in (("pm_freq_a", 290, "FREQ A"), ("pm_pw_a", 342, "PW A"), ("pm_filter", 394, "FILTER")):
        t.switch(k, x, 104, above=s)
    t.bracket(266, 414, 186, "DESTINATION")
    t.box(452, 26, 1238, 205, "OSCILLATOR A")
    t.knob("a_freq", 548, 108, r=30, label="FREQUENCY")
    t.wave(690, 66, "saw"); t.switch("a_saw", 690, 104)
    t.wave(746, 66, "pulse"); t.switch("a_pulse", 746, 104)
    t.bracket(662, 774, 186, "SHAPE")
    t.knob("a_pw", 892, 108, r=30, label="PULSE WIDTH")
    t.switch("a_sync", 1040, 104, label="SYNC")
    # row 2: LFO, OSCILLATOR B
    t.box(42, 223, 424, 402, "LFO")
    t.knob("lfo_init", 104, 305, label="INITIAL AMOUNT"); t.knob("lfo_freq", 206, 305, label="FREQUENCY")
    for k, x, s in (("lfo_saw", 290, "saw"), ("lfo_tri", 342, "tri"), ("lfo_square", 394, "pulse")):
        t.wave(x, 263, s); t.switch(k, x, 301)
    t.bracket(266, 414, 383, "SHAPE")
    t.box(452, 223, 1238, 402, "OSCILLATOR B")
    t.knob("b_freq", 548, 305, r=30, label="FREQUENCY"); t.knob("b_fine", 662, 305, r=30, label="FINE")
    for k, x, s in (("b_saw", 772, "saw"), ("b_tri", 828, "tri"), ("b_pulse", 884, "pulse")):
        t.wave(x, 263, s); t.switch(k, x, 301)
    t.bracket(744, 912, 383, "SHAPE")
    t.knob("b_pw", 1000, 305, r=30, label="PULSE WIDTH")
    t.switch("b_lo", 1114, 301, label="LO FREQ"); t.switch("b_kbd", 1184, 301, label="KEYBOARD")
    # row 3: WHEEL-MOD, glide, unison
    t.box(42, 420, 424, 600, "WHEEL-MOD")
    t.knob("wm_mix", 104, 502, label=None, hide_value=True)
    t.text(70, 560, "LFO", 9.5); t.text(140, 560, "NOISE", 9.5); t.text(104, 578, "SOURCE MIX", 10)
    for k, x, s in (("wm_freq_a", 182, "FREQ A"), ("wm_freq_b", 232, "FREQ B"), ("wm_pw_a", 282, "PW A"), ("wm_pw_b", 332, "PW B"), ("wm_filter", 382, "FILTER")):
        t.switch(k, x, 498, w=30, above=s)
    t.bracket(162, 402, 580, "DESTINATION")
    t.knob("glide", 548, 502, r=30, label="GLIDE RATE")
    t.switch("unison", 680, 498, label="UNISON")
    t.plaque(1105, 558, "profit~5")
    t.qlinks = [("OSC KNOBS", ["pm_fenv", "pm_oscb", "a_freq", "a_pw", "lfo_init", "lfo_freq", "b_freq", "b_fine", "b_pw", "wm_mix", "glide"]),
                ("MOD SWITCHES", ["pm_freq_a", "pm_pw_a", "pm_filter", "lfo_saw", "lfo_tri", "lfo_square", "wm_freq_a", "wm_freq_b", "wm_pw_a", "wm_pw_b", "wm_filter"]),
                ("OSC SWITCHES", ["a_saw", "a_pulse", "a_sync", "b_saw", "b_tri", "b_pulse", "b_lo", "b_kbd", "unison"])]
    return t


def tab_filter():
    t = Tab("FILTER", "FILTER AMP")
    t.box(42, 26, 410, 205, "MIXER")
    for k, x, s in (("mix_a", 104, "OSC A"), ("mix_b", 226, "OSC B"), ("mix_noise", 348, "NOISE")):
        t.knob(k, x, 108, r=30, label=s)
    t.box(436, 26, 1238, 402, "FILTER")
    t.switch("rev", 500, 104, above="1/2    3", label="REV")
    t.knob("cutoff", 634, 108, r=30, label="CUTOFF"); t.knob("reso", 776, 108, r=30, label="RESONANCE")
    t.knob("env_amt", 918, 108, r=30, label="ENVELOPE AMOUNT")
    t.switch("kbd_half", 1060, 104, above="HALF / FULL"); t.switch("f_kbd", 1160, 104, label="KEYBOARD")
    for k, x, s in (("f_attack", 634, "ATTACK"), ("f_decay", 776, "DECAY"), ("f_sustain", 918, "SUSTAIN"), ("f_release", 1060, "RELEASE")):
        t.knob(k, x, 305, r=30, label=s)
    t.knob("vintage", 104, 305, r=30, label="VINTAGE")
    t.knob("tune", 226, 305, r=26, label="MASTER TUNE")
    t.knob("volume", 348, 305, r=26, label="VOLUME")
    t.box(42, 420, 410, 600, "TOUCH")
    for k, x, s in (("vel_filter", 88, "FLT"), ("vel_amp", 172, "AMP"), ("at_filter", 280, "FLT"), ("at_lfo", 364, "LFO")):
        t.knob(k, x, 494, r=21, label=s)
    t.bracket(56, 216, 580, "VELOCITY"); t.bracket(236, 396, 580, "AFTERTOUCH")
    t.box(436, 420, 1238, 600, "AMPLIFIER")
    for k, x, s in (("a_attack", 634, "ATTACK"), ("a_decay", 776, "DECAY"), ("a_sustain", 918, "SUSTAIN"), ("a_release", 1060, "RELEASE")):
        t.knob(k, x, 502, r=30, label=s)
    t.switch("release", 1170, 498, label="RELEASE")
    t.qlinks = [("FILTER", ["mix_a", "mix_b", "mix_noise", "cutoff", "reso", "env_amt", "f_attack", "f_decay", "f_sustain", "f_release"]),
                ("AMP", ["a_attack", "a_decay", "a_sustain", "a_release", "vintage", "tune", "volume", "vel_filter", "vel_amp", "at_filter", "at_lfo"]),
                ("FILTER SWITCHES", ["rev", "f_kbd", "kbd_half", "release"])]
    return t


def tab_program():
    t = Tab("PROGRAM", "PROGRAMMER")
    t.box(42, 26, 1238, 300, "PROGRAMMER")
    t.text(640, 56, "PROGRAM", 13)
    t.text(640, 188, "IMPORT STATUS", 13)                                       # the name (a live stepper) and the import status (a live readout) are drawn by MPC
    t.text(640, 266, "STEP WITH THE ARROWS, THE DATA WHEEL OR THE MPC PRESET MENU:  38 FACTORY SHEETS, THEN U01-U90 IMPORTED, THEN INIT.", 10, bold=False)
    t.text(640, 284, "NEW FILES IN THE BANKS FOLDER NEED A PLUGIN RELOAD.", 10, bold=False)
    t.box(42, 330, 640, 600, "VOICES")
    t.knob("voices", 150, 450, r=30, label="VOICES", scale=False)
    for i, n in enumerate("12345"):
        x, y = polar(150, 450, 45, -SWEEP + 2 * SWEEP * i / 4)
        t.text(x, y, n, 11)
    t.switch("unison", 300, 446, label="UNISON")
    t.switch("quality", 430, 446, above="ECO / HIGH", label="QUALITY")
    t.switch("rev", 560, 446, above="REV 2 / 4", label="REV")
    t.box(670, 330, 1238, 600, "ABOUT")
    for i, s in enumerate(["Five voices, two VCOs, a 4-pole lowpass, Poly-Mod.", "REV 2: SSM-style VCOs, filter and envelopes, stepped knobs.",
                           "REV 4: CEM-style, smooth knobs, velocity and aftertouch.", "Programs: the 38 factory sheets, transcribed.",
                           "IMPORT: Repro-5 .h2p files or zips in the plugin's banks folder, then reload."]):
        t.text(700, 380 + i * 34, s, 12, anchor="lm", bold=False)
    if STYLE == "rev2":
        t.text(700, 380 + 5 * 34, "* not on the 1978 panel: INIT AMT, REV, HALF/FULL, VINTAGE, TOUCH.", 12, anchor="lm", bold=False)
    t.qlinks = [("PROGRAM", ["preset", "voices", "unison", "quality", "rev"])]
    return t


# ---------------------------------------------------------------- output

def cut(t, cx, cy, w, h):
    return t.hi.crop((S(cx - w / 2), S(cy - h / 2), S(cx + w / 2), S(cy + h / 2))).convert("RGBA")


def final(im, w, h):
    return im.convert("RGB").resize((w, h), Image.LANCZOS)


def norm_default(params, key):
    p = params[key]
    if p.get("options"):
        return p["options"].index(p["default"]) / max(1, len(p["options"]) - 1)
    return (p["default"] - p["min"]) / (p["max"] - p["min"])


def build(root):
    print("skin style:", STYLE)
    params = {p["key"]: p for p in json.load(open(os.path.join(root, "params.json")))["params"]}
    art = os.path.join(root, "art")
    prev_dir = os.path.join(root, "preview")
    os.makedirs(art, exist_ok=True)
    os.makedirs(prev_dir, exist_ok=True)
    lines = ["# Generated by skin/make_skin.py: do not edit. Profit-5's panel, drawn from scratch after the Rev 4 layout (no logos, no traced art).",
             "theme_bg=151517", "theme_panel=151517", "theme_ink=eaeae4", "theme_ink_dim=9a9a94", "theme_accent=ff2e26", "theme_accent_hi=ff6a5a",
             "theme_box=151517", ""]
    seen = set()
    for t in (tab_osc(), tab_filter(), tab_program()):
        bg = t.hi.copy()
        prev = bg.convert("RGBA")
        lines.append("[tab %s]" % t.title)
        lines.append("art file=art/%s.png fit=stretch" % t.name.lower())
        for c in t.controls:
            k = c["key"]
            if k not in params:
                sys.exit("skin: %s is not a parameter" % k)
            dn = norm_default(params, k)
            if c["kind"] == "knob":
                side = 2 * c["r"] + 8
                spr = knob_sprite(c["r"], c["silver"])
                n = len(params[k]["options"]) if params[k].get("options") else FRAMES
                n = min(n, FRAMES) if n > 1 else FRAMES
                frames = []
                for i in range(n):
                    deg = -SWEEP + 2 * SWEEP * i / (n - 1)
                    f = cut(t, c["cx"], c["cy"], side, side)
                    kn = spr.rotate(-deg, resample=Image.BICUBIC)
                    f.alpha_composite(kn, ((f.width - kn.width) // 2, (f.height - kn.height) // 2))
                    frames.append(final(f, side, side))
                name = k if k not in seen else "%s_%s" % (k, t.name.lower())
                strip = Image.new("RGB", (side, side * n))
                for i, f in enumerate(frames):
                    strip.paste(f, (0, i * side))
                strip.save(os.path.join(art, name + ".png"), optimize=True)
                prev.alpha_composite(frames[int(round(dn * (n - 1)))].resize((S(side), S(side))).convert("RGBA"), (S(c["cx"] - side / 2), S(c["cy"] - side / 2)))
                lines.append('knob cx=%d cy=%d r=%d label="" key=%s strip=art/%s.png frames=%d ns=22 vs=%d bw=%d ink=151517 ink_dim=%s'
                             % (c["cx"], c["cy"] + Y_OFF, c["r"], k, name, n, c["vs"], max(76, 2 * c["r"] + 40), "151517" if c.get("hide") else "c8c8c0"))
            else:
                w = c["w"] + 6
                pics = []
                for lit in (0, 1):
                    f = cut(t, c["cx"], c["cy"], w, w)
                    sp = switch_sprite(c["w"], lit, c["color"])
                    f.alpha_composite(sp, ((f.width - sp.width) // 2, (f.height - sp.height) // 2))
                    pics.append(final(f, w, w))
                name = k if k not in seen else "%s_%s" % (k, t.name.lower())
                pics[0].save(os.path.join(art, name + "_off.png"), optimize=True)
                pics[1].save(os.path.join(art, name + "_on.png"), optimize=True)
                prev.alpha_composite(pics[1 if dn >= 0.5 else 0].resize((S(w), S(w))).convert("RGBA"), (S(c["cx"] - w / 2), S(c["cy"] - w / 2)))
                lines.append('toggle cx=%d cy=%d label="" key=%s img=art/%s_off.png img_on=art/%s_on.png w=%d h=%d ns=0'
                             % (c["cx"], c["cy"] + Y_OFF, k, name, name, w, w))
            seen.add(k)
        if t.name == "PROGRAM":
            lines.append('stepper w=1040 h=64 cx=640 cy=%d label="" key=preset get=program_name label_align=center' % (112 + Y_OFF))
            lines.append('readout w=1040 h=38 cx=640 cy=%d label="" key=import_status label_align=center' % (222 + Y_OFF))
        for page, keys in t.qlinks:
            lines.append('qlinks "%s" = %s' % (page, ",".join(keys)))
        lines.append("")
        final(bg, W, H).save(os.path.join(art, t.name.lower() + ".png"), optimize=True)
        final(prev, W, H).save(os.path.join(prev_dir, t.name.lower() + ".png"), optimize=True)
    open(os.path.join(root, "layout.conf"), "w").write("\n".join(lines))
    print("skin: %d tabs -> %s" % (3, os.path.relpath(os.path.join(root, "layout.conf"))))


if __name__ == "__main__":
    build(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "vst"))
