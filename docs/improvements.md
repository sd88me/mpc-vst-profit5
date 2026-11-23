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
- **Rev 4 (2026-11-23):** the plugin's Rev 4 setting is the reissue in its Rev 3 (CEM 3320) filter mode. Target: the Rev 3 filter section
  (TM1000D SD432) with the 3320's law from the V3320 datasheet; the reissue's own differences (Q compensation, mixer, DC blocking) from the
  maffez.com notes where traced. Tier: circuit public for Rev 3, partial for Rev 4; no reference recordings.

## Check set (run before and after every round)
| Check | State (2026-11-22, after round 2) |
|---|---|
| `tools/spice_check.py` (every netlist against ngspice; `ma_vcf2040` and `ma_vcf3320` against their decks at 48 and 96 kHz; `tools/docker/Dockerfile`) | 28 checks, 0 failed, 5 open (2026-11-23) |
| `test/test_engine.c` (152 program renders, glide, pitch) | passes |
| `tools/golden.c` (304 renders; `build/golden-2.0.0`, local) | model 0: 304 identical; model 1: the 152 Rev 2 renders differ, Rev 4 identical |
| `test/test_corners.c` (extremes, random settings, filter sweeps; NaN, bounds, resets, unconverged solves) | 256 cases, 0 failed |
| `tools/service_check.c` (TM1000C 4-17 C3 and C5, 3.4, 4-10, 4-11; TM1000D 4-20 A3 and A4 for the Rev 4 setting); trims by `tools/service_trim.py [rev4]` | model 1: 0 failed |
| `tests/run.sh` (the plugin under ASan: state round trips, legacy chunks, favourites) | OK |
| CPU: `tools/bench.c` (5 voices, 1-7 Sync I; argument = model), on x86 and cross-built with zig for the devices (`-O2`, the builder's targets), run over ssh; budget: Force, 5 voices, under 30 % | Device, 2026-11-22: Force (armv7) model 0 Eco 14.3 / High 28.4 %, model 1 Rev 2 Eco 18.1 / High **36.2 %**; Live III (arm64) model 0 4.2 / 8.1 %, model 1 5.6 / 10.7 % (before the rational tanh). Rev 4 unchanged. Repeat runs agree to 0.2 % |

## Known differences (circuit against engine), in proposed order
1. **Force CPU:** model 1's Rev 2 in High is 36.2 % at five voices (2.0.0: 28.4 %), over the 30 % budget; Eco is 18.1 %. The Live III
   is far inside (10.7 %). The owner's call after listening: Eco on the Force, or a CPU round.
2. **Wheel-Mod depths** are an estimate (below); the pink noise's amplitude is not read. (The LFO's rate law is declined: below.) (x0.549 to freq, x7.52 / x6.67 to PW, x7.52 to cutoff): the LFO as read on SD312 (13 octaves,
   0.002-21 Hz) contradicts the factory sheets (5-5 Helicopter would chop at 1.3 Hz); the gap is probably in the CV path upstream (firmware
   or S&H scaling). Waits on the firmware's handling of LFO FREQ, or a recording. The engine keeps the 2.0.0 Wheel-Mod and LFO.
7. **Autotune** uses four bias CVs per oscillator by range (manual 2-19); the engine has one static residual.
8. **Glide's range** (the CA3080 / AD820 circuit) is still the 2.0.0 assumption; its unison-only behaviour is modelled.
3. **Noise level** into the mixer: the noise VCA on board 3 and the 5837's level are not read; model 1 keeps 2.0.0's level against the
   oscillators.
