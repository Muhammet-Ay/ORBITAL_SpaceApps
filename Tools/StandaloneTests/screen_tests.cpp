// Tests for OrbitalAssemblyScreen.h, including the first Unreal demo script (TC-A).
// Build: g++ -std=c++17 -O2 screen_tests.cpp -o screen_tests && ./screen_tests
#include "OrbitalAssemblyScreen.h"
#include "OrbitalBuiltInCatalog.h"
#include <cstdio>
using namespace Orbital;
namespace {
int g_checks = 0, g_failures = 0;
void Check(bool ok, const char* what, int line)
{ ++g_checks; if (!ok) { ++g_failures; std::printf("  FAIL line %d: %s\n", line, what); } }
#define CHECK(e) Check((e), #e, __LINE__)
const Catalog kCatalog = OrbitalData::MakeCatalog();
const Mission kMission = OrbitalData::MakeMission();
AssemblyScreen NewScreen() { return AssemblyScreen(kCatalog, kMission, OrbitalData::MakeRules()); }
const SocketView* FindView(const std::vector<SocketView>& v, const Id& name)
{ for (const auto& s : v) if (s.name == name) return &s; return nullptr; }

void DemoTcA()
{
    std::printf("demo: TC-A through the screen\n");
    AssemblyScreen s = NewScreen();
    int notified = 0; std::string lastJson;
    s.SetListener([&](const AssemblySnapshot& snap) { ++notified; lastJson = snap.spacecraftJson; });

    // Probe first, no rocket anywhere: allowed.
    for (const char* id : {"bus_small", "sc_tank_s", "sc_engine_mono", "rcs", "solar_s", "battery_s",
                           "antenna_lga", "obc_basic", "inst_neutron"})
        CHECK(s.Attach(id).ok);
    CHECK(s.SetPropellant(130).ok && s.SetOrbit("LPO_100").ok);
    CHECK(!s.ProbeOnRocket() && s.FairingIssues().empty());
    for (const char* id : {"s1_tank_200", "s1_engine_kl190", "sep_system", "s2_tank_7", "s2_engine_hl10", "fairing_s"})
        CHECK(s.Attach(id).ok);
    CHECK(s.SetStage1EngineCount(2).ok);
    CHECK(notified == 18 && s.Revision() == 18 && lastJson == s.Snapshot().spacecraftJson);
    CHECK(s.ProbeOnRocket() && s.FairingIssues().empty());
    const MissionResult r = Evaluate(kCatalog, kMission, s.Snapshot().spacecraft, s.Snapshot().launcher, "O1_ICE");
    CHECK(r.success && std::fabs(r.score - 33.51111111111111) < 1e-9);
    std::printf("  TC-A score %.4f\n", r.score);

    // Wrong socket is refused, with a reason, and nothing changes.
    const std::string before = s.Snapshot().spacecraftJson;
    const ChangeResult wrong = s.Replace("solar", 1, "battery_s");
    CHECK(!wrong.ok && wrong.code == A_SLOT_MISMATCH && !wrong.reason.empty());
    CHECK(s.Revision() == 18 && notified == 18 && s.Snapshot().spacecraftJson == before);
    std::printf("  battery into solar socket -> %s: %s\n", wrong.code.c_str(), wrong.reason.c_str());

    // Removing the main part removes what is attached to it; rocket untouched.
    const ChangeResult gone = s.Remove("probe-core", 0);
    CHECK(gone.ok && gone.removed == 9);
    CHECK(s.Snapshot().spacecraft.bus.empty() && s.Snapshot().spacecraft.tanks.empty() &&
          s.Snapshot().spacecraft.instruments.empty());
    CHECK(s.Snapshot().launcher.fairing == "fairing_s" && s.Revision() == 19);
    std::printf("  remove bus -> %d parts removed\n", gone.removed);
    CHECK(!s.Remove("probe-core", 0).ok && s.Revision() == 19);
}

void SocketViews()
{
    std::printf("socket views\n");
    AssemblyScreen s = NewScreen();
    auto v = s.Sockets();
    CHECK(FindView(v, "escape_1") == nullptr && FindView(v, "life-support_1") == nullptr); // crewed only
    CHECK(FindView(v, "deck_a") == nullptr);                    // no bus, no decks
    CHECK(FindView(v, "solar_1") && FindView(v, "solar_1")->locked);
    CHECK(FindView(v, "probe-core_1") && !FindView(v, "probe-core_1")->locked);
    CHECK(FindView(v, "stage1-tank_1") && !FindView(v, "stage1-tank_1")->locked);
    CHECK(FindView(v, "stage2-tank_1")->locked);
    v = s.Sockets("solar_s");
    CHECK(!FindView(v, "solar_1")->canAccept && FindView(v, "solar_1")->rejectCode == A_PARENT_MISSING);
    CHECK(s.Attach("bus_medium").ok && s.Attach("solar_l").ok);
    v = s.Sockets("solar_s");
    int accept = 0;
    for (const auto& x : v) accept += x.canAccept ? 1 : 0;
    CHECK(accept == 4);                                         // the four solar sockets only
    CHECK(FindView(v, "solar_1")->partId == "solar_l" && FindView(v, "solar_1")->wouldReplace);
    CHECK(!FindView(v, "solar_2")->wouldReplace && FindView(v, "battery_1")->rejectCode == A_SLOT_MISMATCH);
    CHECK(!FindView(v, "battery_1")->rejectReason.empty());
    CHECK(FindView(v, "deck_e") != nullptr);
    v = s.Sockets("command_module");
    CHECK(FindView(v, "probe-core_1")->rejectCode == A_MODE_MISMATCH);
    CHECK(s.Sockets().size() == 6 + 1 + 2 + 1 + 2 + 4 + 2 + 1 + 1 + 5);

    // The dry run agrees with the real operation for every part and every socket.
    const auto sc = OrbitalData::SpacecraftPresets().at("SC_SURVEYOR");
    AssemblyScreen full = NewScreen();
    for (const Id& id : {sc.bus, sc.engine, sc.antenna, sc.computer}) CHECK(full.Attach(id).ok);
    for (const Ids* g : {&sc.tanks, &sc.adcs, &sc.solar, &sc.battery, &sc.instruments})
        for (const Id& id : *g) CHECK(full.Attach(id).ok);
    CHECK(full.Attach("s1_tank_200").ok);
    int compared = 0, mismatched = 0;
    for (const AssemblyScreen* base : {&s, &full})
        for (const auto& [id, part] : kCatalog)
            for (const SocketView& view : base->Sockets(id))
            {
                AssemblyScreen copy = *base;
                const ChangeResult r = copy.Replace(view.socket, view.index, id);
                ++compared;
                if (r.ok != view.canAccept || r.code != view.rejectCode) ++mismatched;
            }
    CHECK(compared > 1000 && mismatched == 0);
    std::printf("  dry run vs real: %d combinations, %d mismatches\n", compared, mismatched);
}

void ReplaceAndFairing()
{
    std::printf("replace and fairing check\n");
    AssemblyScreen s = NewScreen();
    const auto sc = OrbitalData::SpacecraftPresets().at("SC_SURVEYOR");
    for (const Id& id : {sc.bus, sc.engine, sc.antenna, sc.computer}) CHECK(s.Attach(id).ok);
    for (const Ids* g : {&sc.tanks, &sc.adcs, &sc.solar, &sc.battery, &sc.instruments})
        for (const Id& id : *g) CHECK(s.Attach(id).ok);
    CHECK(s.SetPropellant(260).ok && s.SetOrbit("LPO_50").ok);
    const ChangeResult swap = s.Replace("antenna", 0, "antenna_mga");
    CHECK(swap.ok && swap.removed == 1 && s.Snapshot().spacecraft.antenna == "antenna_mga");
    CHECK(s.Replace("antenna", 0, "antenna_hga").ok);
    CHECK(!s.Attach("antenna_lga").ok);                         // Tak on a full type is refused
    // Small fairing meets the medium probe: both fairing codes, as in fixture TC-C.
    for (const char* id : {"s1_tank_200", "s1_engine_kl190", "sep_system", "s2_tank_7", "s2_engine_hl10"})
        CHECK(s.Attach(id).ok);
    CHECK(s.FairingIssues().empty());
    CHECK(s.Attach("fairing_s").ok);                            // attaching is allowed; the check reports
    CHECK((s.FairingIssues() == Ids{"E_FAIRING_MASS", "E_FAIRING_SIZE"}));
    CHECK(s.SetStage1EngineCount(2).ok);
    const MissionResult r = Evaluate(kCatalog, kMission, s.Snapshot().spacecraft, s.Snapshot().launcher, "O1_ICE");
    CHECK((r.block == Ids{"E_LV_DELTA_V", "E_FAIRING_MASS", "E_FAIRING_SIZE"}));
    CHECK(s.Replace("fairing", 0, "fairing_l").ok && s.FairingIssues().empty());
    CHECK(s.Snapshot().spacecraft.bus == "bus_medium");         // fairing swap keeps the probe
    // Bus swap removes the old bus's 13 attached parts.
    const ChangeResult bus = s.Replace("probe-core", 0, "bus_small");
    CHECK(bus.ok && bus.removed == 14);
    CHECK(!s.SetYears(5).ok && !s.SetStage1EngineCount(9).ok && !s.SetOrbit("x").ok);
}
} // namespace
int main()
{
    DemoTcA(); SocketViews(); ReplaceAndFairing();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
