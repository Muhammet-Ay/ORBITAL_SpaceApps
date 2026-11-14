# Build status

## Release candidate

The release candidate is the `main` revision `a26bfc9`, tagged `submission-v1`. It includes the playable mission loop, assembly screen, star background, engine flame, and audio. The current `main` tip, `b5249c9`, adds the documentation merge.

| PC | Revision | Verified result |
| --- | --- | --- |
| Muhammet | `main` at `3067b58` | Win64 Development Editor built. `ORBITAL.Engineering.CatalogParity` passed through the JSON loader (TC-A **33.5111**, TC-B **80.7778**). `ORBITAL.Assembly.DemoTCA` passed, and the TC-A flight played in `L_Mission`. |
| Ram | `main` at `3067b58` | Win64 Development Editor built successfully. |
| Muhammet | `integration/loop` at `a26bfc9` | Ran the full battery redesign loop in Unreal with the automated test driver and captured the demo. |
| Ram | `integration/loop` at `a26bfc9` | Win64 Development Editor built successfully. Ran the full loop by hand with the mouse in normal Play on `L_Mission`. |
| Isa | `isa/game-loop` at `989ab51` and `2266ee8` | Reported successful Win64 Development Editor builds of both commits. These results cover Isa's branch. |

### Manual release acceptance on Ram's PC

1. Loaded TC-E with Battery S. The amber Power row showed about **240 / 382 Wh** for eclipse.
2. Chose **Launch anyway**. The spacecraft lost power during its first lunar eclipse. The report showed `R_BATTERY_ECLIPSE` with the cause and fix.
3. Chose **Redesign** and returned to assembly. Replaced Battery S with Battery L and launched again.
4. The retry reached **OPERATIONS**. The report showed **Mission success. Score 80.8** and **Science 100.0**.

The automated test on Muhammet's PC additionally compared the design JSON before launch and after Redesign and reported an identical match.

## Outside the release candidate

- Isa's separate **Power in Eclipse** and flight telemetry panels are on `isa/panels` at `2266ee8`.
- A live 3D assembly preview, a main menu with mission selection, sound settings, and additional destinations such as Neptune are outside the candidate's scope.
- Windows Shipping packaging and clean-machine testing are pending. The verified demo runs in Play in Editor.

## Standalone verification

- The v2.1 engineering parity suite passed **17/17** on Ram's MSVC toolchain. Isa also reported passes with GCC and Clang.
- Ram ran **118 assembly**, **90 screen-binding**, and **418 rev 3 timeline** checks with MSVC.
- The dashboard passed TC-A, TC-B, TC-C, TC-E, TC-G, and TC-N under MSVC. The session passed TC-A, TC-B, TC-C, and TC-E, including JSON round trips and launch snapshot and report flow.
