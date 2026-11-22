#!/usr/bin/env python3
"""Set the model 1 filter trims (src/engine.c CUT_KV, CUT_TRIM) as manual 4-17 does: R407 INIT FREQ so C3 rings at 440 Hz, R406
V/OCT so C5 rings two octaves higher, repeated until they settle. Rewrites the two #defines; run after any change to the filter."""
import re, subprocess, math
src = "src/engine.c"
import sys
REV4 = len(sys.argv) > 1 and sys.argv[1] == "rev4"   # the 3320 trims (CUT4_*) against the Rev 3 manual 4-20 cases
kv, tr = 1.0, 0.0
for it in range(4):
    s = open(src).read()
    tag = 'CUT4_' if REV4 else 'CUT_'
    s = re.sub(r'#define %sTRIM [-0-9.e]+f' % tag, '#define %sTRIM %.5ff' % (tag, tr), s)
    s = re.sub(r'#define %sKV [-0-9.e]+f' % tag, '#define %sKV %.5ff' % (tag, kv), s)
    open(src, 'w').write(s)
    subprocess.run('gcc -O2 -std=c11 tools/service_check.c src/engine.c -lm -o build/service_check', shell=True, check=True)
    out = subprocess.run(['build/service_check'], capture_output=True, text=True).stdout
    if REV4:
        lo, hi = [float(x) for x in re.findall(r'rings at A[34][^\n]*?([0-9.]+) Hz', out)]
        c3, c5, ref, octs = lo, hi, 440.0, 1
    else:
        c3, c5 = [float(x) for x in re.findall(r'rings at C[35][^\n]*?([0-9.]+) Hz', out)]
        ref, octs = 440.0, 2
    print('iter', it, 'kv %.5f trim %.5f  C3 %.3f  C5 %.3f' % (kv, tr, c3, c5))
    # the oct value at C3 is about 0 (450 Hz base); scale about it, then offset
    kv *= octs / math.log2(c5 / c3)
    tr += math.log2(ref / c3)
