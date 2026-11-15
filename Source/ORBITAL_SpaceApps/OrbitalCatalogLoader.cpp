#include "OrbitalCatalogLoader.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
Orbital::Id ToId(const FString& Value)
{
    return TCHAR_TO_UTF8(*Value);
}

struct FReader
{
    FString& Error;

    bool Fail(const TCHAR* Field)
    {
        if (Error.IsEmpty())
            Error = FString::Printf(TEXT("Invalid or missing JSON field: %s"), Field);
        return false;
    }

    const FJsonObject* Object(const FJsonObject& Json, const TCHAR* Field)
    {
        const TSharedPtr<FJsonObject>* Value = nullptr;
        if (Json.TryGetObjectField(Field, Value) && Value && Value->IsValid())
            return Value->Get();
        Fail(Field);
        return nullptr;
    }

    const TArray<TSharedPtr<FJsonValue>>* Array(const FJsonObject& Json, const TCHAR* Field)
    {
        const TArray<TSharedPtr<FJsonValue>>* Value = nullptr;
        if (Json.TryGetArrayField(Field, Value) && Value)
            return Value;
        Fail(Field);
        return nullptr;
    }

    void Id(const FJsonObject& Json, const TCHAR* Field, Orbital::Id& Out, bool Nullable = false)
    {
        if (Nullable && Json.HasTypedField<EJson::Null>(Field))
            return;
        FString Value;
        if (!Json.HasTypedField<EJson::String>(Field) || !Json.TryGetStringField(Field, Value) || Value.IsEmpty())
            Fail(Field);
        else
            Out = ToId(Value);
    }

    void Ids(const FJsonObject& Json, const TCHAR* Field, Orbital::Ids& Out)
    {
        const auto* Values = Array(Json, Field);
        if (!Values)
            return;
        for (const auto& Value : *Values)
        {
            FString IdValue;
            if (!Value || Value->Type != EJson::String || !Value->TryGetString(IdValue) || IdValue.IsEmpty())
            {
                Fail(Field);
                return;
            }
            Out.emplace_back(ToId(IdValue));
        }
    }

    void Number(const FJsonObject& Json, const TCHAR* Field, double& Out, bool Optional = false)
    {
        if (Optional && !Json.HasField(Field))
            return;
        if (!Json.HasTypedField<EJson::Number>(Field) || !Json.TryGetNumberField(Field, Out) ||
            !std::isfinite(Out) || Out < 0.0)
            Fail(Field);
    }

    void Integer(const FJsonObject& Json, const TCHAR* Field, int& Out, bool Optional = false)
    {
        if (Optional && !Json.HasField(Field))
            return;
        double Value = 0;
        Number(Json, Field, Value);
        if (Value > MAX_int32 || std::floor(Value) != Value)
            Fail(Field);
        else
            Out = static_cast<int>(Value);
    }
};

