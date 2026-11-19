# Roadmap: an accuracy pass in the Lemus method (for later)

Written 2026-11-19, not started. Most of the engine's laws are still marked **assumed** in `docs/ENGINE.md`. This plan is for
turning as many as possible into values derived from the circuit without a recording of a real unit. What only a recording can
settle gets listed as "waits on data" rather than guessed.

The method is from [Lemus](https://github.com/lemus-dev/lemus) (`CONTRIBUTING.md`, and e.g. `machines/sh101/docs/improvements.md`):
schematic-first circuit models, a check set run before and after every round, per-event measurements in absolute units, and one
public tracker of what is wrong. **Method only, no code:** Lemus is AGPL-3.0 and Profit-5 is MIT, so nothing is copied from it.

## Needs
- The Rev 1 service manual, the schematic scans and the SSM 2030/2040/2050/2020 datasheets, locally (they stay out of the repo by
  the rules in `CLAUDE.md`). Items 1, 2 and 4 need them.

## Items, in order of value
1. **The filter and its resonance loop as a circuit.** Replace `ma_ssm2040_lpf` and "resonance k = 4.4 x pot (assumed)" with the
   SSM 2040's four OTA stages and the board's resonance network, solved from the schematic's part values and the datasheet. The
   resonance law, the bass loss under resonance, the knee and the self-oscillation point then follow from the parts. Check each
   block against ngspice in `tools/spice_check.py`, with limits.
2. **The service manual as a test suite.** Every adjustment and specification in it becomes an automated pass/fail check against
   the engine (`tools/service_check.py`). Today only one figure is used (self-oscillation at 400-500 Hz at cutoff 6).
3. **The factory patch sheets as reference data.** They were set by ear on real units, so each of the 38 programs should do what
   its name says: audible, the right speed, the right movement. 5-5 Helicopter being nearly silent (amp attack 8 = about 14 s under
   the datasheet law) is evidence about the envelope's time anchor. A per-program check (level, onset time, LFO rate, sweep range)
   constrains the envelope, LFO and modulation ranges.
4. **The unread summer inputs** (412k, 182k, 2.21M on the master summers): a clearer scan or wire tracing. They set the filter and
   PW modulation depths (also `docs/HANDOFF.md` section 3, item 2).
5. **"Vintage" from part tolerances.** A Monte Carlo over the parts' tolerances (Lemus's `spread.md`) gives the real unit-to-unit
   spread of tuning, cutoff and envelope times, in place of the hand-picked 8 % / 3 %.
6. **The check set and tracker:**
   - golden renders of every program (both revisions, both qualities), so each change is measured, and CPU rounds bit-exact or
     with the sound difference stated;
   - a corners test (every control at its extremes: no NaN, no filter reset, no unconverged solve);
   - `docs/improvements.md`: known inaccuracies, plan, tried and dropped, declined, done, dated.

## Later, with recordings
Fit what is left (envelope and LFO anchors, cutoff span, glide range, unison detune) to free recordings of real units, measured
per note against each unit (`docs/HANDOFF.md` section 3, item 1).
