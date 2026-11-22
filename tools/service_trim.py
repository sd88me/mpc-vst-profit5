#!/usr/bin/env python3
"""Set the model 1 filter trims (src/engine.c CUT_KV, CUT_TRIM) as manual 4-17 does: R407 INIT FREQ so C3 rings at 440 Hz, R406
V/OCT so C5 rings two octaves higher, repeated until they settle. Rewrites the two #defines; run after any change to the filter."""
import re, subprocess, math
src = "src/engine.c"
kv, tr = 1.0, 0.0
for it in range(4):
    s = open(src).read()
    s = re.sub(r'#define CUT_TRIM [-0-9.e]+f', '#define CUT_TRIM %.5ff' % tr, s)
    s = re.sub(r'#define CUT_KV [-0-9.e]+f', '#define CUT_KV %.5ff' % kv, s)
    open(src, 'w').write(s)
    subprocess.run('gcc -O2 -std=c11 tools/service_check.c src/engine.c -lm -o build/service_check', shell=True, check=True)
    out = subprocess.run(['build/service_check'], capture_output=True, text=True).stdout
    c3, c5 = [float(x) for x in re.findall(r'rings at C[35][^\n]*?([0-9.]+) Hz', out)]
    print('iter', it, 'kv %.5f trim %.5f  C3 %.3f  C5 %.3f' % (kv, tr, c3, c5))
    # the oct value at C3 is about 0 (450 Hz base); scale about it, then offset
    kv *= 2 / math.log2(c5 / c3)
    tr += math.log2(440 / c3) * 1.0
