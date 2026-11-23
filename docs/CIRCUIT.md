# Circuit transcription (Rev 2 voice and board 3 summers)

Started 2026-11-22, offline. The first step of `docs/ROADMAP-accuracy.md` (Lemus method: transcribe the schematic part by part,
derive each law from the parts, check it in ngspice, and only then change the engine). Nothing here has been checked on a device or
against a recording.

## Sources (local only, never committed)
| Source | What | Used for |
|---|---|---|
| "Sequential Circuits Prophet 5 Service Manual.pdf" (141 pp, 72 ppi scan, hand-marked "REV C") | The Rev 1/2 technical manual (TM1000C). Voice 1 = **SD411 rev 2.0** (ECO 8-13-79), PDF pp 85-86 (manual 3-21); board 3 analog = **SD312**, PDF pp 75-76 (3-16); TUN MUX / S&H / AUD OUT = SD313, PDF pp 77-78 (3-17); voices 2-5 PDF pp 89-102; calibration Section 4, PDF pp 107-119; parts list Section 6, PDF pp 126-137 | Everything below. The scan is legible at 2-3x zoom; values marked "?" are not |
| "sequentialcircuitsprophet-5servicemanual-text-470674.pdf" (134 pp, 2176x3000, OCR) | **TM1000D.2, Rev 3.0-3.2** (CEM chips, S/N 1301+). Not the instrument Profit-5's Rev 2 models | Later: the Rev 3 filter (the Rev 4 reissue's "Rev 3" voicing) |
| P5REV4_VOICE_VCO.pdf | A partial community KiCad trace of the Rev 4 reissue's VCO mixer (SSI2140 and 3320 filter inputs) | Later: replaces guesses in `REV[1]` |
| SSM 2020 and SSM 2040 datasheets (bitsavers.org, components/solidStateMusic) | 2020: I_out = I_c (V+ - V-) / 14 V, gm 1/14k at I_c = 1 mA. 2040: -18 mV/oct, gm 1/5k typ (1/10k-1/3k), w0 = 200 G / (R2 C), outputs swing +-1 V, THD 0.1 % at 30 mV and 1 % at 80 mV on the cell input | Mixer, VCAs, filter |
| SSM 2030 and SSM 2050 datasheets (supplied 2026-11-22) | 2050: 100 ms at 0 V with 0.1 uF, +18 mV/oct, 2 V/decade through 100k, control input 3.1k. 2030: 10 Vpp saw, 7.5 Vpp pulse, 500 ns fall, positive control lowers the pitch | Envelopes; the VCO constants confirmed |

## Voice board (SD411, voice 1; reference designators of voice 1)

### Osc A (U463 SSM 2030)
- **Pitch summer** U455 (348), inverting: BIAS 1A CV via R499 10M 1 %, KEY 1 CV via R4100 100k, A SUM CV via R497 100k (R497 = R4100 matched
  to 0.01 %), R498 300k 1 % from P-MOD FREQ A (a 13202 on the P-MOD MIX output), INIT FREQ trim R411 100k (+-15 V) via R4122 2.2M. Feedback R496 90.9k 1 % + R410 5k
  (V/OCT, 20 turn).
- **Expo divider:** R451 54.9k 1 % into RT402 1k 1 % **3600 ppm** + R452 121 1 %, to the 2030's BASE (pin 12); 20.0 mV per summer volt at 25 C.
- HI V/OCT: R414 5k into EMIT (pin 13), R450 487k 1 % + D402 1N914 to BASE. Reference: R449 2.2M 1 % from +15 V into COLL (pin 10) with U480
  (TL082) and C406 100 pF, R448 10k.
- Timing cap C415 **1000 pF polystyrene**; C416 .001 on COMP (pin 15); R455 22k; C407 .001 on HARD SYNC (pin 7) from a 4016 (OSC A SYNC S).
- Outputs: saw pin 6 (R453 22k load) and pulse pin 8 (R454 22k), each through a 4016 and 10k (R456, R457) to C409 0.1 uF and R481 200k:
  the mixer VCA input, AC coupled at 8 Hz.
