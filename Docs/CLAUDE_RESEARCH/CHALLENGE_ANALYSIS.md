# ORBITAL: Challenge Analysis

Author: Claude (mission systems). Date: 2026-11-14. Status: baseline v2, reconciled with Codex's analysis.

Source of truth: the official challenge page, read on 2026-11-14.
- Details: https://www.spaceappschallenge.org/2026/challenges/space-mission-design-game/?tab=details
- Resources: https://www.spaceappschallenge.org/2026/challenges/space-mission-design-game/?tab=resources

## 1. What the challenge asks for

Difficulty: Intermediate, Beginner/Youth. Subject: Games.

Core task: an interactive game where participants design, manage and simulate a complete space
mission, make engineering decisions, manage limited resources, and see how each choice shapes
mission success. The page stresses that existing tools cover single topics (orbits, spacecraft,
launch) and that the gap is showing how they interact as one system.

Nothing on the page is a hard requirement beyond that core task. Everything else is worded as
"could" or "may consider". The table below treats the repeated themes as what judges will look for.

## 2. Requirement to feature map

Priority: M = must have in the demo, S = should have, C = could have if time allows.

| # | Challenge theme | ORBITAL feature | Priority | Status |
|---|---|---|---|---|
| 1 | Define a mission objective first | Mission briefing; player picks 1 primary objective of 5 | M | Specified |
| 2 | Design the spacecraft | Bus, propulsion, power, comms, computer, attitude control | M | Specified |
| 3 | Select scientific instruments | 5 instruments, each with mass, power, cost, data, science value | M | Specified |
| 4 | Choose launch vehicle | Two-stage rocket assembled from tanks, engines, fairing | M | Specified |
| 5 | Choose an orbit | 3 lunar polar orbits with different science, fuel and eclipse | M | Specified |
| 6 | Check mass and orbit against launch vehicle | Rocket delta-v, liftoff thrust-to-weight, fairing limits | M | Specified |
| 7 | Show the main resources clearly | Dashboard: mass, power, budget, data, communications, time | M | Specified |
| 8 | Every decision has a consequence | All parts feed the same budgets; see MISSION_SYSTEMS_SPEC section 6 | M | Specified |
| 9 | No single perfect solution | At least 3 different valid designs (TEST_CASES TC-A, TC-B, TC-K, TC-N) | M | Verified in model |
| 10 | Explain why a mission fails | Flawed designs may launch; the report gives phase reached, measured vs required, cause and fix | M | Specified |
| 11 | Simulate, then modify and rerun | Return to assembly after the mission report | M | To build |
| 12 | Mission report and scoring | Science score, cost efficiency, per-objective breakdown | M | Specified |
| 13 | Simple explanations for beginners | Tooltip per resource and per part, one or two sentences | S | Gemini to write |
| 14 | Team play up to 4, with roles | 4 roles on one PC, each signs off their subsystem | S | Specified |
| 15 | Difficulty levels | Easy, normal, hard change the budget only | S | Specified |
| 16 | Multiple scenarios | Data model supports it; only the lunar scenario is built | C | Out of scope |
| 17 | Random events | One optional event during the mission (for example solar storm) | C | Not specified yet |
| 18 | Leaderboard, save and share designs | Local best-score list; design saved as JSON | C | Not specified yet |
| 19 | Crewed missions | Parts and rules exist in data, disabled in this scenario | C | Data only |

## 3. Gaps found in the original plan

1. Data and mission lifetime were missing as resources. Both are now in the model.
2. Orbit choice was missing. Three orbits are now specified.
3. Failure feedback was pass or fail only. Error codes now carry cause and fix text.
4. Team play was absent. A four-role sign-off on one PC is specified, with no networking.
5. Free-form 3D assembly is the highest schedule risk. The spec uses fixed sockets.

## 4. Challenge resources

Eight resources are listed on the Resources tab. "Read" means opened and used for this spec.

| Resource | URL | Contribution to ORBITAL | Read |
|---|---|---|---|
| Design and Develop Science Missions (NASA) | https://www.nasa.gov/wp-content/uploads/2025/04/250323-design-and-develop-science-mission.pdf | Mission design process; should shape the briefing and phase names | Not yet |
| General Mission Analysis Tool, GMAT (NASA) | https://software.nasa.gov/software/GSC-19468-1 | Reference for what real trajectory tools do; we do not embed it | Not yet |
| Concept Maturity Levels (JPL) | https://dataverse.jpl.nasa.gov/dataset.xhtml?persistentId=hdl:2014/44299 | Could label design stages in the UI | Not yet |
| Mission Design Lab (NASA GSFC) | https://etd.gsfc.nasa.gov/capabilities/integrated-design-center/mission-design-lab/ | Model for the team roles: discipline engineers designing together | Not yet |
| NASA Mission Viz | https://nasa.github.io/mission-viz/ | Visual reference for trajectory display | Not yet |
| A Learning Game About Telescopes (NASA Webb) | https://science.nasa.gov/mission/webb/game-build-it-yourself/ | Example of a NASA build-it-yourself trade-off game | Not yet |
| CubeSat 101 (NASA CSLI) | https://www3.nasa.gov/sites/default/files/atoms/files/nasa_csli_cubesat_101_508.pdf | Subsystem list and beginner explanations | Not yet |
| Digital Games and Activities (Canadian Space Agency) | https://www.asc-csa.gc.ca/eng/youth-educators/digital-games-and-activities.asp | Examples of educational space games | Not yet |

Action: I will read these next and update this table and Docs/CHALLENGE_TRACEABILITY.md inputs.
Licensing has not been checked for any of them. The game bundles no external images, datasets,
code or logos. Before importing any asset, check its credits and terms and keep the attribution.
NASA names and logos have separate usage rules.

## 5. Other sources used in this spec

| Source | URL | Used for |
|---|---|---|
| NSSDC Moon Fact Sheet (NASA) | https://nssdc.gsfc.nasa.gov/planetary/factsheet/moonfact.html | Moon GM, radius, solar irradiance |
| NSSDC Earth Fact Sheet (NASA) | https://nssdc.gsfc.nasa.gov/planetary/factsheet/earthfact.html | Earth GM, radius, standard gravity |
| LRO overview paper (NASA, archived copy) | https://corpora.tika.apache.org/base/docs/govdocs1/698/698820.pdf | 50 km polar orbit, propulsion Isp 212.2 s, mass limits |
| LRO (eoPortal) | https://eoportal.org/satellite-missions/lro | 113 min period, near side about 1 hour in 2 |
| Lunar Prospector (NASA Science) | https://science.nasa.gov/category/lunar-prospector/ | 100 km orbit, 118 min period, neutron spectrometer and polar hydrogen |
| Lunar Reconnaissance Orbiter (Wikipedia) | https://en.wikipedia.org/wiki/Lunar_Reconnaissance_Orbiter | Payload mass, power, instrument list (secondary source) |