bool ReadPart(const FJsonObject& Json, const Orbital::Id& Mode,
              Orbital::Id& Id, Orbital::Part& Part, FString& Error)
{
    FReader R{Error};
    R.Id(Json, TEXT("id"), Id);
    R.Id(Json, TEXT("slot"), Part.slot);
    R.Ids(Json, TEXT("mission_modes"), Part.missionModes);
    if (Json.HasField(TEXT("objective")))
        R.Id(Json, TEXT("objective"), Part.objective);
    if (Json.HasField(TEXT("fits_bus")))
        R.Ids(Json, TEXT("fits_bus"), Part.fitsBus);
    if (Json.HasField(TEXT("needs_fine_pointing")) &&
        !Json.TryGetBoolField(TEXT("needs_fine_pointing"), Part.needsFinePointing))
        R.Fail(TEXT("needs_fine_pointing"));
    R.Number(Json, TEXT("mass_kg"), Part.massKg, true);
    R.Number(Json, TEXT("cost_mcr"), Part.costMCr, true);
    R.Number(Json, TEXT("power_w"), Part.powerW, true);
    R.Number(Json, TEXT("base_power_w"), Part.basePowerW, true);
    R.Number(Json, TEXT("prop_capacity_kg"), Part.capacityKg, true);
    R.Number(Json, TEXT("isp_s"), Part.ispS, true);
    R.Number(Json, TEXT("max_wet_mass_kg"), Part.maxWetMassKg, true);
    R.Number(Json, TEXT("area_m2"), Part.areaM2, true);
    R.Number(Json, TEXT("capacity_wh"), Part.capacityWh, true);
    R.Number(Json, TEXT("steady_power_w"), Part.steadyPowerW, true);
    R.Number(Json, TEXT("data_gb_per_day"), Part.dataGBPerDay, true);
    R.Number(Json, TEXT("data_rate_mbps"), Part.dataRateMbps, true);
    R.Number(Json, TEXT("storage_gb"), Part.storageGB, true);
    R.Number(Json, TEXT("isp_sl_s"), Part.seaLevelIspS, true);
    R.Number(Json, TEXT("isp_vac_s"), Part.vacuumIspS, true);
    R.Number(Json, TEXT("thrust_sl_n"), Part.seaLevelThrustN, true);
    R.Number(Json, TEXT("thrust_vac_n"), Part.vacuumThrustN, true);
    if (Part.slot == "probe-engine")
    {
        R.Number(Json, TEXT("thrust_n"), Part.thrustN);
        if (Part.thrustN <= 0)
            R.Fail(TEXT("thrust_n (must be positive)"));
    }
    R.Number(Json, TEXT("max_payload_kg"), Part.maxPayloadKg, true);
    R.Integer(Json, TEXT("max_instruments"), Part.maxInstruments, true);
    R.Integer(Json, TEXT("max_count"), Part.maxCount, true);
    for (const auto* Field : {TEXT("mass_kg"), TEXT("cost_mcr")})
        if (!Json.HasField(Field))
            R.Fail(Field);
    const auto& Slot = Part.slot;
    if (Orbital::FitsSlot(Part, Slot, Mode))
    {
        if ((Slot == "probe-engine" && Part.ispS <= 0) ||
            (Slot == "stage1-engine" && (Part.seaLevelIspS <= 0 || Part.vacuumIspS <= 0 ||
                                         Part.seaLevelThrustN <= 0 || Part.maxCount <= 0)) ||
            (Slot == "stage2-engine" && (Part.vacuumIspS <= 0 || Part.vacuumThrustN <= 0)) ||
            ((Slot == "probe-tank" || Slot == "stage1-tank" || Slot == "stage2-tank") &&
                Part.capacityKg <= 0) ||
            (Slot == "instrument" && Part.objective.empty()))
            R.Fail(TEXT("part performance"));
    }
    return Error.IsEmpty();
}

