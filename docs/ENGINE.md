# Engine (`src/engine.c`)

Started 2026-10-20, offline only (x86 tests under ASan/UBSan; no device yet). Each law below is marked with its source: **manual** (Rev 1 service manual),
**firmware** (v7.0 disassembly), **sheets** (factory patch sheets) or **assumed** (a placeholder until measured on a real unit or a recording).

## Rev 2 / Rev 4 switch (`P5P_REV`)
One switch changes everything that differs between the two voicings. Rev 2 is modelled from the schematics. Rev 4 is modelled from the public
description of the reissue, not from a unit or its schematics.

| | Rev 2 (SSM) | Rev 4 (CEM style) |
|---|---|---|
| VCO | SSM 2030: 500 ns reset (the datasheet's saw fall time), 30 % left after the HI V/OCT trim (an 8.4 kHz note is 1.8 cents flat), pulse at 0.75 of the saw, extra saw bow, osc B triangle with a per-voice symmetry error and the peak glitch (manual 3.4) | 0.2 us, 10 % (0.2 cents flat), clean saw and triangle, full-level pulse |
| Filter | `ma_ssm2040_lpf`: bass drops by 1/(1+k), harder knee (manual schematic) | `ma_ota_lpf` 4-pole: part of the bass given back, softer knee |
| Envelopes | SSM 2050 times; per-voice RC spread 8 % x Vintage (manual 3.4) | attack x0.6, decay/release x0.7 (assumed), spread 3 % |
| Pots | 121 steps, the 7-bit DAC in 1/12 V steps (manual 3.3, firmware) | smooth |
| Velocity, pressure | none (the instrument has neither) | `VEL_FILT`, `VEL_AMP` (filter env / amp level), `AT_FILT` (cutoff), `AT_LFO` (adds to the wheel) |
| Tuning residual | autotune bias step about 1.3 cents, drift 0.7 cents, cutoff drift 3 % of an octave (Vintage x) | 0.2 cents, 1.5 % |

Both revisions share `P5P_VINTAGE` (0..1), which scales drift and tolerance (it defaults to 0.6). Frequency pots step in semitones in both.

## Is the VCO different from Profit-08's?
Yes. Profit-08 models DCOs (`ma_dco_*`), which are digitally timed and exactly in tune. Profit-5 uses the band-limited ramp-core `ma_osc_*` with the behaviour of a real
exponential VCO on top:
- **Pitch:** the key, frequency pot, tune and bend go through an exponential, then the reset error `f / (1 + f * t_reset * residual)`. That's the flat top end the HI V/OCT trim
  only partly fixes.
- **Offsets:** a static per-oscillator offset (the autotune bias residual) and a slow 1/f drift per oscillator.
- **Saw:** a slight bow from the leaky charging.
- **Osc B triangle:** taken from the core through a buffer with a symmetry error and a short glitch after the peak.

The waveform's reset itself (3 us) is far shorter than a sample even at 96 kHz, so it only shows up in pitch, not in the waveform. The polyBLEP saw is the right model at these rates.

## Laws
| Control | Law | Source |
|---|---|---|
| Osc frequency | 0..60 semitones, frequency 0 = C0 (16.35 Hz) on the lowest key (MIDI 36) | manual 3.3, 4.1-4.3; firmware |
| Osc B fine | 0..+6 semitones: the fine CV enters the 100k osc B master summer through 2M (0.05 x 10 V = 0.5 V) | board 3 p3 (derived) |
| Osc B LO FREQ | 7 octaves down (a 100k/100k divider switched into the summer; the scale is assumed) | board 3 p3 / assumed |
| Pulse width | duty = pot: the 2030's PWM is 10 %/V, 100 % at 0 V to 0 % at 10 V (the engine's sense is reversed; it is the same range); silent at the ends. Pulse level 0.75 of the saw in Rev 2 (7.5 Vpp against 10 Vpp) | SSM 2030 datasheet |
| Cutoff | 450 Hz at pot 6 on the lowest key with KBD on, 10.5 octaves over the pot; KBD = 1 V/oct | manual 3.4 (one point); span assumed |
| Resonance | k = 4.4 x pot; self-oscillation from about pot 0.91, seeded with noise and a key click above k = 3.6 | assumed; the sheets' 5-6 needs it |
| Filter env amount | 0..8 octaves | assumed |
| Env times | tau = 0.5 ms x 10^(5.33 x pot), capped at 30 s: 2 V per decade over the 0..10.67 V CV. Attack approaches +13 V and ends at +10 V (1.466 tau, target 1.3). Decay and release are exponentials to sustain and to 0 (pot-to-time 4.6 tau to -40 dB) | SSM 2050 datasheet (slope, shape); the 0.5 ms anchor is assumed |
| RELEASE switch off | release 5 ms | assumed |
| LFO | 0.5 Hz x 2^(5.33 x pot), about 0.5-20 Hz: the LFO is a 2030 at 1 V/oct whose CV is halved, so 5.33 octaves. Pot 6.5 gives 5.5 Hz, the sheets' vibrato setting. Saw / tri / square summed | board 3 p3 + datasheet (span); 0.5 Hz anchor assumed |
| Wheel-Mod | source = LFO x (1 - mix) + noise x mix (a 2020 crossfade on SOURCE MIX, then the 100k linear wheel pot); the noise is the board's "pink" output, one pole at 159 Hz (100k, 0.01 uF); pitch 4 semitones, PW 0.4, cutoff 3 octaves at full | assumed (sheets: "wheel 1/8 for vibrato") |
| Poly-Mod | freq A: 48 semitones x filter env + 24 x osc B; PW A 0.5; cutoff 6 / 5 octaves | assumed |
| Glide | constant slew rate (a CA3080 OTA charging 0.1 uF, rate set exponentially by an AD820 pair), tanh-soft over the last half semitone; 3000 .. 10 semitones/s over the pot, off below 0.03 | board 3 p3 (shape derived; range assumed) |
| Voice assignment | the same note reuses its voice, else the next free voice after the last one used, else the oldest is stolen; 1-5 voices | firmware |
| Unison | every voice on the newest note; releasing it falls back to the held one | firmware (stacking); fallback assumed |
| CV RC | 2 ms one-pole after every pot | assumed |
| Output | sum of voices, 2x half-band decimation in High, soft limiter with a 1.0 ceiling | as Profit-08 |