4. **The final VCA's control law** is an estimate (below).
6. **The 2020's signal-input range:** model 1 takes it as linear to 10 V; if it compresses above the datasheet's 6 Vpp, the filter
   envelope amount and Poly-Mod envelope depths are smaller than modelled. Waits on a datasheet curve or a recording.
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
- **2026-11-22, round 3 (model 1, Rev 2 only; offline): oscillator output networks and the modulation VCAs.** The shapes average
  through their resistor networks (saw 0-10 V, pulse -1.4-6.2 V, triangle +-5 V into osc B's node X through 3.3k), each oscillator is AC
  coupled at 8 Hz into its mixer VCA; Poly-Mod is the circuit's (75k / 39k VCAs into R432 47k, +-13.5 V, x1/3 oct, x3.32 PW, x1.82 oct),
  from node X with its DC; the filter envelope amount through its 62k VCA (10.9 octaves at 10 against 8). Check set: model 0 304/304
  identical, Rev 4 identical, service 0 failed, corners 256/0, engine and host tests OK, CPU unchanged (x86 Rev 2 Eco 3.2 / High 6.5 %).
  Against round 2 (High chord): 36 of 38 programs moved; the filter envelope makes most brighter (1-1 Brass +11 semitones of spectral
  centroid, +1.3 dB); Poly-Mod programs move most (5-1 Alien -9 dB, 4-7 Sawtooth Arpeggiator -6 dB / -12 semitones, 3-8 Echo-Repeat
  +11 semitones). Not yet heard.
- **2026-11-22, round 4 (model 1, Rev 2 only; offline):** glide works only in unison (manual 2-22: "The GLIDE circuit, active only in
  UNISON mode"), so many sheets' glide 6.5 in poly programs is inert, as on the instrument; the pulse is 0-7.5 V (manual 2-23; DC 3.75 V).
  The LFO reading was checked at 3x and against the sheets (above, item 2): left at model 0. Check set: model 0 304/304 identical, Rev 4
  identical, service 0 failed, corners 256/0, SPICE 20/0, engine test (now also: no poly glide in model 1, poly glide kept in model 0) and
  host test OK.
- **2026-11-22, round 5 (model 1, Rev 2 only; offline):** the firmware (v7.0c, `sub_0380`) pins both release CVs to 1.667 V when RELEASE is
  off: release 8.5 ms to -40 dB under the 2050 law (model 0: 5 ms). The LFO checked against the parts list, the firmware and the pot
  tapers: the circuit reading stands (item 2). Check set: model 0 304/304 identical, Rev 4 identical, service 0 failed, corners 256/0,
  engine and host tests OK.
- **2026-11-23, round 6 (model 1, Rev 4 only; offline): the Rev 3 filter section as a circuit.** `ma_vcf3320` (analog/mpc_analog_p5.h):
  four 3320 sections (current-mode cells, 150 pF, 100k / 91k / 240k), the 1.06 Hz output coupling, U475 x3.4, the Q IN network (with
  C4146) and the Q VCA. Against ngspice of the same circuit (`circuits/vcf3320*.cir`): gains within 0.24 % (48 kHz) / 0.08 % (96 kHz),
  -3 dB 433.4 / 433.1 Hz, self-oscillation 976.64 / 976.50 Hz at 0.8821 / 0.8837 V, 0 unconverged samples (MA_VCF_ITER 12: the
  heaviest drive at 19 kHz needed 9-12 passes). Trimmed by the Rev 3 manual's 4-20 (`tools/service_trim.py rev4`): CUT4_KV 0.99826,
  CUT4_TRIM +0.0364 oct; A3 440.000 Hz, A4 880.005 Hz. Against 2.0.0's Rev 4 (High chord): median -2.6 dB (to -9.9 dB on resonant
  programs: no Q compensation, item 9) and +3.7 semitones of centroid. Check set: SPICE 28/0, service 0 failed (7 cases), corners 256/0,
  goldens model 0 304/304 identical and model 1 Rev 2 unchanged, engine (the Rev 4 bass test now model 0's) and host tests OK. CPU: Live
  III Rev 4 Eco 4.1 -> 5.9 %, High 8.1 -> 11.4 %.
- **2026-11-22: installed for listening** as "Profit-5 2 Beta" (uid Pf5B, `./build.sh beta`, next to the 2.0.0 release) on the Force
  (/sdcard/Synths, its pluginList-arm entry added, MPC.settings backed up as MPC.settings.bak-p5beta-20261122) and the Live III
  (/nvme/VST2 Plugins). MPC restarted on both; no plugin errors in the log. Not yet heard.

9. **Rev 4's Q compensation** is an estimate (below); its topology needs tracing.
10. **Rev 4's mixer drive into the 3320** is an estimate (below). Rev 4's VCOs, envelopes and modulation keep the 2.0.0 laws: nothing on
   them is documented to estimate from.
11. **Force CPU for Rev 4:** Live III Rev 4 High 8.1 -> 11.4 %; the Force (offline on 2026-11-23) is estimated near 40 % in High.

## Done (continued)
- **2026-11-23, round 7 (model 1, Rev 4): the CEM 3320 datasheet.** It confirms every constant taken from the V3320 clone (I_ref 63 uA,
  60 mV/decade, gain 0.9 / 3.0, 1M output resistance, Q input 3.6k, 12 Vpp), and adds Fig. 6: the resonance cell's gm against its control
  current, now in the kernel and the deck (gm = 2.24 mmho x I / (I + 122.5 uA)). Rev 4's resonance is stronger (loop gain 6.2 at RES 10,
  oscillation from about RES 5.6) and its self-oscillation louder. Retrimmed (4-20): CUT4_KV 0.99833, CUT4_TRIM +0.0707 oct. Check set: SPICE
  28/0 (the 3320 AC cases moved to 0-25 uA, below the new onset), service 0 failed, corners 256/0, model 0 304/304 identical, Rev 2
  unchanged, engine and host tests OK.

## Estimates in model 1 (2026-11-23, the owner's request: "include the not modelled items even as some kind of estimate")
Each is marked ESTIMATE in `src/engine.c`, with its basis; replace it when a trace, a datasheet or a recording settles it.
| Item | Estimate | Basis |
|---|---|---|
| Rev 4 RES CV resistor | 225k (the Rev 3 schematic: 200k) | GForce Prophet 5 black-box render, filter "3" (2026-11-23, offline): self-oscillation from RES 6.5-7; 200k gave 6, 225k gives 6.5 |
| Rev 4 Q compensation | the output's bass (one-pole lowpass two octaves under the cutoff) x (1 + k)^0.30 (was 0.68; GForce loses about 5 dB of level at RES 5 and its Q comp changes it by under 1 dB), k the 3320 loop gain; until 2026-11-23 the whole output was scaled, which boosted the resonant peak and self-oscillation by up to +11.7 dB (a jump and overdrive above RES 6, heard on the Live III) | maffez.com: an SSI2164 cell (U9-D) on the Q CV adds level at the 3320's output; R274 16.2k, "more" at 10-12k, so stock is about 11/16.2 of full; a 2164 is dB-linear, so the fraction is taken in dB |
| Rev 4 mixer drive | one osc at 10 = 0.25 x I_ref into the 3320 | the Rev 2 circuit's headroom for one osc (0.27 of the 2040's swing); maffez's R82 169k "input limiting" |
| Rev 2 Wheel-Mod depth | W-MOD bus 1.31 V peak (one LFO shape, wheel at 10): freq +-8.6 semitones, PW A +-9.9 V, PW B +-8.7 V, cutoff +-9.9 octaves | SD312: shapes averaged through 10k / 200k, the SOURCE MIX 2020s (10k, diode law), the bus load 8.4k, the wheel network x0.489, the summers' gains; the pink noise keeps 2.0.0's level against the LFO |
| Rev 2 final VCA | gain = the 2020's diode-law control current through R474 15k, against its value at 10 V | the other 2020s' wiring on SD411; low sustain levels are quieter (1-3 Muted Clav, sustain 1: -7.1 dB) |
| Glide range | not estimated | the AD820 / CA3080 wiring (R3121-R3125) is not legible enough to give a current |

