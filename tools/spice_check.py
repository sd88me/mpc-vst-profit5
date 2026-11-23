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

# 3. osc B master summer (SD312, read 2026-11-22: fine is R3107 10M, W-MOD FREQ B is R3100 182k, LO a -15 V 100k/100k divider)
s = run('summer.cir')
g = [float(x) for x in re.findall(r'^v\(o2\) = ([-+0-9.eE]+)', s, re.M)]
lo = float(re.findall(r"^v\(mo\) = ([-+0-9.eE]+)", s, re.M)[0])
t63 = meas(s, 't63')[0]
check('summer: glide / init input gain', '%.3f' % g[0], '1 (semitones 1:1)', abs(g[0] - 1) < 0.002)
check('summer: fine (10M) gain', '%.4f' % g[1], '1 semitone (b_fine)', 'open', '0.01 x 10 V = 1.2 semitones; docs/improvements.md')
check('summer: W-MOD FREQ B (182k) gain', '%.3f' % g[2], 'not used (4 semitones at full)', 'open', 'needs the W-MOD source level')
check('summer: LO FREQ shift (V = octaves)', '%.3f' % lo, '7 octaves (1/128)', 'open', 'circuit 5 octaves; docs/improvements.md')
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

# 6. voice filter cutoff CV: CONT volts per summer volt over the V/OCT trim, against the 2040's -18 mV/oct (datasheet, 25 C)
c = [abs(float(x)) for x in re.findall(r'^v\(c\) = ([-+0-9.eE]+)', run('filtcv.cir'), re.M)]
lo_o, hi_o = c[0] / 0.018, c[-1] / 0.018
check('filter CV: oct/V over the V/OCT trim', '%.3f .. %.3f' % (lo_o, hi_o), '1 (10.5 oct over the pot)', 'open',
      'trim cannot reach 1.000 at 25 C; span over the pot ~10 oct; docs/CIRCUIT.md')

# 7. mixer bus: one osc at 10 into stage 1 of the 2040 (2020 law from its datasheet)
m = run('mixbus.cir')
iin = abs(float(re.findall(r'^i\(vin1\) = ([-+0-9.eE]+)', m, re.M)[0]))
check('mixer: one osc at 10, 2040 stage level (V)', '%.3f' % (iin * 10e3), '0.45 x level into a +-1.4 tanh', 'open',
      'stage outputs swing +-1 V (datasheet); docs/improvements.md')

# 8. the filter as a circuit: ma_vcf2040 (analog/mpc_analog.h) against ngspice of the same circuit (circuits/vcf2040*.cir)
def num(v):
    m = {'p': 1e-12, 'n': 1e-9, 'u': 1e-6, 'm': 1e-3, 'k': 1e3}
    return float(v[:-1]) * m[v[-1]] if v[-1] in m else float(v)
def rows(text, tag):
    return [[num(v) for v in l.split()[1:]] for l in text.splitlines() if l.startswith(tag + ' ')]
sp_ac, sp_osc = rows(run('vcf2040_ac.cir'), 'ROW'), rows(run('vcf2040_osc.cir'), 'OSC')
exe = os.path.join(HERE, '..', 'build', 'vcf_compare')
os.makedirs(os.path.dirname(exe), exist_ok=True)
subprocess.run(['gcc', '-O2', '-std=c11', '-I' + os.path.join(HERE, '..', 'analog'), os.path.join(HERE, '..', 'tools', 'vcf_compare.c'),
                '-lm', '-o', exe], check=True)
for fs in ('48000', '96000'):
    out = subprocess.run([exe, fs], capture_output=True, text=True).stdout
    k_ac, k_osc = rows(out, 'ROW'), rows(out, 'OSC')
    worst = max(abs(a / b - 1) for sr, kr in zip(sp_ac, k_ac) for a, b in zip(kr[1:4], sr[1:4]))
    check('filter circuit @%s: gains at 20/30/566 Hz, 5 resonances' % fs, 'worst %.3f %%' % (100 * worst), 'ma_vcf2040', worst < 0.005)
    check('filter circuit @%s: -3 dB at RES 0 (Hz)' % fs, '%.1f' % sp_ac[0][4], '%.1f' % k_ac[0][4], abs(k_ac[0][4] / sp_ac[0][4] - 1) < 0.002)
    check('filter circuit @%s: self-osc at RES 10 (Hz, V)' % fs, '%.2f / %.4f' % (sp_osc[0][1], sp_osc[0][2]),
          '%.2f / %.4f' % (k_osc[0][1], k_osc[0][2]), abs(k_osc[0][1] / sp_osc[0][1] - 1) < 0.002 and abs(k_osc[0][2] / sp_osc[0][2] - 1) < 0.01)
    unc = int(out.split('unconverged samples:')[1].split()[0])
    check('filter circuit @%s: unconverged samples' % fs, '0', str(unc), unc == 0)

# 9. the Rev 3 filter (the Rev 4 setting) as a circuit: ma_vcf3320 against ngspice of the same circuit (circuits/vcf3320*.cir)
sp_ac, sp_osc = rows(run('vcf3320_ac.cir'), 'ROW'), rows(run('vcf3320_osc.cir'), 'OSC')
exe = os.path.join(HERE, '..', 'build', 'vcf3320_compare')
subprocess.run(['gcc', '-O2', '-std=c11', '-I' + os.path.join(HERE, '..', 'analog'), os.path.join(HERE, '..', 'tools', 'vcf3320_compare.c'),
                '-lm', '-o', exe], check=True)
for fs in ('48000', '96000'):
    out = subprocess.run([exe, fs], capture_output=True, text=True).stdout
    k_ac, k_osc = rows(out, 'ROW'), rows(out, 'OSC')
    worst = max(abs(a / b - 1) for sr, kr in zip(sp_ac, k_ac) for a, b in zip(kr[1:4], sr[1:4]))
    check('3320 circuit @%s: gains at 20/100/900 Hz, 4 resonances' % fs, 'worst %.3f %%' % (100 * worst), 'ma_vcf3320', worst < 0.005)
    check('3320 circuit @%s: -3 dB at RES 0 (Hz)' % fs, '%.1f' % sp_ac[0][4], '%.1f' % k_ac[0][4], abs(k_ac[0][4] / sp_ac[0][4] - 1) < 0.003)
    check('3320 circuit @%s: self-osc at RES 10 (Hz, V)' % fs, '%.2f / %.4f' % (sp_osc[0][1], sp_osc[0][2]),
          '%.2f / %.4f' % (k_osc[0][1], k_osc[0][2]), abs(k_osc[0][1] / sp_osc[0][1] - 1) < 0.002 and abs(k_osc[0][2] / sp_osc[0][2] - 1) < 0.01)
    unc = int(out.split('unconverged samples:')[1].split()[0])
    check('3320 circuit @%s: unconverged samples' % fs, '0', str(unc), unc == 0)

w = max(len(x[0]) for x in results)
bad = nopen = 0
for what, c, e, ok, note in results:
    state = 'OPEN' if ok == 'open' else 'PASS' if ok else 'FAIL'
    bad += state == 'FAIL'; nopen += state == 'OPEN'
    print('%-*s  circuit %-22s engine %-28s %s%s' % (w, what, c, e, state, ('  (' + note + ')') if note else ''))
print('%d checks, %d failed, %d open (known differences, docs/improvements.md)' % (len(results), bad, nopen))
sys.exit(1 if bad else 0)
