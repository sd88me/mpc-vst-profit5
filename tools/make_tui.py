#!/usr/bin/env python3
"""Profit-5's native MPC skin: skins/TUI.json + skins/Q-Links.json + the PNGs they use (after Welle's layout_tab).

The pages are the 0.2.1 skin's own art (vst/art/, drawn by skin/make_skin.py: panel pages, 128-frame knob strips, on/off switch caps)
with live widgets at the positions vst/layout.conf gives them (layout y = screen y + 86). Three tabs as before, then BANKS (drawn
here: bank and program tiles, steppers, RANDOM, +FAV / -FAV); Q-Links in blocks of
at most four per group and four groups per sub-tab, grouped by panel row so the frames do not overlap.
    python3 tools/make_tui.py [--preview]     # -> skins/  (build.sh runs it; --preview also writes build/skin-preview/*.png)"""
import json, os, re, shutil, sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "skins")
ART = os.path.join(ROOT, "vst", "art")
PREVIEW = os.path.join(ROOT, "build", "skin-preview")
LOCK = [l.strip() for l in open(os.path.join(ROOT, "vst", "params.lock")) if l.strip() and not l.startswith("#")]
PARAMS = {p["key"]: p for p in json.load(open(os.path.join(ROOT, "vst", "params.json")))["params"]}
Y_OFF = 86                 # vst/layout.conf y = screen y + 86 (skin/make_skin.py Y_OFF)
FRAMES = 128               # knob strips: 128 frames, square
FONT = "Titillium Web"
GESTURE = "Instant"
RED = "ffff2e26"           # the panel's accent (layout.conf theme_accent)


def P(key):
    return "Parameter %d" % LOCK.index(key)


# ---- a JSON writer that keeps key order and allows the overlay widgets' duplicate "version" key ------------------
class Obj:
    def __init__(self, *pairs):
        self.pairs = list(pairs)


def dump(v, ind=0):
    sp = "  " * ind
    pairs = v.pairs if hasattr(v, "pairs") else list(v.items()) if isinstance(v, dict) else None
    if pairs is not None:
        if not pairs:
            return "{}"
        return "{\n%s\n%s}" % (",\n".join("%s  %s: %s" % (sp, dump(k), dump(x, ind + 1)) for k, x in pairs), sp)
    if isinstance(v, list):
        return "[]" if not v else "[\n%s\n%s]" % (",\n".join(sp + "  " + dump(x, ind + 1) for x in v), sp)
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return repr(v) if isinstance(v, float) else str(v)
    return '"%s"' % str(v).replace("\\", "\\\\").replace('"', '\\"')


# ---- building blocks (Welle's) ---------------------------------------------------------------------------------------
def B(x, y, w, h):
    return "%d %d %d %d" % (x, y, w, h)


def entry(name, typ, data, b, remap=None, focus="No", ind=None):
    bd = {"version": 2 if ind else 1, "acceptsHWFocus": focus, "showWhenDataModelInvalid": "Show", "whenVisible": "Always",
          "boundsType": "Absolute", "bounds": b}
    if ind:   # shown only while the handle has that index (Welle's drawn pick-lists, verified on a Live III)
        bd["additionalInvalidatingHandles"] = ind
    return {"version": 2,
            "componentData": {"version": 1, "name": name, "type": typ, "data": data},
            "handle remapping": {"version": 1, "map": [{"key": "Data", "value": remap}] if remap else []},
            "bounds": bd}


def bg(colour="0"):
    return {"version": 1, "focussed": {"version": 1, "colour": colour, "image": ""}, "unfocussed": {"version": 1, "colour": colour, "image": ""}}


def component(children, actions=None, ignore=False, background=None):
    return {"version": 2, "actions": actions or [], "backgroundData": background or bg(), "ignoreMousePresses": ignore,
            "disableCoarseDataWheel": False, "componentsData": children}


def act(on, handler, extra=""):
    return {"version": 1, "onAction": on, "handler": handler, "handleName": "Data", "additionalData": extra}


FOCUS = {"version": 1, "backgroundColour": "19ffffff", "outlineColour": RED, "backgroundInset": 4.0, "outlineThickness": 2.0}
QL = [act("Mouse Down", "Q-Link"), act("Double Click", "Show Overlay", "knob overlay"), act("Enter Pressed", "Show Overlay", "knob overlay")]


def focus_ring(b):
    e = entry("focus", "Focus", FOCUS, b)
    e["bounds"]["whenVisible"] = "WhenFocussed"
    return e


def text_style(h=22.0, colour="ffeaeae4", style="SemiBold", just="horizontallyCentred verticallyCentred", case="Original"):
    return {"version": 1, "font": {"version": 1, "name": FONT, "style": style, "height": h}, "colour": colour, "justification": just, "case": case}


def label(b, remap=None, **ts):
    return entry("label", "Label", {"version": 1, "textStyle": text_style(**ts), "type": "Value", "handleName": "Data" if remap else "Invalid"}, b, remap)


