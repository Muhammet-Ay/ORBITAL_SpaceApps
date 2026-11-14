// Tests for OrbitalGameFlow.h (assembly -> review -> flight -> report -> assembly).
// Build: g++ -std=c++17 -O2 -I../../Source/ORBITAL_SpaceApps/Assembly gameflow_tests.cpp -o gameflow_tests
#include "OrbitalGameFlow.h"
#include "OrbitalTimeline.h"
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
GameFlow NewFlow()
{
    FlowPresets presets{OrbitalData::SpacecraftPresets(), OrbitalData::LauncherPresets()};
    return GameFlow(kCatalog, kMission, OrbitalData::MakeRules(), presets);
}
const ResourceRow& Row(const GameFlow& flow, Resource r) { return flow.Review()->rows[static_cast<unsigned>(r)]; }
bool HasIssue(const GameFlow& flow, const char* code)
{ for (const auto& i : flow.Review()->issues) if (i.code == code) return true; return false; }
const char* Name(DashboardState s) { return s == DashboardState::Green ? "green" : s == DashboardState::Amber ? "amber" : "red"; }

void FirstGoal()
{
    std::printf("first goal: TC-E battery row amber, large battery turns it green\n");
    GameFlow flow = NewFlow();
    CHECK(flow.LoadCase("TC-E"));
    CHECK(flow.Review() != nullptr);
    const ResourceRow before = Row(flow, Resource::Power);
    CHECK(before.state == DashboardState::Amber && before.measure == "Usable eclipse battery");
    CHECK(before.current && *before.current == 240.0 && before.limit && std::fabs(*before.limit - 381.54153985482264) < 1e-6);
    CHECK(HasIssue(flow, "R_BATTERY_ECLIPSE") && flow.Preview().failPhase == "eclipse");
    std::printf("  TC-E power row: %s, %s %.0f / %.1f %s\n", Name(before.state), before.measure.c_str(), *before.current, *before.limit, before.unit.c_str());
    CHECK(flow.Replace("battery", 0, "battery_l").ok);
    const ResourceRow after = Row(flow, Resource::Power);
    CHECK(after.state == DashboardState::Green && !HasIssue(flow, "R_BATTERY_ECLIPSE"));
    CHECK(flow.Preview().success && std::fabs(flow.Preview().score - 80.77777777777777) < 1e-9);
    std::printf("  after battery_l: %s, %s %.0f / %.1f %s, score %.4f\n", Name(after.state), after.measure.c_str(), *after.current, *after.limit, after.unit.c_str(), flow.Preview().score);
}

