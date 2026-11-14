// ORBITAL ascent model: a gravity-turn launch to the parking orbit, integrated in 2D.
// Plain C++ (no Unreal types), same style as OrbitalEngineering_v2.1.h. Tested by
// Tools/StandaloneTests/ascent_tests.cpp.
//
// What it is for: the flight playback used to move the rocket straight up. Real launchers
// rise vertically for a few seconds, pitch over by a few degrees, then let gravity bend the
// path while the engine pushes along the direction of travel (a "gravity turn"). This file
// produces that path, plus the numbers a player should see on the way: pitch, speed,
// dynamic pressure (max-Q) and nose heating.
//
// What it is NOT: the mission outcome. Orbital::Evaluate() still decides success. This model
// only draws the launch and feeds telemetry. Its assumptions are gameplay assumptions:
//   - planar flight over a non-rotating, spherical Earth (no launch-site or azimuth effects)
//   - exponential atmosphere: 1.217 kg/m3 at the surface, 8.5 km scale height
//     (NSSDC Earth Fact Sheet: https://nssdc.gsfc.nasa.gov/planetary/factsheet/earthfact.html)
//   - constant drag coefficient 0.3 on a 3.7 m diameter body
//   - stage 1: thrust rises from its sea-level value to sea-level x (vacuum Isp / sea-level Isp)
//     as the air thins; mass flow is the value Orbital::Launcher() uses, so the burn time
//     matches the timeline
//   - stage 2: a simple altitude-hold steering law, cut off at circular speed
//   - nose heating: Sutton-Graves stagnation-point estimate, 1 m nose radius
//     (Sutton and Graves, NASA TR R-376, 1971)
#pragma once

#include "../OrbitalEngineering_v2.1.h"

