# Build status

Baseline: `main` at `3067b58`. Results below are attributed to the machine and test that produced them.

## Verified in Unreal on Muhammet's PC

| Check | Result |
| --- | --- |
| Win64 Development Editor | Build passed. |
| `ORBITAL.Engineering.CatalogParity` | Passed through the JSON loader. TC-A **33.5111**, TC-B **80.7778**. |
| `ORBITAL.Assembly.DemoTCA` | Passed. |
| `L_Mission` | TC-A flight played in the level. |

## Verified on Ram's PC

Win64 Development Editor built from `main` at `3067b58` with `Result: Succeeded`. The automation and flight results above are from Muhammet's PC.

## Standalone verification

- Engineering parity passed **17/17** under MSVC. Isa also reported the standalone suite passing under GCC and Clang.
- Ram ran **118 assembly**, **90 screen-binding**, and **418 rev 3 timeline** checks under MSVC. The standalone suites also checked deterministic exports and mission outcomes. The Unreal checks above cover the TC-A path.
- The dashboard logic passed TC-A, TC-B, TC-C, TC-E, TC-G, and TC-N under MSVC.
- The mission session passed TC-A, TC-B, TC-C, and TC-E under MSVC, including JSON round trips, role approvals, and launch snapshot and report flow. Dashboard and session verification here is standalone C++.

## Open checks

- The combined TC-E → eclipse failure → battery swap → TC-B success flow has no confirmed end-to-end Unreal result in this log. Verify the review, playback, report, and redesign controls together.
- Packaged game build and run verification is pending.
