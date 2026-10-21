# Engine (`src/engine.c`)

Started 2026-10-20, offline only (x86 tests under ASan/UBSan; no device yet). Each law below is marked with its source: **manual** (Rev 1 service manual),
**firmware** (v7.0 disassembly), **sheets** (factory patch sheets) or **assumed** (a placeholder until measured on a real unit or a recording).

## Rev 2 / Rev 4 switch (`P5P_REV`)
One switch changes everything that differs between the two voicings. Rev 2 is modelled from the schematics. Rev 4 is modelled from the public
description of the reissue, not from a unit or its schematics.

| | Rev 2 (SSM) | Rev 4 (CEM style) |
|---|---|---|
| VCO | SSM 2030: 3 us reset, 20 % left after the HI V/OCT trim (an 8.4 kHz note is 7 cents flat), extra saw bow, osc B triangle with a per-voice symmetry error and the peak glitch (manual 3.4) | 1 us, 10 % (1 cent flat), clean saw and triangle |
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
| Osc B LO FREQ | 7 octaves down | assumed |
| Pulse width | duty = pot; the pulse goes silent at 0 and 1 | assumed (as Profit-08) |
| Cutoff | 450 Hz at pot 6 on the lowest key with KBD on, 10.5 octaves over the pot; KBD = 1 V/oct | manual 3.4 (one point); span assumed |
| Resonance | k = 4.4 x pot; self-oscillation from about pot 0.91, seeded with noise and a key click above k = 3.6 | assumed; the sheets' 5-6 needs it |
| Filter env amount | 0..8 octaves | assumed |
| Env times | 1.5 ms .. 12 s, exponential; attack aims at 1.3 and switches to decay at 1 | assumed |
| RELEASE switch off | release 5 ms | assumed |
| LFO | 0.1..25.6 Hz; saw / tri / square summed | assumed |
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

Still to derive: boards 1/2 (knob tapers and the pot mux order), the LFO and envelope time constants (they need SSM 2030 / 2050 datasheet constants),
and the remaining summer inputs (a clearer scan, or tracing each wire from its connector number).

## Results (2026-10-21, x86, offline)
- **Pitch:** a 440 Hz note plays 439.85 Hz in Rev 2 (-0.6 cents) and 439.93 Hz in Rev 4 (-0.3 cents).
- **High-note error:** 8.4 kHz is 7.3 cents flat in Rev 2 and 1.2 cents flat in Rev 4.
- **Self-oscillation:** at cutoff 6 on the lowest key with KBD on, it rings at 432 Hz (Rev 2) and 429 Hz (Rev 4). The manual says 400-500 Hz.
- **Bass under resonance:** a low note at resonance 7 peaks at 0.147 in Rev 2 and 0.299 in Rev 4.
- **All programs:** the 38 programs x 2 revisions x 2 qualities (152 renders, chord + release) have no NaN and no filter resets, with a max peak of 0.930 (the limiter's ceiling is 1.0).
  2 renders are quiet: 5-5 Helicopter in Eco and High, both Rev 2, at peak 0.0147. Its sheet has every mixer level at 0.5 of 10, so it probably relies on a real unit's mixer
  feed-through; to check against a recording.
- **Glide:** 12 semitones take 411 ms and 24 take 796 ms at glide 8 (ratio 1.94, a constant rate).
- **CPU:** x86 bench (`tools/bench.c`, five voices held): 2.3-2.7 % of real time in Eco and 4.8-4.9 % in High. The device bench is still to do (docs/BENCH.md).

## Build and test
```
gcc -O2 -Wall -Wextra -Werror -std=c11 -fsanitize=address,undefined test/test_engine.c src/engine.c -lm && ./a.out
gcc -O2 -std=c11 tools/render_wav.c src/engine.c -lm -o render_wav && ./render_wav 0 2 brass-rev2.wav
gcc -O2 -std=c11 tools/bench.c src/engine.c -lm -o bench && ./bench
```

## Not done yet
VST wrapper and skin, MIDI CC map, the pot index map (for the cassette importer), and fitting the assumed laws to recordings.
