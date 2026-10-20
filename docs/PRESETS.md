# Factory programs from the patch sheets

`tools/factory_sheets.txt` holds 38 of the 40 original factory programs (banks 1-4 complete, plus 5-1 to 5-6; the source has no 5-7 or 5-8).
They were read by hand on 2026-10-20 from the user's scan of the patch-sheet booklet.

- **Accuracy:** I read each dial pointer onto the 0-10 scale to about ±0.5. Switches are as drawn. Each oscillator frequency was checked against the
  sheet's printed "OSC A/B: up N octaves" note (dial = semitones / 6, because the knob spans 5 octaves) and corrected to it where my reading disagreed (4-5, 4-8, 5-1, 5-6).
- **What a sheet does not give:** master tune and volume, the wheel positions the notes ask for (for example "move wheel up 1/4"), and the actual pot-to-sound
  laws. So the programs are only as close as the engine's fitted curves.
- **Generation:** run `python3 tools/gen_presets.py` to produce `src/presets_factory.h` (do not edit the output). Dials become 0..1, frequencies become semitones,
  and the switches become `P5S_*` bits.
- **Rights:** this is a transcription of published panel settings, not a dump of the instrument's memory. Check that you're comfortable shipping it before a release.
  The rule against shipping factory `.syx` banks still holds.

| Bank | Programs |
|---|---|
| 1 | Brass, Low Strings, Muted Clav, Percussive Electric Piano, Flutes, Harpsichord, Sync I, Percussive Organ |
| 2 | Unison Glide With Resonance, Harmonium, Organ With Resonance, Toy Piano, Trumpet/Flute, Filter Mod, Reed Organ, Brass In Fifths |
| 3 | Pipe Organ Flutes, Sync II, Electric Piano, High Strings, Octave Sawteeth, Release-Repeat, Delayed Harmonic, Echo-Repeat |
| 4 | Pulse-Width Mod, Slow Sync Sweep, Fourths With Resonance, Sweeping Harmonics, Slow Sync, Random Arpeggiator, Sawtooth Arpeggiator, Clangorous Bells |
| 5 | Alien, Noise Sweep, Descending Bells, Descending Pulse Wave Mod, Helicopter, Resonance Bells |
