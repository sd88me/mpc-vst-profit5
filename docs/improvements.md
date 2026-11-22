# Improvements

The one tracker of what is known to be wrong, in the shape Lemus uses (`CONTRIBUTING.md`, "Rounds"). Dated; names no one. The method
and its limits are in `docs/ROADMAP-accuracy.md`; the circuit as read is in `docs/CIRCUIT.md`; sources in `docs/SOURCES.md`.

## Phase 0 (2026-11-22, OK'd by the owner the same day)
- **Target unit:** Rev 2, S/N 184-1299 (590 and up for the envelope S&H gain modification), voice board SD411 rev 2.0 (TM1000C). The
  "Rev 4" voicing stays a switch with the 2.0.0 laws, to be modelled later from TM1000D (Rev 3 filter) and the Rev 4 trace.
- **Tier:** circuit public (the TM1000C schematics); custom chips by datasheet (SSM 2020, 2030, 2040, 2050, all in hand); reference: none,
  so the model is checked against the schematic, ngspice and the service manual only.
- **Free reference recordings found:** none.
- **Model version:** a state without "m<n>" loads as model 0, the 2.0.0 laws, bit-identical; new instances are model 1.

## Check set (run before and after every round)
| Check | State (2026-11-22, after round 2) |
|---|---|
| `tools/spice_check.py` (every netlist against ngspice, and `ma_vcf2040` against `circuits/vcf2040*.cir` at 48 and 96 kHz; `tools/docker/Dockerfile`) | 20 checks, 0 failed, 5 open |
| `test/test_engine.c` (152 program renders, glide, pitch) | passes |
| `tools/golden.c` (304 renders; `build/golden-2.0.0`, local) | model 0: 304 identical; model 1: the 152 Rev 2 renders differ, Rev 4 identical |
| `test/test_corners.c` (extremes, random settings, filter sweeps; NaN, bounds, resets, unconverged solves) | 256 cases, 0 failed |
| `tools/service_check.c` (manual 4-17 C3 and C5, 3.4, 4-10, 4-11); trims by `tools/service_trim.py` | model 1: 0 failed; model 0: 4 failed (as 2.0.0) |
| `tests/run.sh` (the plugin under ASan: state round trips, legacy chunks, favourites) | OK |
| CPU: `tools/bench.c` (x86, 5 voices, 1-7 Sync I); budget: Force, 5 voices, mean under 30 % (2.0.0: 28.3 %, 2026-10-21) | Rev 2 model 1: Eco 3.3 %, High 6.7 % (2.0.0: 2.7 / 5.4); Rev 4 unchanged. Device: not measured |

## Known differences (circuit against engine), in proposed order
1. **Device CPU:** model 1's Rev 2 is about 24 % heavier on x86; scaled from 2.0.0's device figure that is about 35 % on the Force at
   five voices in High, over the 30 % budget. Needs a device bench, then the owner's call (Eco, or a CPU round).
2. **Modulation depths** (W-MOD x0.549 to freq, x7.52 / x6.67 to PW, x7.52 to cutoff; Poly-Mod x0.31 / x3.32 / x1.95): wait on the W-MOD
   source level and the P-MOD and FILT ENV AMT VCAs' control wiring (`docs/CIRCUIT.md`, Open). The engine keeps the 2.0.0 depths.
3. **Noise level** into the mixer: the noise VCA on board 3 and the 5837's level are not read; model 1 keeps 2.0.0's level against the
   oscillators.
4. **The mixer inputs' coupling** (C409 / R481 and C408 / R439: 8 Hz high-pass on each oscillator) and the final VCA's control law (R474,
   R469, R470, BAL) are not modelled.
5. **Rev 4** is still the 2.0.0 voicing.

## Open questions
- The V/oct trims can't reach 1.000 oct/V at 25 C: VCO 1.02-1.08 (54.9k over 1k + 121, summer x0.909-0.959), filter 1.014-1.113
  (`circuits/filtcv.cir`). Manual 4-17 says the filters are not temperature compensated and 4-18 says to tune "in a very warm room", so
  the filter's is consistent with a warm unit; the VCO's has a tempco resistor and isn't. The model is trimmed to 1 V/oct either way.