bool ReadMission(const FJsonObject& Json, Orbital::Mission& Mission, FString& Error)
{
    FReader R{Error};
    R.Id(Json, TEXT("mode"), Mission.mode);
    R.Ids(Json, TEXT("required_probe_slots"), Mission.requiredProbeSlots);
    const auto* C = R.Object(Json, TEXT("constants"));
    const auto* A = R.Object(Json, TEXT("assumptions"));
    const auto* Orbits = R.Object(Json, TEXT("orbits"));
    const auto* Ground = R.Object(Json, TEXT("ground"));
    const auto* Budgets = R.Object(Json, TEXT("budget_mcr"));
    const auto* Objectives = R.Object(Json, TEXT("objectives"));
    const auto* Limits = R.Object(Json, TEXT("socket_limits"));
    if (!Error.IsEmpty())
        return false;
    R.Number(*C, TEXT("g0"), Mission.g0);
    R.Number(*C, TEXT("mu_earth"), Mission.earthMu);
    R.Number(*C, TEXT("mu_moon"), Mission.moonMu);
    R.Number(*C, TEXT("r_earth_m"), Mission.earthRadiusM);
    R.Number(*C, TEXT("r_moon_m"), Mission.moonRadiusM);
    R.Number(*C, TEXT("a_moon_m"), Mission.moonSemiMajorAxisM);
    R.Number(*C, TEXT("solar_w_m2"), Mission.solarWm2);
    R.Number(*A, TEXT("leo_alt_m"), Mission.leoAltM);
    R.Number(*A, TEXT("dv_ascent_mps"), Mission.ascentDeltaV);
    R.Number(*A, TEXT("dv_mcc_mps"), Mission.correctionDeltaV);
    R.Number(*A, TEXT("dv_margin"), Mission.deltaVMargin);
    R.Number(*A, TEXT("cell_eff"), Mission.cellEfficiency);
    R.Number(*A, TEXT("point_loss"), Mission.pointingFactor);
    R.Number(*A, TEXT("dod"), Mission.depthOfDischarge);
    R.Number(*A, TEXT("chg_eff"), Mission.chargeEfficiency);
    R.Number(*A, TEXT("min_twr_liftoff"), Mission.minLiftoffTwr);
    R.Number(*A, TEXT("min_twr_stage2"), Mission.minStage2Twr);
    R.Number(*A, TEXT("orbit_visible"), Mission.orbitVisible);
    R.Number(*A, TEXT("ops_base_cost_per_year"), Mission.opsBaseCostPerYear);
    R.Number(*A, TEXT("primary_pass"), Mission.primaryPass);
    if (const auto* Years = R.Array(*A, TEXT("planned_years_options")))
    {
        for (const auto& Value : *Years)
        {
            double Year = 0;
            if (!Value || Value->Type != EJson::Number || !Value->TryGetNumber(Year) ||
                !std::isfinite(Year) || Year <= 0)
                return R.Fail(TEXT("planned_years_options"));
            Mission.plannedYearsOptions.push_back(Year);
        }
    }
    for (const auto& Entry : Objectives->Values)
    {
        const auto* Obj = R.Object(*Objectives, *Entry.Key);
        if (!Obj)
            return false;
        R.Number(*Obj, TEXT("weight"), Mission.objectiveWeights[ToId(FString(*Entry.Key))]);
    }
    for (const auto& Entry : Orbits->Values)
    {
        const auto* Obj = R.Object(*Orbits, *Entry.Key);
        if (!Obj)
            return false;
        auto& Orbit = Mission.orbits[ToId(FString(*Entry.Key))];
        R.Number(*Obj, TEXT("hp_m"), Orbit.periM);
        R.Number(*Obj, TEXT("ha_m"), Orbit.apoM);
        R.Number(*Obj, TEXT("sk_mps_per_year"), Orbit.stationKeepingMpsPerYear);
        const auto* Factors = R.Object(*Obj, TEXT("factor"));
        if (!Factors)
            return false;
        for (const auto& Objective : Objectives->Values)
            R.Number(*Factors, *Objective.Key, Orbit.objectiveFactors[ToId(FString(*Objective.Key))]);
        if (Orbit.stationKeepingMpsPerYear <= 0 || Orbit.apoM < Orbit.periM)
            return R.Fail(TEXT("orbit"));
    }
    for (const auto& Entry : Ground->Values)
    {
        const auto* Obj = R.Object(*Ground, *Entry.Key);
        if (!Obj)
            return false;
        auto& Network = Mission.ground[ToId(FString(*Entry.Key))];
        R.Number(*Obj, TEXT("hours"), Network.hoursPerDay);
        R.Number(*Obj, TEXT("cost_per_year"), Network.costPerYear);
        R.Number(*Obj, TEXT("max_gap_h"), Network.maxGapHours);
    }
    for (const auto& Entry : Budgets->Values)
    {
        auto& Budget = Mission.budgets[ToId(FString(*Entry.Key))];
        R.Number(*Budgets, *Entry.Key, Budget);
        if (Budget <= 0)
            return R.Fail(TEXT("budget_mcr"));
    }
    for (const auto& Entry : Limits->Values)
        R.Integer(*Limits, *Entry.Key, Mission.socketLimits[ToId(FString(*Entry.Key))]);
    if (Mission.g0 <= 0 || Mission.earthMu <= 0 || Mission.moonMu <= 0 ||
        Mission.earthRadiusM <= 0 || Mission.moonRadiusM <= 0 ||
        Mission.moonSemiMajorAxisM <= Mission.earthRadiusM + Mission.leoAltM ||
        Mission.chargeEfficiency <= 0 || Mission.plannedYearsOptions.empty() ||
        Mission.objectiveWeights.empty() || Mission.orbits.empty() || Mission.ground.empty() ||
        Mission.budgets.empty())
        R.Fail(TEXT("mission constants or options"));
    return Error.IsEmpty();
}
} // namespace

