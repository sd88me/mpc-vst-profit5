# Profit-5

This is a planned five-voice MPC OS VST2 instrument, modelled on an early (SSM-chip) Prophet-5. It's a sibling of [Profit-08](https://github.com/sd88me/mpc-vst-profit8).

**Status: engine started, offline only.** The engine (`src/engine.c`, Rev 2 / Rev 4 voicing switch) renders all 38 factory programs; there's no VST wrapper or skin yet. The repo has these parts so far:
- the firmware and schematic findings (`docs/FIRMWARE.md`)
- the architecture (`docs/ARCHITECTURE.md`)
- draft analog kernels for `mpc_analog.h` (`analog/mpc_analog_p5.h`)
- the factory patch sheets transcribed into a generated preset table (`docs/PRESETS.md`)
- the engine and its laws (`docs/ENGINE.md`)
- tests for the kernels, the preset table and the engine

```
gcc -O2 -Wall -Wextra -Werror -fsanitize=address,undefined test/test_kernels.c -lm && ./a.out
python3 tools/gen_presets.py && gcc -Wall -Wextra -Werror -std=c11 test/test_presets.c && ./a.out
gcc -O2 -Wall -Wextra -Werror -std=c11 -fsanitize=address,undefined test/test_engine.c src/engine.c -lm && ./a.out
```

*Profit-5 is an independent project, not affiliated with or endorsed by Sequential or Dave Smith Instruments. No firmware, ROM image or
manual is included; the findings are written up as layouts and formulas.*
