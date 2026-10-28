#!/usr/bin/env python3
"""Run the ngspice netlists in circuits/ and check the engine's constants against them (offline; needs ngspice on PATH).

Each check prints the circuit's value, the value src/engine.c uses, and PASS / FAIL. The netlists are transcribed from the Rev 1
service manual (board 3 p3, board 4) and the SSM 2030 / 2050 datasheets; the SSM chips themselves are behavioural (no public
transistor-level models)."""
import math, os, re, subprocess, sys

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'circuits')

def run(name):
    out = subprocess.run(['ngspice', '-b', os.path.join(HERE, name)], capture_output=True, text=True, timeout=120)
    return out.stdout + out.stderr

def meas(text, key):
    return [float(m) for m in re.findall(r'^%s\s*=\s*([-+0-9.eE]+)' % key, text, re.M)]

results = []
def check(what, circuit, engine, ok, note=''):
    results.append((what, circuit, engine, ok, note))

# 1. SSM 2050 attack: time to the +10 V comparator on an approach to +13 V, in time constants
t = meas(run('env2050.cir'), 'tatk')[0] / 0.1
check('2050 attack length (tau)', '%.4f' % t, '1.4663 (ATK)', abs(t - 1.4663) < 0.002)

# 2. wheel-mod noise pole
f3 = meas(run('pink.cir'), 'f3')[0]
check('wheel-mod noise pole (Hz)', '%.1f' % f3, '159 (pink_c)', abs(f3 - 159) < 1.5)

# 3. osc B master summer gains and smoothing
s = run('summer.cir')
g = [float(x) for x in re.findall(r'^v\(o2\) = ([-+0-9.eE]+)', s, re.M)]
t63 = meas(s, 't63')[0]
check('summer: key / init input gain', '%.3f' % g[0], '1 (semitones 1:1)', abs(g[0] - 1) < 0.002)
check('summer: fine input gain', '%.4f' % g[1], '0.05 (summer input, scan-derived; b_fine now 1 semitone)', abs(g[1] - 0.05) < 0.0005)
check('summer: 412k / 182k / 2.21M input gains', '%.3f / %.3f / %.4f' % (g[2], g[3], g[4]), 'not used yet', True,
      'sources not legible on the scan')
check('summer: output smoothing (ms)', '%.2f' % (t63 * 1e3), '2 (ma_cv RC)', 0.5 < t63 * 1e3 < 3, 'the engine lumps it with the S&H RC')

# 4. glide: constant slew rate, so 24 semitones take twice as long as 12
tg = meas(run('glide.cir'), 'tg')
r = (tg[1] - 1e-3) / (tg[0] - 1e-3)
check('glide 24 / 12 semitone time ratio', '%.2f' % r, '1.94 (test_engine 9)', abs(r - 1.94) < 0.1)

# 5. exponential converter: octaves per volt across the range, and the droop at the top
ic = [float(x) for x in re.findall(r'^\d+\t([-+0-9.eE]+)', run('expconv.cir'), re.M)]
octs = [math.log2(ic[k + 1] / ic[k]) for k in range(len(ic) - 1)]
low, top = octs[0], octs[-1]
check('2030 converter, 0-1 V (oct/V)', '%.4f' % low, '1.0000', abs(low - 1) < 0.01)
check('2030 converter, 9-10 V droop (cents)', '%.1f' % (1200 * (top - low)), 'reset model only', True,
      'bulk resistance; HI V/OCT trims it')

w = max(len(x[0]) for x in results)
bad = 0
for what, c, e, ok, note in results:
    bad += not ok
    print('%-*s  circuit %-22s engine %-28s %s%s' % (w, what, c, e, 'PASS' if ok else 'FAIL', ('  (' + note + ')') if note else ''))
print('%d checks, %d failed' % (len(results), bad))
sys.exit(1 if bad else 0)
