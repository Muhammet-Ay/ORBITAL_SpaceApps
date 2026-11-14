# ORBITAL

Your spacecraft reached the Moon. Will its battery last through the first eclipse?

ORBITAL is an Unreal Engine game for NASA Space Apps Challenge 2026. Design a lunar polar survey by choosing a science objective, assembling an uncrewed orbiter and a two-stage rocket from a 38-part catalog, and selecting one of three lunar orbits. Each choice affects the mission's mass, power, cost, data, communications, time, and delta-v budgets.

## Build and try the demo

Use Unreal Engine 5.8.4, Visual Studio with Game development with C++, and the Windows SDK. Build the **Win64 Development Editor** target, open `ORBITAL_SpaceApps.uproject`, then open `L_Mission` and press **Play** to see the TC-A reference flight. See [Build and test](Docs/BUILD_AND_TEST.md) for the command and automation tests.

## How the mission works

Choose what to study, configure the spacecraft and launcher, and review the engineering limits. BLOCK issues prevent launch. RISK issues point to problems that may affect the mission in flight. The mission report explains the result so the design can be improved and flown again.

The small-battery redesign is the team's end-to-end acceptance scenario. Its first flight uses TC-E and loses power during the first eclipse. Replacing the battery with the larger pack gives TC-B, with an expected score of **80.7778**. See [Build status](Docs/BUILD_STATUS.md) for what has been run in Unreal.

## Team

- Muhammet — Unreal project setup, scene, lighting, and submission materials
- Isa — vehicle assembly, screen binding, and mission timeline
- Ram — engineering integration, JSON loader, dashboard, session flow, and documentation
