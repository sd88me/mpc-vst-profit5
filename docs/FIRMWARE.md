# Firmware and service-manual findings

The analysis was done offline on 2026-10-20 from the user's own ROM images (`p5_v7.0c_0/1/2.bin`, `p5diags.bin`, never committed) and the
Rev 1 service manual (serials 0001-0182, board 4 dated 6/19/78). These are layouts and formulas, not ROM contents.

## ROM images
- `p5_v7.0c_0/1/2.bin`: three 1 KB images. Each 2 KB file holds its 1 KB twice, so they are 2708 parts read as 2716s. Z80 code at
  0x0000 / 0x0400 / 0x0800, and chip 2 uses 330 bytes.
- The service manual (4.0) names v3.0 and v5.0 firmware on **two** 2708s (board 3 ICs 12, 13). This image is v7.0, on **three**: a later
  Rev 1/2 firmware. Which instrument it came from is to be confirmed.
- `p5diags.bin`: a separate 2 KB diagnostics program (stack at 0x43FF, a different memory map). I didn't analyse it.

## Memory and I/O (from the disassembly)
| Address | Use |
|---|---|
| 0x1000-0x107F | Scratch RAM, cleared at reset. The edit buffer is 0x1000-0x1017, switch bytes 0x1038-0x103A, program number 0x103E, voice notes 0x1024-0x1028, voice slots 0x1057-0x105B, round-robin pointer 0x1061. |
| 0x1400-0x17BF | CMOS program memory: 40 programs x 24 bytes, at 0x1400 + 24 x program. |
| 0x17C0-0x17E7 | Autotune bias table: 10 oscillators x 4 pitch ranges, reset value 25, limit 100. |
| 0x0C00-0x0C03 | 8253 timer, for period counting in autotune and cassette timing. |
| ports | 0x00/0x01 switch and LED matrix. 0x02 ADC (bit 7 cassette in), 0x03 status. 0x10 pot mux. 0x20 DAC data, 0x33 S&H mux, 0x38 S&H strobe. 0x28/0x30/0x31 switch latches. 0x39 gates. 0x18 cassette out + misc. |

## Behaviour
- **Program:** 24 bytes. The low 7 bits of each byte are a pot; bit 7 of the bytes, taken 8 at a time, gives the 24 switches.
- **CV path:** pot → 7-bit ADC → RAM → the same value to the DAC → that parameter's S&H. Manual 3.3 says "all S&H's increment in .083 (1/12) volt
  steps". The ADC gain is trimmed so the oscillator frequency pot at 10 gives 5.000 V ("exactly five octave range"). The firmware halves the two
  frequency pots, so each pot has 121 steps and each frequency knob 61 semitone steps (0-60). Model: `ma_cv_*`.
- **Oscillator pitch:** key note + frequency pot (semitones) + the bias CV from the autotune table. The table range is picked at notes 57 / 69 / 82.
  Manual 3.4: the bias covers "a little more than a semitone", and when an oscillator can't be tuned its bias goes to mid-point.
  Manual 4.2: frequency 0 puts the lowest key at C0, so 3 octaves + 3 semitones above that is about 156 Hz. The third C is about 624 Hz.
- **Panel combinations** (manual 4.1) match the button code at 0x05AD/0x05C0. Program 1 + 7 resets the bias table (all biases to mid-point). Program 1 + 6 sets
  a unison mode that takes pitch from the five keyboard S&Hs.
- **Voice assignment:** 5 voices (a status input selects 4), round-robin starting after the last voice used. A free voice is preferred, otherwise the oldest is stolen.
  Unison stacks all voices on one note.
- **Cassette:** bit-banged on port 0x18 bit 2 / port 0x02 bit 7, with save, load and verify, and a byte sum over 0x1400-0x17BF.
- **No MIDI** in this firmware.

## Voice circuit (board 4 schematic)
- **VCO:** SSM 2030 (IC 63 for A, IC 59 for B). The sync, saw and pulse outputs are switched by 4016 gates. Oscillator B's triangle is made from its saw by an
  op-amp shaper (IC 57) with a symmetry trim, and has a small glitch at the peak (manual 3.4 says this is normal). Oscillator A has no triangle. PW comes from the panel CV,
  poly-mod and wheel-mod CVs, summed.
- **Mixer:** an SSM 2020 VCA per source (IC 73 for osc A).
- **Filter:** SSM 2040 (IC 78). That's four OTA stages fed through 10k/200 attenuators, with 0.001 uF capacitors and a 10k/10k feedback ladder. Resonance is an SSM 2020 VCA (IC 90)
  from the output back to the input summer. The output buffer gain is 200k/10k. The cutoff CV comes from a summer with V/oct and initial-frequency trims.
  The filter isn't temperature-compensated and "will never track as well as the oscillators" (manual 3.4). Model: `ma_ssm2040_lpf`.
- **Envelopes:** two SSM 2050 per voice (ICs 1-5 amp, 6-10 filter), with a 0.1 uF timing capacitor, ADSR CVs through matched 100k resistors, and an RC trim per voice
  ("touchy", wide range, so voices differ). Model: `ma_env_*` plus `ma_tol` for per-voice time spread.
- **Poly-Mod:** an SSM 2020 VCA each for the osc B amount and the filter env amount, mixed by a 741 and switched to Freq A, PW A and Filter.
- **Final VCA:** SSM 2020 driven by the amp envelope, then the voice volume trim (100k).

## Front panel (boards 1 and 2, Rev 1)
- **Board 1 (right panel):** 11 x 100k linear pots, each a divider from the reference to ground. Their wipers go to these connector pins: cutoff 8, resonance 7, env amount 4,
  filter A/D/S/R 20/17/13/14, amp A/D/S/R 9/10/5/6. The program LEDs 1-8 are on 30/2/24/28/27/25/23/15, and the program switches go through a diode matrix (11 x 1N914).
- **Board 2 (left panel):** a switch-driver / LED-sink matrix of 4042 latches (CS7, CS8) into current sinks. Its first three columns are:
  - osc A pulse, osc A saw, P-Mod filter, P-Mod PW A, P-Mod freq A, LFO saw
  - W-Mod filter, W-Mod PW B, LFO square, LFO triangle, W-Mod PW A, W-Mod freq B
  - osc B saw, osc B triangle, osc B pulse, sync, W-Mod freq A, LO FREQ
- This is the Rev 1 board. v7.0 uses a different port map, so these are not taken as the firmware's bit order.

## Still open
- Pot-to-index map for indices 0-23 (only 0x12/0x13 = osc A/B frequency are certain), and the switch latch bits. The Rev 1 board 3 interface
  (page 2) confirms a 7-bit DAC (4042 latches) and a 7-bit ADC with a "tune interrupt latch" on NMI. But its port decoding (CS0-CS10 from a 74LS138 and a
  14556) is not the port map v7.0 uses, so v7.0 targets a later board 3 and the pot map has to come from that revision's schematic or from tracing the code.
- Envelope time law (CV → seconds), filter cutoff CV scaling and initial frequency, LFO rate range, and the glide circuit.
