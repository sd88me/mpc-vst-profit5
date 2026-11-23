# SPICE checks

These are ngspice netlists of the instrument's discrete circuits. Values are transcribed from the Rev 1/2 technical manual (TM1000C: SD312, SD411; `docs/SOURCES.md`) and the
SSM 2030 / SSM 2050 datasheets. Each one checks a constant in `src/engine.c`.
Run `python3 tools/spice_check.py` (it needs ngspice 42 or later on PATH), or without installing anything:
`docker build -t p5tools tools/docker && docker run --rm -u $(id -u):$(id -g) -v $PWD:/w -w /w p5tools python3 tools/spice_check.py`.
The part values and their pages are in `docs/CIRCUIT.md`; OPEN checks are known differences listed in `docs/improvements.md`.

| Netlist | What | Engine constant |
|---|---|---|
| `env2050.cir` | 2050 attack: approach to +13 V ended at +10 V | `ATK` = 1.4663 tau |
| `pink.cir` | wheel-mod noise filter, 100k / 0.01 uF | `pink_c`, 159 Hz |
| `summer.cir` | osc B master summer (SD312): glide 100k, fine 10M, W-MOD 182k, the LO divider, 0.01 uF smoothing | the 1:1 inputs; fine, W-MOD and LO are open |
| `glide.cir` | CA3080 slew limiter on 0.1 uF (behavioural OTA) | constant-rate glide |
| `filtcv.cir` | voice cutoff CV summer and the 54.9k / 1k divider to the 2040's CONT | cutoff oct/V and span (open) |
| `mixbus.cir` | mixer bus: a 2020 (datasheet law) into R460 / R476 and stage 1 | mixer level into the filter (open) |
| `expconv.cir` | 2030 exponential converter (54.9k / 1k, matched pair) | 1 V/oct |

The SSM 2030, 2040, 2050 and 2020 have no public transistor-level models, so the chips are behavioural here and in the engine.
Only the circuits around them are simulated as drawn.
