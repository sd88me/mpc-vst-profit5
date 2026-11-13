#!/usr/bin/env bash
# tests/data/legacy-<ver>[-user].chunk: the chunks projects saved with the released 1.x-wrapper builds hold, made by each build's own
# engine code (its git tag) driven by tests/legacy_dump.c. 0.1.1 = the 58-value format; 0.2.0 and 0.2.1 = the current format, the
# "-user" one with an imported patch (a synthetic Repro-5 patch from test/fixture_import.h) picked. No factory or third-party bytes.
set -euo pipefail
cd "$(dirname "$0")/.."
MV="${MPC_VST:-$PWD/../mpc-vst-plugins}"
w="$(mktemp -d)"; trap 'rm -rf "$w"' EXIT
mkdir -p tests/data
gcc -std=c11 -O1 -w -Itest -o "$w/fix" -x c - <<'C'
#include <stdio.h>
#include "fixture_import.h"
int main(int c, char **v) { FILE *f = fopen(v[1], "wb"); fputs(FIX_BRASS, f); return fclose(f); }
C
for ver in 0.1.1 0.2.0 0.2.1; do
  d="$w/$ver"; mkdir -p "$d/src" "$d/banks"
  git archive "profit-5-vst-v$ver" src analog | tar -x -C "$d/src"
  srcs="$d/src/src/plugin.c $d/src/src/engine.c"; [ -f "$d/src/src/p5_import.c" ] && srcs="$srcs $d/src/src/p5_import.c"
  gcc -std=gnu11 -O1 -w -I"$d/src/src" -I"$d/src/analog" -I"$MV/wrapper" -o "$d/dump" tests/legacy_dump.c $srcs -lm
  out="tests/data/legacy-$ver.chunk"
  "$d/dump" "$d/banks" 5 > "$out"; printf '\0' >> "$out"     # the wrapper saved the string with its NUL
  echo "$out: $(wc -c < "$out") bytes"
  if [ "$ver" != 0.1.1 ]; then
    "$w/fix" "$d/banks/Test Brass.h2p"
    out="tests/data/legacy-$ver-user.chunk"
    "$d/dump" "$d/banks" 38 > "$out"; printf '\0' >> "$out"
    echo "$out: $(wc -c < "$out") bytes"
  fi
done
