# Sources

Every file the circuit work uses, with its SHA1 (Lemus `CONTRIBUTING.md`, "Nothing from memory"). None of them is in the repo; they live in
the private folder `~/roms/prophet-5/` on the development machine. Page numbers in `docs/CIRCUIT.md` are PDF pages of these files.

| File | SHA1 | What | Where from |
|---|---|---|---|
| `Sequential Circuits Prophet 5 Service Manual.pdf` | 741a809ae24c5a6cc1a7b63b35e06b819e1b8e31 | Technical manual for Rev 1/2 (TM1000C), 141 pp, 72 ppi; voice SD411 rev 2.0 | supplied by the user, 2026-11-22 |
| `sequentialcircuitsprophet-5servicemanual-text-470674.pdf` | a29528b77d2a25e3dcb2b53b73a697d2dda482aa | Technical manual TM1000D.2, Rev 3.0-3.2, 134 pp, OCR | supplied by the user (an Internet Archive scan), 2026-11-22 |
| `P5REV4_VOICE_VCO.pdf` | 7155f72c87e4d4e5989c4ad42279e3971f4b237a | Partial community KiCad trace of the Rev 4 reissue's VCO mixer | supplied by the user, 2026-11-22 |
| `datasheets/SSM2020.pdf` | d0e0ebdccb5711984504edda08a9fd7bc29fcdb1 | SSM 2020 dual linear-antilog VCA, 4 pp | https://bitsavers.mirrorservice.org/components/solidStateMusic/SSM2020.pdf |
| `datasheets/SSM2040.pdf` | 9e21cd89e708ba85cd80802909c8d088240cdf5d | SSM 2040 VCF, July 1977, 4 pp | https://bitsavers.mirrorservice.org/components/solidStateMusic/SSM2040.pdf |
| `datasheets/SSM2030.pdf` | d58fb72c9b45ca1f6f8971d6df86684f5dfb1610 | SSM 2030 VCO, Aug 1977, 2 pp | supplied by the user, 2026-11-22 |
| `datasheets/SSM2030-emu.pdf` | 99cf46ce6b5ca6228fb40cc931ce8b4ed0b1c8ba | SSM 2030 (E-mu document), 4 pp; not read yet | supplied by the user, 2026-11-22 |
| `datasheets/SSM2050.pdf` | a2eb00a7ebfef17fd8b03be0c8ce0027472d979e | SSM 2050 transient generator, 2 pp: 100 ms at 0 V with 0.1 uF, +18 mV/oct, 2 V/decade via 100k | supplied by the user, 2026-11-22 |
| `Sequential Circuits Prophet 5 Rev 2.0 v7.0c/p5_v7.0c_0.bin` | c764358b74285d3e0082d6138b21b909fc2aa060 | Rev 2 firmware v7.0c, 2708 image 0 (each 1 KB, doubled) | the user's dump, supplied 2026-11-22 |
| `Sequential Circuits Prophet 5 Rev 2.0 v7.0c/p5_v7.0c_1.bin` | 974f18c5a90887293ab90dbeac3794d7d5b3f7fe | image 1 | as above |
| `Sequential Circuits Prophet 5 Rev 2.0 v7.0c/p5_v7.0c_2.bin` | e84c34ccfad8863137bbb098d5c74e40808e64f6 | image 2 | as above |
| `Sequential Circuits Prophet-5 3.0 V8.1/` | v81_2732.bin b58aac3a69dda5c8ab459d9769e04065836f2e8b; bank0-2 915c4648..., 81f958f1..., a5993436...; p5diags.bin ef2cf8d1a25aa91380a16c6e454a5a04ab7b3014 | Rev 3.0 firmware V8.1 and diagnostics | the user's dumps, supplied 2026-11-22 |
| https://maffez.com/?page_id=3556 | (web page, read 2026-11-22) | Rev 4 notes: SSI2140 and CEM3320 both fitted, SSI2164 VCAs, Q-compensation, DC blocking, part designators | public page |
| `datasheets/V3320.pdf` | 764873f1c4ef8a84958f276155d50a8f61b0eb8e | Coolaudio V3320 (CEM 3320 clone) datasheet, 9 pp: the cell law, I_ref, 60 mV/decade, Q VCA gm and input | https://akizukidenshi.com/goodsaffix/v3320.pdf |

Wanted: the original CEM 3320 datasheet (the clone's constants stand in), free recordings of real Rev 2 units (none yet).