def overlay_def():
    return component([entry("overlay", "KnobOverlay", Obj(
        ("version", 2), ("version", 1),
        ("actions", [{"version": 1, "onAction": "Focus Lost", "handler": "Focus Lost", "handleName": "Focus Lost", "additionalData": ""}]),
        ("overlayType", "Knob With Header"), ("scaleType", "Linear"), ("polarityType", "Unipolar"),
        ("showMinusSign", True), ("handleName", "Data")), "0 0 1280 800", focus="Yes")], ignore=True)


DEFS = {}


def knob_def(strip, size):
    """A film-strip knob (the 0.2.1 strip, dragged vertically), a focus ring, Q-Link on touch, the overlay on double tap / Enter."""
    key = "knob_" + os.path.splitext(strip)[0]
    if key not in DEFS:
        DEFS[key] = component([
            focus_ring(B(-6, -6, size + 12, size + 12)),
            entry("strip", "Knob", {"version": 3, "knobType": "FilmStrip", "filmStrip": strip, "numFrames": FRAMES, "invert": False,
                                    "dragOrientation": "Vertical", "handleName": "Data"}, B(0, 0, size, size)),
        ], actions=QL)
    return key


def readout_def(w, h, px=30.0):
    """PROGRAM: the slot name (the preset param's own text), focusable, so the data wheel and the Q-Link step the program."""
    key = "readout_%dx%d" % (w, h) + ("" if px == 30.0 else "_%d" % px)
    if key not in DEFS:
        lab = label(B(0, 0, w, h), None, h=px, style="Bold")
        lab["componentData"]["data"]["handleName"] = "Data"
        DEFS[key] = component([focus_ring(B(-4, -4, w + 8, h + 8)), lab], actions=[act("Mouse Down", "Q-Link")])
    return key


def programmer_art():
    """PROGRAMMER's page: the 0.2.1 art with its text areas redrawn for this build (the program selector, imports, favourites and
    ABOUT). The VOICES box and its controls are untouched, so the knob and switch pictures still match the face around them."""
    sys.path.insert(0, os.path.join(ROOT, "skin"))
    import make_skin as ms
    art = Image.open(os.path.join(ART, "program.png")).convert("RGB")
    grain = ms.face(ms.W * ms.SS, ms.H * ms.SS).resize((ms.W, ms.H), Image.LANCZOS)   # the face's paint and grain, fresh
    for box in ((50, 36, 1230, 292), (678, 340, 1230, 592)):                            # inside the PROGRAMMER and ABOUT outlines
        art.paste(grain.crop(box), box[:2])
    ink, dim = ms.INK + (255,), (150, 150, 144, 255)
    hi = Image.new("RGBA", (ms.W * ms.SS, ms.H * ms.SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(hi)

    def face_font(px, bold):   # the panel's FreeSans when installed, else Liberation Sans (SIL OFL, skin/fonts), the same shapes
        free = os.path.join(ms.FONT_DIR, "FreeSansBold.ttf" if bold else "FreeSans.ttf")
        path = free if os.path.exists(free) else os.path.join(ROOT, "skin", "fonts", "LiberationSans-Bold.ttf" if bold else "LiberationSans-Regular.ttf")
        return ImageFont.truetype(path, int(round(px * ms.SS)))

    def text(x, y, s, px, fill=ink, anchor="mm", bold=True):
        d.text((ms.S(x), ms.S(y)), s, font=face_font(px, bold), fill=fill, anchor=anchor)
    def legend(x0, x1, y, label, px=11):   # the panel's group legend: "—— LABEL ——"
        f = face_font(px, True)
        tw = d.textlength(label, font=f) / ms.SS
        cx = (x0 + x1) / 2
        for a, b in ((x0, cx - tw / 2 - 8), (cx + tw / 2 + 8, x1)):
            d.line((ms.S(a), ms.S(y), ms.S(b), ms.S(y)), fill=ink, width=ms.S(1))
        text(cx, y, label, px)
    # PROGRAMMER: the selector, imports on the left, favourites on the right; panel-style captions and legends only
    legend(120, 1160, 168, "PROGRAM")
    text(360, 258, "REPRO-5 .H2P  ·  REV 4 .SYX  ·  .ZIP  FROM BANKS FOLDER", 10, dim)
    legend(70, 650, 280, "IMPORTED PATCHES")
    text(1010, 258, "STORE", 10)
    text(1122, 258, "DELETE", 10)
    legend(950, 1182, 280, "FAVOURITES")
    # ABOUT: a spec plate
    plate = [("VOICES", "5  ·  POLY / UNISON"), ("OSCILLATORS", "2 + NOISE"), ("FILTER", "4-POLE LOW-PASS"),
             ("MODULATION", "POLY-MOD  ·  WHEEL-MOD  ·  LFO"), ("REV 2", "SSM  ·  STEPPED KNOBS"),
             ("REV 4", "CEM  ·  SMOOTH KNOBS  ·  VELOCITY / AT"), ("PROGRAMS", "1-1 TO 5-6  ·  U01-U90  ·  F01-F32  ·  ALL ON BANKS")]
    for i, (k, v) in enumerate(plate):
        y = 372 + i * 28
        text(704, y, k, 11, anchor="lm")
        text(850, y, v, 11, (190, 190, 184, 255), anchor="lm")
    text(704, 574, "*  NOT ON THE 1978 PANEL:  INIT AMT, REV, HALF / FULL, VINTAGE, TOUCH", 9.5, dim, anchor="lm")
    art = art.convert("RGBA")
    art.alpha_composite(hi.resize((ms.W, ms.H), Image.LANCZOS))
    return save(art.convert("RGB"), "program_native.png")


def copy_art(name):
    shutil.copy(os.path.join(ART, name), os.path.join(OUT, name))
    return name


def font(size, bold=True):
    for f in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",):
        if os.path.exists(f):
            return ImageFont.truetype(f, size)
    return ImageFont.load_default()


def arrow(w, h, direction, lit):
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, w - 1, h - 1), 8, fill=(58, 58, 62) if lit else (34, 34, 37), outline=(234, 234, 228), width=2)
    cx, cy, s = w // 2, h // 2, 14
    tip, base = cx + direction * s, cx - direction * s * 0.6
    d.polygon([(tip, cy), (base, cy - s), (base, cy + s)], fill=(255, 46, 38) if lit else (234, 234, 228))
    return im