void EmptyAndBuildUp()
{
    std::printf("empty design, build-up, auto separator\n");
    GameFlow flow = NewFlow();
    CHECK(flow.Phase() == GamePhase::Assembly && flow.Review() != nullptr);
    CHECK(!flow.Preview().launchAllowed && flow.RequestLaunch() == LaunchAnswer::Refused);
    CHECK(!flow.LastChange().reason.empty() && flow.Phase() == GamePhase::Assembly && !flow.LaunchSnapshot());
    // Build TC-A by hand, rocket first; the separator comes with the stage 1 tank.
    CHECK(flow.Attach("s1_tank_200").ok && flow.Screen().Model().Get("separator", 0) == "sep_system");
    for (const char* id : {"s1_engine_kl190", "s2_tank_7", "s2_engine_hl10", "fairing_s", "bus_small", "sc_tank_s",
                           "sc_engine_mono", "rcs", "solar_s", "battery_s", "antenna_lga", "obc_basic", "inst_neutron"})
        CHECK(flow.Attach(id).ok);
    CHECK(flow.SetStage1EngineCount(2).ok && flow.SetPropellant(130).ok);
    CHECK(std::fabs(flow.Preview().score - 33.51111111111111) < 1e-9);
    const ChangeResult wrong = flow.Replace("solar", 1, "battery_s");
    CHECK(!wrong.ok && wrong.code == A_SLOT_MISMATCH && flow.LastChange().reason == wrong.reason);
    CHECK(std::fabs(flow.Preview().score - 33.51111111111111) < 1e-9);
    // Clean design: launches without a review stop.
    CHECK(flow.RequestLaunch() == LaunchAnswer::Launched && flow.Phase() == GamePhase::Flight);
    CHECK(flow.LaunchSnapshot() && std::fabs(flow.LaunchSnapshot()->result.score - 33.51111111111111) < 1e-9);
    CHECK(!flow.Attach("solar_s").ok);                           // no edits during flight
    CHECK(flow.LoadCase("TC-B") == false);
    // The snapshot is what the flight view plays.
    const auto& shot = *flow.LaunchSnapshot();
    const Timeline t = BuildTimeline(kCatalog, kMission, shot.design.spacecraft, shot.design.launcher, shot.result);
    CHECK(t.size() == 11 && t.back().success);
    CHECK(flow.FlightFinished() && flow.Phase() == GamePhase::Report && !flow.FlightFinished());
    CHECK(flow.Redesign() && flow.Phase() == GamePhase::Assembly && !flow.LaunchSnapshot());
    CHECK(flow.Screen().Snapshot().spacecraft.bus == "bus_small");  // design kept for the next attempt
    CHECK(std::fabs(flow.Preview().score - 33.51111111111111) < 1e-9 && flow.Review() != nullptr);
    CHECK(flow.Attach("solar_s").ok);                              // editable again
}

