# Architecture (draft)

This draft was first written on 2026-10-20 from the ROMs alone, then checked the same day against the Rev 1 service manual schematics and the factory patch sheets (offline).
`docs/FIRMWARE.md` has the evidence for each point.

## What changed after the schematic review
| First draft | After the manual |
|---|---|
| Rev 3 OS, CEM 3320 filter, so the existing `ma_ota_lpf` was enough | v7.0 on three 2708s is Rev 1/2-era firmware. The voice is SSM: 2030 VCO, **2040 VCF**, 2050 EG, 2020 VCA |
| SSM option as a transistor ladder (`ma_ladder_lpf`) | The SSM 2040 is a **four-stage OTA cascade**, with resonance through a 2020 VCA and no bass compensation. Replaced with `ma_ssm2040_lpf` |
| 128-step pots | **1/12 V S&H steps**: 121 steps per pot, 61 semitone steps per frequency knob (the 5-octave range is trimmed in) |
| Osc B triangle as a band-limited triangle | It is **derived from the saw** by an op-amp shaper, with a small peak glitch (`ma_osc` triangle is close; the glitch is optional) |
| CEM 3310 envelope | **SSM 2050** with a per-voice RC trim, so voice-to-voice time spread is real (`ma_tol`) |

## Voice
```
OscA SSM2030: saw + pulse (summed switches), hard sync to B   <- Poly-Mod (2020 VCAs: FiltEnv*amt + OscB*amt) -> FreqA / PWA / Filter
OscB SSM2030: saw + tri(from saw) + pulse, LO FREQ, KBD on/off <- Wheel-Mod: LFO<->noise mix * wheel -> FreqA/FreqB/PWA/PWB/Filter
mixer: 2020 VCA per source (A, B, white noise) -> SSM2040 4-pole OTA LP (2020 resonance VCA) -> 2020 VCA (amp EG) -> voice volume trim
EGs: SSM2050 ADSR x2; filter EG via 2020 amount VCA to cutoff; KBD track switch (full)
controls: every pot through ma_cv (121 / 61 steps + S&H RC, or smooth); autotune residual bias per osc per range; drift
```
High quality runs at 2x (Profit-08's Eco/High), because sync and Poly-Mod FM alias at 1x.

## Kernels
| Need | Source |
|---|---|
| Band-limited saw/pulse/tri, hard sync | `mpc_analog.h`: `ma_osc`, `ma_sync` (reuse; morpho-PE's newer split `ma_osc_saw`/`ma_osc_tri` lets each shape be computed alone) |
| SSM 2040 filter | **new** `ma_ssm2040_lpf`, in the same style as `ma_ota_lpf` |
| SSM 2050 ADSR | **new** `ma_env_*` |
| 1/12 V CV steps + S&H RC | **new** `ma_cv_*` |
| Drift, per-voice tolerance | **new** `ma_drift_*`, `ma_tol` |
| Self-oscillation seed, glide | **new** `ma_selfosc_seed`, `ma_glide` |
| Decimator, `ma_tanh`, `ma_ota_G` | reuse |
| Wrapper, vst.json, skin, banks page, release, test_port | Profit-08 / mpc-vst-plugins (reuse) |

The new kernels live in `analog/mpc_analog_p5.h` until they move into morpho-PE's master `analog/mpc_analog.h` (then `sync.sh`).
`test/test_kernels.c` checks four things. The attack reaches 1 in 10.02 ms for a 10 ms setting. The SSM 2040 self-oscillates at 991 Hz for a 1 kHz setting (0.16 semitone flat)
and stays bounded. Bass at k = 3 is 0.25 (= 1/(1+k)). Drift std is 0.81.

## Programs
- The native layout is 24 x 7-bit pots + 24 switches, 40 programs (5 x 8), which becomes the VST parameter order. Wheel amounts, master tune, volume and glide aren't stored.
- **Factory sheets:** 38 programs transcribed by hand from the 1978 patch sheets into `tools/factory_sheets.txt`. `tools/gen_presets.py` generates
  `src/presets_factory.h`, and `test/test_presets.c` checks it. See `docs/PRESETS.md`.
- Imports from the user's own files at run time: later `.syx` dumps, Repro-5 `.h2p`, or a cassette WAV decoder following the firmware routines.

## Next
1. Pot/switch index map from a later board 3 schematic, or by tracing the code.
2. Fits for the envelope time law, cutoff scaling and LFO range, from a recording of a real unit or the user's measurements.
3. ~~Engine~~: started 2026-10-20, see `docs/ENGINE.md` (Rev 2 / Rev 4 switch, all 38 programs render clean offline).
4. Skin (3 tabs), host test, bench, release.

## Other circuit-modelling projects (2026-10-21)
[idlefoundry/ca-72](https://github.com/idlefoundry/ca-72) (a Minimoog model) and its [plugin-kit](https://github.com/idlefoundry/plugin-kit) are **GPL-3.0**.
None of their code can go into this MIT repo or `mpc_analog.h` (which is written without GPL code). They are useful as method:
- Transcribe each board into a SPICE netlist, simulate it in ngspice, and test the real-time model against the simulation (pitch, resonance, envelope shapes).
- Measure renders the same way every time (loudness, peaks, pitch tracks, A/B comparison).

For this instrument the method applies to the **discrete** parts: the master summers, exponential converters, glide OTA, noise filter and wheel-mod network.
The SSM chips have no public transistor-level models, so the VCO, filter and envelope cores stay behavioural, fitted to datasheet curves and recordings.