namespace Orbital
{
struct AscentSample
{
    double timeS = 0, altitudeM = 0, downrangeM = 0, speedMps = 0;
    double pitchDeg = 90;      // thrust direction above the local horizon (90 = straight up)
    double flightPathDeg = 90; // velocity direction above the local horizon
    double massKg = 0, accelG = 0, dynamicPressurePa = 0, heatFluxWm2 = 0;
    int stage = 1;
};

struct AscentProfile
{
    std::vector<AscentSample> samples; // every sampleStepS seconds, liftoff to cutoff
    double kickDeg = 0;                // pitch-over angle chosen for this vehicle
    double stage1BurnoutS = 0, cutoffS = 0;
    double maxQPa = 0, maxQTimeS = 0, maxQAltitudeM = 0;
    double peakHeatFluxWm2 = 0;
    double periapsisAltM = 0, apoapsisAltM = 0;
    double deltaVSpentMps = 0, gravityLossMps = 0, dragLossMps = 0;
    double stage2PropLeftKg = 0;
    bool reachedOrbit = false; // periapsis above the atmosphere when the engine cut off
};

namespace AscentModel
{
inline constexpr double SurfaceDensity = 1.217, ScaleHeightM = 8500.0;
inline constexpr double DragCoefficient = 0.3, BodyDiameterM = 3.7, NoseRadiusM = 1.0;
inline constexpr double SuttonGravesEarth = 1.7415e-4; // SI units
inline constexpr double KickSpeedMps = 60.0;           // start the pitch-over at this speed
inline constexpr double AtmosphereTopM = 120000.0;

inline double Density(double altitudeM)
{
    return altitudeM > 150000.0 ? 0.0 : SurfaceDensity * std::exp(-std::max(altitudeM, 0.0) / ScaleHeightM);
}

// Stagnation-point convective heating on a blunt nose, W/m2. Also usable for atmospheric entry.
inline double HeatFluxWm2(double densityKgM3, double speedMps, double noseRadiusM = NoseRadiusM)
{
    return SuttonGravesEarth * std::sqrt(densityKgM3 / noseRadiusM) * speedMps * speedMps * speedMps;
}

struct Vehicle
{
    double mass1StartKg = 0, prop1Kg = 0, flow1 = 0, thrust1SeaN = 0, thrust1VacN = 0;
    double mass2StartKg = 0, prop2Kg = 0, flow2 = 0, thrust2N = 0;
    bool valid = false;
};

// Same mass bookkeeping as Orbital::Launcher().
inline Vehicle MakeVehicle(const Catalog& catalog, const Mission& m, const LauncherDesign& d, double payloadKg)
{
    Vehicle v;
    for (const Id* id : {&d.firstTank, &d.firstEngine, &d.secondTank, &d.secondEngine, &d.fairing})
    {
        if (catalog.find(*id) == catalog.end())
            return v;
    }
    const auto separator = catalog.find("sep_system");
    if (separator == catalog.end() || d.firstEngineCount < 1)
        return v;
    const Part& tank1 = catalog.at(d.firstTank);
    const Part& engine1 = catalog.at(d.firstEngine);
    const Part& tank2 = catalog.at(d.secondTank);
    const Part& engine2 = catalog.at(d.secondEngine);
    const int count = d.firstEngineCount;
    const double secondDry = tank2.massKg + engine2.massKg + separator->second.massKg;
    v.mass2StartKg = secondDry + tank2.capacityKg + payloadKg; // fairing is dropped at staging
    v.prop2Kg = tank2.capacityKg;
    v.thrust2N = engine2.vacuumThrustN;
    v.flow2 = engine2.vacuumThrustN / (engine2.vacuumIspS * m.g0);
    v.mass1StartKg = tank1.massKg + count * engine1.massKg + separator->second.massKg + tank1.capacityKg
        + catalog.at(d.fairing).massKg + v.mass2StartKg;
    v.prop1Kg = tank1.capacityKg;
    v.thrust1SeaN = count * engine1.seaLevelThrustN;
    // The catalog gives stage 1 thrust at sea level only. In vacuum the same engine pushes
    // harder in proportion to its specific impulse.
    v.thrust1VacN = engine1.vacuumThrustN > 0 ? count * engine1.vacuumThrustN
        : v.thrust1SeaN * engine1.vacuumIspS / std::max(engine1.seaLevelIspS, 1.0);
    v.flow1 = count * engine1.seaLevelThrustN / ((engine1.seaLevelIspS + engine1.vacuumIspS) / 2.0 * m.g0);
    v.valid = v.flow1 > 0 && v.flow2 > 0 && v.mass1StartKg > 0;
    return v;
}

struct State
{
    double x = 0, y = 0, vx = 0, vy = 0;
};

// One flight with a given pitch-over angle. keepSamples = false is used while searching for the angle.
inline AscentProfile Fly(const Mission& m, const Vehicle& v, double kickDeg, bool keepSamples, double sampleStepS = 1.0)
{
    AscentProfile out;
    out.kickDeg = kickDeg;
    const double mu = m.earthMu, radius = m.earthRadiusM, targetAltM = m.leoAltM;
    const double area = Pi * BodyDiameterM * BodyDiameterM / 4.0;
    const double kick = kickDeg * Pi / 180.0;
    const double dt = 0.1;
    State s;
    s.y = radius;
    double mass = v.mass1StartKg, prop = v.prop1Kg, time = 0, nextSample = 0;
    int stage = 1;

    struct Forces
    {
        double ax = 0, ay = 0, thrustAccel = 0, dragAccel = 0, gravityAlongPath = 0, pitch = Pi / 2;
    };
    const auto forces = [&](const State& st, double massNow) {
        Forces f;
        const double r = std::sqrt(st.x * st.x + st.y * st.y);
        const double ux = st.x / r, uy = st.y / r; // local up
        const double ex = uy, ey = -ux;            // local horizontal, downrange
        const double alt = r - radius;
        const double speed = std::sqrt(st.vx * st.vx + st.vy * st.vy);
        const double vUp = st.vx * ux + st.vy * uy, vEast = st.vx * ex + st.vy * ey;
        const double g = mu / (r * r);
        const double pressure = std::exp(-std::max(alt, 0.0) / ScaleHeightM);
        const double thrust = stage == 1 ? v.thrust1VacN - (v.thrust1VacN - v.thrust1SeaN) * pressure : v.thrust2N;
        f.thrustAccel = thrust / massNow;

        double sinPitch = 1.0; // thrust direction above the horizon
        if (stage == 1)
        {
            const double fromVertical = speed > 1.0 ? std::atan2(std::max(vEast, 0.0), vUp) : 0.0;
            if (speed < KickSpeedMps)
                sinPitch = 1.0;                       // vertical rise off the pad
            else if (fromVertical < kick)
                sinPitch = std::cos(kick);            // pitch-over: lean the thrust by the kick angle
            else
                sinPitch = std::cos(fromVertical);    // gravity turn: thrust along the velocity
        }
        else
        {
            // Hold the target altitude: cancel the part of gravity that speed does not yet
            // balance, and steer the vertical speed to zero at the target.
            const double tau = 30.0;
            const double wanted = (targetAltM - alt) / (tau * tau) - 2.0 * vUp / tau + (g - vEast * vEast / r);
            sinPitch = std::clamp(wanted / f.thrustAccel, -0.6, 0.95);
        }
        f.pitch = std::asin(std::clamp(sinPitch, -1.0, 1.0));
        const double cosPitch = std::cos(f.pitch);
        const double tx = ux * sinPitch + ex * cosPitch, ty = uy * sinPitch + ey * cosPitch;
        const double drag = 0.5 * Density(alt) * speed * speed * DragCoefficient * area / massNow;
        f.dragAccel = drag;
        f.gravityAlongPath = speed > 1.0 ? g * vUp / speed : g;
        f.ax = f.thrustAccel * tx - g * ux - (speed > 0 ? drag * st.vx / speed : 0.0);
        f.ay = f.thrustAccel * ty - g * uy - (speed > 0 ? drag * st.vy / speed : 0.0);
        return f;
    };

    const auto record = [&](const Forces& f) {
        const double r = std::sqrt(s.x * s.x + s.y * s.y);
        const double ux = s.x / r, uy = s.y / r;
        const double alt = r - radius, speed = std::sqrt(s.vx * s.vx + s.vy * s.vy);
        const double q = 0.5 * Density(alt) * speed * speed;
        const double heat = HeatFluxWm2(Density(alt), speed);
        if (q > out.maxQPa)
        {
            out.maxQPa = q;
            out.maxQTimeS = time;
            out.maxQAltitudeM = alt;
        }
        out.peakHeatFluxWm2 = std::max(out.peakHeatFluxWm2, heat);
        if (keepSamples && time + 1e-9 >= nextSample)
        {
            nextSample += sampleStepS;
            AscentSample a;
            a.timeS = time;
            a.altitudeM = alt;
            a.downrangeM = radius * std::atan2(s.x, s.y);
            a.speedMps = speed;
            a.pitchDeg = f.pitch * 180.0 / Pi;
            const double vUp = s.vx * ux + s.vy * uy, vEast = s.vx * uy - s.vy * ux;
            a.flightPathDeg = speed > 1.0 ? std::atan2(vUp, vEast) * 180.0 / Pi : 90.0;
            a.massKg = mass;
            a.accelG = f.thrustAccel / m.g0;
            a.dynamicPressurePa = q;
            a.heatFluxWm2 = heat;
            a.stage = stage;
            out.samples.push_back(a);
        }
    };

    const double maxTimeS = 3000.0;
    bool cutoff = false;
    while (time < maxTimeS && !cutoff)
    {
        const Forces now = forces(s, mass);
        record(now);
        // Midpoint (RK2) step; the steering law is smooth enough for dt = 0.1 s.
        const double flow = stage == 1 ? v.flow1 : v.flow2;
        State mid{s.x + s.vx * dt / 2, s.y + s.vy * dt / 2, s.vx + now.ax * dt / 2, s.vy + now.ay * dt / 2};
        const Forces half = forces(mid, mass - flow * dt / 2);
        s = State{s.x + mid.vx * dt, s.y + mid.vy * dt, s.vx + half.ax * dt, s.vy + half.ay * dt};
        mass -= flow * dt;
        prop -= flow * dt;
        time += dt;
        out.deltaVSpentMps += half.thrustAccel * dt;
        out.gravityLossMps += half.gravityAlongPath * dt;
        out.dragLossMps += half.dragAccel * dt;

        const double r = std::sqrt(s.x * s.x + s.y * s.y);
        if (r < radius - 1.0)
            break; // fell back to the ground
        if (stage == 1 && prop <= 0)
        {
            out.stage1BurnoutS = time;
            stage = 2;
            mass = v.mass2StartKg;
            prop = v.prop2Kg;
        }
        else if (stage == 2)
        {
            // Cut off when the horizontal speed alone is enough for a circular orbit here.
            const double horizontal = (s.vx * s.y - s.vy * s.x) / r;
            cutoff = horizontal >= std::sqrt(mu / r) || prop <= 0;
        }
    }
    out.cutoffS = time;
    out.stage2PropLeftKg = stage == 2 ? std::max(prop, 0.0) : 0.0;
    if (keepSamples)
    {
        nextSample = time;
        record(forces(s, mass));
    }

    // Orbit left behind at cutoff.
    const double r = std::sqrt(s.x * s.x + s.y * s.y);
    const double speed2 = s.vx * s.vx + s.vy * s.vy;
    const double energy = speed2 / 2.0 - mu / r;
    const double h = s.x * s.vy - s.y * s.vx;
    if (stage == 2 && energy < 0 && r >= radius)
    {
        const double axis = -mu / (2.0 * energy);
        const double ecc = std::sqrt(std::max(0.0, 1.0 + 2.0 * energy * h * h / (mu * mu)));
        out.periapsisAltM = axis * (1.0 - ecc) - radius;
        out.apoapsisAltM = axis * (1.0 + ecc) - radius;
        out.reachedOrbit = out.periapsisAltM > AtmosphereTopM;
    }
    return out;
}
} // namespace AscentModel

// Flies the launcher to the parking orbit. Tries pitch-over angles and keeps the one whose
// orbit at engine cutoff is closest to the circular parking orbit (mission.leoAltM).
// If no angle reaches orbit, returns the attempt that got closest, with reachedOrbit = false.
inline AscentProfile SimulateAscent(const Catalog& catalog, const Mission& m, const LauncherDesign& design,
                                    double payloadKg, double sampleStepS = 1.0)
{
    const AscentModel::Vehicle vehicle = AscentModel::MakeVehicle(catalog, m, design, payloadKg);
    if (!vehicle.valid || vehicle.thrust1SeaN <= vehicle.mass1StartKg * m.g0)
        return AscentProfile(); // cannot leave the pad
    double bestKick = 0;
    double bestScore = -1e30;
    for (double kick = 0.25; kick <= 25.0; kick += 0.25)
    {
        const AscentProfile trial = AscentModel::Fly(m, vehicle, kick, false);
        // Prefer the orbit closest to the circular target; propellant left breaks ties.
        const double miss = std::fabs(trial.apoapsisAltM - m.leoAltM) + std::fabs(trial.periapsisAltM - m.leoAltM);
        const double score = trial.reachedOrbit ? 1e9 - miss + 0.01 * trial.stage2PropLeftKg : trial.periapsisAltM;
        if (score > bestScore)
        {
            bestScore = score;
            bestKick = kick;
        }
    }
    return AscentModel::Fly(m, vehicle, bestKick, true, sampleStepS);
}

// Linear interpolation of the profile at a mission time.
inline AscentSample SampleAscent(const AscentProfile& profile, double timeS)
{
    if (profile.samples.empty())
        return AscentSample();
    if (timeS <= profile.samples.front().timeS)
        return profile.samples.front();
    if (timeS >= profile.samples.back().timeS)
        return profile.samples.back();
    const auto after = std::lower_bound(profile.samples.begin(), profile.samples.end(), timeS,
        [](const AscentSample& a, double t) { return a.timeS < t; });
    const AscentSample& b = *after;
    const AscentSample& a = *(after - 1);
    const double u = (timeS - a.timeS) / std::max(b.timeS - a.timeS, 1e-9);
    const auto mix = [u](double from, double to) { return from + (to - from) * u; };
    AscentSample out = a;
    out.timeS = timeS;
    out.altitudeM = mix(a.altitudeM, b.altitudeM);
    out.downrangeM = mix(a.downrangeM, b.downrangeM);
    out.speedMps = mix(a.speedMps, b.speedMps);
    out.pitchDeg = mix(a.pitchDeg, b.pitchDeg);
    out.flightPathDeg = mix(a.flightPathDeg, b.flightPathDeg);
    out.massKg = a.stage == b.stage ? mix(a.massKg, b.massKg) : a.massKg;
    out.accelG = a.stage == b.stage ? mix(a.accelG, b.accelG) : a.accelG;
    out.dynamicPressurePa = mix(a.dynamicPressurePa, b.dynamicPressurePa);
    out.heatFluxWm2 = mix(a.heatFluxWm2, b.heatFluxWm2);
    return out;
}
} // namespace Orbital
