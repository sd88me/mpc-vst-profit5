# Handoff (2026-10-21)

State at the end of the cloud session, and what to do next. Read `CLAUDE.md`, then this, then `docs/ENGINE.md`.

## Where everything is
- **Repo:** `sd88me/mpc-vst-profit5`, branch `claude/optimistic-pascal-xbed8c` (the only branch; no PR opened). Check out `../mpc-vst-plugins` and
  `../mpc-vst-morpho-PE` next to it (tools and the master `analog/mpc_analog.h`).
- **Test build:** the draft release `profit-5-vst-v0.1.0` (Gen1 `-mpc-armv7.zip` and Gen2 `-mpc-aarch64.zip`, plus skin previews), built by the
  "Release (draft)" workflow from commit `46dd3c2` with mpc-vst-plugins `c121b12`. It's unpublished. **Never installed or played on a device.**
- **Offline results:** engine, preset, kernel and SPICE tests pass; the wrapper's host test (`tools/test_port.sh vst/vst.json`, ASan) passes; the skin builds
  with no warnings; x86 CPU is about 5 % of real time at five voices.
- **Not in the repo (by rule):** the ROM images, the service manual, the patch-sheet scans, the SSM datasheets and the photos. They were uploads to the
  cloud session. Anything that needs them (decoding more firmware, re-reading a schematic) needs them supplied again in the local session.

