#!/usr/bin/env bash
# Build Profit-5 2 with the MPC plugin builder (tools/build.py, zig): native test build + arm32 (Force, MPC Gen1) + arm64 (Live III and
# other 64-bit units), into build/"sd88me - VST - Profit-5"/. The 1.x wrapper build is still vst/vst.json (mpc-vst-plugins gen_vst.py).
#   ./build.sh          build
#   ./build.sh test     build, the offline ASan/UBSan host test (tests/run.sh) and the builder's host_sim
#   ./build.sh beta     a "Profit-5 2 Beta" (uid Pf5B) build that installs next to the release: build/beta-project/build/...
# MPC_BUILDER: the plugin builder checkout.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
BUILDER="${MPC_BUILDER:-$HOME/mpc-plugin-builder-sdk}"
[ -f "$BUILDER/tools/build.py" ] || { echo "need the plugin builder (MPC_BUILDER)" >&2; exit 1; }
python3 "$here/tools/gen_native_params.py"
[ -f "$here/tools/make_tui.py" ] && python3 "$here/tools/make_tui.py"

# The arm32 .so must load on the Force (MPC OS 3.9, glibc 2.29 or older): fail if it asks for a newer glibc symbol.
glibc_check() {
  local so="$1" max
  max="$(readelf -V "$so" | grep -o 'GLIBC_[0-9.]*' | sed 's/GLIBC_//' | sort -uV | tail -n 1)"
  echo "  $(basename "$so"): newest glibc symbol $max"
  [ "$(printf '%s\n2.29\n' "$max" | sort -V | tail -n 1)" = 2.29 ] || { echo "$so needs glibc $max > 2.29 (the Force)" >&2; exit 1; }
}

if [ "${1:-}" = beta ]; then
  stage="$here/build/beta-project"; rm -rf "$stage"; mkdir -p "$stage"
  ln -s "$here/src" "$stage/src"; ln -s "$here/analog" "$stage/analog"; [ -d "$here/skins" ] && ln -s "$here/skins" "$stage/skins"
  python3 - "$here/plugin.json" "$stage/plugin.json" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
m.update(name="Profit-5 2 Beta", uid="Pf5B", file32="VST/profit52beta-arm32.so", file64="VST/profit52beta-arm64.so")
json.dump(m, open(sys.argv[2], "w"), indent=2)
PY
  python3 "$BUILDER/tools/build.py" "$stage" --no-ui --define 'P5_NAME="Profit-5 2 Beta"' --define "P5_UID='P','f','5','B'"
  glibc_check "$stage/build/sd88me - VST - Profit-5 2 Beta/VST/profit52beta-arm32.so"
  exit
fi
python3 "$BUILDER/tools/build.py" "$here" --no-ui
glibc_check "$here/build/sd88me - VST - Profit-5/VST/profit5-arm32.so"
if [ "${1:-}" = test ]; then
  "$here/tests/run.sh"
  scratch="$(mktemp -d)"; trap 'rm -rf "$scratch"' EXIT
  cp "$here/plugin.json" "$scratch/"
  MPC_PLUGIN_FOLDER="$scratch" "$(ls -d "$BUILDER"/.*-tools | head -n 1)/host_sim" "$here/build/native/profit-5.so" test --notes 60,64,67 --wav "$here/build/test.wav"
fi
