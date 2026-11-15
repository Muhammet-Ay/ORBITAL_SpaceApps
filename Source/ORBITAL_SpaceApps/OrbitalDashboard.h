#pragma once

#include "OrbitalEngineering_v2.1.h"

#include <array>
#include <iterator>

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
enum class DashboardState { Green, Amber, Red };
enum class ReviewSeverity { Block, Risk, Warn };
enum class ReviewRole { MissionLead, PropulsionEngineer, ScienceOfficer, PowerAndCommsEngineer };
enum class Resource : unsigned { Mass, Power, Budget, Data, Communications, Time, DeltaV, Count };

struct ReviewIssue
{
    Id code;
    ReviewSeverity severity;
    ReviewRole owner;
    // Both keys select the validation row by code in the UI copy table.
    // The UI reads the Cause or Fix column respectively.
    Id causeKey, fixKey;
};

struct ResourceRow
{
    Id tooltipKey;
    Id measure;
    Id unit;
    std::optional<double> current, limit, marginPercent;
    DashboardState state = DashboardState::Amber;
    Ids activeCodes; // Each code is also the Cause and Fix table row key.
};

struct DashboardReview
{
    std::array<ResourceRow, static_cast<unsigned>(Resource::Count)> rows;
    std::vector<ReviewIssue> issues;
};