bool ReadOrbitalSpacecraft(const FJsonObject& Json, Orbital::SpacecraftDesign& Out, FString& Error)
{
    Error.Reset();
    FReader R{Error};
    Orbital::SpacecraftDesign Design;
    R.Id(Json, TEXT("bus"), Design.bus, true);
    R.Id(Json, TEXT("engine"), Design.engine, true);
    R.Id(Json, TEXT("antenna"), Design.antenna, true);
    R.Id(Json, TEXT("computer"), Design.computer, true);
    R.Id(Json, TEXT("orbit"), Design.orbit);
    R.Id(Json, TEXT("ground"), Design.ground);
    R.Ids(Json, TEXT("tanks"), Design.tanks);
    R.Ids(Json, TEXT("adcs"), Design.adcs);
    R.Ids(Json, TEXT("solar"), Design.solar);
    R.Ids(Json, TEXT("battery"), Design.battery);
    R.Ids(Json, TEXT("instruments"), Design.instruments);
    R.Number(Json, TEXT("prop_kg"), Design.propKg);
    R.Number(Json, TEXT("years"), Design.years);
    if (!Error.IsEmpty())
        return false;
    Out = std::move(Design);
    return true;
}

bool ReadOrbitalLauncher(const FJsonObject& Json, Orbital::LauncherDesign& Out, FString& Error)
{
    Error.Reset();
    FReader R{Error};
    Orbital::LauncherDesign Design;
    R.Id(Json, TEXT("s1_tank"), Design.firstTank, true);
    R.Id(Json, TEXT("s1_engine"), Design.firstEngine, true);
    R.Id(Json, TEXT("s2_tank"), Design.secondTank, true);
    R.Id(Json, TEXT("s2_engine"), Design.secondEngine, true);
    R.Id(Json, TEXT("fairing"), Design.fairing, true);
    R.Integer(Json, TEXT("s1_count"), Design.firstEngineCount);
    if (!Error.IsEmpty())
        return false;
    Out = std::move(Design);
    return true;
}

