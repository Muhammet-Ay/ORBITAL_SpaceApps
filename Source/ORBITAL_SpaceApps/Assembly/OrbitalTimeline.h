#pragma once

// ORBITAL v2.1 mission timeline (plain C++, no Unreal dependency).
// MISSION_SYSTEMS_SPEC section 10: the outcome is decided by Evaluate() on the launch
// snapshot; this turns that result into the ordered event list that the launch view and
// the mission report replay. It never re-decides the outcome.
//
// Times: missionTimeS is real mission time from liftoff. displayTimeS is the compressed
// playback clock (cruise and operations are accelerated; see TimelineOptions).

#include "OrbitalEngineering_v2.1.h"

#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace Orbital
{
struct TimelineOptions
{
    double launchDisplayS = 75.0;    // liftoff to TLI on screen (decision: 60-90 s). <= 0 plays in real time
    double insertionDisplayS = 4.0;  // insertion burn on screen
    double cruiseDisplayS = 20.0;    // whole cruise, TLI to arrival
    double opsIntroDisplayS = 5.0;   // arrival to first eclipse
    double yearDisplayS = 15.0;      // each year of operations
    // Cruise duration. <= 0 uses the Hohmann half-period from the catalog constants
    // (about 5.0 days). The spec text says "about 4 days" (LRO); set 4*86400 to match it.
    double cruiseS = 0.0;
};

struct TimelineEvent
{
    double missionTimeS = 0, displayTimeS = 0;
    Id phase;   // design, launch, ascent, parking, transfer, cruise, insertion, operations, report
    Id event;   // stable event ID, e.g. "STAGE1_SEPARATION"
    Id vehicle; // "launcher" or "spacecraft": whose delta-v and propellant the values refer to
    Id camera;  // suggested shot for the 3D view
    std::string message;
    std::string altitude; // label; empty where the model has no value
    // Empty where the model cannot compute the value at this event.
    std::optional<double> deltaVUsed, propellantLeftKg, batteryWh, dataReturnedGB, missionYears;
    std::optional<double> burnDurationS; // insertion events only, when thrust is known
    // MISSION_REPORT only:
    Id phaseReached, cause; // cause: the v2.1 code that ended or limited the mission, or empty
    Ids block, risk, warn;  // all codes, for the UI copy lookup
    bool success = false;
    double science = 0, score = 0;
};

using Timeline = std::vector<TimelineEvent>;

namespace TimelineDetail
{
inline std::string Text(const char* format, double a = 0, double b = 0)
{
    char buffer[200];
    std::snprintf(buffer, sizeof(buffer), format, a, b);
    return buffer;
}

inline bool Has(const Ids& codes, const char* code)
{
    return std::find(codes.begin(), codes.end(), code) != codes.end();
}

inline Id Cause(const MissionResult& r)
{
    if (!r.block.empty())
        return r.block.front();
    for (const char* code : {"R_SC_DELTA_V_INSERTION", "R_BATTERY_ECLIPSE", "R_POWER_GENERATION"})
        if (!r.failPhase.empty() && Has(r.risk, code))
            return code;
    for (const char* code : {"R_PRIMARY_INSTRUMENT_MISSING", "R_NO_INSTRUMENT", "R_NEEDS_FINE_POINTING",
                             "R_SC_DELTA_V_LIFETIME", "R_STORAGE"})
        if (Has(r.risk, code))
            return code;
    return r.warn.empty() ? Id{} : r.warn.front();
}
} // namespace TimelineDetail

inline Timeline BuildTimeline(const Catalog& catalog, const Mission& m, const SpacecraftDesign& sc,
                              const LauncherDesign& lv, const MissionResult& r,
                              const TimelineOptions& options = {})
{
    using namespace TimelineDetail;
    Timeline timeline;
    const double yearS = 365.25 * 86400.0;
    Id reached = "design";

    const auto add = [&](double missionS, double displayS, const char* phase, const char* event,
                         const char* vehicle, const char* camera, std::string message) -> TimelineEvent& {
        TimelineEvent e;
        e.missionTimeS = missionS;
        e.displayTimeS = displayS;
        e.phase = phase;
        e.event = event;
        e.vehicle = vehicle;
        e.camera = camera;
        e.message = std::move(message);
        timeline.push_back(std::move(e));
        reached = phase;
        return timeline.back();
    };
    const auto report = [&]() {
        const double missionS = timeline.empty() ? 0.0 : timeline.back().missionTimeS;
        const double displayS = timeline.empty() ? 0.0 : timeline.back().displayTimeS;
        // A failed flight reports the engine's own fail phase (insertion, eclipse).
        const Id phaseReached = r.launchAllowed && !r.failPhase.empty() ? r.failPhase : reached;
        TimelineEvent& e = add(missionS, displayS, "report", "MISSION_REPORT", "spacecraft", "report",
            r.success ? Text("Mission success. Score %.1f.", r.score)
                      : !r.launchAllowed ? std::string("Launch refused at design review.")
                                         : Text("Mission not successful. Score %.1f.", r.score));
        e.phaseReached = phaseReached;
        e.cause = Cause(r);
        e.block = r.block;
        e.risk = r.risk;
        e.warn = r.warn;
        e.success = r.success;
        e.science = r.science;
        e.score = r.score;
    };

    // BLOCK, or a launcher the model could not time: no liftoff.
    if (!r.launchAllowed || !r.lv.parkingS || !r.lv.tliS)
    {
        report();
        return timeline;
    }

    // ---- Launch (real time) ----
    const Part& firstTank = catalog.at(lv.firstTank);
    const Part& secondTank = catalog.at(lv.secondTank);
    const Part& secondEngine = catalog.at(lv.secondEngine);
    const double secondFlow = secondEngine.vacuumThrustN / (secondEngine.vacuumIspS * m.g0);
    const double batteryFull = r.sc.usableBatteryWh;
    const double t1 = r.lv.firstBurnS;
    const double tParking = t1 + *r.lv.parkingS;
    const double tTli = t1 + *r.lv.tliS;
    const double ls = options.launchDisplayS > 0 && tTli > 0 ? options.launchDisplayS / tTli : 1.0;
    const auto launcherEvent = [&](double t, const char* phase, const char* event, const char* camera,
                                   std::string message, double dvUsed, double propLeft,
                                   std::string altitude) {
        TimelineEvent& e = add(t, t * ls, phase, event, "launcher", camera, std::move(message));
        e.deltaVUsed = dvUsed;
        e.propellantLeftKg = std::max(0.0, propLeft);
        e.batteryWh = batteryFull;
        e.dataReturnedGB = 0.0;
        e.altitude = std::move(altitude);
    };
    const std::string leo = Text("%.0f km", m.leoAltM / 1000.0);
    launcherEvent(0.0, "launch", "LIFTOFF", "pad_wide",
        Text("Liftoff. Thrust-to-weight %.2f.", r.lv.firstTwr), 0.0,
        firstTank.capacityKg + secondTank.capacityKg, "0 km");
    launcherEvent(t1, "ascent", "STAGE1_SEPARATION", "stage_separation",
        Text("Stage 1 cutoff and separation at T+%.0f s.", t1), r.lv.firstDeltaV, secondTank.capacityKg, {});
    launcherEvent(t1, "ascent", "FAIRING_RELEASE", "fairing",
        "Fairing released. Stage 2 ignition.", r.lv.firstDeltaV, secondTank.capacityKg, {});
    launcherEvent(tParking, "parking", "PARKING_ORBIT", "orbit_earth",
        "Parking orbit reached at " + leo + ".", std::max(m.ascentDeltaV, r.lv.firstDeltaV),
        secondTank.capacityKg - secondFlow * *r.lv.parkingS, leo);
    launcherEvent(tTli, "transfer", "TLI_COMPLETE", "departure",
        "Trans-lunar injection complete. Spacecraft separated.", r.lv.requiredDeltaV,
        secondTank.capacityKg - secondFlow * *r.lv.tliS, leo);

    // ---- Cruise (compressed) ----
    const Part& engine = catalog.at(sc.engine);
    const Orbit& orbit = m.orbits.at(sc.orbit);
    const double exhaust = engine.ispS * m.g0;
    const double margin = 1.0 + m.deltaVMargin;
    // The engine charges the margin on every burn (possibleYears = (dv/margin - fixed)/sk),
    // so propellant use below does the same and runs out exactly at possibleYears.
    const auto propAfter = [&](double dvSpent) {
        return std::max(0.0, r.sc.wetKg * std::exp(-dvSpent / exhaust) - r.sc.dryKg);
    };
    const double transferAxis = (m.earthRadiusM + m.leoAltM + m.moonSemiMajorAxisM) / 2.0;
    const double cruiseS = options.cruiseS > 0 ? options.cruiseS
        : Pi * std::sqrt(transferAxis * transferAxis * transferAxis / m.earthMu);
    const double dTli = tTli * ls;
    const auto spacecraftEvent = [&](double t, double display, const char* phase, const char* event,
                                     const char* camera, std::string message, double dvSpent,
                                     double battery, double dataGB, double years) -> TimelineEvent& {
        TimelineEvent& e = add(t, display, phase, event, "spacecraft", camera, std::move(message));
        e.deltaVUsed = std::min(dvSpent, r.sc.deltaV);
        e.propellantLeftKg = propAfter(dvSpent);
        e.batteryWh = battery;
        e.dataReturnedGB = dataGB;
        e.missionYears = years;
        return e;
    };
    const double dvMcc = m.correctionDeltaV * margin;
    spacecraftEvent(tTli + cruiseS / 2.0, dTli + options.cruiseDisplayS / 2.0, "cruise", "MIDCOURSE_CORRECTION",
        "deep_space", Text("Mid-course correction. Cruise takes %.1f days.", cruiseS / 86400.0),
        dvMcc, batteryFull, 0.0, 0.0).altitude = "Earth-Moon transfer";

    // ---- Insertion ----
    const double tArrival = tTli + cruiseS;
    const double dArrival = dTli + options.cruiseDisplayS;
    const double dvFixed = r.sc.fixedDeltaV * margin;
    const std::string orbitLabel = orbit.periM == orbit.apoM
        ? Text("%.0f km lunar orbit", orbit.periM / 1000.0)
        : Text("%.0f x %.0f km lunar orbit", orbit.periM / 1000.0, orbit.apoM / 1000.0);
    // Burn duration = propellant the burn consumes / mass flow (Part::thrustN / exhaust speed).
    // The consumed propellant is the same figure the propellant readout shows: mass after the
    // mid-course burn, insertion delta-v with the model's margin. A failed burn ends at an empty tank.
    const double loiDeltaV = r.sc.fixedDeltaV - m.correctionDeltaV;
    const double flow = engine.thrustN > 0 ? engine.thrustN / exhaust : 0.0;
    const bool insertionFails = r.failPhase == "insertion";
    std::optional<double> burnS;
    if (flow > 0)
        burnS = (propAfter(dvMcc) - (insertionFails ? 0.0 : propAfter(dvFixed))) / flow;
    spacecraftEvent(tArrival, dArrival, "insertion", "INSERTION_BURN_START", "moon_approach",
        burnS ? Text("Arrived at the Moon. Insertion burn started: %.0f m/s needed.", loiDeltaV)
              : Text("Arrived at the Moon. Insertion burn: %.0f m/s needed.", loiDeltaV),
        dvMcc, batteryFull, 0.0, 0.0).burnDurationS = burnS;
    timeline.back().altitude = "Lunar approach";
    const double tOrbit = tArrival + burnS.value_or(0.0);
    const double dOrbit = dArrival + options.insertionDisplayS;
    if (insertionFails)
    {
        TimelineEvent& e = spacecraftEvent(tOrbit, dOrbit, "insertion", "INSERTION_FAILED", "moon_flyby",
            Text("Insertion burn ran out of propellant: %.0f of %.0f m/s. Flew past the Moon.",
                 r.sc.deltaV, dvFixed),
            r.sc.deltaV, batteryFull, 0.0, 0.0);
        e.altitude = "Lunar flyby";
        e.burnDurationS = burnS;
        report();
        return timeline;
    }
    {
        TimelineEvent& e = spacecraftEvent(tOrbit, dOrbit, "insertion", "LUNAR_ORBIT_INSERTION", "lunar_orbit",
            burnS ? Text("Insertion burn complete after %.0f min. In ", *burnS / 60.0) + orbitLabel + "."
                  : "Insertion burn complete. In " + orbitLabel + ".",
            dvFixed, batteryFull, 0.0, 0.0);
        e.altitude = orbitLabel;
        e.burnDurationS = burnS;
    }

    // ---- Operations (compressed) ----
    const double periodS = r.sc.periodMin * 60.0, eclipseS = r.sc.eclipseMin * 60.0;
    const double tEclipse = tOrbit + (periodS - eclipseS);
    const double dEclipse = dOrbit + options.opsIntroDisplayS;
    if (r.failPhase == "eclipse")
    {
        const bool battery = Has(r.risk, "R_BATTERY_ECLIPSE");
        // Battery too small: it empties part-way through the eclipse. Arrays too small:
        // the model fails the mission in the first eclipse; the failure is placed at its start.
        const double intoEclipseS = battery && r.sc.loadW > 0
            ? std::min(eclipseS, r.sc.usableBatteryWh / r.sc.loadW * 3600.0) : 0.0;
        spacecraftEvent(tEclipse + intoEclipseS, dEclipse, "operations", "ECLIPSE_FAILED", "lunar_night",
            battery ? Text("Battery empty in the first eclipse: %.0f Wh usable, %.0f Wh needed.",
                           r.sc.usableBatteryWh, r.sc.eclipseWh)
                    : Text("Solar arrays cannot recharge the battery: %.0f W generated, %.0f W needed.",
                           r.sc.generationW, r.sc.requiredGenerationW),
            dvFixed, battery ? 0.0 : batteryFull, 0.0, 0.0).altitude = orbitLabel;
        report();
        return timeline;
    }
    spacecraftEvent(tEclipse + eclipseS, dEclipse, "operations", "FIRST_ECLIPSE", "lunar_night",
        Text("First eclipse passed: %.0f min in shadow, battery used %.0f Wh.", r.sc.eclipseMin, r.sc.eclipseWh),
        dvFixed, batteryFull - r.sc.eclipseWh, 0.0, 0.0).altitude = orbitLabel;

    const double endYears = sc.years * r.sc.lifeFraction;
    const double gbPerYear = r.sc.generatedGBPerDay * r.sc.dataFraction * 365.25;
    const double skPerYear = orbit.stationKeepingMpsPerYear * margin;
    const auto opsEvent = [&](double years, const char* event, std::string message) {
        spacecraftEvent(tOrbit + years * yearS, dEclipse + years * options.yearDisplayS, "operations", event,
            "lunar_orbit", std::move(message), dvFixed + skPerYear * years,
            batteryFull - r.sc.eclipseWh, gbPerYear * years, years).altitude = orbitLabel;
    };
    for (int year = 1; year < endYears; ++year)
        opsEvent(year, "YEAR_COMPLETE", Text("Year %.0f complete: %.1f GB returned.", year, gbPerYear * year));
    if (r.sc.lifeFraction < 1.0)
        opsEvent(endYears, "PROPELLANT_DEPLETED",
            Text("Station-keeping propellant ran out after %.2f of %.0f planned years.", endYears, sc.years));
    else
        opsEvent(endYears, "OPERATIONS_COMPLETE",
            Text("Planned %.0f-year mission complete: %.1f GB returned.", sc.years, gbPerYear * endYears));
    report();
    return timeline;
}

// One line per event. Used for logs and for the determinism test.
inline std::string Describe(const TimelineEvent& e)
{
    const auto value = [](const std::optional<double>& v) {
        return v ? TimelineDetail::Text("%.6f", *v) : std::string("-");
    };
    std::string out = TimelineDetail::Text("T+%.3f s (show %.3f) ", e.missionTimeS, e.displayTimeS);
    out += e.phase + " " + e.event + " [" + e.vehicle + "/" + e.camera + "] alt=" + e.altitude +
        " dv=" + value(e.deltaVUsed) + " prop=" + value(e.propellantLeftKg) + " batt=" + value(e.batteryWh) +
        " data=" + value(e.dataReturnedGB) + " years=" + value(e.missionYears) + " burn=" + value(e.burnDurationS) + " | " + e.message;
    if (e.event == "MISSION_REPORT")
    {
        out += " reached=" + e.phaseReached + " cause=" + e.cause + (e.success ? " success" : " fail");
        for (const Ids* codes : {&e.block, &e.risk, &e.warn})
            for (const Id& code : *codes)
                out += " " + code;
    }
    return out;
}
} // namespace Orbital

namespace Orbital
{
// Skip button: the display time to jump to from `displayNow`. Before TLI it jumps to TLI
// (skips the launch); afterwards to the next event. Returns the last event's time at the end.
inline double SkipTarget(const Timeline& timeline, double displayNow)
{
    if (timeline.empty())
        return 0.0;
    for (const TimelineEvent& e : timeline)
        if (e.event == "TLI_COMPLETE" && e.displayTimeS > displayNow)
            return e.displayTimeS;
    for (const TimelineEvent& e : timeline)
        if (e.displayTimeS > displayNow)
            return e.displayTimeS;
    return timeline.back().displayTimeS;
}
} // namespace Orbital
