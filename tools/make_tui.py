#!/usr/bin/env python3
"""Profit-5's native MPC skin: skins/TUI.json + skins/Q-Links.json + the PNGs they use (docs/SDK-PORTING.md, after Welle's layout_tab).

The pages are the 0.2.1 skin's own art (vst/art/, drawn by skin/make_skin.py: panel pages, 128-frame knob strips, on/off switch caps)
with live widgets at the positions vst/layout.conf gives them (layout y = screen y + 86). Three tabs as before; Q-Links in blocks of
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


def entry(name, typ, data, b, remap=None, focus="No"):
    return {"version": 2,
            "componentData": {"version": 1, "name": name, "type": typ, "data": data},
            "handle remapping": {"version": 1, "map": [{"key": "Data", "value": remap}] if remap else []},
            "bounds": {"version": 1, "acceptsHWFocus": focus, "showWhenDataModelInvalid": "Show", "whenVisible": "Always",
                       "boundsType": "Absolute", "bounds": b}}


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


def readout_def(w, h):
    """PROGRAM: the slot name (the preset param's own text), focusable, so the data wheel and the Q-Link step the program."""
    key = "readout_%dx%d" % (w, h)
    if key not in DEFS:
        lab = label(B(0, 0, w, h), None, h=30.0, style="Bold")
        lab["componentData"]["data"]["handleName"] = "Data"
        DEFS[key] = component([focus_ring(B(-4, -4, w + 8, h + 8)), lab], actions=[act("Mouse Down", "Q-Link")])
    return key


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
        ("OSC KNOBS", [["pm_fenv", "pm_oscb", "a_freq", "a_pw"], ["lfo_init", "lfo_freq", "b_freq", "b_fine"], ["b_pw"], ["wm_mix", "glide"]]),
        ("OSC SWITCHES", [["pm_freq_a", "pm_pw_a", "pm_filter"], ["a_saw", "a_pulse", "a_sync"], ["lfo_saw", "lfo_tri", "lfo_square"],
                          ["b_saw", "b_tri", "b_pulse"]]),
        ("MOD SWITCHES", [["b_lo", "b_kbd"], ["wm_freq_a", "wm_freq_b", "wm_pw_a", "wm_pw_b"], ["wm_filter"], ["unison"]]),
    ],
    "FILTER AMP": [
        ("FILTER AMP", [["mix_a", "mix_b", "mix_noise"], ["cutoff", "reso", "env_amt"], ["f_attack", "f_decay", "f_sustain", "f_release"],
                        ["a_attack", "a_decay", "a_sustain", "a_release"]]),
        ("TOUCH SWITCHES", [["rev", "kbd_half", "f_kbd"], ["vintage", "tune", "volume"], ["vel_filter", "vel_amp", "at_filter", "at_lfo"], ["release"]]),
    ],
    "PROGRAMMER": [
        ("PROGRAMMER", [["preset"], ["voices", "unison", "quality", "rev"]]),
    ],
}
TAB_ORDER = ["OSCILLATORS", "FILTER AMP", "PROGRAMMER"]


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
            p.kids.append(label(B(cx - w // 2, cy - h // 2, w, h), P(key), h=22.0, colour="ffc8c8c0", style="SemiBold"))
            p.sample[key] = "1 imported (U01-U01)"
    return p


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
    for subname, rects in rects_by_sub:
        img = Image.open(os.path.join(OUT, bgname)).convert("RGBA")
        d = ImageDraw.Draw(img)
        for k in p.kids:
            cd, (x, y, w, h) = k["componentData"], [int(v) for v in k["bounds"]["bounds"].split()]
            typ, data = cd["type"], cd["data"]
            remap = k["handle remapping"]["map"]
            key = LOCK[int(remap[0]["value"].split()[-1])] if remap else None
            spec = PARAMS.get(key, {})
            if typ.startswith("knob_"):
                lo, hi = spec.get("min", 0), spec.get("max", 1)
                frac = (spec.get("default", 0) - lo) / (hi - lo) if hi > lo else 0
                strip = Image.open(os.path.join(OUT, DEFS[typ]["componentsData"][1]["componentData"]["data"]["filmStrip"])).convert("RGBA")
                f = int(round(frac * (FRAMES - 1)))
                img.alpha_composite(strip.crop((0, f * w, w, f * w + w)), (x, y))
            elif typ == "Button":
                opts = spec.get("options")
                lit = bool(opts) and opts.index(spec["default"]) == 1
                pic = Image.open(os.path.join(OUT, data["onImage" if lit else "offImage"])).convert("RGBA")
                img.alpha_composite(pic, (x, y))
            elif typ.startswith("readout_") or typ == "Label":
                d.text((x + w / 2, y + h / 2), p.sample.get(key, ""), font=font(22 if typ == "Label" else 30), fill=(234, 234, 228), anchor="mm")
        for r in rects:
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
        spec = spec_all[name]
        bgname = copy_art(spec["art"])
        p = build_page(name, fn, spec)
        t, q = tabs_for(p, bgname)
        tabs += t
        qmaps += q
        if preview:
            subs = [(tb["tabName"], [tuple(map(int, r.split())) for r in tb["qlinkBoundsData"]]) for tb in t]
            subs = [(n, [(x, y, x + w, y + h) for x, y, w, h in rs]) for n, rs in subs]
            for subname, img in render_preview(p, bgname, subs):
                img.convert("RGB").save(os.path.join(PREVIEW, subname.replace(" ", "_").lower() + ".png"))
    for key in LOCK:
        if PARAMS[key].get("options"):
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
    missing = [k for k in LOCK if k not in used and not PARAMS[k].get("momentary") and PARAMS[k].get("display") != "string" and k != "user"]
    print("wrote %s - %d tabs, %d files; not on a Q-Link: %s" % (OUT, len(tabs), len(os.listdir(OUT)), ", ".join(missing) or "none"))


if __name__ == "__main__":
    main()