bool LoadOrbitalCatalog(const FString& Filename, FOrbitalCatalog& Out, FString& Error)
{
    Error.Reset();
    FString Text;
    if (!FFileHelper::LoadFileToString(Text, *Filename))
    {
        Error = FString::Printf(TEXT("Could not read catalog: %s"), *Filename);
        return false;
    }
    TSharedPtr<FJsonObject> Root;
    const auto JsonReader = TJsonReaderFactory<>::Create(Text);
    if (!FJsonSerializer::Deserialize(JsonReader, Root) || !Root)
    {
        Error = JsonReader->GetErrorMessage();
        if (Error.IsEmpty())
            Error = TEXT("Catalog root must be a JSON object.");
        return false;
    }
    FReader R{Error};
    Orbital::Id Version;
    R.Id(*Root, TEXT("schema_version"), Version);
    if (Version != "2.1")
        return R.Fail(TEXT("schema_version (expected 2.1)"));
    const auto* Mission = R.Object(*Root, TEXT("mission"));
    const auto* Parts = R.Array(*Root, TEXT("parts"));
    const auto* Presets = R.Object(*Root, TEXT("reference_builds"));
    if (!Error.IsEmpty())
        return false;

    FOrbitalCatalog Catalog;
    if (!ReadMission(*Mission, Catalog.Mission, Error))
        return false;
    R.Id(*Mission, TEXT("default_orbit"), Catalog.DefaultOrbit);
    if (Catalog.Mission.orbits.count(Catalog.DefaultOrbit) == 0)
        return R.Fail(TEXT("mission.default_orbit"));
    const auto* SocketTree = R.Object(*Mission, TEXT("socket_tree"));
    if (!SocketTree)
        return false;
    for (const auto& Entry : SocketTree->Values)
        R.Ids(*SocketTree, *Entry.Key, Catalog.SocketTree[ToId(FString(*Entry.Key))]);
    if (!Error.IsEmpty())
        return false;
    if (Catalog.SocketTree.count("launch-mount") == 0 || Catalog.SocketTree.count("probe-core") == 0)
        return R.Fail(TEXT("mission.socket_tree roots"));
    for (const auto& Value : *Parts)
    {
        if (!Value || Value->Type != EJson::Object || !Value->AsObject())
            return R.Fail(TEXT("parts[]"));
        Orbital::Id Id;
        Orbital::Part Part;
        if (!ReadPart(*Value->AsObject(), Catalog.Mission.mode, Id, Part, Error))
        {
            Error = FString::Printf(TEXT("Part %s: %s"), UTF8_TO_TCHAR(Id.c_str()), *Error);
            return false;
        }
        if (!Catalog.Parts.emplace(std::move(Id), std::move(Part)).second)
            return R.Fail(TEXT("duplicate part id"));
    }
    const auto Separator = Catalog.Parts.find("sep_system");
    if (Separator == Catalog.Parts.end() ||
        !Orbital::FitsSlot(Separator->second, "separator", Catalog.Mission.mode))
        return R.Fail(TEXT("sep_system"));
    for (const auto& [Id, Part] : Catalog.Parts)
    {
        if (!Part.objective.empty() && Catalog.Mission.objectiveWeights.count(Part.objective) == 0)
            return R.Fail(TEXT("part objective"));
        for (const auto& Bus : Part.fitsBus)
            if (Catalog.Parts.count(Bus) == 0 || Catalog.Parts.at(Bus).slot != "probe-core")
                return R.Fail(TEXT("fits_bus"));
    }
    for (const auto& Entry : Presets->Values)
    {
        const auto* Obj = R.Object(*Presets, *Entry.Key);
        if (!Obj)
            return false;
        if (Obj->HasField(TEXT("bus")))
        {
            Orbital::SpacecraftDesign Design;
            if (!ReadOrbitalSpacecraft(*Obj, Design, Error))
                return false;
            const auto Result = Orbital::Spacecraft(Catalog.Parts, Catalog.Mission, Design);
            if (!Result.block.empty())
                return R.Fail(*Entry.Key);
            Catalog.SpacecraftPresets.Add(FString(*Entry.Key), std::move(Design));
        }
        else
        {
            Orbital::LauncherDesign Design;
            if (!ReadOrbitalLauncher(*Obj, Design, Error))
                return false;
            // Use a compatible bus at zero payload to validate the stored hardware.
            const auto Fairing = Catalog.Parts.find(Design.fairing);
            if (Fairing == Catalog.Parts.end() || Fairing->second.fitsBus.empty())
                return R.Fail(*Entry.Key);
            const auto Result = Orbital::Launcher(Catalog.Parts, Catalog.Mission, Design,
                                                   0.0, Fairing->second.fitsBus.front());
            if (!Result.block.empty())
                return R.Fail(*Entry.Key);
            Catalog.LauncherPresets.Add(FString(*Entry.Key), std::move(Design));
        }
    }
    Catalog.Source = Root;
    Out = std::move(Catalog);
    return true;
}
