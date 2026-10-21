#!/usr/bin/env bash
# Builds the plugin (armv7 + aarch64) and the skin into vst/build/ (needs Docker; ../mpc-vst-plugins next to this repo, or MPC_VST set).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MV="${MPC_VST:-$HERE/../../mpc-vst-plugins}"
python3 "$HERE/../tools/gen_presets.py"
python3 "$HERE/../tools/gen_params.py"
# the skin's lettering is GNU FreeSans
fc-list 2>/dev/null | grep -qi freesans || { sudo -n apt-get install -y -q fonts-freefont-ttf >/dev/null 2>&1 || apt-get install -y -q fonts-freefont-ttf >/dev/null 2>&1 || true; }
P5_STYLE="${P5_STYLE:-rev2}" python3 "$HERE/../skin/make_skin.py" "$HERE"
exec "$MV/tools/build_port.sh" "$HERE/vst.json" "${1:-all}"