## 1. Install the test build and check it (you, on the device)
The device is shared with your live setup. Save your project first; the installer stops and restarts MPC.
1. Download the Gen1 zip from the draft release (or the workflow run's artifact). Gen1 = Force, MPC Live / One / X / Key. Gen2 is untested.
2. `scp -r Profit-5-0.1.0 root@<device-ip>:/tmp/` then `ssh root@<device-ip> sh /tmp/Profit-5-0.1.0/install.sh` (backs up `MPC.settings` first).
3. Add **Profit-5** on an instrument track. Check, and note what fails:
   - **Loads and sounds:** the first program (1-1 Brass) plays; all five voices; no clicks, no stuck notes.
   - **Programs:** PRESET menu lists 38 names; the Program knob changes program; the display text follows.
   - **Skin:** all three tabs show; knobs turn smoothly (128 steps); switches light; no overlapping touch areas; the legends are readable on the screen.
   - **Q-Links:** 7 pages, in order, each moving the right control; Q-Links also work on the track-edit automation screen (that is what PR #486 fixes, and this build lacks it).
   - **Rev switch:** Rev 2 vs Rev 4 are audibly different (Rev 4: more bass at resonance, smooth knobs); velocity and aftertouch do something only in Rev 4.
   - **Save and reload a project:** values and program come back. Unload and reload the plugin.
   - **MIDI:** pitch bend (two semitones), mod wheel into Wheel-Mod, sustain pedal, program change.
4. **CPU:** `tools/bench.sh build/profit_5.so <ip> -j | tee vst/bench.txt` in mpc-vst-plugins (`docs/BENCH.md`). Commit `vst/bench.txt` (the release workflow
   uses it). Try Eco and High, and Rev 2 and Rev 4.
5. Record the result in `docs/NOTES.md` of mpc-vst-plugins and in `docs/ENGINE.md` here, dated, "device".
6. If it's good, publish the draft; otherwise fix, bump the version and re-run the workflow (below).

## 2. Likely first problems, and where to look
- **Quiet or too loud:** output level is `master` in `p5_render` (`src/engine.c`); the soft limiter ceilings at 1.0. Profit-08's notes (`docs/STATUS.md`) have the level-matching method.
- **5-5 Helicopter silent:** its amp attack of 8 is about 14 s under the 2050 datasheet law. A recording, or a faster envelope anchor (`env_tau`, 0.5 ms at pot 0), settles it.
- **Skin text too small on the device:** raise font sizes and spacing in `skin/make_skin.py`; `scale_names` and the 1.52 font factor are in mpc-vst-plugins `docs/SKIN_STUDIO.md`.
- **Wrong param touched by Q-Link or automation:** `vst/params.lock` fixes the order (engine enum order, then `preset`). Append new params only.
- **Touch boxes:** `gen_vst.py` prints "TOUCH ... overlap"; the knob box size is `bw` in `make_skin.py`.

## 3. Open engine work (in order of value)
1. **Fit the guessed laws to a recording of a real unit** (listed in `docs/ENGINE.md`): envelope and LFO anchors, cutoff span and keyboard scaling, resonance law,
   Poly-Mod and Wheel-Mod depths, mixer level, glide range, unison detune.
2. **The three unread summer inputs** (412k, 182k, 2.21M on the master summers, `docs/ENGINE.md`): need a clearer scan or wire tracing. They set the filter and PW modulation depths.
3. **The knob-order map** of the v7.0 firmware (only the two frequency pots are certain) and then a cassette-dump and Rev 3.3/Rev 4 `.syx` importer.
4. **More SPICE checks:** the VCO's exponential converter with real part tolerances, the filter's control summer, the VCA balance. `tools/spice_check.py` is the place.
5. **Rev 4 is from public descriptions only.** A Rev 4 unit or its schematic would replace the assumptions in `REV[1]` and the Rev 4 column of `docs/ENGINE.md`.
6. **Missing:** the second output, a Program-button strip that does something (the eight buttons are printed art), and 5-7 and 5-8 (not on the sheets).

## 4. Pending in mpc-vst-plugins
- **PRs #486-#489** (wrapper: no UpdateDisplay on a host write, a 50 ms UpdateDisplay limit, `params.lock`, wrapper-limits docs) are open. Profit-5 was built and host-tested against all four,
  but the release workflow is pinned to `main` `c121b12`, which lacks them. After they merge: bump the sha in `.github/workflows/release.yml` (both places), re-run.
- **`panel-skin` skill:** pushed to mpc-vst-plugins branch `claude/optimistic-pascal-xbed8c` (one commit on `main`), no PR yet. Open one when ready; its docs change (`docs/COMMUNITY_SKINS.md`, `CLAUDE.md`) is in the same commit.
- **Catalog:** `release.py` ran with `--repo sd88me/mpc-vst-profit5 --license MIT --id profit-5` and `catalog_check.py --catalog` passed in CI. Adding the catalog entry itself is a separate PR to mpc-vst-plugins.

## 5. Commands
```
python3 tools/gen_presets.py && python3 tools/gen_params.py                 # after editing tools/factory_sheets.txt or the param list
python3 skin/make_skin.py vst                                              # Rev 2 look (default); P5_STYLE=rev4 for the other
gcc -O2 -Wall -Wextra -Werror -std=c11 -fsanitize=address,undefined test/test_engine.c src/engine.c -lm && ./a.out
python3 tools/spice_check.py                                               # needs ngspice
../mpc-vst-plugins/tools/test_port.sh vst/vst.json
vst/build.sh                                                               # device build; needs Docker (the cloud session had none, so CI built it)
```
Re-run the CI build: Actions, "Release (draft)", input `version` (a published tag can't be reused; a draft's tag can).

## 6. Things that will trip you
- `engine.h` in this repo is `src/p5_engine.h`: the wrapper has its own `engine.h` on the include path.
- The skin needs GNU FreeSans (`fonts-freefont-ttf`) and Pillow; the Playwright 1.56 renderer is needed by `gen_vst.py`.
- `analog/mpc_analog.h` is a synced copy of morpho-PE's; `analog/mpc_analog_p5.h` holds our new kernels until they move there.
- Don't commit ROMs, the manual, scans, photos or `.syx` banks. The README's disclaimer depends on it.
- The product name, plugin id and skin art must not say "Prophet", "Sequential" or "Dave Smith". The README may say what it's modelled on.
