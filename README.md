# ORBITAL

**Your spacecraft reached the Moon. Will its battery last through the first eclipse?**

ORBITAL is a mission design game for the NASA Space Apps Challenge 2026 ("Space Mission Design Game"), built in Unreal Engine 5. You pick a lunar science mission, build an uncrewed orbiter and a two-stage rocket from a 38-part catalog, plan the orbit and ground stations, and launch. The game then flies the mission and tells you, in plain language, what worked, what failed, why, and how to fix it.

Team: Three-Body Problem. (Istanbul local event)

## What it is

- **Main menu:** Missions, Settings, Credits, Quit, and a language switch (English or Turkish). The game starts in English.
- **Three missions**, each with its own science goal and budget (see below).
- **A builder in three stages:** 1 Spacecraft, 2 Rocket, 3 Mission Plan. Parts are grouped by what they do, each part has a short description, and a live 3D model shows what you are building.
- **A live dashboard** with seven rows: mass, power, budget, data, communications, time and delta-v. Each row shows your value against its limit.
- **A power panel** showing battery energy against what the spacecraft needs while it is in the Moon's shadow.
- **A design review** before launch. Items are marked **MUST FIX** (launch is blocked), **RISK** (you may launch, but the mission can fail in flight) or **NOTE**.
- **A flight view:** liftoff, gravity turn, stage separation, fairing release, parking orbit, transfer to the Moon and lunar operations, with a telemetry panel (mission phase, fuel left, battery energy, data sent to Earth).
- **A mission report:** result, score, the cause of any failure and a suggested fix, with a **Redesign** button that returns you to the same design.

## How to run it

There is no packaged build. The game runs from the Unreal Editor on Windows.

**You need:** Unreal Engine 5.8.4, Visual Studio with the "Game development with C++" workload, the Windows SDK, and Git LFS (textures, sounds and the level are stored with LFS).

1. Clone the repository and run `git lfs pull`.
2. Build the **Development Editor / Win64** target. The exact command is in [Build and test](Docs/BUILD_AND_TEST.md).
3. Open `ORBITAL_SpaceApps.uproject`, open the level `L_Mission` (Content/Maps) and press **Play**. The main menu appears.

**Quickest demonstration (about two minutes):**

1. Click **Missions** and start any mission. **Full Polar Survey** has the largest budget.
2. In the builder, under "Ready-made designs", click **Weak battery**.
3. Click **LAUNCH**, then **Launch anyway**. The review warns that the battery is a risk.
4. Watch the flight. The spacecraft reaches the Moon and loses power in its first eclipse.
5. Read the report for the cause and the fix, click **Redesign**, swap the battery for a larger one, and launch again.

The other ready-made designs are **Small & cheap**, **Full survey** and **Small arrays** (a solar array that is too small).

## The three missions

| Mission | Science goal | Budget | The idea |
| --- | --- | --- | --- |
| Ice Prospector | Water ice at the lunar poles | 310 M credits | A tight budget: carry only what the search needs. |
| Landing Site Scout | Imaging of landing sites | 360 M credits | Cameras produce a lot of data, so plan how to send it home. |
| Full Polar Survey | Water ice, with all five instruments | 420 M credits | A larger budget for ice, terrain, temperature, landing sites and shadowed craters. |

All three use the same Moon and the same parts. What changes is the goal the design is scored against and how much you can spend. In the Mission Plan stage you also choose one of three lunar orbits (50 km, 100 km, or 30 x 216 km), the mission length, and one or three ground stations.

## NASA data and resources

| NASA resource | How ORBITAL uses it |
| --- | --- |
| [NSSDC planetary fact sheets](https://nssdc.gsfc.nasa.gov/planetary/factsheet/) | Earth and Moon constants in the engineering model that computes delta-v, orbit period and eclipse time. |
| Blue Marble Next Generation, NASA Earth Observatory / NASA Visible Earth | The Earth surface texture in the flight view. |
| [Blue Marble: Clouds](https://visibleearth.nasa.gov/images/57747/blue-marble-clouds), NASA Visible Earth | The cloud layer around the Earth. |
| CGI Moon Kit, NASA's Scientific Visualization Studio (LRO/LROC data) | The Moon surface texture. |
| Deep Star Maps 2020, NASA/Goddard Space Flight Center Scientific Visualization Studio | The star background. Star data: Gaia DR2, ESA/Gaia/DPAC. |
| [NASA Historical Sounds](https://www.nasa.gov/historical-sounds/) ("Atlas V: Launch", "SLS Test Fire") | Engine ignition and the engine sound during burns, trimmed and converted for the game. |

Credit: NASA. Exact files, source links and the edits made to each are in [Challenge traceability](Docs/CHALLENGE_TRACEABILITY.md) (parts of that document are in Turkish). Individual model values and their sources are in [Engineering formulas](Docs/CLAUDE_RESEARCH/ENGINEERING_FORMULAS.md); the model's simplifications are summarised in [Engineering assumptions](Docs/ENGINEERING_ASSUMPTIONS.md). Values without a cited source are gameplay assumptions and are labelled as such.

ORBITAL does not use the NASA logo or insignia.

## Use of AI

The team used Claude (Anthropic), ChatGPT/Codex (OpenAI) and Gemini (Google) to help write code, tests, documentation and on-screen text. Team members set the tasks, reviewed the results, built the project and played it before changes were accepted. Commits written with AI help carry a co-author line.

The stage separation and button click sounds are not NASA recordings; the in-game credits describe them as "generated for this project". *Not verified for this README: which tool generated them.*

## Limitations

- **Simplified educational model.** The numbers are meant to teach trade-offs. They are unsuitable for real flight planning.
- **Single player.** There is no team or multiplayer mode; one player makes every decision.
- **The flight is a played-back timeline.** The outcome is computed from your design before liftoff, and the flight view shows that result step by step. You do not steer the rocket, and orbits are not simulated continuously.
- **One destination.** All three missions are lunar polar orbiters.
- **Windows, from the editor.** No packaged build, and no other platform has been tried.

*Not verified for this README:* this text was written from the source code at commit `4da8943` (`integration/day2`). Menu items, mission names, budgets, button labels and credits were read from the code. The flight view's appearance on that exact commit was not watched by the author of this file; see [Build status](Docs/BUILD_STATUS.md) for what has been built and played.

## Team

- **Issa:** team leader, game design, vehicle assembly, game screens and mission timeline
- **Muhammet:** Unreal project setup, scene, lighting, flight view and submission materials
- **Ram:** engineering integration, JSON loader, dashboard, session flow and documentation
