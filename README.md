# ORBITAL

Your spacecraft reached the Moon. Will its battery last through the first eclipse?

ORBITAL is an Unreal Engine game for NASA Space Apps Challenge 2026. Design a lunar polar survey by choosing a science objective, assembling an uncrewed orbiter and a two-stage rocket from a 38-part catalog, and selecting one of three lunar orbits. Each choice affects the mission's mass, power, cost, data, communications, time, and delta-v budgets.

## Demo video

[VIDEO LINK]

## Build and play submission-v1

Use Unreal Engine 5.8.4, Visual Studio with Game development with C++, and the Windows SDK. Check out the `submission-v1` tag for the demo, build the **Win64 Development Editor** target, and open `ORBITAL_SpaceApps.uproject`. See [Build and test](Docs/BUILD_AND_TEST.md) for the build command and automation tests.

1. Open `L_Mission` and press **Play**. The assembly screen opens.
2. Click **Load TC-E**, then **LAUNCH** and **Launch anyway**. The small battery cannot power the spacecraft through its first lunar eclipse. Read the mission report for the cause and suggested fix.
3. Click **Redesign** to return to the same design. Select **Battery 1**, then choose **Battery L**.
4. Click **LAUNCH** again. The expected report reads **Mission success. Score 80.8**.

## How the mission works

Choose what to study, configure the spacecraft and launcher, and review the engineering limits. BLOCK issues prevent launch. RISK issues flag problems that can affect the mission in flight. The report explains the outcome so you can improve the design and try again. See [Build status](Docs/BUILD_STATUS.md) for verification results.

## NASA data and resources

- **NSSDC planetary fact sheets** — Earth and Moon constants for the engineering model. See the [engineering formulas](Docs/CLAUDE_RESEARCH/ENGINEERING_FORMULAS.md#1-constants) for individual values and sources.
- **Blue Marble Next Generation** — Earth texture from NASA Visible Earth.
- **CGI Moon Kit** and **Deep Star Maps 2020** — Moon and star map textures from NASA's Scientific Visualization Studio.

Credit: NASA. See [Challenge traceability](Docs/CHALLENGE_TRACEABILITY.md) for full texture credits, including the Gaia data attribution.

## Use of AI

The team used Claude, ChatGPT/Codex, and Gemini to help write code, tests, documentation, and UI text. The team directed and reviewed this work. Engineering values are labeled as gameplay assumptions unless a source is cited.

## Limitations

ORBITAL uses a simplified educational model. Its estimates are unsuitable for real flight planning. The demo runs in **Play in Editor**. A packaged build is pending.

## Team

- Muhammet — Unreal project setup, scene, lighting, and submission materials
- Isa — vehicle assembly, screen binding, and mission timeline
- Ram — engineering integration, JSON loader, dashboard, session flow, and documentation
