# Profit-5: agent guide

MPC OS VST2 instrument modelled on the early SSM-chip Prophet-5 (five voices). **Resuming? Read `docs/HANDOFF.md` first.** Then `docs/ARCHITECTURE.md` and `docs/FIRMWARE.md`, then mpc-vst-plugins'
`CLAUDE.md`, `docs/NOTES.md` and `docs/PORTING.md`. Sibling ports: `../mpc-vst-profit8` (wrapper, skin, banks page) and `../mpc-vst-morpho-PE` (master `analog/mpc_analog.h`).

Ground rules:
- Never commit ROM images, disassembly listings, the service manual, patch-sheet scans, `.syx` banks or recordings (`.gitignore` covers the usual ones).
- No "Prophet", "Sequential" or "Dave Smith" in the product name, plugin id or skin art; the README may say what it is modelled on, with the disclaimer.
- `src/presets_factory.h` is generated: edit `tools/factory_sheets.txt` and run `tools/gen_presets.py`.
- `analog/mpc_analog.h` is a synced copy of morpho-PE's (`../mpc-vst-morpho-PE/analog/sync.sh --check analog`). New kernels go in `analog/mpc_analog_p5.h` until they are merged there.
- Date new facts and say offline vs device. Every release must be catalog-conformant (`release.py --repo sd88me/mpc-vst-profit5 --license MIT`, `catalog_check.py --catalog` OK).
