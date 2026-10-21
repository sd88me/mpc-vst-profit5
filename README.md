# Profit-5

A five-voice analog polysynth for Akai MPC OS standalone devices (Force, MPC Live / One / X / Key), built as a native VST2 instrument
with its own screen skin and Q-Link pages. It's modelled on an early SSM-chip Prophet-5: two oscillators per voice with hard sync,
a four-pole resonant lowpass, Poly-Mod, a wheel-mod section with LFO and noise, glide and unison, and the 38 factory programs from the
1978 patch sheets. One switch changes the whole voicing between **Rev 2** (SSM chips, stepped controls) and **Rev 4** (CEM-style,
smooth controls, velocity and aftertouch).

![The Oscillators tab, Rev 2 look](docs/img/skin-rev2-osc.png)

**Status: version 0.1.0, a test build.** It builds, passes the offline host test and its own engine, preset and circuit tests, and its
CPU use on x86 is about 5 % of real time at five voices. It has **not** been installed on a device yet, and many of the knob response
laws are still measured guesses (see "What is measured, and what isn't"). Expect to hear the character, not a calibrated copy.

*Profit-5 is an independent project, not affiliated with or endorsed by Sequential or Dave Smith Instruments. It contains no firmware,
ROM image, manual, schematic, patch-sheet scan or photograph: everything in it is code and generated art. The patch names are the factory
program names as published in the patch-sheet booklet; their knob settings were read by hand.*

## Install

