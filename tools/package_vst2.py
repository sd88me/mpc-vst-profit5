#!/usr/bin/env python3
"""The two "VST2 Plugins" zips for a release, beside the catalog zips (tools/package_catalog.sh), as Profit-08 2.0.0 shipped them:
  dist/<id>-<ver>-mpc.zip       the plugin builder's tools/package.py zip (folder with VST/<so>-arm32.so / -arm64.so, INSTALL.txt, README.md),
                                with INSTALL.txt's OS-version line reworded
  dist/<Name>-<ver>-mpc-sdk.zip one folder with both .so at its root as <legacy so>-arm32.so / -arm64.so, plugin.json, mpc-plugin.json (from the
                                catalog zips, both CPUs), version.xml, Plugin Skins/
Run after ./build.sh and tools/package_catalog.sh:  python3 tools/package_vst2.py
MPC_BUILDER: the plugin builder checkout (tools/package.py)."""
import json, os, re, subprocess, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILDER = os.environ.get("MPC_BUILDER", os.path.expanduser("~/mpc-plugin-builder-sdk"))
plug = json.load(open(os.path.join(ROOT, "plugin.json")))
NAME, VER = plug["name"], plug["version"]
FOLDER = "sd88me - VST - %s" % NAME
BUILD = os.path.join(ROOT, "build", FOLDER)
DIST = os.path.join(ROOT, "dist")
OLD_LINE = re.compile(r"\* Requires MPC OS [0-9.]+ or later\. Older MPC OS versions ignore this folder entirely;")
NEW_LINE = "* Older MPC OS versions that do not load this plugin format ignore this folder entirely;"


def public(text, where):
    t = OLD_LINE.sub(NEW_LINE, text)
    assert not re.search(r"3\.10\.1|\bSDK\b", t), "%s still names the OS version or the builder" % where
    return t


def rewrite(src, dst, fix):
    zin = zipfile.ZipFile(src)
    with zipfile.ZipFile(dst + ".tmp", "w", zipfile.ZIP_DEFLATED) as zo:
        for i in zin.infolist():
            data = zin.read(i.filename)
            data = fix(i.filename, data)
            zo.writestr(i, data)
    os.replace(dst + ".tmp", dst)


# 1. the package.py zip
subprocess.check_call([sys.executable, os.path.join(BUILDER, "tools", "package.py"), ROOT, "--out", DIST], stdout=subprocess.DEVNULL)
pkg = [f for f in os.listdir(DIST) if f.lower() == ("%s-%s-mpc.zip" % (NAME, VER)).lower()]
assert len(pkg) == 1, pkg
pz = os.path.join(DIST, pkg[0])
rewrite(pz, pz, lambda n, d: public(d.decode(), n).encode() if n.endswith((".txt", ".md")) else d)

# 2. the flat zip, its metadata from the catalog zips
cat = {cpu: zipfile.ZipFile(os.path.join(DIST, "%s-%s-mpc-%s.zip" % (NAME, VER, cpu))) for cpu in ("armv7", "aarch64")}
meta = None
for cpu, zf in cat.items():
    m = json.loads(zf.read([n for n in zf.namelist() if n.count("/") == 1 and n.endswith("/mpc-plugin.json")][0]))   # the package's own, with builds
    if meta is None:
        meta = m
    meta.setdefault("builds", {})[cpu] = {"max_glibc": m.get("max_glibc")}
meta["arch"] = "universal"
meta.pop("max_glibc", None)
meta["cpu"] = None
legacy = meta["so"][:-3]                      # profit_5 / profit_08
pj = json.load(open(os.path.join(BUILD, "plugin.json")))
pj.pop("file32", None); pj.pop("file64", None)
pj["uid"] = "0x" + meta["uid"]
pj["file32"], pj["file64"] = legacy + "-arm32.so", legacy + "-arm64.so"
out = os.path.join(DIST, "%s-%s-mpc-sdk.zip" % (NAME, VER))
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zo:
    zo.writestr(FOLDER + "/plugin.json", json.dumps(pj, indent=2) + "\n")
    zo.writestr(FOLDER + "/mpc-plugin.json", json.dumps(meta, indent=2) + "\n")
    so = {"arm32": pj["file32"], "arm64": pj["file64"]}
    for f in os.listdir(os.path.join(BUILD, "VST")):
        arch = "arm32" if "arm32" in f else "arm64"
        zo.write(os.path.join(BUILD, "VST", f), FOLDER + "/" + so[arch])
    zo.write(os.path.join(BUILD, "version.xml"), FOLDER + "/version.xml")
    for base, _, files in os.walk(os.path.join(BUILD, "Plugin Skins")):
        for f in sorted(files):
            p = os.path.join(base, f)
            zo.write(p, FOLDER + "/" + os.path.relpath(p, BUILD))
for z in (pz, out):
    zf = zipfile.ZipFile(z)
    for n in zf.namelist():
        if n.endswith((".txt", ".md", ".json", ".xml")) and "Plugin Skins/" not in n:
            public(zf.read(n).decode(), n)
    print("%s (%d files, %.1f MB)" % (os.path.relpath(z, ROOT), len(zf.namelist()), os.path.getsize(z) / 1e6))
