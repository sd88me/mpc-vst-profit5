#!/usr/bin/env bash
# Rebuilds the parameter table the released 0.2.1 build really had (its tag, with the mpc-vst-plugins commit its release workflow
# pinned) and checks that vst/params.lock lists the same keys in the same order. Index = VST parameter number = project/automation slot.
set -euo pipefail
cd "$(dirname "$0")/.."
MV="${MPC_VST:-$PWD/../mpc-vst-plugins}"
TAG=profit-5-vst-v0.2.1
PIN=$(git show "$TAG:.github/workflows/release.yml" | sed -n 's/.*tools_ref: *\([0-9a-f]*\).*/\1/p' | head -1)
w="$(mktemp -d)"; trap 'git -C "$MV" worktree remove --force "$w/tools" 2>/dev/null; rm -rf "$w"' EXIT
git -C "$MV" worktree add -q --detach "$w/tools" "$PIN"
mkdir -p "$w/p"; git archive "$TAG" | tar -x -C "$w/p"
python3 "$w/tools/tools/gen_vst.py" "$w/p/vst/vst.json" --params-h >/dev/null
python3 - "$w/p/vst/build/params.h" vst/params.lock <<'P'
import re, sys
ids = re.findall(r'^\s*\{"([^"]+)", "', open(sys.argv[1]).read(), re.M)
lock = [l.strip() for l in open(sys.argv[2]) if l.strip() and not l.startswith('#')]
if lock[:len(ids)] != ids:
    for i, (a, b) in enumerate(zip(ids, lock)):
        if a != b: sys.exit(f"params.lock differs from the released table at index {i}: released {a!r}, lock {b!r}")
    sys.exit(f"params.lock is shorter than the released table ({len(lock)} < {len(ids)})")
print(f"params.lock OK: {len(ids)} released parameters in order, {len(lock) - len(ids)} appended")
P
