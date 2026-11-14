# ORBITAL: Engineering Formulas

Author: Claude, reconciled with Codex's spec. Date: 2026-11-14. Status: baseline v2.1.
Reference implementation: `reference_model.py` in this folder. Port it 1:1 to C++.

This is a simplified educational model. It is not a flight-design tool and its results must not
be presented as predictions for real hardware.

All units are SI: kg, m, s, N, W. Cost is in M credits, a fictional currency. Data is in decimal GB.
All constants, assumptions, orbits, budgets and objectives live in the `mission` block of
PART_CATALOG.json. Code must read them from there and not hard-code them.

Numeric rules (from Codex's spec): use double precision, round only for display, reject unknown
part IDs and negative or non-finite quantities, and never evaluate a stage with a missing part.

## 1. Constants

| Symbol | Value | Unit | Source |
|---|---|---|---|
| g0 | 9.80665 | m/s^2 | Exact by definition; NSSDC Earth Fact Sheet |
| mu_E | 3.9860e14 | m^3/s^2 | NSSDC Earth Fact Sheet (GM 0.39860e6 km^3/s^2) |
| R_E | 6378.137e3 | m | NSSDC Earth Fact Sheet, equatorial radius |
| mu_M | 4.90e12 | m^3/s^2 | NSSDC Moon Fact Sheet (GM 0.00490e6 km^3/s^2) |
| R_M | 1737.4e3 | m | NSSDC Moon Fact Sheet, volumetric mean radius |
| a_Moon | 384.4e6 | m | Moon semimajor axis, 0.3844e6 km |
| S | 1361.0 | W/m^2 | NSSDC fact sheets, solar irradiance at Earth and Moon |

## 2. Gameplay assumptions

These are design choices, not NASA data. Change them only in one place.

| Name | Value | Meaning |
|---|---|---|
| LEO_ALT | 200 km | Parking orbit altitude |
| DV_ASCENT | 9400 m/s | Delta-v to reach the parking orbit, including gravity and drag losses. Orbital speed there is 7784 m/s; the rest is an assumed loss allowance |
| DV_MCC | 30 m/s | Mid-course corrections on the way to the Moon |
| DV_MARGIN | 10 % | Spacecraft delta-v margin |
| Station keeping | 150, 60, 10 m/s per year | For the 50 km, 100 km and 30 x 216 km orbits. Real low lunar orbits do need regular maintenance burns and the elliptical one is low-maintenance, but these numbers are illustrative |
| CELL_EFF | 0.28 | Solar cell efficiency |
| POINT_LOSS | 0.90 | Array pointing and temperature factor |
| DOD | 0.60 | Usable battery depth of discharge |
| CHG_EFF | 0.90 | Battery charging efficiency |
| MIN_TWR_LIFTOFF | 1.20 | Minimum liftoff thrust-to-weight ratio |
| MIN_TWR_STAGE2 | 0.25 | Minimum second-stage thrust-to-weight at ignition |
| ORBIT_VISIBLE | 0.5 | Fraction of a low lunar orbit with Earth in view. LRO is on the near side about 1 hour in 2 |
| Ground contact | 8 h/day single station, 24 h/day three-station network | Before applying ORBIT_VISIBLE |
| Stage 1 Isp | mean of sea-level and vacuum Isp | Replaces integrating Isp over altitude |

## 3. Orbital mechanics

Circular speed: `v_c = sqrt(mu / r)`

Vis-viva: `v = sqrt(mu * (2/r - 1/a))`

Period: `T = 2*pi*sqrt(a^3 / mu)`

### 3.1 Trans-lunar injection (done by the rocket's second stage)

```
r1 = R_E + LEO_ALT
a_t = (r1 + a_Moon) / 2
dv_TLI = vis_viva(mu_E, r1, a_t) - v_c(mu_E, r1)        = 3131.3 m/s
```

### 3.2 Lunar orbit insertion (done by the spacecraft)

Patched conic, one burn at perilune.

```
v_inf  = v_c(mu_E, a_Moon) - vis_viva(mu_E, a_Moon, a_t) = 831.5 m/s
rp, ra = R_M + hp, R_M + ha
a      = (rp + ra) / 2
dv_LOI = sqrt(v_inf^2 + 2*mu_M/rp) - vis_viva(mu_M, rp, a)
```

| Orbit | dv_LOI (m/s) | Period (min) | Worst eclipse (min) |
|---|---|---|---|
| 50 km circular polar | 829.1 | 113.0 | 48.0 |
| 100 km circular polar | 821.6 | 117.8 | 46.5 |
| 30 x 216 km polar | 791.1 | 120.0 | 46.0 |

Sanity check against real missions: LRO's 50 km orbit has a period of about 113 minutes and
Lunar Prospector's 100 km orbit 118 minutes. The model reproduces both.

### 3.3 Eclipse

Worst case: Sun in the orbit plane, cylindrical shadow, mean altitude.

```
r = R_M + (hp + ha)/2
f_ecl = asin(R_M / r) / pi
T_ecl = f_ecl * T,   T_sun = T - T_ecl
```

## 4. Rocket equation

`dv = Isp * g0 * ln(m0 / mf)`  (ideal rocket equation; see NASA Glenn, https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/ideal-rocket-equation/)

### 4.1 Spacecraft

```
dry  = sum of part masses
wet  = dry + propellant            (propellant <= total tank capacity)
dv_sc = Isp_engine * g0 * ln(wet / dry)
dv_fixed = DV_MCC + dv_LOI(orbit)
dv_req   = (dv_fixed + SK(orbit) * years) * (1 + DV_MARGIN)
years_possible = max(0, (dv_sc/(1+DV_MARGIN) - dv_fixed) / SK(orbit))
life_frac = 0 if dv_sc/(1+DV_MARGIN) < dv_fixed else min(1, years_possible / years)
```

### 4.2 Launch vehicle, stage by stage

```
payload = spacecraft wet mass
s2_dry  = s2_tank + s2_engine + sep_system
s1_dry  = s1_tank + n * s1_engine + sep_system
m2_0    = s2_dry + s2_prop + payload                 (fairing already dropped)
m1_0    = s1_dry + s1_prop + fairing + m2_0          (liftoff mass)
Isp1    = (Isp_sl + Isp_vac) / 2
dv1     = Isp1 * g0 * ln(m1_0 / (m1_0 - s1_prop))
dv2     = Isp2_vac * g0 * ln(m2_0 / (m2_0 - s2_prop))
dv_req_LV = DV_ASCENT + dv_TLI                       = 12531.3 m/s
TWR1    = n * F_sl / (m1_0 * g0)
TWR2    = F_vac / (m2_0 * g0)
```

Simplification: the fairing is carried for all of stage 1 and dropped at staging.

### 4.3 Burn and event times (for the launch animation)

```
mdot   = F / (Isp * g0)
t_burn = propellant / mdot                              (full burn capacity of a stage)
```

Stage 2 events are timed by the propellant needed for each delta-v target, not by full capacity
(correction from Codex):

```
used(dv)  = m2_0 * (1 - exp(-dv / (Isp2 * g0)))
t(dv)     = used(dv) / mdot2          (not reached if used > stage 2 propellant)
t_parking = t(DV_ASCENT - dv1)        seconds after stage 2 ignition
t_tli     = t(dv_req_LV - dv1)        seconds after stage 2 ignition
```

Example, TC-B on the mid rocket: stage 1 burns 157 s; after stage 2 ignition the parking orbit
is reached at 665 s and trans-lunar injection completes at 868 s; full capacity would be 918 s.
A real flight coasts in the parking orbit between the two; the animation may add that pause.

## 5. Power

```
load = bus base power + sum of part power draws
gen  = total array area * S * CELL_EFF * POINT_LOSS
E_ecl (Wh) = load * T_ecl / 3600
battery_usable (Wh) = total capacity * DOD
gen_needed = load + E_ecl * 3600 / (T_sun * CHG_EFF)
```

Checks: `gen >= gen_needed` (else R_POWER_GENERATION) and `battery_usable >= E_ecl` (else R_BATTERY_ECLIPSE).
Simplification: all loads run all the time. No duty cycling.

## 6. Data and communications

```
generated (GB/day) = sum of instrument data rates
contact_s = ground hours * 3600 * ORBIT_VISIBLE
downlink (GB/day) = rate_Mbps * 1e6 * contact_s / 8 / 1e9
storage_needed (GB) = generated * max_gap_hours / 24
stored_frac = 1 if storage_needed <= storage else storage / storage_needed
data_frac = min(1, stored_frac, downlink / generated)
```

Only instruments that can be pointed count towards `generated`. An antenna that needs fine
pointing and has no reaction wheels gives downlink 0.
Antenna data rates are fixed table values at lunar distance. There is no link budget.

## 7. Cost and time

```
total_cost = spacecraft parts + rocket parts + (8 + ground cost per year) * years
```

Ground cost: 4 per year for a single station, 14 per year for the three-station network.
Budget: easy 420, normal 360, hard 310.
Time is represented by planned mission years (1 or 2) and by `years_possible`.

## 8. Science and score

Objective weights: ice 35, topography 20, thermal 15, imaging 20, radar 10 (total 100).

```
objective_score = weight * orbit_factor * data_frac * life_frac      (0 if instrument absent)
science = sum of objective scores                                     (0 to 100)
primary_ok = objective_score[primary] >= 0.60 * weight[primary]
launch_allowed = no BLOCK codes
fatal   = R_SC_DELTA_V_INSERTION (fails at insertion) or a power risk (fails in first eclipse)
success = launch_allowed and not fatal and primary_ok
score   = 0.8 * science + (20 * max(0, 1 - total_cost / budget) if primary_ok else 0)
          score is 0 when launch is blocked or the failure is fatal
```

Orbit factors are in `reference_model.py` (ORBITS). They express that a lower orbit gives sharper
measurements. They are gameplay values.

## 9. Known limitations

1. No trajectory integration. Success is decided by delta-v budgets, not by flying the path.
2. Patched-conic LOI with a coplanar transfer. No plane change, no launch window.
3. Fixed loss allowance for ascent. No drag or gravity-turn model.
4. Tanks are propellant-agnostic. Real hydrogen tanks are much larger and heavier per kg.
5. No thermal, radiation, reliability or structural analysis.
6. Costs are fictional credits invented for balance. They are not NASA cost data.
7. The launch and transfer animation is illustrative. It replays the budget result.
