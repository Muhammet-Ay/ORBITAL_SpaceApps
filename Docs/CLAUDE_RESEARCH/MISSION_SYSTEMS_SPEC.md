# ORBITAL: Mission Systems Spec

Author: Claude, reconciled with Codex's spec. Date: 2026-11-14. Status: baseline v2.1, for Codex and Isa to implement.
Read with: PART_CATALOG.json (data), ENGINEERING_FORMULAS.md (math), TEST_CASES.md (expected numbers).

## 1. Scenario: Lunar Polar Scientific Survey

Briefing text (draft): "Permanently shadowed craters at the Moon's poles may hold water ice.
Design an uncrewed orbiter, choose its instruments and orbit, build a rocket that can send it to
the Moon, and return enough data to achieve your primary science objective within budget."

Real missions this is modelled on (context only, we do not copy their specifications):
- Lunar Reconnaissance Orbiter: 50 km polar mapping orbit for at least one year, launched on an Atlas V 401.
- Lunar Prospector: small spin-stabilised orbiter in a 100 km polar orbit; its neutron spectrometer found hydrogen at both poles.

## 2. Game flow

| Step | Screen | Player action | Output |
|---|---|---|---|
| 1 | Briefing | Pick difficulty and 1 primary objective | Budget, primary objective |
| 2 | Spacecraft assembly | Pick bus, then attach parts | Spacecraft design |
| 3 | Mission plan | Pick orbit, planned years (1 or 2), ground stations, propellant load | Requirements |
| 4 | Rocket assembly | Pick stage 1 tank, engine and count, stage 2 tank and engine, fairing | Rocket design |
| 5 | Design review | Read dashboard, fix errors, team roles sign off | Go / No-go |
| 6 | Launch | Watch liftoff, staging, fairing release, TLI | Events |
| 7 | Cruise and arrival | Watch transfer and LOI burn | Orbit reached or not |
| 8 | Operations | Time-lapse of mission years: power, data, propellant | Science returned |
| 9 | Mission report | Read score and causes; go back to step 2 to improve | Score |

Steps 2 to 4 can be visited in any order. The dashboard is visible in all of them.

## 3. Science objectives

| ID | Objective | Needs instrument | Weight |
|---|---|---|---|
| O1_ICE | Map hydrogen (water ice) at the poles | Neutron Spectrometer | 35 |
| O2_TOPO | Map terrain inside shadowed craters | Laser Altimeter | 20 |
| O3_THERMAL | Map surface temperature and cold traps | Thermal Radiometer | 15 |
| O4_IMAGING | Image candidate landing sites | High-Resolution Camera | 20 |
| O5_RADAR | Probe shadowed craters by radar | Imaging Radar | 10 |

The primary objective must reach 60 % of its weight for mission success. The others add score.

## 4. Orbit options

| ID | Orbit | Good | Bad |
|---|---|---|---|
| LPO_50 | 50 km circular polar | Best measurements (factor 1.0) | 150 m/s per year station keeping, longest eclipse |
| LPO_100 | 100 km circular polar | 60 m/s per year | Lower resolution, imaging factor 0.6 |
| ELL_30x216 | 30 x 216 km polar | 10 m/s per year, cheapest insertion | Factors 0.6 to 0.7 |

Tooltip idea: "Lower orbits see more detail, but the Moon's uneven gravity pulls them out of
shape, so you burn more fuel to stay there."

## 5. Dashboard resources

| Resource | Shown as | Limit |
|---|---|---|
| Mass | Spacecraft dry / wet, rocket liftoff mass | Bus limit, fairing limit, rocket delta-v |
| Power | Load W vs generation W; eclipse Wh vs battery Wh | Section 5 of formulas |
| Budget | Total cost vs budget, in M credits | Difficulty |
| Data | Generated GB/day vs downlink GB/day; storage | Antenna, ground stations, computer |
| Communications | Data rate, contact hours per day | Antenna, ground choice, pointing |
| Time | Planned years vs years possible | Propellant |
| Delta-v | Spacecraft available vs required; rocket available vs required | Formulas section 4 |

Colour rule: green when margin is 10 % or more, amber under 10 % or for any RISK code, red for any BLOCK code.

## 6. Trade-off chains the game must make visible

