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
| SSM 2030 and SSM 2050 datasheets | **Not found** on bitsavers (2026-11-22). The previous session's copies were cloud uploads | The 2050 time law and the 2030 converter stay as `docs/ENGINE.md` has them |

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
- Each time CV (A, D, R) through 100k 1 % (R426, R425, R424; R417, R418, R419) with **500k 1 %** to a TC trim (R402 / R401 50k to -15 V):
  the time CVs are x0.833 plus a per-voice offset. Sustain CVs go in direct. Timing cap **C418 / C419 0.1 uF**.
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
| Osc B fine | 10M into 100k: 0.01 x 10 V = 0.1 oct, 1.2 semitones (1.28 at the DAC's 10.67 V) | 0..+1 semitone (user report) | circuit agrees with the report; adopt 1.2 |
| Osc B LO | -5 V on the summer: 5 octaves down | 7 octaves (`1/128`) | **change to 5 (1/32)** |
| Cutoff span over the pot | 10 V at FILT SUM CV, ~1 oct/V: 10 octaves | 10.5 | **change to 10** |
| Cutoff offset | R3102: -6.0 V (6 octaves) fixed; with R4120's trim it sets "450 Hz at 6" | anchored on the manual's 400-500 Hz at 6 | consistent; keep the anchor |
| W-MOD to cutoff | x7.52 (13.3k) | 3 octaves at full | needs the W-MOD source level (LFO amplitude, wheel pot): next |
| W-MOD to PW A / PW B | x7.52 / x6.67 | 0.4 of the range | as above |
| W-MOD to freq A / B | x0.549 | 4 semitones | as above |
| Poly-Mod to PW A | x3.32 (30.1k) | 0.5 | needs the P-MOD VCAs' gains |
| Poly-Mod to cutoff | x1.95 (54.9k against ~107k) | 6 / 5 octaves | as above |
| Poly-Mod to freq A | x0.31 (300k against ~93k) | 48 / 24 semitones | as above |
| Pitch wheel | x0.243 (412k) of the wheel pot's span | +-2 semitones | needs the pot's end voltages |
| Mixer level and filter drive | one osc at 10: 0.27 V per stage of +-1 V | 0.45 x levels into a +-1.4 tanh | **derive** (below) |
| Resonance loop | 2020, I_c = (CV - 0.65) / 30k into IN1, AC coupled at 15.9 Hz | k = 4.4 x pot, DC coupled | **derive** (filter as a circuit) |
| Filter stage limits | 2040 outputs +-1 V; cell input THD 1 % at 80 mV | tanh knees fitted by ear | **derive** |
| Envelope time CVs | x0.833 (100k / 500k) plus a TC offset, C = 0.1 uF | 0.5 ms anchor, 5.33 decades over the pot | span x0.833: 4.44 decades if the 2 V/decade law holds; anchor needs the 2050 sheet |
| VCO V/oct trim range | divider 20.0 mV/V; summer x0.909-0.959: 1.02-1.08 oct/V at 25 C, never 1.00 | 1.000 | **open**: a misread or a missing factor; check the parts list and the 2030 sheet |

## Open
- The 2050 and 2030 datasheets (the env time anchor; the 2030's BASE/EMIT converter, which bears on the V/oct question above).
- The P-MOD and FILT ENV AMT VCAs' control wiring (centre of the voice sheet, in the binding): try voices 2-5, which are drawn the same.
- The W-MOD source: the LFO's output levels, the SOURCE MIX crossfade and the wheel pot (SD312 left half, PDF p. 75).
- The pitch wheel pot's end voltages (SD312, top right, near R701).
- Whether the common glide (UNI CV) feeds the voices in poly mode (`docs/FIRMWARE.md` says unison uses the five key S&Hs).
