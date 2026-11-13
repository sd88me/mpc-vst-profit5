#!/usr/bin/env bash
# Offline x86 test under ASan/UBSan (no device): the native front end + the real engine, driven by tests/host_test.cpp. Must print OK.
# P5_NAME / P5_UID_HEX: the build's name and uid (beta tests). The plugin folder gets banks/Test Brass.h2p, a synthetic Repro-5 patch.
set -euo pipefail
cd "$(dirname "$0")/.."
BUILDER="${MPC_BUILDER:-$HOME/mpc-plugin-builder-sdk}"
python3 tools/gen_native_params.py >/dev/null
ls tests/data/legacy-*.chunk >/dev/null 2>&1 || tests/make_legacy_chunks.sh >/dev/null
T=build/test; mkdir -p "$T"
UIDDEF=()   # a beta uid (hex) builds the plugin with it too
[ -n "${P5_UID_HEX:-}" ] && UIDDEF=(-DP5_UID="$(echo "$P5_UID_HEX" | sed -E "s/(..)(..)(..)(..)/0x\1,0x\2,0x\3,0x\4/")")
S="-fsanitize=address,undefined -fno-sanitize-recover=undefined -O1 -g -std=c++17 -pthread"
g++ $S -Wall -DP5_TEST -DP5_EXPECT_NAME="\"${P5_NAME:-Profit-5}\"" -DP5_EXPECT_UID="0x${P5_UID_HEX:-50667435}" ${P5_NAME:+-DP5_NAME="\"$P5_NAME\""} "${UIDDEF[@]}" \
  -I"$BUILDER/framework" -Isrc -o "$T/p5test" src/Profit5Plugin.cpp src/engine_unit.cpp tests/host_test.cpp -lm
gcc -std=c11 -O1 -Itest -o "$T/fix" -x c - <<'C'
#include <stdio.h>
#include "fixture_import.h"
int main(int c, char **v) { FILE *f = fopen(v[1], "wb"); if (!f) return 1; fputs(FIX_BRASS, f); return fclose(f); }
C
pf="$(mktemp -d)"; trap 'rm -rf "$pf"' EXIT
cp plugin.json "$pf/"; mkdir -p "$pf/banks"; "$T/fix" "$pf/banks/Test Brass.h2p"
MPC_PLUGIN_FOLDER="$pf" ASAN_OPTIONS=detect_leaks=1 "$T/p5test" tests/data
