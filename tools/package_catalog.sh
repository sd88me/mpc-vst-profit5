#!/usr/bin/env bash
# The catalog-shaped release zips, from the release build (build/"sd88me - VST - Profit-5", made by ./build.sh):
#   dist/Profit-5-<ver>-mpc-armv7.zip, dist/Profit-5-<ver>-mpc-aarch64.zip   one per CPU, made by mpc-vst-plugins' tools/release.py
# As 0.x, the .so is profit_5.so at the folder root, so the installer upgrades a 0.x install in place; banks/ and
# profit5-favourites.dat are user data, kept on upgrade and uninstall. MPC_VST: an mpc-vst-plugins checkout with tools/release.py.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
MPC_VST="${MPC_VST:-$HOME/mpc-vst-plugins-postlaunch}"
[ -f "$MPC_VST/tools/release.py" ] || { echo "need mpc-vst-plugins with tools/release.py (MPC_VST)" >&2; exit 1; }
DIR="sd88me - VST - Profit-5"; SRC="$here/build/$DIR"
[ -f "$SRC/VST/profit5-arm32.so" ] || { echo "missing $SRC: run ./build.sh first" >&2; exit 1; }
VER=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$SRC/plugin.json")
python3 -c 'import json,sys; m=json.load(open(sys.argv[1])); sys.exit(0 if m["uid"]=="Pft5" and "Beta" not in m["name"] else "not the release identity")' "$SRC/plugin.json"
out="$here/dist"; mkdir -p "$out"
for pair in armv7:arm32:file32 aarch64:arm64:file64; do
  IFS=: read -r cpu so key <<<"$pair"
  st="$here/build/catalog/$cpu"; rm -rf "$st"; mkdir -p "$st/$DIR"
  cp -r "$SRC/Plugin Skins" "$SRC/version.xml" "$st/$DIR/"
  python3 - "$SRC/plugin.json" "$st/$DIR/plugin.json" "$key" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
m.pop("file32", None); m.pop("file64", None)
m[sys.argv[3]] = "profit_5.so"
json.dump(m, open(sys.argv[2], "w"), indent=2)
PY
  cp "$SRC/VST/profit5-$so.so" "$st/profit_5.so"
  printf '<PLUGIN name="Profit-5" descriptiveName="Profit-5" format="VST" category="Synth" manufacturer="sd88me" version="%s" file="profit_5.so" uid="50667435" isInstrument="1" fileTime="0" infoUpdateTime="0" numInputs="0" numOutputs="2" isShell="0"/>\n' "$VER" > "$st/entry.xml"
  python3 "$MPC_VST/tools/release.py" --so "$st/profit_5.so" --skin "$st/$DIR" --entry "$st/entry.xml" --version "$VER" \
    --user-data banks --user-data profit5-favourites.dat \
    --repo sd88me/mpc-vst-profit5 --license MIT --id profit-5 \
    --about "Five-voice analog polysynth modelled on an early SSM-chip Prophet-5: Rev 2 / Rev 4 voicing, 38 factory programs, Repro-5 .h2p import, named favourites; native MPC screen and Q-Links." \
    -o "$st/dist"
  z=$(ls "$st/dist"/*.zip)
  # Public text: the installer template names an unreleased OS version in one comment; reword it and refresh SHA256SUMS
  python3 - "$z" "$out/Profit-5-$VER-mpc-$cpu.zip" <<'PY'
import hashlib, re, sys, zipfile
src, dst = sys.argv[1], sys.argv[2]
zin = zipfile.ZipFile(src)
files = {i.filename: (i, zin.read(i.filename)) for i in zin.infolist()}
for n, (i, data) in list(files.items()):
    if n.endswith("/install.sh"):
        t = data.decode()
        t = re.sub(r"# MPC OS [0-9.]+\+ loads plugins \(skins included\) from <drive>/VST2 Plugins, which is not a content location: no warning there\.",
                   "# Plugins (skins included) loaded from <drive>/VST2 Plugins are not in a content location: no warning there.", t)
        assert "+ loads plugins (skins included)" not in t, "install.sh comment not reworded"
        files[n] = (i, t.encode())
sums = [n for n in files if n.endswith("/SHA256SUMS")]
for sn in sums:
    root = sn[: -len("SHA256SUMS")]
    old = files[sn][1].decode().splitlines()
    lines = []
    for l in old:
        h, _, rel = l.partition("  ")
        rel = rel.strip()
        if root + rel in files:
            h = hashlib.sha256(files[root + rel][1]).hexdigest()
        lines.append("%s  %s" % (h, rel))
    files[sn] = (files[sn][0], ("\n".join(lines) + "\n").encode())
with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as zo:
    for n, (i, data) in files.items():
        zo.writestr(i, data)
PY
done
ls -la "$out"/Profit-5-"$VER"-mpc-*.zip