Levels after the estimates (High chord): Rev 4 against 2.0.0 median +1.6 dB (-4.0 to +8.1: 5-1 Alien, 5-6 Resonance Bells, 4-3 Fourths
With Resonance louder); Rev 2 against round 6 median -0.1 dB (1-3 Muted Clav -7.1, 5-5 Helicopter -3.1). Check set: corners 256/0,
service 0 failed, model 0 304/304 identical, engine and host tests OK. CPU (x86, today's host): Rev 2 High 9.2 -> 9.6 %, Rev 4 11.0 -> 11.1 %.

## Tried and dropped
- 2026-11-22: one division for four in the filter (a product's reciprocal): slower on the Force (High 36.4 -> 38.6 %).
- 2026-11-22: re-linearising the filter until OUT4 moved < 1 uV (always two passes): +60 % CPU. Replaced by stopping when no cell's
  gain moves by 1e-4 (1.02 passes); against a 1e-7 solve the renders are identical or under -88 dB, except 5-6 Resonance Bells, whose
  self-oscillation drifts in phase (same level to 0.01 dB, spectra within 0.1 dB).
- 2026-11-22: damping the re-linearisation: slower to converge, more unconverged samples.
- 2026-11-22: a cubic exp2 inside the filter's tanh: its kinks stall the iteration (High 9.3 -> 13.9 %). Kept instead: Lambert's (7,6)
  continued fraction for tanh(z)/z, capped at 1/|z| (continuous, within 1e-4): Force High 40.9 -> 36.2 %, renders within -89.9 dB of expf.
- 2026-11-22: a hard 0.65 V dead zone in the 2020 control law: silenced 5-5 Helicopter (-71 dB); replaced by the diode equation.

## Declined
- 2026-11-22: the Rev 2 LFO rate law as read (0.002-21 Hz, 0.83 Hz at 6.5): the owner keeps 2.0.0's law, which suits the factory sheets
  (written on Rev 1 units). The evidence is in `docs/CIRCUIT.md`.