- **PW A summer** U458: PW A INIT CV via R495 100k 1 %, PW A W-MOD CV via **R492 13.3k 1 %**, P-MOD via **R494 30.1k 1 %**, feedback R493 100k 1 %;
  then R491 100k / R490 100k 1 % || C417 .001 (tau 0.1 ms) to PWMOD (pin 9). So W-MOD reaches PW A x7.52, Poly-Mod x3.32, both against the
  pot's x1.

### Osc B (U459 SSM 2030)
- Same converter (R440 54.9k, RT401 1k 3600 ppm, R442 121; R412 HI V/OCT 5k, R439 487k, D401; R441 2.21M 1 %; C404 1000 pF poly, C405 .001).
- Pitch summer U447: B SUM CV via R4101 100k (matched to R4103), BIAS 1B CV via R4102 10M 1 %, KEY via a 4066 (OSC B KBD S) and buffer U495
  (R486 2.2M pull-down) into R4103 100k, INIT FREQ R409 100k via R4121 2.21M, feedback R4104 90.9k + R408 5k.
- Triangle: pin 5 via U457 (348) with R445 10k, R446 20k, R447 20k, R489 15k, R487 7.5k, TRI SYM trim R413 100k via R488 47k (the buffer the
  manual's note 3 modifies: R444 22k, TL082).
- Saw and pulse through 4016s and R436 / R435 10k into U456 (TL082), R434 200k: osc B goes to the mixer and to the Poly-Mod VCA.

### Mixer (SSM 2020s on the voice board)
- MIX OSC A AMT VCA (1/2 U473): signal on +SIG (pin 13) from C409 / R481; -SIG (pin 14) grounded; **control pins 9, 10, 12 tied, fed by
  R482 39k from MIX OSC A AMT CV**, emitter (pin 11) grounded: I_c ~ (CV - 0.65 V) / 39k, 240 uA at 10 V.
- MIX OSC B AMT VCA (1/2 U473): the same with R458 39k; input R438 10k, C408 0.1, R439 200k.
- Noise: NSE (from board 3's noise VCA, connector D) through **R460 100k** onto the bus.
- **Bus:** the 2020 current outputs, R460 100k and R476 200k into stage 1 of the filter. The bus is loaded by 100k || 200k = 66.7k, so
  an oscillator at 10 (a +-5 V saw) gives about 5.5 V peak on the bus and 27 uA into stage 1: **0.27 V at each 2040 stage output** against
  the chip's +-1 V swing. Two oscillators and noise use about half of it; resonance takes the peaks into the limits.

### Filter (U478 SSM 2040, four lowpass real-pole stages, datasheet Fig. 1)
- Each stage: R1 10k from the previous output, R2 10k from its own output, 200 ohm to ground at IN, C 1000 pF (C414, C410, C412, C411).
  Ladder: IN1 -R480- O1 -R483- IN2 -R484- O2 -R462- IN3 -R465- O3 -R466- IN4 -R467- O4 -R461- U479. Shunts R475, R485, R464, R463 200 ohm.
  Stage gain -R2/R1 = -1; pole w0 = 200 G / (R2 C) = 637 Hz at G = 1/5k (CONT at 0).
- Input: the mixer bus via R476 200k into IN1.
- Output: O4 via R461 10k into U479 (TL082) virtual ground, R478 200k feedback (x20, current to voltage), R473 10k on +IN. That is "F".
- **Resonance:** F through **C413 0.1 uF, R471 100k to ground (high-pass at 15.9 Hz)** into the RES FDBK VCA (1/2 U490 2020, +SIG pin 3),
  control R468 30k from FILT RES CV (I_c ~ (CV - 0.65) / 30k, 311 uA at 10 V), current output (pin 1) straight into IN1. The loop is
  AC coupled: below 16 Hz there is no feedback, so the bass loss under resonance stops there.
- **Cutoff CV summer** U435 (348), inverting: KEY 1 CV via a 4066 (FILT KBD S) and R4106 100k (matched to R4107), FILT SUM CV via R4107 100k,
  P-MOD FILT via R4105 54.9k 1 %, FILT ENV AMT VCA via R4109 100k 1 %, INIT FREQ R407 100k via R4120 2.2M 1 %; feedback R4108 102k 1 % +
  R406 10k (V/OCT). Output via **R477 54.9k 1 % / R479 1k 1 %** to CONT (pin 7): 17.9 mV per summer volt against -18 mV/oct, so about
  1 oct/V, trimmed by R406. R479 is plain 1 %, not a tempco part.
- Final VCA (1/2 U490 2020): F via C413 / R471, BAL trim R415 100k via R469 15k, R470 1.5k; output to R416 VOICE LEVEL 10k, R472 100k to
  VOICE 1 OUT (the voice summer U498 on SD313).

### Envelopes (U406 filter, U401 amp, SSM 2050)
- Each time CV (A, D, R) through 100k 1 % (R426, R425, R424; R417, R418, R419) into the 2050's 3.1k control input, with **500k 1 %** each
  to a TC trim (R402 / R401 50k rheostat to -15 V): a per-voice offset of about -2.3 to -3 V; the CVs' own scale is unchanged (corrected
  2026-11-22; first read as a x0.833 divider). The S&Hs feeding them are x1.1 on S/N 590 and up (SD313, ENV GEN S/H GAIN MODIF).
  Trimmed so 6.250 V (6.875 V with the modification) gives a 0.9 s attack (manual 4-10, 4-11). Sustain CVs go in direct. Timing cap **C418 / C419 0.1 uF**.
- TRIG: GATE via R423 15k and C402 .01 (R420, C401 on the amp side); GATE via R422 15k (R421).
- Note 2: an extra .01 on OUT for "Interdesign" 2050s.

### Modulation VCAs (2020s)
- P-MOD OSC B AMT VCA (1/2 U426): osc B via R433 3.3k; BAL trim R405 100k via R430 15k, R431 1.5k, R429 75k.
- P-MOD FILT ENV AMT VCA (1/2 U411): R4111 39k, BAL R404 via R4112 15k, R4113 2.2k, output load R432 47k; summed in P-MOD MIX (U433
  TL082) and switched by 13202s to P-MOD FREQ A (R498 300k), P-MOD PW A (R494 30.1k) and P-MOD FILT (R4105 54.9k).
- FILT ENV AMT VCA (1/2 U411): BAL R403 via R427 15k, R428 2.2k, R4110 62k; into the cutoff summer via R4109 100k.
- Their control-pin wiring is not legible enough at this resolution to derive gains: **open**.

## Board 3, common analog (SD312, PDF p. 76)
- **Bend and tune bus** U365 (1458): PITCH WHEEL R701 100k lin via **R3126 412k 1 %** (with C340 0.1 uF: 41 ms), MTUN (rear J301) via
  R3131 2.2M 1 %, OFST trim R307 100k via R3124 2.2M 1 %, R3128 2.2M to -15 V; feedback R3129 100k 1 %. Its output reaches both oscillator
  master summers' second stages (R3113, R3112 100k). **So 412k is the pitch wheel** (x0.243). The wheel pot's end voltages are not read yet,
  so the bend range still waits on them.
- **Osc A master summer:** stage 1 (U377): **W-MOD FREQ A via R3117 182k 1 %** (x0.549; R3135 10k pull-down when its 13202 is open), the
  glide output via R3119 100k; feedback R3118 100k. Stage 2: stage 1 via R3115 100k, OSC A INIT CV via R3114 100k, bend/tune via R3113 100k;
  feedback R3116 100k || C344 0.01 uF (1 ms). Out: A SUM CV.
- **Osc B master summer:** stage 1 (U377): LO via R3103 100k, glide (via a 4066, OSC B KBD S, and U378 buffer, R3120 2.2M pull-down) via
  R3105 100k, **W-MOD FREQ B via R3100 182k 1 %** (R346 10k pull-down), **OSC B FINE CV via R3107 10M 1 %** (x0.01, not 2M); feedback R3109
  100k. Stage 2: R3108 100k, OSC B INIT CV via R3110 100k, bend/tune via R3112 100k; R3111 100k || C347 0.01 uF.
- **LO FREQ:** a 13202 (OSC B LO S) switches a -15 V 100k/100k divider (R3134, R3133: -7.5 V behind 50k) into R3103: x100k/150k, **5.0 V,
  so 5 octaves down** at 1 V/oct (the engine assumes 7).
- **Filter master summer:** stage 1 (U376): FILT CTF CV (the cutoff pot) via R393 100k, FILT CV IN (rear J708) via R394 100k, the glide output
  (via a 4066, FILT KBD S) via R399 100k, **W-MOD FILT via R398 13.3k 1 %** (x7.52); feedback R3104 100k. Stage 2: R3106 100k, **R3102 249k
  1 % from +15 V** (a fixed 6.02 V), R3101 100k || C342 0.01 uF. Out: FILT SUM CV.
- **PW B master summer** U376: OSC B PW CV via R391 100k, **W-MOD PW B via R392 15k** (x6.67); feedback R395 100k; then R396 / R397 100k.
- **W-MOD PW A** leaves board 3 unscaled (P303-46) and is scaled on the voice by R492 13.3k (x7.52).
- **Glide:** UNI CV via R3130 4.7k into a CA3080 (U379) whose inputs are clamped by D302/D303 (constant slew), C348 0.1 uF, U378 buffer,
  feedback R3127 4.7k. I_ABC from an AD820 pair (Q309): GLIDE CV via R3121 100k / R3122 2.7k, R3123 15k from +15 V, R3125 100k. The
  constant-rate glide in the engine is right in shape.
- The 2.21M input the earlier reading listed is not on these summers: it was R4121 2.21M (osc B INIT FREQ trim, voice board).

## Laws this gives, against the engine (`src/engine.c`)
| Law | Circuit | Engine today | Status |
|---|---|---|---|
| Osc B fine | 10M into 100k: 0.01 x 10 V = 0.1 oct, 1.2 semitones (1.28 at the DAC's 10.67 V) | 0..+1 semitone (user report) | done (model 1, round 1) |
| Osc B LO | -5 V on the summer: 5 octaves down | 7 octaves (`1/128`) | done (model 1, round 1) |
| Cutoff span over the pot | 10 V at FILT SUM CV, ~1 oct/V: 10 octaves | 10.5 | done (model 1, round 1; trimmed by 4-17) |
| Cutoff offset | R3102: -6.0 V (6 octaves) fixed; with R4120's trim it sets "450 Hz at 6" | anchored on the manual's 400-500 Hz at 6 | consistent; keep the anchor |
| W-MOD to cutoff | x7.52 (13.3k) | 3 octaves at full | needs the W-MOD source level (LFO amplitude, wheel pot): next |
| W-MOD to PW A / PW B | x7.52 / x6.67 | 0.4 of the range | as above |
| W-MOD to freq A / B | x0.549 | 4 semitones | as above |
| Poly-Mod to PW A | x3.32 (30.1k) | 0.5 | needs the P-MOD VCAs' gains |
| Poly-Mod to cutoff | x1.95 (54.9k against ~107k) | 6 / 5 octaves | as above |
| Poly-Mod to freq A | x0.31 (300k against ~93k) | 48 / 24 semitones | as above |
| Pitch wheel | x0.243 (412k) of the wheel pot's span | +-2 semitones | needs the pot's end voltages |
| Mixer level and filter drive | one osc at 10: 0.27 V per stage of +-1 V | 0.45 x levels into a +-1.4 tanh | done (model 1, round 2: `ma_2020_ctl`) |
| Resonance loop | 2020, I_c = (CV - 0.65) / 30k into IN1, AC coupled at 15.9 Hz | k = 4.4 x pot, DC coupled | done (model 1, round 2: `ma_vcf2040`; k = 4.45 at RES 10) |
| Filter stage limits | 2040 outputs +-1 V; cell input THD 1 % at 80 mV | tanh knees fitted by ear | done (model 1, round 2; where the limit acts is assumed) |
| Envelope time CVs | 2 V/decade x1.1, offset trimmed to 0.9 s at 6.250 V, C = 0.1 uF | model 1 | done (round 1) |
| VCO V/oct trim range | divider 20.0 mV/V; summer x0.909-0.959: 1.02-1.08 oct/V at 25 C, never 1.00 | 1.000 | **open**: a misread or a missing factor; check the parts list and the 2030 sheet |

## Round 3 reading (2026-11-22, voices 3 and 4, SD412-SD415, PDF pp 89-102; SD312 left half, PDF p. 75)

### Oscillator output networks (2030 levels: saw 0 to +10 V and pin 5 "half a sawtooth", E-mu SSM 2030 sheet p. 4; pulse 0 to 7.5 V across its
### pull-down, manual 2-23, which the E-mu sheet gives as -1.4 to +6.2 V)
- Osc A: saw (R453 22k load) and pulse (R454 22k) through 4016s and R456 / R457 10k to one node, C409 0.1 uF, R481 200k into the MIX OSC A
  VCA. The shapes average: each 1 / (n + 0.05) of itself (n shapes on), 0.952 for one. AC coupled at 7.96 Hz.
- Osc B: saw and pulse through 4016s and R436 / R435 10k into R434 200k and the U442 (TL082) buffer, then **R438 10k to node X**. The
  triangle (U462, the datasheet's triangle converter: 4h - 2 saw - 15 = +-5 V, with R445 10k, R446 / R447 20k, R489 15k, R487 7.5k,
  TRI SYM R413 via R488 47k) through a 13202 and **R433 3.3k into node X** (voice 4 shows the junction). With the triangle on, X is
  0.248 of the buffer plus 0.752 of the triangle. X feeds the P-MOD OSC B VCA's +SIG directly (DC coupled) and the MIX OSC B VCA through
  C408 0.1 uF / R439 200k (8 Hz).
- So Poly-Mod from osc B carries the selected shapes' DC (a saw averages +4.76 V at X): raising P-MOD OSC B with a saw or pulse raises
  osc A's pitch. Manual 4-14 trims the VCA's balance with osc B's shapes all off (Table 4-0), so the trim does not remove it.

### Poly-Mod and filter envelope amount (voices 3, 4)
- P-MOD OSC B AMT VCA (1/2 U421-U422): +SIG from X, -SIG on the BAL network (R405 100k via R430 15k, R431 1.5k; voice 3 reads 4.7k),
  control pins tied to **R429 75k** from P-MOD OSC B AMT CV.
- P-MOD FILT ENV AMT VCA (1/2 U413-U414): +SIG from the filter envelope (0-10 V), BAL R404 via R4112 15k / R4113 2.2k, control **R4111
  39k** from P-MOD FILT ENV AMT CV.
- Both current outputs into **R432 47k** and U427 (348, a follower): the P-MOD MIX, switched by 13202s to FREQ A (R498 300k: 1/3 oct per
  volt), PW A (R494 30.1k: x3.32) and FILT (R4105 54.9k: x1.82 oct per volt).
- FILT ENV AMT VCA (1/2 U413): +SIG from the filter envelope, BAL R403 via R427 15k / R428 2.2k, control **R4110 62k**; its current output
  into the cutoff summer's virtual ground through R4109 100k (no load to ground on the drawing), so octaves = I x 100k.
- ASSUMED: the 2020's signal input is linear to 10 V (I = I_c V / 14 V); the datasheet gives 0.1 % THD at 6 Vpp only.

### W-MOD source and LFO (SD312, PDF p. 75)
- SOURCE MIX: two 2020s (U373): pink noise into one (control R376 10k from SRC MIX CV), the LFO into the other (control R388 10k from U367,
  10 V - SRC MIX: R359 100k, R375 100k, R3132 150k from -15 V). Outputs into R364 100k, then R360 4.7k to the MOD WHEEL (R702 100k lin,
  R374 4.7k across it), the wiper buffered by U367 onto the W-MOD bus. BAL R304 / R305.
- LFO: a 2030 (U371) with **C335 0.01 uF** (not 1000 pF), its rate CV through **R368 75k** into U372 (R371 100k, R370 100k from -15 V:
  15 - 1.33 x CV), then R377 54.9k / R381 1k to BASE, R382 2.21M reference. As read, 13.3 octaves over the pot, which puts the sheets'
  LFO 6.5 near 0.8 Hz. Re-read at 3x (2026-11-22): every value stands (R368 75k, R371 100k, R370 100k, R377 54.9k, R381 1k, C335 0.01
  uF poly, R382 2.21M; Q308 discharges C335, "required for low-frequency operation", manual 2-23). Against the factory sheets it fails:
  5-5 Helicopter (LFO 7) would chop at 1.3 Hz, 4-6 Random Arpeggiator (3.5) step at 0.05 Hz, 4-4 and 5-1 (2) at 0.01 Hz; the model 0
  law (0.5 Hz x 2^(5.33 x pot)) gives 6.6, 1.8 and 1.1 Hz. The gap is likely upstream of U372 (the firmware's or the S&H's scaling of
  LFO FREQ CV; only the oscillator FREQ pots are known to be halved). **Open**; the engine keeps its LFO.
  Checked further (2026-11-22): the parts list (Section 6, PDF p. 129) gives R368 75k, R370 / R371 100k, R377 54.9k, R381 1k, R382 2.21M,
  C335 .01 uF poly, and every panel pot as 100k linear (R101-R113, R201-R213); the firmware passes the LFO pot to its S&H unchanged
  (`docs/FIRMWARE.md`); and the same frequency formula (I_ref / (C x 10 V)) puts the VCO's lowest key near C0, as manual 4.2 says. So a
  Rev 2 LFO spans about 13.3 octaves, 0.002-21 Hz, 0.83 Hz at 6.5. The factory sheets (1978) were written on Rev 1 units, whose board 3
  may have differed; that is the likeliest reason they disagree.
- The LFO's shapes: saw and pulse through 4066s and R385 / R386 10k, the triangle by a converter (U372, R365 10k, R355 / R362 20k, R369 15k).

## Open
- The W-MOD bus level: the LFO's and the pink noise's amplitudes (the crossfade and wheel are read above).
- The LFO's rate law (above).
- The pitch wheel pot's end voltages (SD312, top right, near R701).
- Whether the common glide (UNI CV) feeds the voices in poly mode (`docs/FIRMWARE.md` says unison uses the five key S&Hs).

## Theory section facts (manual Section 2, PDF pp 14-49, OCR 2026-11-22)
- "The GLIDE circuit, active only in UNISON mode" (2-22, U379): poly mode does not glide on Rev 1/2. Model 1 follows it.
- Pulse output "0-7.5V across pull down R454 or R443" (2-23).
- The triangle: "subtracting the sawtooth from the half-sawtooth ... and amplifying by X2" (2-23).
- The LFO "differs by having R378/80 fix PW at 50%, and Q308 discharge C335" (2-22).
- Autotune: four BIAS CVs per oscillator, matched at 300, 600, 1200 and 2400 Hz and used by range (2-19); not modelled.

## Rev 3 filter (TM1000D, voice 2 SD432, IA scan PDF p. 51; the model 1 "Rev 4" setting) - read 2026-11-23
Chip law from the CEM 3320 datasheet (Curtis, 1980; the V3320 clone's gives the same constants; `docs/SOURCES.md`): each section is a current-in, current-out cell,
I_out = (I_ref - I_in) A e^(-Vc/V_T), I_ref = (0.46 Vcc - 0.65) / 100k (63 uA), into its pole cap and a buffer (quiescent 0.46 Vcc =
6.9 V, swing Vcc - 3 V peak to peak); the cell's 1M output resistance reflects across the feedback resistor; frequency control 60 mV per
decade (18 mV/oct), positive lower; the Q VCA: signal input 3.6k to ground (AC coupled), gm 1 mmho at 100 uA of control current into a
low-impedance node, its output into stage 1.
- **Sections (U470):** CAP A-D C4153, C4152, C4171, C4172 **150 pF 5 % poly**. Feedback R4419, R4420, R4464, R4463 **100k**; coupling
  R4422, R4418, R4461 **91k** (stage gain -(100k || 1M) / 91k = -0.999); R4421, R4417, R4462 **240k** from -15 V into IN B-D (balance the
  coupling's DC). Iee R4465 1.5k to -15 V.
- **Input:** the mixer's CA3280 current outputs (U465: osc A saw through R4373 150k / R4372 330, pulse into the inverting input through
  R4375 200k / R4374 330, osc B through R4244 / R4245 150k / R4379 330; bias through R4377 / R4378 3.3k from MIX AMT CV) and NOISE through
  R4376 100k, straight into IN A. The CA3280s' bias law (Rev 3's CV scheme) is not resolved, so the drive is **assumed** (below).
- **The resonance cell's gm** is not linear in its control current (CEM 3320 sheet, "a modified linear scale", Fig. 6): digitised at 300
  dpi and fitted, **gm = 2.24 mmho x I / (I + 122.5 uA)** (within 14 umho over 25-300 uA; the figure's "MAX GM" line reads 2.23 mmho;
  1.0 mmho at 100 uA, the typical). The cell is a "traditional transconductance" amplifier: output -I_cr tanh(v gm / I_cr). At Rev 3's
  50 uA (RES 10) gm is 0.65 mmho, not the 0.5 of a straight line: the loop gain at RES 10 is 6.2 and oscillation starts near RES 5.6
  (with the nominal 3.6k Q input; the sheet allows +-900 ohm).
- **Output and resonance:** OUT D through **C4165 2.2 uF / R4468 68k** (1.06 Hz) into U475 (TL082, **x3.4**: R4506 240k / R4505 100k);
  U475 feeds the final VCA and, through **R4425 51k**, Q IN (R4424 3k + C4146 10 uF to ground, with the pin's 3.6k: x0.0311 above about
  25 Hz). Q CNTL from FILT RES CV through **R4423 200k** (50 uA at 10 V). Loop gain 90.9k x gm(I_cr) x 0.0311 x 3.4 (above).
- **Cutoff:** FILT FREQ SMR U434 (LM348): FILT S/H, FILT SUM CV and the Poly-Mod (R4180 54.9k) into R4146 162k + FIL SCALE R4134 50k ||
  C468 .001, then R4466 187k / 1.82k (9.64 mV per volt) to Fc CNTL, offset by F OFF (R4504 100k via R4503 249k): about 1 oct/V once trimmed.
- **Calibration (TM1000D 4-20):** cutoff CV 2.000 V, unison, resonance up: A3 rings at 440 Hz and A4 at 880 Hz (INIT FREQ and FILT SCALE).
  "There is no computer-correction of filter tuning ... There is also no temperature compensation."
- **The Rev 4 reissue** (maffez.com page): both an SSI2140 and a CEM3320 are fitted; the switch turns each one's output VCA on or off and
  shifts the 3320's control an octave (its caps are twice Rev 3's). The 3320 path has output Q compensation (a 2164 cell, U9-D, R274 16.2k)
  and DC blocking; its mixer is SSI2164 cells into R82 169k. **Not modelled**: the compensation's topology and the mixer's gain are not
  traced.