1. More instruments: more science, but more mass, power, data and cost.
2. More mass: more propellant for the same delta-v, then a bigger rocket, then more cost.
3. More power draw: bigger arrays and batteries, which add mass.
4. More data: bigger antenna, which needs reaction wheels, which add mass and power; or pay for the ground network.
5. Lower orbit: better science, more station-keeping propellant, shorter life.
6. Efficient engines (bipropellant, hydrogen upper stage): less propellant, higher cost.
7. Oversized rocket: always works, wastes budget and lowers score.

## 7. Validation rules

Two severities, following Codex's policy: only problems that would stop a real launch block it.
Everything else is a risk the player may fly with, and then learns from in the mission report.

BLOCK: launch refused.

| Code | Condition | Cause shown to player | Fix hint |
|---|---|---|---|
| E_UNKNOWN_PART | Unknown ID, negative or non-finite value | The design file is damaged | Reload the design |
| E_MISSING_REQUIRED_PART | A required slot is empty (RCS counts as required) | The vehicle is incomplete | Add the named part |
| E_SOCKET_LIMIT | More parts than a socket type allows | No room for another one of these | Remove one |
| E_PROP_OVER_CAPACITY | Propellant above tank capacity | Tanks cannot hold this much propellant | Add or enlarge tanks |
| E_BUS_OVERLOAD | Wet mass above bus limit | The structure is too weak for this mass | Larger bus or less mass |
| E_BUS_INSTRUMENT_SLOTS | Too many instruments for the bus | No mounting space left | Larger bus |
| E_COMPUTER_INSTRUMENT_LIMIT | Too many instruments for the computer | The computer cannot run this many instruments | Advanced computer |
| E_ENGINE_COUNT | Engine count outside 1..max_count | This engine cannot be clustered that way | Change the count |
| E_TWR_LIFTOFF | Liftoff TWR below 1.20 | The rocket is too heavy for its engines to lift | More thrust or less mass |
| E_TWR_STAGE2 | Stage 2 TWR below 0.25 | The upper stage accelerates too slowly | Stronger engine |
| E_LV_DELTA_V | Rocket delta-v below 12,531 m/s | The rocket cannot send this mass to the Moon | Bigger stages, better engine, lighter spacecraft |
| E_FAIRING_MASS | Payload above fairing limit (equal passes) | The fairing cannot carry this mass | Larger fairing |
| E_FAIRING_SIZE | Bus not in the fairing's list | The spacecraft does not fit | Larger fairing |
| E_BUDGET | Cost above budget (equal passes) | The mission costs more than approved | Cheaper parts |

RISK: launch allowed, shown in amber at the design review, consequences play out in flight.

| Code | Condition | What happens in the mission | Fix hint |
|---|---|---|---|
| R_SC_DELTA_V_INSERTION | Spacecraft cannot afford mid-course plus insertion | Fails at insertion: flies past the Moon. Science 0 | More propellant, better engine, less mass |
| R_SC_DELTA_V_LIFETIME | Enough to enter orbit, not for the planned years | Mission ends early at years_possible; science scaled | More propellant or an easier orbit |
| R_POWER_GENERATION | Arrays cannot run loads and recharge | Fails in the first eclipses. Science 0 | More array area or fewer loads |
| R_BATTERY_ECLIPSE | Battery smaller than eclipse energy | Fails in the first eclipse. Science 0 | More battery |
| R_NEEDS_FINE_POINTING | Fine-pointing part without reaction wheels | Those instruments return nothing; such an antenna sends nothing | Add reaction wheels |
| R_STORAGE | Storage below need between contacts | Data lost in proportion | Advanced computer or ground network |
| R_NO_INSTRUMENT | No instrument | No science | Add an instrument |
| R_PRIMARY_INSTRUMENT_MISSING | Primary objective's instrument absent or unusable | Primary objective fails | Add the matching instrument |
| W_DATA_BOTTLENECK | Returned data below generated | Science scaled by the fraction returned | Bigger antenna or ground network |