def text_key(w, h, caption, lit):
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, w - 1, h - 1), 8, fill=(58, 58, 62) if lit else (34, 34, 37), outline=(234, 234, 228), width=2)
    d.text((w // 2, h // 2), caption, font=font(20), fill=(255, 46, 38) if lit else (234, 234, 228), anchor="mm")
    return im


def save(im, name):
    im.save(os.path.join(OUT, name))
    return name


# ---- the old layout ---------------------------------------------------------------------------------------------------
def parse_layout():
    tabs, cur = {}, None
    for line in open(os.path.join(ROOT, "vst", "layout.conf")):
        line = line.strip()
        m = re.match(r"\[tab (.+)\]", line)
        if m:
            cur = tabs.setdefault(m.group(1), {"items": [], "art": None})
            continue
        if not cur or not line or line.startswith("#") or line.startswith("qlinks"):
            continue
        kind, rest = line.split(None, 1)
        a = {k: v.strip('"') for k, v in re.findall(r'(\w+)=("[^"]*"|\S+)', rest)}
        if kind == "art":
            cur["art"] = os.path.basename(a["file"])
        else:
            cur["items"].append((kind, a))
    return tabs


# Q-Link groups per tab: (sub-tab name, [keys of each group]); a group is at most four controls, a sub-tab at most four groups
QLINKS = {
    "OSCILLATORS": [
        ("OSC KNOBS", [["pm_fenv", "pm_oscb", "a_freq", "a_pw"], ["lfo_init", "lfo_freq", "b_freq", "b_fine"], ["b_pw"], ["wm_mix", "mix_a", "mix_b", "mix_noise"]]),
        ("OSC SWITCHES", [["pm_freq_a", "pm_pw_a", "pm_filter"], ["a_saw", "a_pulse", "a_sync"], ["lfo_saw", "lfo_tri", "lfo_square"],
                          ["b_saw", "b_tri", "b_pulse"]]),
        ("MOD SWITCHES", [["b_lo", "b_kbd"], ["wm_freq_a", "wm_freq_b", "wm_pw_a", "wm_pw_b"], ["wm_filter"], ["mod_wheel"]]),
    ],
    "FILTER AMP": [
        ("FILTER AMP", [["glide", "voices", "unison"], ["cutoff", "reso", "env_amt"], ["f_attack", "f_decay", "f_sustain", "f_release"],
                        ["a_attack", "a_decay", "a_sustain", "a_release"]]),
        ("TOUCH SWITCHES", [["rev", "quality", "kbd_half", "f_kbd"], ["vintage", "tune", "volume"], ["vel_filter", "vel_amp", "at_filter", "at_lfo"], ["release"]]),
    ],
    "PROGRAMMER": [
        ("PROGRAMMER", [["preset"], ["voices", "unison", "quality", "rev"]]),
    ],
    "BANKS": [
        ("BANKS", [["browse_bank_index"], ["bank_prog"]]),
    ],
}
TAB_ORDER = ["OSCILLATORS", "FILTER AMP", "BANKS"]   # 3.0: BANKS replaces PROGRAMMER (its VOICES and QUALITY are on FILTER AMP)


class Page:
    def __init__(self, name, fn):
        self.name, self.fn = name, fn
        self.kids, self.extent, self.sample = [], {}, {}

    def add(self, key, ext, e):
        self.kids.append(e)
        self.extent[key] = ext     # (x0, y0, x1, y1) of the control with its printed caption: the Q-Link frame covers these


def build_page(name, fn, spec):
    p = Page(name, fn)
    for kind, a in spec["items"]:
        key = a.get("key")
        if key not in LOCK:
            continue
        cx, cy = int(a["cx"]), int(a["cy"]) - Y_OFF
        if kind == "knob":
            strip = copy_art(os.path.basename(a["strip"]))
            size = Image.open(os.path.join(ART, strip)).width
            r = int(a["r"])
            p.add(key, (cx - r - 34, cy - r - 30, cx + r + 34, cy + r + 34),
                  entry(key, knob_def(strip, size), {"version": 1, "handleName": "Data"}, B(cx - size // 2, cy - size // 2, size, size), P(key), focus="Yes"))
        elif kind == "wheel":   # the MOD wheel: a vertical film strip, dragged up (forward) / down
            w, h, n = int(a["w"]), int(a["h"]), int(a["frames"])
            strip = copy_art(os.path.basename(a["strip"]))
            dk = "wheel_%dx%d" % (w, h)
            if dk not in DEFS:
                DEFS[dk] = component([focus_ring(B(-6, -6, w + 12, h + 12)),
                                      entry("strip", "Knob", {"version": 3, "knobType": "FilmStrip", "filmStrip": strip, "numFrames": n, "invert": False,
                                                              "dragOrientation": "Vertical", "handleName": "Data"}, B(0, 0, w, h))], actions=QL)
            p.add(key, (cx - w // 2 - 8, cy - h // 2 - 8, cx + w // 2 + 8, cy + h // 2 + 34),
                  entry(key, dk, {"version": 1, "handleName": "Data"}, B(cx - w // 2, cy - h // 2, w, h), P(key), focus="Yes"))
        elif kind == "toggle":
            on, off = copy_art(os.path.basename(a["img_on"])), copy_art(os.path.basename(a["img"]))
            w, h = int(a["w"]), int(a["h"])
            p.add(key, (cx - w // 2 - 4, cy - h // 2 - 32, cx + w // 2 + 4, cy + h // 2 + 30),   # caption above or below
                  entry(key, "Button", {"version": 2, "onImage": on, "offImage": off, "buttonId": 1, "numButtonsInGroup": 1, "handleName": "Data",
                                        "gestureBehaviour": GESTURE}, B(cx - w // 2, cy - h // 2, w, h), P(key)))
        elif kind == "stepper":   # PROGRAM - [ name ] PROGRAM +
            w, h = int(a["w"]), int(a["h"])
            x0, aw = cx - w // 2, 72
            prev, nxt = a["key"] + "_prev", a["key"] + "_next"
            p.kids.append(entry(prev, "Button", {"version": 2, "onImage": save(arrow(aw, h, -1, True), "prev_on.png"), "offImage": save(arrow(aw, h, -1, False), "prev_off.png"),
                                                 "buttonId": 1, "numButtonsInGroup": 1, "handleName": "Data", "gestureBehaviour": GESTURE},
                                B(x0, cy - h // 2, aw, h), P(prev)))
            p.kids.append(entry(nxt, "Button", {"version": 2, "onImage": save(arrow(aw, h, 1, True), "next_on.png"), "offImage": save(arrow(aw, h, 1, False), "next_off.png"),
                                                "buttonId": 1, "numButtonsInGroup": 1, "handleName": "Data", "gestureBehaviour": GESTURE},
                                B(x0 + w - aw, cy - h // 2, aw, h), P(nxt)))
            rw = w - 2 * aw - 24
            p.add(key, (x0 - 8, cy - h // 2 - 12, x0 + w + 8, cy + h // 2 + 12),
                  entry(key, readout_def(rw, h), {"version": 1, "handleName": "Data"}, B(x0 + aw + 12, cy - h // 2, rw, h), P(key), focus="Yes"))
            p.sample[key] = "1-1 Brass"
        elif kind == "readout":
            w, h = int(a["w"]), int(a["h"])
            if name == "PROGRAMMER":   # imports on the left of the row, +FAV / -FAV on the right (programmer_art)
                p.kids.append(label(B(70, cy - h // 2 + 4, 580, h), P(key), h=24.0, colour="ffeaeae4", style="SemiBold"))
                p.sample[key] = "1 imported (U01-U01)"
                for k, cap, x in (("fav_add", "+FAV", 960), ("fav_del", "-FAV", 1072)):
                    p.kids.append(entry(k, "Button", {"version": 2, "onImage": save(text_key(100, 42, cap, True), k + "_on.png"),
                                                      "offImage": save(text_key(100, 42, cap, False), k + "_off.png"), "buttonId": 1,
                                                      "numButtonsInGroup": 1, "handleName": "Data", "gestureBehaviour": GESTURE},
                                        B(x, cy - 21, 100, 42), P(k)))
            else:
                p.kids.append(label(B(cx - w // 2, cy - h // 2, w, h), P(key), h=22.0, colour="ffc8c8c0", style="SemiBold"))
                p.sample[key] = "1 imported (U01-U01)"
    return p


# ---- BANKS: the bank and program lists (src/plugin.c), like Profit-08's page in this panel's look ------------------------------
BANK_TILES = dict(n=8, x=36, y=94, w=282, h=56, pitch=64)
PROG_TILES = dict(n=30, rows=10, x=354, y=134, w=290, h=42, pitch_x=300, pitch_y=47)
SAMPLE_BANKS = ["Factory 1978", "INHALT-Stack-and-Split-Programs", "P5 Factory Programs FACTORY v1.03", "P5 Factory Programs USER v1.03", "Repro-5"]
SAMPLE_PROGS = ["1-1 Brass", "1-2 Low Strings", "1-3 Muted Clav", "1-4 Percussive Electric Piano", "1-5 Flutes", "1-6 Harpsichord", "1-7 Sync I",
                "1-8 Percussive Organ", "2-1 Unison Glide With Resonance", "2-2 Harmonium", "2-3 Organ With Resonance", "2-4 Toy Piano", "2-5 Trumpet/Flute",
                "2-6 Filter Mod", "2-7 Reed Organ", "2-8 Brass In Fifths", "3-1 Pipe Organ Flutes", "3-2 Sync II", "3-3 Electric Piano", "3-4 High Strings",
                "3-5 Octave Sawteeth", "3-6 Release-Repeat", "3-7 Delayed Harmonic", "3-8 Echo-Repeat", "4-1 Pulse-Width Mod", "4-2 Slow Sync Sweep",
                "4-3 Fourths With Resonance", "4-4 Sweeping Harmonics", "4-5 Slow Sync", "4-6 Random Arpeggiator", "4-7 Sawtooth Arpeggiator",
                "4-8 Clangorous Bells", "5-1 Alien", "5-2 Noise Sweep", "5-3 Descending Bells", "5-4 Descending Pulse Wave Mod", "5-5 Helicopter",
                "5-6 Resonance Bells"]


def banks_art():
    """BANKS's background: the panel face, the BANKS and PROGRAMS outlines with their legends, the readout windows and every tile's
    slot (the live names and the lit tile are widgets on top)."""
    sys.path.insert(0, os.path.join(ROOT, "skin"))
    import make_skin as ms
    face = ms.face(ms.W * ms.SS, ms.H * ms.SS).resize((ms.W, ms.H), Image.LANCZOS).convert("RGBA")
    S = ms.SS
    hi = Image.new("RGBA", (ms.W * S, ms.H * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(hi)
    ink, dim, slot = ms.INK + (255,), (150, 150, 144, 255), (30, 30, 33, 255)

    def ff(px, bold=True):
        free = os.path.join(ms.FONT_DIR, "FreeSansBold.ttf" if bold else "FreeSans.ttf")
        path = free if os.path.exists(free) else os.path.join(ROOT, "skin", "fonts", "LiberationSans-Bold.ttf" if bold else "LiberationSans-Regular.ttf")
        return ImageFont.truetype(path, int(round(px * S)))

    def box(x0, y0, x1, y1, r=8, fill=None, outline=ink, w=2):
        d.rounded_rectangle((x0 * S, y0 * S, x1 * S, y1 * S), r * S, fill=fill, outline=outline, width=w * S)

    def legend_on(x0, x1, y, txt, left=None):   # a legend breaking the box's top line, like the panel's groups
        f = ff(11)
        tw = d.textlength(txt, font=f) / S
        cx = left + tw / 2 if left else (x0 + x1) / 2
        d.rectangle(((cx - tw / 2 - 8) * S, (y - 3) * S, (cx + tw / 2 + 8) * S, (y + 3) * S), fill=(0, 0, 0, 0))
        d.text((cx * S, y * S), txt, font=f, fill=ink, anchor="mm")

    for (x0, x1), cap in (((24, 524), "BANK"), ((540, 948), "PROGRAM")):   # the two readout windows between their arrows
        box(x0 + 64, 12, x1 - 64, 62, 6, fill=slot, outline=(90, 90, 88, 255), w=1)
        d.text(((x0 + 74) * S, 20 * S), cap, font=ff(9), fill=dim, anchor="lt")
    box(24, 78, 330, 616)
    box(342, 78, 1256, 616)
    bt, pt = BANK_TILES, PROG_TILES
    for i in range(bt["n"]):
        y = bt["y"] + i * bt["pitch"]
        box(bt["x"], y, bt["x"] + bt["w"], y + bt["h"], 5, fill=slot, outline=(70, 70, 68, 255), w=1)
    for i in range(pt["n"]):
        c, r = divmod(i, pt["rows"])
        x, y = pt["x"] + c * pt["pitch_x"], pt["y"] + r * pt["pitch_y"]
        box(x, y, x + pt["w"], y + pt["h"], 4, fill=slot, outline=(70, 70, 68, 255), w=1)
    art = Image.alpha_composite(face, hi.resize((ms.W, ms.H), Image.LANCZOS))
    # legends on the outlines, drawn after the downsample so the gap in the line is clean
    d2 = ImageDraw.Draw(art)
    for x0, x1, txt in ((24, 330, "BANKS"), (342, 700, "PROGRAMS")):
        f = font(13)
        tw = d2.textlength(txt, font=f)
        cx = (x0 + x1) / 2 if txt == "BANKS" else x0 + 40 + tw / 2
        d2.rectangle((cx - tw / 2 - 8, 74, cx + tw / 2 + 8, 82), fill=art.getpixel((int(cx), 70)))
        d2.text((cx, 78), txt, font=f, fill=ms.INK, anchor="mm")
    return save(art.convert("RGB"), "banks_native.png")


def tile_img(w, h, lit):
    im = Image.new("RGBA", (w + 1, h + 1), (0, 0, 0, 0))
    if lit:   # translucent: the name Label underneath shows through (a Label on top of a Button swallows taps)
        ImageDraw.Draw(im).rounded_rectangle([0, 0, w, h], 5, fill=(255, 46, 38, 46), outline=(255, 46, 38, 255), width=2)
    return im


def button(k, on, off, b, ind=None):
    return entry(k, "Button", {"version": 2, "onImage": on, "offImage": off, "buttonId": 1, "numButtonsInGroup": 1, "handleName": "Data",
                               "gestureBehaviour": GESTURE}, b, P(k), ind=ind)


def build_banks(fn):
    p = Page("BANKS", fn)
    # steppers: BANK / PROGRAM, a named whole number between its arrows (the data wheel and the Q-Link step it)
    for key, prev, nxt, x0, x1, sample in (("browse_bank_index", "prev_browse_bank", "next_browse_bank", 24, 524, "P5 Factory Programs FACTORY v1.03"),
                                           ("bank_prog", "bank_prog_prev", "bank_prog_next", 540, 948, "001 It's a Prophet 5")):
        aw, h, y = 56, 50, 12
        p.kids.append(button(prev, save(arrow(aw, h, -1, True), "b_prev_on.png"), save(arrow(aw, h, -1, False), "b_prev_off.png"), B(x0, y, aw, h)))
        p.kids.append(button(nxt, save(arrow(aw, h, 1, True), "b_next_on.png"), save(arrow(aw, h, 1, False), "b_next_off.png"), B(x1 - aw, y, aw, h)))
        rw = x1 - x0 - 2 * aw - 16
        p.add(key, (x0 - 4, y - 6, x1 + 4, y + h + 6),
              entry(key, readout_def(rw, h - 12, 19.0), {"version": 1, "handleName": "Data"}, B(x0 + aw + 8, y + 10, rw, h - 12), P(key), focus="Yes"))
        p.sample[key] = sample
    for k, cap, x, w in (("random_patch", "RANDOM", 962, 104), ("fav_add", "+FAV", 1076, 86), ("fav_del", "-FAV", 1170, 86)):
        p.kids.append(button(k, save(text_key(w, 44, cap, True), "bk_%s_on.png" % k), save(text_key(w, 44, cap, False), "bk_%s_off.png" % k), B(x, 15, w, 44)))
    # page keys in the PROGRAMS header, right
    for k, cap, x in (("patch_page_prev", "<", 1004), ("patch_page_next", ">", 1176)):
        p.kids.append(button(k, save(text_key(68, 38, cap, True), "pg_%s_on.png" % k), save(text_key(68, 38, cap, False), "pg_%s_off.png" % k), B(x, 88, 68, 38)))
    p.kids.append(label(B(1074, 88, 100, 38), P("patch_page_text"), h=19.0, colour="ffc8c8c0", style="SemiBold"))
    p.sample["patch_page_text"] = "PAGE 1/1"
    # tiles: a live name, and over it a transparent button lit while it is the current bank / program
    bt, pt = BANK_TILES, PROG_TILES
    on_b, off_b = save(tile_img(bt["w"], bt["h"], True), "tile_bank_on.png"), save(tile_img(bt["w"], bt["h"], False), "tile_bank_off.png")
    for i in range(bt["n"]):
        y, k = bt["y"] + i * bt["pitch"], "bank_slot_%d" % (i + 1)
        p.kids.append(label(B(bt["x"] + 12, y, bt["w"] - 20, bt["h"]), P(k), h=20.0, colour="ffeaeae4", style="SemiBold", just="left verticallyCentred"))
        p.kids.append(button(k, on_b, off_b, B(bt["x"], y, bt["w"] + 1, bt["h"] + 1)))
        p.sample[k] = SAMPLE_BANKS[i] if i < len(SAMPLE_BANKS) else ""
    on_p, off_p = save(tile_img(pt["w"], pt["h"], True), "tile_prog_on.png"), save(tile_img(pt["w"], pt["h"], False), "tile_prog_off.png")
    for i in range(pt["n"]):
        c, r = divmod(i, pt["rows"])
        x, y, k = pt["x"] + c * pt["pitch_x"], pt["y"] + r * pt["pitch_y"], "patch_slot_%d" % (i + 1)
        p.kids.append(label(B(x + 10, y, pt["w"] - 16, pt["h"]), P(k), h=19.0, colour="ffeaeae4", style="SemiBold", just="left verticallyCentred"))
        p.kids.append(button(k, on_p, off_p, B(x, y, pt["w"] + 1, pt["h"] + 1)))
        p.sample[k] = SAMPLE_PROGS[i] if i < len(SAMPLE_PROGS) else ""
    return p


KB_ROWS = [("1234567890", ["kb_bksp"]), ("QWERTYUIOP", []), ("ASDFGHJKL-", []), ("ZXCVBNM", ["kb_space"])]


def keyboard(p):
    """The drawn keyboard for naming a favourite: a full-page panel, the name, 42 keys; shown while kb_open = 1."""
    ind = ["IndexedEnabling/1/2/%s" % P("kb_open")]
    panel = Image.new("RGB", (1280, 628), (21, 21, 23))
    d = ImageDraw.Draw(panel)
    d.rounded_rectangle((42, 14, 1238, 614), 10, outline=(234, 234, 228), width=2)
    d.text((76, 52), "FAVOURITE NAME", font=font(20), fill=(234, 234, 228), anchor="lm")
    d.rounded_rectangle((300, 26, 1210, 80), 6, fill=(34, 34, 37), outline=(255, 46, 38), width=2)
    max_chars = int(re.search(r"kFavNameMax = (\d+)", open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../src/params_gen.h")).read()).group(1))   # the limit the plugin enforces
    d.text((1196, 53), "MAX %d CHARACTERS" % max_chars, font=font(16), fill=(150, 150, 146), anchor="rm")
    p.kids.append(entry("kb panel", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": save(panel, "kb_panel.png")},
                        "0 0 1280 628", ind=ind))
    p.kids.append(label(B(316, 26, 880, 54), P("fav_name"), h=32.0, style="Bold", just="left verticallyCentred"))
    p.kids[-1]["bounds"] = entry("x", "x", {}, B(316, 26, 880, 54), ind=ind)["bounds"]
    kw, kh, gap, x0, y0 = 98, 84, 8, 70, 104

    def key(k, cap, x, y, w):
        p.kids.append(entry(k, "Button", {"version": 2, "onImage": save(text_key(w, kh, cap, True), k + "_on.png"),
                                          "offImage": save(text_key(w, kh, cap, False), k + "_off.png"), "buttonId": 1, "numButtonsInGroup": 1,
                                          "handleName": "Data", "gestureBehaviour": GESTURE}, B(x, y, w, kh), P(k), ind=ind))
    for r, (chars, extra) in enumerate(KB_ROWS):
        y = y0 + r * (kh + gap)
        x = x0 + r * 30
        for c in chars:
            key("kb_" + ("dash" if c == "-" else c.lower()), c, x, y, kw)
            x += kw + gap
        for k in extra:
            w = 1210 - x
            key(k, {"kb_bksp": "DEL", "kb_space": "SPACE"}[k], x, y, w)
    y = y0 + 4 * (kh + gap)
    key("kb_shift", "SHIFT", x0, y, 200)
    key("kb_cancel", "CANCEL", 640 - 130, y, 260)
    key("kb_save", "SAVE", 1210 - 260, y, 260)


def tabs_for(p, bgname):
    out, qmaps = [], []
    for sub, (subname, groups) in enumerate(QLINKS[p.name]):
        assert len(groups) <= 4 and all(len(g) <= 4 for g in groups), subname
        rects = []
        for g in groups:
            ex = [p.extent[k] for k in g]
            x0, y0, x1, y1 = min(e[0] for e in ex), min(e[1] for e in ex), max(e[2] for e in ex), max(e[3] for e in ex)
            x0, y0, x1, y1 = max(x0, 2), max(y0, 2), min(x1, 1278), min(y1, 626)
            rects.append((x0, y0, x1, y1))
        for i in range(len(rects)):   # frames of one sub-tab must not overlap
            for j in range(i):
                a, b = rects[i], rects[j]
                assert a[2] <= b[0] or b[2] <= a[0] or a[3] <= b[1] or b[3] <= a[1], "%s: Q-Link groups %d and %d overlap" % (subname, j, i)
        kids = [entry("bg", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": bgname}, "0 0 1280 628")] + p.kids
        out.append({"version": 1, "tabName": subname, "fnKeyIndex": p.fn, "fnKeySubIndex": sub,
                    "componentDefinition": component(kids, background=bg("ff151517")),
                    "qlinkBoundsData": [B(r[0], r[1], r[2] - r[0], r[3] - r[1]) for r in rects]})
        q = {"Q-Link %d" % n: -1 for n in range(1, 17)}
        for gi, keys in enumerate(groups):
            for i, key in enumerate(keys):
                q["Q-Link %d" % (13 - 4 * gi + i)] = LOCK.index(key)
        qmaps.append({"Tab": p.fn + 1, "SubTab": sub + 1, "Bank Direction": "RowTopDown", "Q-Links": q})
    return out, qmaps


# ---- preview: the page art with the widgets at their defaults and the Q-Link frames of each sub-tab (offline check) -----------
def render_preview(p, bgname, rects_by_sub):
    outs = []
    if any("additionalInvalidatingHandles" in k["bounds"] for k in p.kids):
        rects_by_sub = rects_by_sub + [(rects_by_sub[0][0] + " keyboard", [])]
    for subname, rects in rects_by_sub:
        shown = subname.endswith(" keyboard")
        img = Image.open(os.path.join(OUT, bgname)).convert("RGBA")
        d = ImageDraw.Draw(img)
        for k in p.kids:
            if ("additionalInvalidatingHandles" in k["bounds"]) != shown and shown:
                continue
            if "additionalInvalidatingHandles" in k["bounds"] and not shown:
                continue
            cd, (x, y, w, h) = k["componentData"], [int(v) for v in k["bounds"]["bounds"].split()]
            typ, data = cd["type"], cd["data"]
            remap = k["handle remapping"]["map"]
            key = LOCK[int(remap[0]["value"].split()[-1])] if remap else None
            spec = PARAMS.get(key, {})
            if typ == "Image":
                img.alpha_composite(Image.open(os.path.join(OUT, data["image"])).convert("RGBA"), (x, y))
            elif typ.startswith("knob_"):
                lo, hi = spec.get("min", 0), spec.get("max", 1)
                frac = (spec.get("default", 0) - lo) / (hi - lo) if hi > lo else 0
                strip = Image.open(os.path.join(OUT, DEFS[typ]["componentsData"][1]["componentData"]["data"]["filmStrip"])).convert("RGBA")
                f = int(round(frac * (FRAMES - 1)))
                img.alpha_composite(strip.crop((0, f * w, w, f * w + w)), (x, y))
            elif typ.startswith("wheel_"):   # the MOD wheel at its rest frame
                strip = Image.open(os.path.join(OUT, DEFS[typ]["componentsData"][1]["componentData"]["data"]["filmStrip"])).convert("RGBA")
                img.alpha_composite(strip.crop((0, 0, w, h)), (x, y))
            elif typ == "Button":
                opts = spec.get("options")
                lit = bool(opts) and opts.index(spec["default"]) == 1
                pic = Image.open(os.path.join(OUT, data["onImage" if lit else "offImage"])).convert("RGBA")
                img.alpha_composite(pic, (x, y))
            elif typ.startswith("readout_") or typ == "Label":
                txt = "Lead 2_" if key == "fav_name" else p.sample.get(key, "")
                ts = k["componentData"]["data"].get("textStyle") or DEFS.get(typ, {}).get("componentsData", [{}, {}])[1].get("componentData", {}).get("data", {}).get("textStyle", {})
                left = "left" in ts.get("justification", "")
                px = int(ts.get("font", {}).get("height", 22 if typ == "Label" else 30))
                d.text((x if left else x + w / 2, y + h / 2), txt, font=font(px), fill=(234, 234, 228), anchor="lm" if left else "mm")
        for r in ([] if os.environ.get("P5_PREVIEW_CLEAN") else rects):   # P5_PREVIEW_CLEAN=1: no Q-Link frames (README pictures)
            d.rectangle(r, outline=(255, 46, 38), width=2)
        outs.append((subname, img))
    return outs


def main():
    preview = "--preview" in sys.argv
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    spec_all = parse_layout()
    tabs, qmaps, repl = [], [], []
    if preview:
        os.makedirs(PREVIEW, exist_ok=True)
    for fn, name in enumerate(TAB_ORDER):
        if name == "BANKS":
            bgname, p = banks_art(), build_banks(fn)
            keyboard(p)   # +FAV opens it over this page too
        else:
            spec = spec_all[name]
            bgname = programmer_art() if name == "PROGRAMMER" else copy_art(spec["art"])
            p = build_page(name, fn, spec)
            if name == "PROGRAMMER":
                keyboard(p)
        t, q = tabs_for(p, bgname)
        tabs += t
        qmaps += q
        if preview:
            subs = [(tb["tabName"], [tuple(map(int, r.split())) for r in tb["qlinkBoundsData"]]) for tb in t]
            subs = [(n, [(x, y, x + w, y + h) for x, y, w, h in rs]) for n, rs in subs]
            for subname, img in render_preview(p, bgname, subs):
                img.convert("RGB").save(os.path.join(PREVIEW, subname.replace(" ", "_").lower() + ".png"))
    for key in LOCK:
        if PARAMS.get(key, {}).get("options"):
            repl.append({"key": P(key), "value": PARAMS[key]["options"]})
    defs = [{"key": "knob overlay", "value": overlay_def()}] + [{"key": k, "value": v} for k, v in DEFS.items()]
    tui = {"pageData": {"version": 1,
                        "componentDefinitions": {"version": 2, "importFiles": [], "localComponentDefinitions": defs},
                        "replacementStrings": repl, "info": {"version": 1, "type": "CompleteDescription"}, "tabs": tabs}}
    open(os.path.join(OUT, "TUI.json"), "w").write(dump(tui) + "\n")
    ql = {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
          "Screen Mode Q-Links": {"version": 4, "map": qmaps}, "Program Mode Q-Links": {"Q-Link %d" % n: -1 for n in range(1, 17)}}
    open(os.path.join(OUT, "Q-Links.json"), "w").write(dump(ql) + "\n")
    used = {k for _, gs in sum(QLINKS.values(), []) for g in gs for k in g}
    missing = [k for k in LOCK if k in PARAMS and k not in used and not PARAMS[k].get("momentary") and PARAMS[k].get("display") != "string" and k != "user"]
    print("wrote %s - %d tabs, %d files; not on a Q-Link: %s" % (OUT, len(tabs), len(os.listdir(OUT)), ", ".join(missing) or "none"))


if __name__ == "__main__":
    main()