inline DashboardReview MakeDashboardReview(const MissionResult& result)
{
    using Res = Resource;
    using Role = ReviewRole;
    const auto mask = [](Res resource) { return 1u << static_cast<unsigned>(resource); };
    struct Assignment { const char* code; Role owner; unsigned rows; };
    const Assignment assignments[]{
        {"E_UNKNOWN_PART", Role::MissionLead, mask(Res::Mass)},
        {"E_MISSING_REQUIRED_PART", Role::MissionLead, mask(Res::Mass)},
        {"E_SOCKET_LIMIT", Role::PropulsionEngineer, mask(Res::Mass)},
        {"E_PROP_OVER_CAPACITY", Role::PropulsionEngineer, mask(Res::Mass) | mask(Res::DeltaV)},
        {"E_BUS_OVERLOAD", Role::PropulsionEngineer, mask(Res::Mass)},
        {"E_BUS_INSTRUMENT_SLOTS", Role::ScienceOfficer, mask(Res::Mass)},
        {"E_COMPUTER_INSTRUMENT_LIMIT", Role::ScienceOfficer, mask(Res::Data)},
        {"E_ENGINE_COUNT", Role::PropulsionEngineer, mask(Res::DeltaV)},
        {"E_TWR_LIFTOFF", Role::PropulsionEngineer, mask(Res::Mass)},
        {"E_TWR_STAGE2", Role::PropulsionEngineer, mask(Res::Mass)},
        {"E_LV_DELTA_V", Role::PropulsionEngineer, mask(Res::DeltaV)},
        {"E_FAIRING_MASS", Role::PropulsionEngineer, mask(Res::Mass)},
        {"E_FAIRING_SIZE", Role::PropulsionEngineer, mask(Res::Mass)},
        {"E_BUDGET", Role::MissionLead, mask(Res::Budget)},
        {"R_SC_DELTA_V_INSERTION", Role::PropulsionEngineer, mask(Res::DeltaV) | mask(Res::Time)},
        {"R_SC_DELTA_V_LIFETIME", Role::MissionLead, mask(Res::DeltaV) | mask(Res::Time)},
        {"R_POWER_GENERATION", Role::PowerAndCommsEngineer, mask(Res::Power)},
        {"R_BATTERY_ECLIPSE", Role::PowerAndCommsEngineer, mask(Res::Power)},
        {"R_NEEDS_FINE_POINTING", Role::PowerAndCommsEngineer, mask(Res::Communications) | mask(Res::Data)},
        {"R_STORAGE", Role::ScienceOfficer, mask(Res::Data)},
        {"R_NO_INSTRUMENT", Role::ScienceOfficer, mask(Res::Data)},
        {"R_PRIMARY_INSTRUMENT_MISSING", Role::ScienceOfficer, mask(Res::Data)},
        {"W_DATA_BOTTLENECK", Role::PowerAndCommsEngineer, mask(Res::Data)},
    };

    DashboardReview review;
    const auto row = [&](Res resource) -> ResourceRow& {
        return review.rows[static_cast<unsigned>(resource)];
    };
    row(Res::Mass).tooltipKey = "Mass";
    row(Res::Power).tooltipKey = "Power";
    row(Res::Budget).tooltipKey = "Budget";
    row(Res::Data).tooltipKey = "Data";
    row(Res::Communications).tooltipKey = "Communications";
    row(Res::Time).tooltipKey = "Time";
    row(Res::DeltaV).tooltipKey = "Delta-v";

    // For resources with multiple constraints, display the pair with least headroom.
    // Positive percentages always mean headroom, with the limit as denominator.
    const auto compare = [&](Res resource, const char* measure, const char* unit,
                             double current, double limit, bool maximum = false) {
        auto& item = row(resource);
        const auto margin = limit > 0
            ? std::optional<double>{100.0 * (maximum ? limit - current : current - limit) / limit}
            : std::nullopt;
        if (item.current && (!margin || (item.marginPercent && *item.marginPercent <= *margin)))
            return;
        item.measure = measure;
        item.unit = unit;
        item.current = current;
        item.limit = limit;
        item.marginPercent = margin;
        if (margin && *margin >= 10.0)
            item.state = DashboardState::Green;
        else
            item.state = DashboardState::Amber;
    };

    const auto& sc = result.sc;
    const auto& lv = result.lv;
    if (sc.dryKg > 0)
    {
        compare(Res::Mass, "Bus wet mass", "kg", sc.wetKg, sc.busMassLimitKg, true);
        if (lv.liftoffKg > 0)
            compare(Res::Mass, "Fairing payload mass", "kg", sc.wetKg, lv.fairingMassLimitKg, true);
        compare(Res::Power, "Power generation", "W", sc.generationW, sc.requiredGenerationW);
        compare(Res::Power, "Usable eclipse battery", "Wh", sc.usableBatteryWh, sc.eclipseWh);
        compare(Res::Data, "Onboard storage", "GB", sc.storageGB, sc.requiredStorageGB);
        compare(Res::Data, "Daily downlink capacity", "GB/day", sc.downlinkGBPerDay, sc.generatedGBPerDay);
        const double requiredMbps = sc.effectiveContactHoursDay > 0
            ? sc.generatedGBPerDay * 8000.0 / (sc.effectiveContactHoursDay * 3600.0) : 0.0;
        if (sc.effectiveContactHoursDay > 0 || sc.generatedGBPerDay == 0)
            compare(Res::Communications, "Effective antenna rate", "Mbps", sc.effectiveAntennaMbps, requiredMbps);
        else
            compare(Res::Communications, "Daily downlink capacity", "GB/day", sc.downlinkGBPerDay, sc.generatedGBPerDay);
        compare(Res::Time, "Supported mission lifetime", "years", sc.possibleYears, sc.plannedYears);
        compare(Res::DeltaV, "Spacecraft delta-v", "m/s", sc.deltaV, sc.requiredDeltaV);
        if (lv.liftoffKg > 0)
            compare(Res::DeltaV, "Launcher delta-v", "m/s", lv.totalDeltaV, lv.requiredDeltaV);
    }
    // An incomplete design may carry a partial cost. Leave that comparison unavailable.
    if (sc.dryKg > 0 && lv.liftoffKg > 0 && result.budgetMCr > 0)
        compare(Res::Budget, "Total mission cost", "M credits", result.totalCostMCr, result.budgetMCr, true);

    const auto appendIssues = [&](const Ids& codes, ReviewSeverity severity) {
        for (const auto& code : codes)
        {
            if (std::any_of(review.issues.begin(), review.issues.end(),
                [&](const ReviewIssue& issue) { return issue.code == code; }))
                continue;
            const auto* entry = std::find_if(std::begin(assignments), std::end(assignments),
                [&](const Assignment& assignment) { return code == assignment.code; });
            const bool known = entry != std::end(assignments);
            review.issues.push_back({code, severity, known ? entry->owner : Role::MissionLead, code, code});
            unsigned affected = known ? entry->rows : 0u;
            if (code == "W_DATA_BOTTLENECK" && sc.downlinkGBPerDay < sc.generatedGBPerDay)
                affected |= mask(Res::Communications);
            for (unsigned i = 0; i < review.rows.size(); ++i)
            {
                if ((affected & (1u << i)) == 0)
                    continue;
                auto& item = review.rows[i];
                item.activeCodes.push_back(code);
                if (severity == ReviewSeverity::Block)
                    item.state = DashboardState::Red;
                else if (severity == ReviewSeverity::Risk && item.state != DashboardState::Red)
                    item.state = DashboardState::Amber;
            }
        }
    };
    appendIssues(result.block, ReviewSeverity::Block);
    appendIssues(result.risk, ReviewSeverity::Risk);
    appendIssues(result.warn, ReviewSeverity::Warn);
    return review;
}
} // namespace Orbital
