#pragma once

#include "OrbitalEngineering_v2.1.h"

namespace Orbital
{
struct DashboardData
{
    bool hasSpacecraftMetrics = false;
    bool hasLauncherMetrics = false;
    bool hasCommunicationsMetrics = false;
    bool launchAllowed = false;
    double dryKg = 0, wetKg = 0, liftoffKg = 0;
    double loadW = 0, generationW = 0, requiredGenerationW = 0;
    double eclipseWh = 0, usableBatteryWh = 0;
    double costMCr = 0, budgetMCr = 0;
    double generatedGBDay = 0, downlinkCapacityGBDay = 0, expectedReturnedGBDay = 0;
    double requiredStorageGB = 0, storageGB = 0;
    double antennaMbps = 0, effectiveContactHoursDay = 0;
    double plannedYears = 0, possibleYears = 0;
    double probeDeltaV = 0, requiredProbeDeltaV = 0;
    double launcherDeltaV = 0, requiredLauncherDeltaV = 0;
    Ids block, risk, warn;
};

inline DashboardData MakeDashboardData(const Catalog& catalog, const Mission& mission,
                                       const SpacecraftDesign& design, const MissionResult& result)
{
    DashboardData data;
    data.hasSpacecraftMetrics = result.sc.dryKg > 0;
    data.hasLauncherMetrics = result.lv.liftoffKg > 0 && data.hasSpacecraftMetrics;
    data.launchAllowed = result.launchAllowed;
    data.dryKg = result.sc.dryKg;
    data.wetKg = result.sc.wetKg;
    data.liftoffKg = result.lv.liftoffKg;
    data.loadW = result.sc.loadW;
    data.generationW = result.sc.generationW;
    data.requiredGenerationW = result.sc.requiredGenerationW;
    data.eclipseWh = result.sc.eclipseWh;
    data.usableBatteryWh = result.sc.usableBatteryWh;
    data.costMCr = result.totalCostMCr;
    data.budgetMCr = result.budgetMCr;
    data.generatedGBDay = result.sc.generatedGBPerDay;
    data.downlinkCapacityGBDay = result.sc.downlinkGBPerDay;
    data.expectedReturnedGBDay = result.sc.generatedGBPerDay * result.sc.dataFraction;
    data.requiredStorageGB = result.sc.requiredStorageGB;
    data.storageGB = result.sc.storageGB;
    const auto antenna = catalog.find(design.antenna);
    const auto ground = mission.ground.find(design.ground);
    if (data.hasSpacecraftMetrics && antenna != catalog.end() && ground != mission.ground.end())
    {
        data.hasCommunicationsMetrics = true;
        data.effectiveContactHoursDay = ground->second.hoursPerDay * mission.orbitVisible;
        data.antennaMbps = result.sc.downlinkGBPerDay > 0 ? antenna->second.dataRateMbps : 0;
    }
    data.plannedYears = design.years;
    data.possibleYears = result.sc.possibleYears;
    data.probeDeltaV = result.sc.deltaV;
    data.requiredProbeDeltaV = result.sc.requiredDeltaV;
    data.launcherDeltaV = result.lv.totalDeltaV;
    data.requiredLauncherDeltaV = result.lv.requiredDeltaV;
    data.block = result.block;
    data.risk = result.risk;
    data.warn = result.warn;
    return data;
}
} // namespace Orbital