void ReviewAndLoop()
{
    std::printf("risk needs confirmation; fail, redesign, fly again\n");
    GameFlow flow = NewFlow();
    CHECK(flow.LoadCase("TC-E"));
    CHECK(flow.RequestLaunch() == LaunchAnswer::NeedsConfirmation && flow.Phase() == GamePhase::Review);
    CHECK(!flow.Replace("battery", 0, "battery_l").ok);            // locked while the review is open
    CHECK(flow.CancelReview() && flow.Phase() == GamePhase::Assembly && !flow.ConfirmLaunch());
    CHECK(flow.RequestLaunch() == LaunchAnswer::NeedsConfirmation && flow.ConfirmLaunch());
    CHECK(flow.Phase() == GamePhase::Flight && flow.LaunchSnapshot()->result.failPhase == "eclipse");
    CHECK(flow.Redesign() && flow.Phase() == GamePhase::Assembly);  // Redesign straight from the flight view
    CHECK(flow.Replace("battery", 0, "battery_l").ok);
    CHECK(flow.RequestLaunch() == LaunchAnswer::Launched);
    CHECK(flow.LaunchSnapshot()->result.success && std::fabs(flow.LaunchSnapshot()->result.score - 80.77777777777777) < 1e-9);
    CHECK(flow.LaunchSnapshot()->design.spacecraft.battery == Ids{"battery_l"});

    // BLOCK: medium probe on the light rocket is refused, rows go red, nothing is snapshotted.
    GameFlow blocked = NewFlow();
    CHECK(blocked.LoadDesign(OrbitalData::SpacecraftPresets().at("SC_SURVEYOR"), OrbitalData::LauncherPresets().at("LV_LIGHT")));
    CHECK(Row(blocked, Resource::DeltaV).state == DashboardState::Red && Row(blocked, Resource::Mass).state == DashboardState::Red);
    CHECK(blocked.RequestLaunch() == LaunchAnswer::Refused && blocked.LastChange().code == "E_LV_DELTA_V" && !blocked.LaunchSnapshot());
    // Hard difficulty turns TC-B into a budget block (fixture TC-M).
    GameFlow hard = NewFlow();
    CHECK(hard.LoadCase("TC-B") && hard.Preview().launchAllowed && hard.SetDifficulty("hard"));
    CHECK(!hard.Preview().launchAllowed && Row(hard, Resource::Budget).state == DashboardState::Red);
    CHECK(!hard.SetDifficulty("impossible") && !hard.SetPrimary("O9_NONE") && hard.SetPrimary("O4_IMAGING"));

    // Every reference case loads and matches its fixture score; unknown names are refused.
    struct Case { const char* name; double score; };
    for (const Case& c : {Case{"TC-A", 33.51111111111111}, Case{"TC-B", 80.77777777777777}, Case{"TC-D", 0.0}, Case{"TC-E", 0.0}, Case{"TC-F", 0.0}})
    {
        GameFlow f = NewFlow();
        CHECK(f.LoadCase(c.name) && std::fabs(f.Preview().score - c.score) < 1e-9);
        // Saved design round trip through Ram's session JSON.
        const auto json = f.SessionState().SaveDesignJson();
        CHECK(json && json->find("\"primary\":\"O1_ICE\"") != std::string::npos);
    }
    GameFlow f = NewFlow();
    CHECK(!f.LoadCase("TC-Z") && f.Screen().Snapshot().spacecraft.bus.empty());
    // A design the socket rules refuse leaves the current one untouched.
    SpacecraftDesign bad = OrbitalData::SpacecraftPresets().at("SC_PROSPECTOR");
    bad.instruments = {"inst_neutron", "inst_camera", "inst_radar"};   // 3 instruments on the 2-deck bus
    CHECK(f.LoadCase("TC-A") && !f.LoadDesign(bad, OrbitalData::LauncherPresets().at("LV_LIGHT")));
    CHECK(f.Screen().Snapshot().spacecraft.instruments == Ids{"inst_neutron"});
    CHECK((f.Screen().PartsForSocket("battery") == Ids{"battery_l", "battery_s"}));
}
void MissionMenu()
{
    std::printf("main menu mission cards\n");
    const std::vector<MissionCard> cards = MissionCards();
    CHECK(cards.size() == 3 && cards[0].title == "Ice Prospector" && cards[1].title == "Landing Site Scout" && cards[2].title == "Full Polar Survey");
    const double budgets[] = {310.0, 360.0, 420.0};
    for (std::size_t i = 0; i < cards.size(); ++i)
    {
        GameFlow flow = NewFlow();
        CHECK(flow.StartMission(cards[i]) && flow.Primary() == cards[i].primary && flow.Difficulty() == cards[i].difficulty);
        CHECK(flow.Preview().budgetMCr == budgets[i] && !cards[i].summary.empty() && flow.Review() != nullptr);
        std::printf("  %s: primary %s, budget %.0f M\n", cards[i].title.c_str(), cards[i].primary.c_str(), flow.Preview().budgetMCr);
    }
    // Same design, different card, different outcome: no new physics, only goal and budget.
    GameFlow flow = NewFlow();
    CHECK(flow.LoadCase("TC-B"));
    CHECK(flow.StartMission(cards[0]) && !flow.Preview().launchAllowed);                      // 346 M over the 310 M budget (TC-M)
    CHECK(flow.Screen().Snapshot().spacecraft.bus == "bus_medium");                           // design kept
    CHECK(flow.StartMission(cards[2]) && flow.Preview().success);
    CHECK(std::fabs(flow.Preview().score - (80.0 + 20.0 * (1.0 - 346.0 / 420.0))) < 1e-9);    // larger budget, better cost score
    CHECK(flow.LoadCase("TC-A") && flow.StartMission(cards[1]));
    CHECK(!flow.Preview().success && std::fabs(flow.Preview().score - 22.4) < 1e-9);          // no camera for the imaging goal (TC-J)
    MissionCard bad = cards[0]; bad.primary = "O9_NONE";
    CHECK(!flow.StartMission(bad) && flow.Primary() == "O4_IMAGING");
    CHECK(flow.RequestLaunch() == LaunchAnswer::NeedsConfirmation && !flow.StartMission(cards[0])); // not while the review is open
}
} // namespace
int main()
{
    FirstGoal(); EmptyAndBuildUp(); ReviewAndLoop(); MissionMenu();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
