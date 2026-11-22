# Improvements

The one tracker of what is known to be wrong, in the shape Lemus uses (`CONTRIBUTING.md`, "Rounds"). Dated; names no one. The method
and its limits are in `docs/ROADMAP-accuracy.md`; the circuit as read is in `docs/CIRCUIT.md`; sources in `docs/SOURCES.md`.

## Phase 0 (2026-11-22, waiting for the owner's OK)
- **Target unit:** Rev 2, S/N 184-1299, voice board SD411 rev 2.0 (TM1000C). The "Rev 4" voicing stays a switch, modelled later from
  TM1000D (Rev 3 filter) and the Rev 4 trace.
- **Tier:** circuit public (the TM1000C schematics); custom chips by datasheet (2020 and 2040 in hand, 2030 and 2050 not); reference: none
  yet, so the model is checked against the schematic, ngspice and the service manual's specifications only.
- **Free reference recordings found:** none.
- **Model version:** 2.0.0 is released, so a round that changes the sound must keep the old one for old projects (a model version saved
  in the plugin state). Not built yet: the first item of the first round.

## Check set (to build before the first round)
| Check | State |
|---|---|
| `tools/spice_check.py` (every netlist against ngspice; `tools/docker/Dockerfile`) | 12 checks, 0 failed, 5 open (2026-11-22) |
| `test/test_engine.c` (152 program renders, glide, pitch) | passes (2026-11-22) |
| Golden renders of every program (both revisions, both qualities) and a max-difference check in dB | missing |
| Corners (every control at its extremes: finite, bounded, no filter reset) | missing (`diag_nan` counts resets) |
| `tools/service_check.py` (the manual's Section 4 checks as pass/fail) | missing |
| CPU: `tools/bench.c` (x86) and `tools/bench.sh` on the device; budget: Force, 5 voices, mean under 30 % (28.3 % on 2026-10-21) | x86 exists; device as before |

## Known differences (circuit against engine), in proposed order
1. **Osc B LO FREQ:** the circuit shifts 5 octaves (`circuits/summer.cir`, 5.000 V); the engine 7. Small, certain.
2. **Cutoff span:** about 10 octaves over the pot (FILT SUM CV 0-10 V, ~1 oct/V); the engine 10.5. Keep the 450 Hz at 6 anchor.
3. **Mixer level and filter drive:** one oscillator at 10 puts 0.27 V on each 2040 stage against its +-1 V output swing (`circuits/mixbus.cir`);
   the engine's 0.45 x level into a +-1.4 tanh was set by ear. Needs the filter as a circuit (item 4) to mean anything.
4. **The filter as a circuit:** four 2040 cells (a tanh input pair, gm 1/5k at CONT 0 and -18 mV/oct per the datasheet, +-1 V output limits),
   the 10k/10k/200 network, the x20 output stage, and the resonance loop through a 2020 (I_c = (CV - 0.65) / 30k) AC coupled at 15.9 Hz.
   Replaces `ma_ssm2040_lpf` and "k = 4.4 x pot". Implicit solve (the loop gain is above 1 near self-oscillation). CPU cost to be stated.
5. **Osc B fine:** 0.01 x 10 V = 1.2 semitones; the engine 1 (a user report). Within the report's "about a semitone".
6. **Modulation depths** (W-MOD x0.549 to freq, x7.52 / x6.67 to PW, x7.52 to cutoff; Poly-Mod x0.31 / x3.32 / x1.95): wait on the W-MOD
   source level and the P-MOD VCAs' control wiring (`docs/CIRCUIT.md`, Open).
7. **Envelope time span:** the time CVs are x0.833 (100k / 500k), so 4.44 decades if the 2 V/decade law holds, against the engine's 5.33.
   Waits on the 2050 datasheet for the law and the anchor (C = 0.1 uF).

## Open questions
- The V/oct trims can't reach 1.000 oct/V at 25 C: VCO 1.02-1.08 (54.9k over 1k + 121, summer x0.909-0.959), filter 1.014-1.113
  (`circuits/filtcv.cir`). The filter's is consistent with a warm instrument; the VCO's has a tempco resistor and isn't.

## Done
(none yet)

## Tried and dropped
(none yet)

## Declined
(none yet)
