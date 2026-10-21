# SPICE checks

These are ngspice netlists of the instrument's discrete circuits. Values are transcribed from the Rev 1 service manual (board 3 page 3, board 4) and the
SSM 2030 / SSM 2050 datasheets. Each one checks a constant in `src/engine.c`.
Run `python3 tools/spice_check.py` (it needs ngspice 42 or later on PATH; `apt-get install ngspice`).

| Netlist | What | Engine constant |
|---|---|---|
| `env2050.cir` | 2050 attack: approach to +13 V ended at +10 V | `ATK` = 1.4663 tau |
| `pink.cir` | wheel-mod noise filter, 100k / 0.01 uF | `pink_c`, 159 Hz |
| `summer.cir` | osc B master summer, input gains and 0.01 uF smoothing | `b_fine` (2M = 0.05), the 1:1 inputs |
| `glide.cir` | CA3080 slew limiter on 0.1 uF (behavioural OTA) | constant-rate glide |
| `expconv.cir` | 2030 exponential converter (54.9k / 1k, matched pair) | 1 V/oct |

The SSM 2030, 2040, 2050 and 2020 have no public transistor-level models, so the chips are behavioural here and in the engine.
Only the circuits around them are simulated as drawn.