## From the schematics (2026-10-21, offline)
Board 3 page 3 (analog), read with the voice page:
- **DAC:** seven weighted transistors, MSB 5.333 V, so 128 x 83.3 mV. That confirms the 1/12 V steps and 121 steps to 10 V.
- **LFO:** the LFO is itself an SSM 2030 with a 1000 pF timing capacitor and its own triangle-symmetry trim. Its saw, pulse and triangle are switched (4016) and summed.
  The rate range needs the 2030's converter constants; still assumed.
- **Noise:** a 5837 noise IC. White noise goes to the voices through a 2020 VCA (NOISE VOL). The wheel-mod's noise is a "pink" output, a one pole at 159 Hz.
- **Wheel-Mod:** 2020 crossfade (SOURCE MIX), 100k linear mod wheel, 13202 switches to each destination's summer; PW B through 15k into a 100k summer.
- **Master summers:** one per osc and one for the filter, built from matched 1 % resistors (100k unity inputs, 412k, 182k, 2.21M), each followed by a 0.01 uF / 100k
  smoothing stage (tau 1 ms). Osc B fine goes through 2M. Which control feeds the 412k, 182k and 2.21M inputs isn't legible on this scan, so those depths are still assumed.
- **Glide:** the CA3080 slew limiter on the unison/common CV. Osc B LO switches a 100k/100k divider into osc B's summer.

