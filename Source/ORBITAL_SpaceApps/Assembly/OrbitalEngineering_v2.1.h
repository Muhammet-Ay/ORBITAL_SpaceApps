// TEMPORARY COPY on isa/assembly. Identical to Docs/CLAUDE_RESEARCH/OrbitalEngineering_v2.1.h plus Part::thrustN
// (stand-in for the field Ram added on ram/engineering). Delete this file when ram/engineering is merged
// and include his header instead.
#pragma once

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Orbital
{
using Id = std::string;
using Ids = std::vector<Id>;

// Adapter contract: load schema "2.1" from CLAUDE_RESEARCH/PART_CATALOG.json.
// Validate required keys, units, numeric ranges and reference IDs at load time.
// Part::costMCr maps to cost_mcr; capacityKg maps to prop_capacity_kg.
struct Part
{
    Id slot, objective;
    double massKg = 0, costMCr = 0, powerW = 0, basePowerW = 0;
    double capacityKg = 0, ispS = 0, maxWetMassKg = 0;
    double areaM2 = 0, capacityWh = 0, dataGBPerDay = 0, dataRateMbps = 0, storageGB = 0;
    double seaLevelIspS = 0, vacuumIspS = 0, seaLevelThrustN = 0, vacuumThrustN = 0;
    double maxPayloadKg = 0;
    double thrustN = 0; // LOCAL STAND-IN for Ram's Part::thrustN (catalog thrust_n)
    int maxInstruments = 0, maxCount = 0;
    bool needsFinePointing = false;
    Ids fitsBus, missionModes;
};

using Catalog = std::unordered_map<Id, Part>;

struct Orbit
{
    double periM = 0, apoM = 0, stationKeepingMpsPerYear = 0;
    std::unordered_map<Id, double> objectiveFactors;
};

struct GroundNetwork
{
    double hoursPerDay = 0, costPerYear = 0, maxGapHours = 0;
};

// Read these values from mission.constants, mission.assumptions and the
// associated maps in the catalog. Lookups below assume a validated catalog.
struct Mission
{
    Id mode;
    double g0 = 0, earthMu = 0, moonMu = 0, earthRadiusM = 0, moonRadiusM = 0;
    double moonSemiMajorAxisM = 0, solarWm2 = 0;
    double leoAltM = 0, ascentDeltaV = 0, correctionDeltaV = 0, deltaVMargin = 0;
    double cellEfficiency = 0, pointingFactor = 0, depthOfDischarge = 0, chargeEfficiency = 0;
    double minLiftoffTwr = 0, minStage2Twr = 0, orbitVisible = 0;
    double opsBaseCostPerYear = 0, primaryPass = 0;
    std::unordered_map<Id, Orbit> orbits;
    std::unordered_map<Id, GroundNetwork> ground;
    std::unordered_map<Id, double> budgets, objectiveWeights;
    std::unordered_map<Id, int> socketLimits;
    Ids requiredProbeSlots;
    std::vector<double> plannedYearsOptions;
};

struct SpacecraftDesign
{
    Id bus, engine, antenna, computer, orbit, ground;
    Ids tanks, adcs, solar, battery, instruments;
    double propKg = 0, years = 1;
};

struct LauncherDesign
{
    Id firstTank, firstEngine, secondTank, secondEngine, fairing;
    int firstEngineCount = 1;
};

struct SpacecraftResult
{
    double dryKg = 0, propKg = 0, wetKg = 0, costMCr = 0, opsCostMCr = 0;
    double deltaV = 0, requiredDeltaV = 0, fixedDeltaV = 0, possibleYears = 0, lifeFraction = 0;
    double loadW = 0, generationW = 0, requiredGenerationW = 0;
    double eclipseWh = 0, usableBatteryWh = 0, periodMin = 0, eclipseMin = 0;
    double generatedGBPerDay = 0, downlinkGBPerDay = 0, dataFraction = 1;
    double requiredStorageGB = 0, storageGB = 0;
    Ids block, risk, warn, missing, working;
};

struct LauncherResult
{
    double liftoffKg = 0, firstDeltaV = 0, secondDeltaV = 0, totalDeltaV = 0;
    double requiredDeltaV = 0, firstTwr = 0, secondTwr = 0, costMCr = 0;
    double firstBurnS = 0, secondBurnS = 0;
    // Elapsed burn time after upper-stage ignition. Empty if unreachable.
    std::optional<double> parkingS, tliS;
    Ids block;
};

struct MissionResult
{
    SpacecraftResult sc;
    LauncherResult lv;
    double totalCostMCr = 0, budgetMCr = 0, science = 0, score = 0;
    bool launchAllowed = false, primaryOk = false, success = false;
    Id failPhase;
    Ids block, risk, warn;
    std::unordered_map<Id, double> perObjective;
};

inline constexpr double Pi = 3.14159265358979323846;

inline double VisViva(double mu, double radius, double semiMajorAxis)
{
    return std::sqrt(mu * (2.0 / radius - 1.0 / semiMajorAxis));
}

inline double DeltaV(double ispS, double startKg, double endKg, double g0)
{
    return ispS * g0 * std::log(startKg / endKg);
}

inline double TliDeltaV(const Mission& m)
{
    const double start = m.earthRadiusM + m.leoAltM;
    const double axis = (start + m.moonSemiMajorAxisM) / 2.0;
    return VisViva(m.earthMu, start, axis) - std::sqrt(m.earthMu / start);
}

inline bool FitsSlot(const Part& part, const Id& slot, const Id& mode)
{
    return part.slot == slot &&
        std::find(part.missionModes.begin(), part.missionModes.end(), mode) != part.missionModes.end();
}

inline SpacecraftResult Spacecraft(const Catalog& catalog, const Mission& m,
                                   const SpacecraftDesign& design)
{
    SpacecraftResult result;
    const std::vector<std::pair<Id, Ids>> slots{
        {"probe-core", {design.bus}}, {"probe-engine", {design.engine}},
        {"antenna", {design.antenna}}, {"avionics", {design.computer}},
        {"probe-tank", design.tanks}, {"adcs", design.adcs}, {"solar", design.solar},
        {"battery", design.battery}, {"instrument", design.instruments},
    };
    if (!std::isfinite(design.propKg) || design.propKg < 0 ||
        m.orbits.count(design.orbit) == 0 || m.ground.count(design.ground) == 0 ||
        std::find(m.plannedYearsOptions.begin(), m.plannedYearsOptions.end(), design.years)
            == m.plannedYearsOptions.end())
    {
        result.block = {"E_UNKNOWN_PART"};
        return result;
    }
    for (const auto& [slot, ids] : slots)
    {
        for (const Id& id : ids)
        {
            if (id.empty() && (slot == "probe-core" || slot == "probe-engine" ||
                               slot == "antenna" || slot == "avionics"))
                continue;
            const auto part = catalog.find(id);
            if (part == catalog.end() || !FitsSlot(part->second, slot, m.mode))
            {
                result.block = {"E_UNKNOWN_PART"};
                return result;
            }
        }
    }
    for (const Id& required : m.requiredProbeSlots)
    {
        const auto slot = std::find_if(slots.begin(), slots.end(),
            [&](const auto& entry) { return entry.first == required; });
        if (slot == slots.end() || slot->second.empty() || slot->second.front().empty())
            result.missing.push_back(required);
    }
    const auto hasAdcs = [&](const Id& id) {
        return std::find(design.adcs.begin(), design.adcs.end(), id) != design.adcs.end();
    };
    if (!hasAdcs("rcs") && std::find(result.missing.begin(), result.missing.end(), "adcs") == result.missing.end())
        result.missing.emplace_back("adcs");
    if (!result.missing.empty())
    {
        result.block = {"E_MISSING_REQUIRED_PART"};
        return result;
    }
    for (const auto& [slot, ids] : slots)
    {
        const auto limit = m.socketLimits.find(slot);
        if (limit != m.socketLimits.end() && ids.size() > static_cast<std::size_t>(limit->second))
        {
            result.block.emplace_back("E_SOCKET_LIMIT");
            break;
        }
    }
    const Orbit& orbit = m.orbits.at(design.orbit);
    const GroundNetwork& ground = m.ground.at(design.ground);
    const Part& bus = catalog.at(design.bus);
    const Part& antenna = catalog.at(design.antenna);
    const Part& computer = catalog.at(design.computer);

    result.dryKg = bus.massKg;
    result.costMCr = bus.costMCr;
    result.loadW = bus.basePowerW;
    const auto addPart = [&](const Part& part) {
        result.dryKg += part.massKg;
        result.costMCr += part.costMCr;
        result.loadW += part.powerW;
    };
    addPart(catalog.at(design.engine));
    addPart(antenna);
    addPart(computer);
    for (const Ids* group : {&design.tanks, &design.adcs, &design.solar,
                            &design.battery, &design.instruments})
    {
        for (const Id& id : *group)
            addPart(catalog.at(id));
    }
    const auto sum = [&](const Ids& ids, double Part::*field) {
        double total = 0;
        for (const Id& id : ids)
            total += catalog.at(id).*field;
        return total;
    };
    const double capacityKg = sum(design.tanks, &Part::capacityKg);
    result.propKg = design.propKg;
    result.wetKg = result.dryKg + result.propKg;
    result.deltaV = DeltaV(catalog.at(design.engine).ispS, result.wetKg, result.dryKg, m.g0);
    const double earthTransferAxis = (m.earthRadiusM + m.leoAltM + m.moonSemiMajorAxisM) / 2.0;
    const double vInf = std::sqrt(m.earthMu / m.moonSemiMajorAxisM)
        - VisViva(m.earthMu, m.moonSemiMajorAxisM, earthTransferAxis);
    const double perilune = m.moonRadiusM + orbit.periM;
    const double axis = m.moonRadiusM + (orbit.periM + orbit.apoM) / 2.0;
    const double loi = std::sqrt(vInf * vInf + 2.0 * m.moonMu / perilune)
        - VisViva(m.moonMu, perilune, axis);
    const double margin = 1.0 + m.deltaVMargin;
    result.fixedDeltaV = m.correctionDeltaV + loi;
    result.requiredDeltaV = (result.fixedDeltaV + orbit.stationKeepingMpsPerYear * design.years) * margin;
    result.possibleYears = std::max(0.0,
        (result.deltaV / margin - result.fixedDeltaV) / orbit.stationKeepingMpsPerYear);
    result.lifeFraction = std::min(1.0, result.possibleYears / design.years);

    result.generationW = sum(design.solar, &Part::areaM2) * m.solarWm2 * m.cellEfficiency * m.pointingFactor;
    const double periodS = 2.0 * Pi * std::sqrt(axis * axis * axis / m.moonMu);
    const double eclipseS = std::asin(m.moonRadiusM / axis) / Pi * periodS;
    result.periodMin = periodS / 60.0;
    result.eclipseMin = eclipseS / 60.0;
    result.eclipseWh = result.loadW * eclipseS / 3600.0;
    result.usableBatteryWh = sum(design.battery, &Part::capacityWh) * m.depthOfDischarge;
    result.requiredGenerationW = result.loadW + result.eclipseWh * 3600.0
        / ((periodS - eclipseS) * m.chargeEfficiency);

    const bool fine = hasAdcs("reaction_wheels");
    const bool antennaOk = fine || !antenna.needsFinePointing;
    for (const Id& id : design.instruments)
    {
        if (fine || !catalog.at(id).needsFinePointing)
            result.working.push_back(id);
    }
    result.generatedGBPerDay = sum(result.working, &Part::dataGBPerDay);
    const double contactS = ground.hoursPerDay * 3600.0 * m.orbitVisible;
    if (antennaOk)
        result.downlinkGBPerDay = antenna.dataRateMbps * 1e6 * contactS / 8.0 / 1e9;
    result.requiredStorageGB = result.generatedGBPerDay * ground.maxGapHours / 24.0;
    result.storageGB = computer.storageGB;
    const double storedFraction = result.requiredStorageGB <= result.storageGB
        ? 1.0 : result.storageGB / result.requiredStorageGB;
    if (result.generatedGBPerDay > 0)
        result.dataFraction = std::min({1.0, storedFraction, result.downlinkGBPerDay / result.generatedGBPerDay});
    result.opsCostMCr = (m.opsBaseCostPerYear + ground.costPerYear) * design.years;

    const auto failIf = [&](bool failed, const char* code) {
        if (failed)
            result.block.emplace_back(code);
    };
    const int instrumentCount = static_cast<int>(design.instruments.size());
    failIf(result.propKg > capacityKg + 1e-9, "E_PROP_OVER_CAPACITY");
    failIf(result.wetKg > bus.maxWetMassKg, "E_BUS_OVERLOAD");
    failIf(instrumentCount > bus.maxInstruments, "E_BUS_INSTRUMENT_SLOTS");
    failIf(instrumentCount > computer.maxInstruments, "E_COMPUTER_INSTRUMENT_LIMIT");
    const auto riskIf = [&](bool risky, const char* code) {
        if (risky)
            result.risk.emplace_back(code);
    };
    riskIf(design.instruments.empty(), "R_NO_INSTRUMENT");
    riskIf(result.working.size() < design.instruments.size() || !antennaOk, "R_NEEDS_FINE_POINTING");
    if (result.deltaV / margin < result.fixedDeltaV)
        result.risk.emplace_back("R_SC_DELTA_V_INSERTION");
    else
        riskIf(result.deltaV < result.requiredDeltaV, "R_SC_DELTA_V_LIFETIME");
    riskIf(result.generationW < result.requiredGenerationW, "R_POWER_GENERATION");
    riskIf(result.usableBatteryWh < result.eclipseWh, "R_BATTERY_ECLIPSE");
    riskIf(result.storageGB < result.requiredStorageGB, "R_STORAGE");
    if (result.dataFraction < 1.0)
        result.warn.emplace_back("W_DATA_BOTTLENECK");
    return result;
}

inline LauncherResult Launcher(const Catalog& catalog, const Mission& m, const LauncherDesign& design,
                               double payloadKg, const Id& busId)
{
    LauncherResult result;
    const std::pair<const Id&, const char*> parts[]{
        {design.firstTank, "stage1-tank"}, {design.firstEngine, "stage1-engine"},
        {design.secondTank, "stage2-tank"}, {design.secondEngine, "stage2-engine"},
        {design.fairing, "fairing"},
    };
    for (const auto& [id, slot] : parts)
    {
        if (id.empty())
        {
            result.block = {"E_MISSING_REQUIRED_PART"};
            return result;
        }
    }
    for (const auto& [id, slot] : parts)
    {
        const auto part = catalog.find(id);
        if (part == catalog.end() || !FitsSlot(part->second, slot, m.mode))
        {
            result.block = {"E_UNKNOWN_PART"};
            return result;
        }
    }
    const Part& firstTank = catalog.at(design.firstTank);
    const Part& firstEngine = catalog.at(design.firstEngine);
    const Part& secondTank = catalog.at(design.secondTank);
    const Part& secondEngine = catalog.at(design.secondEngine);
    const Part& fairing = catalog.at(design.fairing);
    const Part& separator = catalog.at("sep_system");
    const int count = design.firstEngineCount;
    if (count < 1 || count > firstEngine.maxCount)
    {
        result.block = {"E_ENGINE_COUNT"};
        return result;
    }
    const double firstDry = firstTank.massKg + count * firstEngine.massKg + separator.massKg;
    const double secondDry = secondTank.massKg + secondEngine.massKg + separator.massKg;
    const double secondWet = secondDry + secondTank.capacityKg + payloadKg;
    result.liftoffKg = firstDry + firstTank.capacityKg + fairing.massKg + secondWet;
    const double firstIsp = (firstEngine.seaLevelIspS + firstEngine.vacuumIspS) / 2.0;
    result.firstDeltaV = DeltaV(firstIsp, result.liftoffKg, result.liftoffKg - firstTank.capacityKg, m.g0);
    result.secondDeltaV = DeltaV(secondEngine.vacuumIspS, secondWet, secondWet - secondTank.capacityKg, m.g0);
    result.totalDeltaV = result.firstDeltaV + result.secondDeltaV;
    result.requiredDeltaV = m.ascentDeltaV + TliDeltaV(m);
    result.firstTwr = count * firstEngine.seaLevelThrustN / (result.liftoffKg * m.g0);
    result.secondTwr = secondEngine.vacuumThrustN / (secondWet * m.g0);
    result.costMCr = firstTank.costMCr + count * firstEngine.costMCr + secondTank.costMCr
        + secondEngine.costMCr + fairing.costMCr + 2.0 * separator.costMCr;

    const auto failIf = [&](bool failed, const char* code) {
        if (failed)
            result.block.emplace_back(code);
    };
    failIf(result.firstTwr < m.minLiftoffTwr, "E_TWR_LIFTOFF");
    failIf(result.secondTwr < m.minStage2Twr, "E_TWR_STAGE2");
    failIf(result.totalDeltaV < result.requiredDeltaV, "E_LV_DELTA_V");
    failIf(payloadKg > fairing.maxPayloadKg, "E_FAIRING_MASS");
    failIf(std::find(fairing.fitsBus.begin(), fairing.fitsBus.end(), busId) == fairing.fitsBus.end(),
        "E_FAIRING_SIZE");

    const double firstFlow = count * firstEngine.seaLevelThrustN / (firstIsp * m.g0);
    const double secondExhaustSpeed = secondEngine.vacuumIspS * m.g0;
    const double secondFlow = secondEngine.vacuumThrustN / secondExhaustSpeed;
    result.firstBurnS = firstTank.capacityKg / firstFlow;
    result.secondBurnS = secondTank.capacityKg / secondFlow;
    const auto timeToDeltaV = [&](double target) -> std::optional<double> {
        if (target <= 0)
            return 0.0;
        const double usedKg = secondWet * (1.0 - std::exp(-target / secondExhaustSpeed));
        if (usedKg > secondTank.capacityKg + 1e-9)
            return std::nullopt;
        return usedKg / secondFlow;
    };
    result.parkingS = timeToDeltaV(m.ascentDeltaV - result.firstDeltaV);
    result.tliS = timeToDeltaV(result.requiredDeltaV - result.firstDeltaV);
    return result;
}

inline MissionResult Evaluate(const Catalog& catalog, const Mission& m, const SpacecraftDesign& sc,
                              const LauncherDesign& lv, const Id& primary, const Id& difficulty = "normal")
{
    MissionResult result;
    const auto budget = m.budgets.find(difficulty);
    const auto primaryWeight = m.objectiveWeights.find(primary);
    if (budget == m.budgets.end() || primaryWeight == m.objectiveWeights.end())
    {
        result.block = {"E_UNKNOWN_PART"};
        return result;
    }
    result.sc = Spacecraft(catalog, m, sc);
    result.lv = Launcher(catalog, m, lv, result.sc.wetKg, sc.bus);
    result.totalCostMCr = result.sc.costMCr + result.sc.opsCostMCr + result.lv.costMCr;
    result.budgetMCr = budget->second;
    result.block = result.sc.block;
    result.block.insert(result.block.end(), result.lv.block.begin(), result.lv.block.end());
    if (result.totalCostMCr > result.budgetMCr)
        result.block.emplace_back("E_BUDGET");
    result.risk = result.sc.risk;
    result.warn = result.sc.warn;
    result.launchAllowed = result.block.empty();
    if (!result.launchAllowed)
        return result;

    std::set<Id> observed;
    for (const Id& id : result.sc.working)
        observed.insert(catalog.at(id).objective);
    if (observed.count(primary) == 0)
        result.risk.emplace_back("R_PRIMARY_INSTRUMENT_MISSING");
    for (const Id& risk : result.risk)
    {
        if (risk == "R_SC_DELTA_V_INSERTION")
            result.failPhase = "insertion";
        else if (risk == "R_POWER_GENERATION" || risk == "R_BATTERY_ECLIPSE")
            result.failPhase = "eclipse";
        if (!result.failPhase.empty())
            return result;
    }

    const Orbit& orbit = m.orbits.at(sc.orbit);
    for (const auto& [objective, weight] : m.objectiveWeights)
    {
        const double value = observed.count(objective)
            ? weight * orbit.objectiveFactors.at(objective) * result.sc.dataFraction * result.sc.lifeFraction
            : 0.0;
        result.perObjective.emplace(objective, value);
        result.science += value;
    }
    result.primaryOk = result.perObjective.at(primary) >= m.primaryPass * primaryWeight->second;
    result.success = result.primaryOk;
    result.score = 0.8 * result.science;
    if (result.success)
        result.score += 20.0 * std::max(0.0, 1.0 - result.totalCostMCr / result.budgetMCr);
    return result;
}
} // namespace Orbital
