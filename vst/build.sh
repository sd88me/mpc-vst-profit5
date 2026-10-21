#!/usr/bin/env bash
# Builds the plugin (armv7 + aarch64) and the skin into vst/build/ (needs Docker; ../mpc-vst-plugins next to this repo, or MPC_VST set).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MV="${MPC_VST:-$HERE/../../mpc-vst-plugins}"
python3 "$HERE/../tools/gen_presets.py"
python3 "$HERE/../tools/gen_params.py"
python3 "$HERE/../skin/make_skin.py" "$HERE"
exec "$MV/tools/build_port.sh" "$HERE/vst.json" "${1:-all}"