Boards 1 and 2 (2026-10-21): every panel pot is **100k linear**, a divider from the reference to ground (board 1: "11 x 100K LIN"), so all the curves are in
the circuits after the pots. Board 2 is the switch/LED matrix (4042 latches, current sinks); its grouping is the Rev 1 board's and is recorded in FIRMWARE.md.

SSM datasheets (2026-10-21): the 2050's time law and shape, and the 2030's reset time, pulse level, PWM range and 1 V/oct converter, are now in the engine (rows above).

**SPICE checks** (`circuits/`, `tools/spice_check.py`, ngspice 42; offline): 9 checks, all pass.
- **2050 attack:** 1.4663 tau, the same as the engine.
- **Noise pole:** 159.2 Hz.
- **Osc B summer:** key gain 1.000, fine gain 0.0500. The unlabelled inputs are 0.243 / 0.549 / 0.0452, and the output smoothing is 1.0 ms.
- **Glide:** 24 / 12 semitone time ratio 1.95, against the engine's 1.94.
- **2030 converter:** 0.9975 oct/V, drooping 17.8 cents over the 9-10 V octave from bulk resistance. The HI V/OCT trim corrects that; the engine models only the reset error left over.

Still to derive: the sources of the 412k / 182k / 2.21M summer inputs (needs a clearer scan or wire tracing), the anchors of the envelope and LFO ranges (a recording),
the filter's cutoff span, and the pot mux order on the later board 3.

## Results (2026-10-21, x86, offline)
- **Pitch:** a 440 Hz note plays 439.85 Hz in Rev 2 (-0.6 cents) and 439.93 Hz in Rev 4 (-0.3 cents).
- **High-note error:** 8.4 kHz is 1.8 cents flat in Rev 2 and 0.2 cents flat in Rev 4 (after the datasheet's 500 ns reset).
- **Self-oscillation:** at cutoff 6 on the lowest key with KBD on, it rings at 432 Hz (Rev 2) and 429 Hz (Rev 4). The manual says 400-500 Hz.
- **Bass under resonance:** a low note at resonance 7 peaks at 0.147 in Rev 2 and 0.299 in Rev 4.
- **All programs:** the 38 programs x 2 revisions x 2 qualities (152 renders, chord + release) have no NaN and no filter resets, with a max peak of 0.961 (the limiter's ceiling is 1.0).
  4 renders are quiet: 5-5 Helicopter in both revisions and qualities. Its amp attack of 8 now takes about 14 s under the datasheet law, so the 1.5 s test chord barely opens, and its sheet has every mixer level at 0.5 of 10. To check against a recording.
- **Glide:** 12 semitones take 411 ms and 24 take 796 ms at glide 8 (ratio 1.94, a constant rate).
- **CPU:** x86 bench (`tools/bench.c`, five voices held): 2.3-2.7 % of real time in Eco and 4.8-4.9 % in High. Device bench (Force, 2026-10-21, `tools/bench.sh -v 1,3,5`, `vst/bench.txt`): 1 voice 6.8 % mean, 3 voices 17.5 %, 5 voices 28.3 % (p99 29.4 %), Q-Link sweep p99 31.4 %, worst block 37.3 %: WARN (docs/BENCH.md). The device is about six times the x86 figure; the per-voice filter and the exponential converters are the place to look.

## Build and test
```
gcc -O2 -Wall -Wextra -Werror -std=c11 -fsanitize=address,undefined test/test_engine.c src/engine.c -lm && ./a.out
gcc -O2 -std=c11 tools/render_wav.c src/engine.c -lm -o render_wav && ./render_wav 0 2 brass-rev2.wav
gcc -O2 -std=c11 tools/bench.c src/engine.c -lm -o bench && ./bench
```

## Not done yet
VST wrapper and skin, MIDI CC map, the pot index map (for the cassette importer), and fitting the assumed laws to recordings.