- Where the 2040 limits its swing (at CAP or at OUT): the datasheet says only that OUT swings +-1 V. Model 1 limits CAP (no wind-up).
- The 2020 control transistor's I_s (assumed: 0.65 V at 240 uA). It sets how fast a mixer or resonance setting below 1 fades out.
- **5-5 Helicopter** is 27 dB quieter than in 2.0.0: its sheet transcription has every mixer level at 0.5 (the sheets are read to +-0.5),
  which is a 0.5 V CV, -56 dB in the 2020. Check the patch sheet. **5-1 Alien** is 11-13 dB quieter (resonance 8.5): waits on a recording.

## Done
- **2026-11-22, round 1 (model 1, Rev 2 only; offline):** model version in the state ("m1"; absent = 0 = the 2.0.0 laws); LO FREQ 5 octaves;
  osc B fine 1.2 semitones; cutoff 10 octaves over the pot, trimmed as manual 4-17 (CUT_TRIM 0.0601 oct); envelopes from the SSM 2050
  sheet (2 V/decade, x1.1 S&H mod) anchored at 0.9 s for 6.250 V (4-10/4-11), 1 ms-100 s. Before / after: service checks 4 failed -> 0
  (C3 413 -> 440.1 Hz, C5 1663 -> 1772 Hz, attack 1.58 -> 0.899 s); goldens model 0: 304/304 identical, model 1: the 152 Rev 2 renders
  differ, Rev 4 identical; corners 256/0 both; host test OK; CPU unchanged (x86 Eco 2.7 %, High 5.4 %). Not yet heard on a device.
- **2026-11-22, round 2 (model 1, Rev 2 only; offline): the filter section as a circuit.** `ma_vcf2040` (analog/mpc_analog_p5.h): four
  2040 cells (OTA tanh, 1000 pF, +-1 V), the 10k / 10k / 200 networks, U479 (x20, +-13.5 V), the C413 / R471 high-pass and the 2020
  resonance VCA, solved implicitly per sample. Against ngspice of the same circuit (`circuits/vcf2040*.cir`): gains within 0.09 %, -3 dB
  267.8 / 267.6 Hz, self-oscillation 620.87 / 620.85 Hz at 0.3548 / 0.3550 V; 0 unconverged samples. The mixer and resonance levels come
  from the 2020s' control ports (`ma_2020_ctl`: a diode through 39k / 30k), so the mixer drives the filter as the circuit does (one osc
  at 10: 0.27 V per stage against +-1 V). Retrimmed by `tools/service_trim.py` (4-17): CUT_KV 1.00679, CUT_TRIM -0.0246 oct. Levels
  against 2.0.0 over the 38 programs (Rev 2, 1.5 s chord): median -0.3 dB; 5-6 Resonance Bells +6 dB (the self-oscillation's own level),
  5-1 Alien -11 to -13 dB, 5-5 Helicopter -27 dB (Open questions). CPU (x86): Rev 2 Eco 2.7 -> 3.3 %, High 5.4 -> 6.7 %.

## Tried and dropped
- 2026-11-22: re-linearising the filter until OUT4 moved < 1 uV (always two passes): +60 % CPU. Replaced by stopping when no cell's
  gain moves by 1e-4 (1.02 passes); against a 1e-7 solve the renders are identical or under -88 dB, except 5-6 Resonance Bells, whose
  self-oscillation drifts in phase (same level to 0.01 dB, spectra within 0.1 dB).
- 2026-11-22: damping the re-linearisation: slower to converge, more unconverged samples.
- 2026-11-22: a cubic exp2 inside the filter's tanh: its kinks stall the iteration (High 9.3 -> 13.9 %).
- 2026-11-22: a hard 0.65 V dead zone in the 2020 control law: silenced 5-5 Helicopter (-71 dB); replaced by the diode equation.

## Declined
(none yet)