1. Download the zip for your device from the [releases](https://github.com/sd88me/mpc-vst-profit5/releases): `-mpc-armv7.zip` for Gen1
   (MPC Live / X / One, Force) and `-mpc-aarch64.zip` for Gen2.
2. Unzip, and run `install.sh` (or copy the plugin folder into a `Synths` folder by hand; `INSTALL.md` in the zip has both).
3. Restart MPC, add an instrument track, and choose **Profit-5**. MPC OS 3.x only (the skin format).

## What you get

- **Five voices**, each with two oscillators, a mixer, a four-pole lowpass and an amplifier. Poly (1 to 5 voices, round-robin with oldest-note
  stealing) or **Unison** (every voice on the newest key; releasing it falls back to the key still held).
- **Oscillator A:** sawtooth and pulse, mixed; pulse width; **hard sync** to oscillator B, band-limited.
- **Oscillator B:** sawtooth, triangle and pulse, mixed; pulse width; fine tune; **LO FREQ** (an LFO-rate mode) and a KEYBOARD switch.
- **Poly-Mod:** filter envelope and oscillator B, to oscillator A's frequency, its pulse width, and the filter cutoff.
- **Wheel-Mod:** LFO (saw / triangle / square, mixed) and noise, crossfaded by SOURCE MIX, to either oscillator's frequency and pulse width and to the filter.
- **Filter and amplifier:** cutoff, resonance, envelope amount, keyboard tracking, and a four-stage ADSR for each.
- **Glide** and **master tune**, pitch bend, mod wheel, sustain pedal and program change.
- **38 factory programs** (banks 1-4 and 5-1 to 5-6 of the original set), in MPC's PRESET menu and on the Program knob.
- **Three screen tabs** with real-panel layout, printed 0-10 scales and 128-step knob pictures, and seven Q-Link pages.

### Rev 2 and Rev 4

One switch, `REV`, changes everything that differs between the two voicings:

| | **Rev 2** (SSM chips) | **Rev 4** (CEM style) |
|---|---|---|
| Oscillators | SSM 2030 behaviour: reset time flattens the top end, a slightly bowed saw, osc B triangle with a per-voice symmetry error and the small peak glitch, pulse at 0.75 of the saw | clean saw and triangle, smaller tuning error, full-level pulse |
| Filter | SSM 2040: bass drops as resonance rises, harder knee | CEM-style four-pole: part of the bass given back, softer knee |
| Envelopes | SSM 2050 times, 8 % per-voice timing spread | faster, 3 % spread |
| Knobs | **121 steps**, like the 7-bit control path of the instrument | smooth |
| Velocity, aftertouch | none (the original has neither) | velocity to filter and level; aftertouch to cutoff and LFO depth |

**Vintage** scales the drift and part-to-part spread in both. **Eco / High** picks 1x or 2x oversampling.
The skin has two looks, the 1978 panel's naming and light walnut (the default build) and the reissue's naming and darker walnut; they change
only the picture, not the sound.

## How it was designed

This isn't a guess at "a Prophet sound". Each part of the engine was checked against a source, and the source is named in
[`docs/ENGINE.md`](docs/ENGINE.md) next to the law it set.

**The firmware** (disassembled offline from the user's own ROM images; none is included here). The control processor's code told us how the
instrument is *controlled*, since all the sound is analog:
- every program is 24 bytes: 24 seven-bit knobs plus 24 switches, 40 programs in five banks of eight;
- each knob goes through a **7-bit converter into a sample-and-hold, so a knob has 121 steps over its travel**, which is why Rev 2's knobs step;
- the two oscillator frequency knobs are halved by the firmware, so they move in **semitone steps over five octaves**;
- **voice assignment** is round-robin starting after the last voice used, a free voice first, the oldest stolen, with a 4-voice mode and a unison mode;
- an **autotune** table holds a bias for each of ten oscillators in four pitch ranges: the source of the per-oscillator tuning residual (Vintage).

**The service manual and schematics** (Rev 1 board set, read page by page):
- the voice is **SSM 2030 oscillators, an SSM 2040 filter, SSM 2050 envelopes and SSM 2020 VCAs**, which decided the voicing of Rev 2;
- the filter is a **four-stage OTA cascade** with resonance fed back through a VCA and no bass compensation: it became `ma_ssm2040_lpf`;
- osc B's triangle is **derived from its saw** by an op-amp shaper, and osc A has none;
- the **glide** is a CA3080 slew limiter (a constant rate), the wheel-mod noise a one-pole lowpass at 159 Hz, the LFO a second 2030;
- the manual's tuning procedure gave the pitch references: frequency 0 puts the lowest key on C0, and cutoff 6 on the lowest key rings at 400-500 Hz;
- every panel pot is 100k linear, so every curve lives in the circuits after the knob.

**The datasheets** (SSM 2030 and SSM 2050): the 2050's **2 V per decade** time law and its attack that aims at +13 V and ends at +10 V,
the 2030's 500 ns reset, 7.5 Vpp pulse against a 10 Vpp saw, 10 %/V pulse-width sweep, and 1 V/octave converter.

**The patch sheets:** the 38 factory programs, transcribed by hand from the knob pointers (to about half a division) and cross-checked
against each sheet's printed "OSC A / B: up N octaves" note, with four readings corrected to the notes.

**SPICE:** the discrete circuits around the chips were transcribed into ngspice netlists (`circuits/`), and `tools/spice_check.py` checks the
engine's constants against them: 9 checks, all passing, for example the 2050 attack at 1.4663 time constants, the noise pole at 159.2 Hz, the
glide's 24- to 12-semitone time ratio of 1.95 and the converter at 0.9975 octave per volt. The SSM chips have no public transistor-level models, so the
chips themselves are behavioural, fitted to their datasheets.

## What is measured, and what isn't

Honest status, offline on x86 (2026-10-21):
- **Measured and tested:** a 440 Hz note plays within 0.6 cent; an 8.4 kHz note is 1.8 cents flat in Rev 2; self-oscillation at cutoff 6 rings at 432 Hz;
  bass at resonance 7 is half of Rev 4's in Rev 2; glide is a constant rate; all 38 programs render with no NaN and no clipping, in both revisions and
  both qualities; the voice allocator behaves as in the firmware.
- **Still guessed:** the start of the envelope and LFO ranges, the cutoff span, resonance law, Poly-Mod and Wheel-Mod depths, and a few summing-amp inputs the
  scan doesn't show. Rev 4 is modelled from public descriptions, not from a unit or its schematics.
- **Known quiet program:** 5-5 Helicopter barely sounds in a short test (its amplifier attack is about 14 s under the datasheet law). To check against a recording.
- **Not done:** device test and bench, a cassette-dump importer, a second output. A recording of a real unit would let me fit most of the guessed laws.

All of this is written up, with the evidence, in [`docs/ENGINE.md`](docs/ENGINE.md), [`docs/FIRMWARE.md`](docs/FIRMWARE.md) and
[`docs/PRESETS.md`](docs/PRESETS.md).

## Controls

| Tab | Contents |
|---|---|
| **Oscillators** | Poly-Mod (filter envelope, osc 2, three destinations), LFO (initial amount, frequency, three shapes), Wheel-Mod (source mix, five destinations), oscillator 1 and 2, glide, unison |
| **Filter Amp** | Mixer, filter (cutoff, resonance, envelope amount, keyboard tracking, half/full, ADSR), amplifier ADSR and release, vintage, master tune, volume, velocity and aftertouch depths |
| **Programmer** | Program knob and display, voices, unison, quality, rev |

MIDI: notes with velocity, polyphonic and channel aftertouch, pitch bend (two semitones), CC 1 mod wheel, CC 64 sustain, CC 120 / 123 all off, program change.

## Build from source

```
python3 tools/gen_presets.py && python3 tools/gen_params.py          # src/presets_factory.h, vst/params.json, src/plugin_keys.h
python3 skin/make_skin.py vst                                         # the skin (P5_STYLE=rev4 for the reissue look)
gcc -O2 -Wall -Wextra -Werror -std=c11 -fsanitize=address,undefined test/test_engine.c src/engine.c -lm && ./a.out
gcc -O2 -Wall -Wextra -Werror -fsanitize=address,undefined test/test_kernels.c -lm && ./a.out
python3 tools/spice_check.py                                          # needs ngspice
../mpc-vst-plugins/tools/test_port.sh vst/vst.json                    # the wrapper's offline host test
vst/build.sh                                                          # the device build (Docker), or run the "Release (draft)" workflow
```

Listen offline: `gcc -O2 -std=c11 tools/render_wav.c src/engine.c -lm -o render_wav && ./render_wav 0 2 brass.wav`.

## Credits and licence

MIT, see `LICENSE`. The analog kernels (`analog/mpc_analog.h`) are shared with the other mpc-vst-plugins ports and written from the published papers
(Valimaki and Huovilainen, Zavalishin); the SSM kernels in `analog/mpc_analog_p5.h` are new. The skin's lettering is GNU FreeSans. Built on
[mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins), whose wrapper, skin renderer and release tools do the rest. The Minimoog model
[CA-72](https://github.com/idlefoundry/ca-72) is GPL and none of its code is used here; its method of checking a real-time model against a SPICE simulation
inspired `circuits/`.