The report shows the phase reached, the measured value, the required value and the fix
(Codex's reporting rule), and links to the real mission for comparison:
https://science.nasa.gov/mission/lro/spacecraft-and-instruments/

## 8. Assembly and attachment

This is Codex's attachment contract, applied to this catalog. No free placement.

A build is a set of part IDs indexed by named sockets, plus orbit, years, ground choice and
propellant load. Every part has a `slot`; a socket accepts only parts with that slot and the
current mission mode. A child socket exists only when its parent part is present. Removing a
parent removes its descendants and recomputes the design. Socket names are stable IDs.

| Parent | Child sockets |
|---|---|
| launch-mount | stage1-tank |
| stage1-tank | stage1-engine (count 1..max_count), separator |
| separator | stage2-tank |
| stage2-tank | stage2-engine, fairing |
| fairing | probe-core, escape (crewed only) |
| probe-core | probe-tank (max 2), probe-engine, adcs (max 2), solar (max 4), battery (max 2), antenna, avionics, instrument (bus limit: 2 on the small bus, 5 on the medium bus, sockets deck_a..deck_e), life-support (crewed only) |

The tree and limits are in the catalog as `mission.socket_tree` and `mission.socket_limits`.
The second separator (payload release) is added automatically and is included in mass and cost.
Geometry: use each part's `visual` primitive (cylinder, cone, box, sphere, ring, panel, dish),
one local transform per parent and socket type, +Z along the stack.

### Snapshot and replay (Codex's rule)

At launch, capture an immutable snapshot of the validated design. The simulation, the report
and any retry use that snapshot. Editing the design invalidates the old forecast. The same
snapshot must always produce the same report.

## 9. Team play (one PC, up to 4 players)

| Role | Owns |
|---|---|
| Mission Lead | Objective, orbit, mission years, budget; final Go |
| Propulsion Engineer | Rocket, spacecraft engine, tanks, propellant |
| Science Officer | Instruments, computer |
| Power and Comms Engineer | Arrays, batteries, antenna, ground stations, attitude control |

Design review: each role sees the errors in the parts they own and must press Go. With fewer
players, one person holds several roles. Single player holds all four. This needs no networking.

## 10. Simulation timeline (for the launch and mission view)

The outcome is decided by the budget model, evaluated on the launch snapshot. The 3D view replays it, so the visuals
and the numbers cannot disagree.

| T | Event | Source of timing |
|---|---|---|
| 0 | Liftoff | |
| t_burn1 | Stage 1 cutoff, separation, fairing release | Formulas 4.3 |
| stage 2 ignition + t_parking | Parking orbit reached (200 km) | Formulas 4.3 |
| stage 2 ignition + t_tli | TLI complete, spacecraft separation | Formulas 4.3 |
| about 4 days | Cruise, compressed to seconds | LRO took about 4 days |
| arrival | LOI burn | dv_LOI and spacecraft engine burn time |
| years | Operations time-lapse with eclipse, data and propellant bars | Formulas 5, 6 |
| end | Mission report | Formulas 8 |

## 11. Crewed missions (data only in this build)

Parts `command_module`, `crew_capsule`, `life_support`, `les` are in the catalog with
`mission_modes: ["crewed"]`, so the uncrewed scenario never offers them. Rules to apply when a crewed scenario is added: a crewed bus requires
a Life Support Module and a Launch Escape System; consumables mass = rate x crew x days.
These values are placeholders and have not been balanced.

## 12. Open items

1. Random event (challenge "could have"): not specified yet.
2. Educational tooltips: Gemini to write from sections 4, 5 and 7.
3. The eight official resources have not been read yet (see CHALLENGE_ANALYSIS section 4).

## 13. Reconciliation with Codex's spec (2026-11-14)

One baseline now exists: these files. Codex's five files are superseded.

| Topic | Decision | Why |
|---|---|---|
| Physics and numbers | Claude's model | Orbits are computed from NASA constants and match LRO and Lunar Prospector periods; trade space has several valid designs |
| Who does trans-lunar injection | Rocket's second stage | Simpler for players and how LRO flew; Codex's probe carried 4,050 m/s itself |
| Science scale | GB per day over mission years | 18 Mbit per mission is not credible next to real missions |
| Part choice | Claude's 38 parts | Codex's catalog had one part per slot, so few trade-offs |
| Block vs risk severity | Codex's policy | Flying a flawed design and seeing why it failed teaches more than a refused launch |
| Socket tree, slot and visual fields | Codex's contract | Maps directly to Unreal components |
| Mission block in JSON, quick-load build | Codex's idea | No constants hard-coded in C++ |
| Snapshot, deterministic replay, input checks, boundary tests | Codex's rules | Robustness |
| Currency | Fictional M credits (Codex) | Avoids implying real costs |
| Orbit options | Three polar orbits (Claude) | Codex's equatorial option was a trap choice with no trade-off |
