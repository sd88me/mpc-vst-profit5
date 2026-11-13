# Profit-5 2.0.0

Profit-5 2.0.0 adds a native MPC screen, Q-Links on every page and named favourites. The sound engine is unchanged.

## What's new
- Native MPC screen: the Oscillators, Filter Amp and Programmer pages with live knobs, switches and LEDs. The mixer now sits next to the
  oscillators; Glide and Unison moved to the filter page.
- Q-Links on every page: six sub-tabs in groups of four that follow the panel rows.
- Favourites: `+FAV` names the current sound on an on-screen keyboard and stores it in the PRESET menu as `F01 <name>`, after the programs;
  `-FAV` deletes the picked one. They are kept in `profit5-favourites.dat` and shared by every project.
- Programmer page: the program name takes the data wheel and a Q-Link, with step keys either side, and shows how many Repro-5 patches were imported.
- One zip holds the 32-bit (Force) and 64-bit builds.

## Compatibility
- Parameter order and the project format of 0.1.x and 0.2.x are kept, so saved projects load with their sound.
- Imported Repro-5 patches stay in `banks/`; the installer keeps that folder and the favourites file.

## Tested
Live III: all pages, Q-Links, programs, the data wheel, favourites and naming, sound.
Not yet tested: the Force with this version, a real project saved with 0.2.1 on the device (old project data passes the offline tests), and saving and
reloading a 2.0 project on the device.
