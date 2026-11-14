// Socket-rule tests for OrbitalAssembly.h. Plain C++17, no Unreal.
// Build: g++ -std=c++17 -O2 assembly_tests.cpp -o assembly_tests && ./assembly_tests [exports.json]

#include "OrbitalBuiltInCatalog.h"

#include <cstdio>
#include <fstream>
#include <limits>

using namespace Orbital;

namespace
{
int g_checks = 0, g_failures = 0;

void Check(bool ok, const char* what, int line)
{
    ++g_checks;
    if (!ok)
    {
        ++g_failures;
        std::printf("  FAIL line %d: %s\n", line, what);
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)
#define CHECK_CODE(result, expected) Check((result).code == (expected), #result " == " #expected, __LINE__)

const Catalog kCatalog = OrbitalData::MakeCatalog();
const Mission kMission = OrbitalData::MakeMission();

Assembly NewAssembly()
{
    return Assembly(kCatalog, kMission, OrbitalData::MakeRules());
}

bool SameDesign(const SpacecraftDesign& a, const SpacecraftDesign& b)
{
    return a.bus == b.bus && a.engine == b.engine && a.antenna == b.antenna && a.computer == b.computer &&
        a.orbit == b.orbit && a.ground == b.ground && a.tanks == b.tanks && a.adcs == b.adcs &&
        a.solar == b.solar && a.battery == b.battery && a.instruments == b.instruments &&
        a.propKg == b.propKg && a.years == b.years;
}

bool SameDesign(const LauncherDesign& a, const LauncherDesign& b)
{
    return a.firstTank == b.firstTank && a.firstEngine == b.firstEngine &&
        a.firstEngineCount == b.firstEngineCount && a.secondTank == b.secondTank &&
        a.secondEngine == b.secondEngine && a.fairing == b.fairing;
}

// Attach in a deliberately scrambled order (bus first, everything else mixed).
bool BuildSpacecraft(Assembly& a, const SpacecraftDesign& d)
{
    bool ok = a.Attach(d.bus).ok;
    for (const Id& id : d.instruments)
        ok = a.Attach(id).ok && ok;
    for (const Id& id : d.solar)
        ok = a.Attach(id).ok && ok;
    ok = a.Attach(d.computer).ok && ok;
    for (const Id& id : d.adcs)
        ok = a.Attach(id).ok && ok;
    ok = a.Attach(d.antenna).ok && ok;
    for (const Id& id : d.battery)
        ok = a.Attach(id).ok && ok;
    ok = a.Attach(d.engine).ok && ok;
    for (const Id& id : d.tanks)
        ok = a.Attach(id).ok && ok;
    ok = a.SetOrbit(d.orbit).empty() && ok;
    ok = a.SetYears(d.years).empty() && ok;
    ok = a.SetGround(d.ground).empty() && ok;
    ok = a.SetPropellant(d.propKg).empty() && ok;
    return ok;
}

bool BuildLauncher(Assembly& a, const LauncherDesign& d)
{
    bool ok = a.Attach(d.firstTank).ok;
    ok = a.Attach(d.firstEngine).ok && ok;
    ok = a.SetStage1EngineCount(d.firstEngineCount).empty() && ok;
    ok = a.Attach("sep_system").ok && ok;
    ok = a.Attach(d.secondTank).ok && ok;
    ok = a.Attach(d.fairing).ok && ok;
    ok = a.Attach(d.secondEngine).ok && ok;
    return ok;
}

std::string g_exports = "{";
void Export(const char* name, const std::string& json)
{
    if (g_exports.size() > 1)
        g_exports += ",\n";
    g_exports += "\"";
    g_exports += name;
    g_exports += "\":";
    g_exports += json;
}

void TestReferenceBuildsRoundTrip()
{
    std::printf("reference builds round trip\n");
    for (const auto& [name, preset] : OrbitalData::SpacecraftPresets())
    {
        Assembly a = NewAssembly();
        CHECK(BuildSpacecraft(a, preset));
        CHECK(SameDesign(a.ToSpacecraftDesign(), preset));
        Export(name.c_str(), a.Snapshot().spacecraftJson);
    }
    for (const auto& [name, preset] : OrbitalData::LauncherPresets())
    {
        Assembly a = NewAssembly();
        CHECK(BuildLauncher(a, preset));
        CHECK(SameDesign(a.ToLauncherDesign(), preset));
        Export(name.c_str(), a.Snapshot().launcherJson);
    }
}

void TestScoresFromAssembly()
{
    std::printf("engine scores from assembled designs\n");
    const auto sc = OrbitalData::SpacecraftPresets();
    const auto lv = OrbitalData::LauncherPresets();
    struct Case
    {
        const char *probe, *rocket, *primary, *difficulty;
        double score;
    };
    for (const Case& c : {Case{"SC_PROSPECTOR", "LV_LIGHT", "O1_ICE", "normal", 33.51111111111111},
                          Case{"SC_SURVEYOR", "LV_MID", "O1_ICE", "normal", 80.77777777777777}})
    {
        Assembly a = NewAssembly();
        CHECK(BuildSpacecraft(a, sc.at(c.probe)));
        CHECK(BuildLauncher(a, lv.at(c.rocket)));
        const AssemblySnapshot s = a.Snapshot();
        const MissionResult r = Evaluate(kCatalog, kMission, s.spacecraft, s.launcher, c.primary, c.difficulty);
        CHECK(r.success);
        CHECK(std::fabs(r.score - c.score) < 1e-9);
        std::printf("  %s + %s -> score %.4f\n", c.probe, c.rocket, r.score);
    }

    // TC-N: duplicate solar and battery parts, MGA, hard budget (fixture score 57.1697).
    Assembly a = NewAssembly();
    SpacecraftDesign n = sc.at("SC_SURVEYOR");
    n.solar = {"solar_s", "solar_s"};
    n.battery = {"battery_s", "battery_s"};
    n.antenna = "antenna_mga";
    n.instruments = {"inst_neutron", "inst_altimeter", "inst_radiometer"};
    n.propKg = 240;
    CHECK(BuildSpacecraft(a, n));
    CHECK(BuildLauncher(a, lv.at("LV_MID")));
    const AssemblySnapshot s = a.Snapshot();
    CHECK(SameDesign(s.spacecraft, n));
    const MissionResult r = Evaluate(kCatalog, kMission, s.spacecraft, s.launcher, "O1_ICE", "hard");
    CHECK(std::fabs(r.score - 57.16970546984574) < 1e-9);
    std::printf("  TC-N assembled -> score %.4f\n", r.score);
    Export("TC_N_SC", s.spacecraftJson);
}

void TestPlacementRules()
{
    std::printf("placement rules\n");
    Assembly a = NewAssembly();
    CHECK_CODE(a.Attach("no_such_part"), "E_UNKNOWN_PART");
    CHECK_CODE(a.Attach("solar_s"), A_PARENT_MISSING);           // no bus yet
    CHECK_CODE(a.Attach("s1_engine_kl190"), A_PARENT_MISSING);   // no stage 1 tank yet
    CHECK_CODE(a.Attach("command_module"), A_MODE_MISMATCH);     // crewed-only bus
    CHECK_CODE(a.Attach("les"), A_MODE_MISMATCH);                // crewed-only escape tower
    CHECK(a.Attach("bus_small").ok);
    CHECK_CODE(a.Attach("life_support"), A_MODE_MISMATCH);
    CHECK_CODE(a.AttachAt("solar", 0, "battery_s"), A_SLOT_MISMATCH);
    CHECK_CODE(a.AttachAt("warp-drive", 0, "solar_s"), A_UNKNOWN_SOCKET);
    CHECK(a.Attach("s1_tank_200").ok);
    CHECK_CODE(a.Attach("s2_tank_7"), A_PARENT_MISSING);         // separator comes first
    CHECK(a.Attach("sep_system").ok);
    CHECK(a.Attach("s2_tank_7").ok);
    CHECK_CODE(a.Attach("s2_tank_21"), "E_SOCKET_LIMIT");        // single socket already used
    const AttachResult replaced = a.AttachAt("stage2-tank", 0, "s2_tank_21");
    CHECK(replaced.ok && a.Get("stage2-tank", 0) == "s2_tank_21");
}

void TestSocketLimits()
{
    std::printf("socket limits\n");
    Assembly a = NewAssembly();
    CHECK(a.Attach("bus_medium").ok);
    CHECK(a.Attach("sc_tank_s").ok && a.Attach("sc_tank_m").ok);
    CHECK_CODE(a.Attach("sc_tank_l"), "E_SOCKET_LIMIT");          // probe-tank max 2
    for (int i = 0; i < 4; ++i)
        CHECK(a.Attach("solar_s").ok);
    CHECK_CODE(a.Attach("solar_l"), "E_SOCKET_LIMIT");            // solar max 4
    CHECK(a.Attach("battery_s").ok && a.Attach("battery_l").ok);
    CHECK_CODE(a.Attach("battery_s"), "E_SOCKET_LIMIT");          // battery max 2
    CHECK(a.Attach("rcs").ok && a.Attach("reaction_wheels").ok);
    CHECK_CODE(a.Attach("rcs"), "E_SOCKET_LIMIT");                // adcs max 2
    CHECK(a.Attach("antenna_lga").ok);
    CHECK_CODE(a.Attach("antenna_hga"), "E_SOCKET_LIMIT");        // antenna max 1
    CHECK(a.Attach("obc_basic").ok);
    CHECK_CODE(a.Attach("obc_advanced"), "E_SOCKET_LIMIT");       // avionics max 1
    CHECK(a.Attach("sc_engine_mono").ok);
    CHECK_CODE(a.Attach("sc_engine_biprop"), "E_SOCKET_LIMIT");   // engine max 1
    CHECK_CODE(a.AttachAt("solar", 4, "solar_s"), "E_SOCKET_LIMIT");
    CHECK_CODE(a.AttachAt("solar", -1, "solar_s"), "E_SOCKET_LIMIT");

    // Instrument decks come from the bus: 5 on bus_medium, named deck_a..deck_e.
    const char* five[] = {"inst_neutron", "inst_altimeter", "inst_radiometer", "inst_camera", "inst_radar"};
    for (int i = 0; i < 5; ++i)
    {
        const AttachResult r = a.Attach(five[i]);
        CHECK(r.ok && r.socketName == Assembly::SocketName("instrument", i));
    }
    CHECK(a.Get("instrument", 4) == "inst_radar" && Assembly::SocketName("instrument", 4) == "deck_e");
    CHECK_CODE(a.Attach("inst_neutron"), "E_BUS_INSTRUMENT_SLOTS");

    Assembly small = NewAssembly();
    CHECK(small.Attach("bus_small").ok);
    CHECK(small.Capacity("instrument") == 2);
    CHECK(small.Attach("inst_neutron").ok && small.Attach("inst_camera").ok);
    CHECK_CODE(small.Attach("inst_radar"), "E_BUS_INSTRUMENT_SLOTS");
    CHECK_CODE(small.AttachAt("instrument", 2, "inst_radar"), "E_BUS_INSTRUMENT_SLOTS");
}

void TestCascadeRemoval()
{
    std::printf("cascade removal\n");
    const auto sc = OrbitalData::SpacecraftPresets();
    const auto lv = OrbitalData::LauncherPresets();
    Assembly a = NewAssembly();
    CHECK(BuildSpacecraft(a, sc.at("SC_SURVEYOR")));
    CHECK(BuildLauncher(a, lv.at("LV_MID")));

    // Middle solar removal keeps the other sockets where they are (stable names).
    Assembly b = NewAssembly();
    CHECK(b.Attach("bus_medium").ok);
    CHECK(b.Attach("solar_s").ok && b.Attach("solar_l").ok && b.Attach("solar_s").ok);
    CHECK(b.Remove("solar", 1) == 1);
    CHECK(b.Get("solar", 2) == "solar_s" && b.Get("solar", 1).empty());
    CHECK((b.ToSpacecraftDesign().solar == Ids{"solar_s", "solar_s"}));
    const AttachResult refill = b.Attach("solar_l");
    CHECK(refill.ok && refill.index == 1);                       // first free socket is reused

    // Removing the fairing leaves the spacecraft (own root by default).
    CHECK(a.Remove("fairing", 0) == 1);
    CHECK(a.ToSpacecraftDesign().bus == "bus_medium");

    // Removing stage 1 tank clears the rest of the rocket: engine, separator, stage 2, its engine.
    CHECK(a.Remove("stage1-tank", 0) == 5);
    const LauncherDesign empty = a.ToLauncherDesign();
    CHECK(empty.firstTank.empty() && empty.firstEngine.empty() && empty.secondTank.empty() &&
          empty.secondEngine.empty() && empty.fairing.empty() && empty.firstEngineCount == 2);

    // Replacing the bus removes everything attached to the old one (10 children of SC_SURVEYOR).
    const SpacecraftDesign before = a.ToSpacecraftDesign();
    CHECK(before.instruments.size() == 5);
    CHECK(a.AttachAt("probe-core", 0, "bus_small").ok);
    const SpacecraftDesign after = a.ToSpacecraftDesign();
    CHECK(after.bus == "bus_small" && after.tanks.empty() && after.instruments.empty() &&
          after.engine.empty() && after.antenna.empty() && after.computer.empty());
    CHECK(after.orbit == "LPO_50" && after.propKg == 260);       // mission plan is kept
    Export("BUS_SWAPPED_SC", ToJson(after));

    // Removing the bus empties the spacecraft completely.
    CHECK(a.Attach("solar_s").ok);
    CHECK(a.RemovePart("bus_small") == 2);
    CHECK(a.ToSpacecraftDesign().bus.empty() && a.ToSpacecraftDesign().solar.empty());

    // Literal tree option: fairing removal takes the spacecraft with it.
    AssemblyRules literal = OrbitalData::MakeRules();
    literal.probeRequiresFairing = true;
    Assembly c(kCatalog, kMission, literal);
    CHECK_CODE(c.Attach("bus_small"), A_PARENT_MISSING);
    CHECK(BuildLauncher(c, lv.at("LV_LIGHT")));
    CHECK(c.Attach("bus_small").ok && c.Attach("rcs").ok);
    CHECK(c.Remove("fairing", 0) == 3);
    CHECK(c.ToSpacecraftDesign().bus.empty());
}

void TestEngineCount()
{
    std::printf("stage 1 engine count\n");
    Assembly a = NewAssembly();
    CHECK(a.Attach("s1_tank_200").ok && a.Attach("s1_engine_kl190").ok);
    CHECK(a.SetStage1EngineCount(0) == "E_ENGINE_COUNT");
    CHECK(a.SetStage1EngineCount(4) == "E_ENGINE_COUNT");        // kl190 max_count 3
    CHECK(a.SetStage1EngineCount(3).empty());
    CHECK(a.AttachAt("stage1-engine", 0, "s1_engine_kl380").ok);  // max_count 2: count clamps
    CHECK(a.ToLauncherDesign().firstEngineCount == 2);
    CHECK(a.SetStage1EngineCount(3) == "E_ENGINE_COUNT");
}

void TestMissionPlan()
{
    std::printf("mission plan\n");
    Assembly a = NewAssembly();
    const SpacecraftDesign d = a.ToSpacecraftDesign();
    CHECK(d.orbit == "LPO_100" && d.years == 1 && d.ground == "single_station" && d.propKg == 0);
    CHECK(a.SetOrbit("LPO_7") == A_INVALID_PLAN);
    CHECK(a.SetOrbit("ELL_30x216").empty());
    CHECK(a.SetYears(3) == A_INVALID_PLAN);
    CHECK(a.SetYears(2).empty());
    CHECK(a.SetGround("mars_relay") == A_INVALID_PLAN);
    CHECK(a.SetGround("network_3").empty());
    CHECK(a.SetPropellant(-1) == A_INVALID_PLAN);
    CHECK(a.SetPropellant(std::numeric_limits<double>::quiet_NaN()) == A_INVALID_PLAN);
    CHECK(a.SetPropellant(212.5).empty());
    CHECK(a.ToSpacecraftDesign().propKg == 212.5);
}

void TestEmptyAndDeterminism()
{
    std::printf("empty assembly and determinism\n");
    Assembly a = NewAssembly();
    const AssemblySnapshot s = a.Snapshot();
    Export("EMPTY_SC", s.spacecraftJson);
    Export("EMPTY_LV", s.launcherJson);
    CHECK(s.spacecraftJson == "{\"bus\":null,\"tanks\":[],\"engine\":null,\"prop_kg\":0,\"adcs\":[],\"solar\":[],"
                              "\"battery\":[],\"antenna\":null,\"computer\":null,\"instruments\":[],"
                              "\"orbit\":\"LPO_100\",\"years\":1,\"ground\":\"single_station\"}");
    CHECK(s.launcherJson == "{\"s1_tank\":null,\"s1_engine\":null,\"s1_count\":1,\"s2_tank\":null,"
                            "\"s2_engine\":null,\"fairing\":null}");
    // The engine must refuse an empty design without crashing.
    const MissionResult r = Evaluate(kCatalog, kMission, s.spacecraft, s.launcher, "O1_ICE");
    CHECK(!r.launchAllowed);
    CHECK(std::find(r.block.begin(), r.block.end(), "E_MISSING_REQUIRED_PART") != r.block.end());

    Assembly x = NewAssembly(), y = NewAssembly();
    const auto sc = OrbitalData::SpacecraftPresets().at("SC_SURVEYOR");
    CHECK(BuildSpacecraft(x, sc) && BuildSpacecraft(y, sc));
    CHECK(x.Snapshot().spacecraftJson == y.Snapshot().spacecraftJson);
    CHECK(x.Snapshot().spacecraftJson == x.Snapshot().spacecraftJson);
}
} // namespace

int main(int argc, char** argv)
{
    TestReferenceBuildsRoundTrip();
    TestScoresFromAssembly();
    TestPlacementRules();
    TestSocketLimits();
    TestCascadeRemoval();
    TestEngineCount();
    TestMissionPlan();
    TestEmptyAndDeterminism();
    if (argc > 1)
    {
        std::ofstream(argv[1]) << g_exports << "}\n";
    }
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
