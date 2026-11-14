// Tests for OrbitalTimeline.h against fixture cases TC-A, B, C, D, D2, E, F, J.
// Build: g++ -std=c++17 -O2 timeline_tests.cpp -o timeline_tests && ./timeline_tests [-v]
#include "OrbitalTimeline.h"
#include "OrbitalBuiltInCatalog.h"
#include <cstdio>
#include <cstring>
#include <set>
using namespace Orbital;
namespace {
int g_checks = 0, g_failures = 0; bool g_verbose = false;
void Check(bool ok, const char* what, int line)
{ ++g_checks; if (!ok) { ++g_failures; std::printf("  FAIL line %d: %s\n", line, what); } }
#define CHECK(e) Check((e), #e, __LINE__)
bool Near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }
const Catalog kCatalog = OrbitalData::MakeCatalog();
const Mission kMission = OrbitalData::MakeMission();
const auto kSc = OrbitalData::SpacecraftPresets();
const auto kLv = OrbitalData::LauncherPresets();

TimelineOptions Options(const SpacecraftDesign&) { return {}; }
struct Run { SpacecraftDesign sc; LauncherDesign lv; MissionResult r; Timeline t; };
Run Fly(const char* label, SpacecraftDesign sc, const LauncherDesign& lv, const char* primary = "O1_ICE")
{
    Run run{sc, lv, Evaluate(kCatalog, kMission, sc, lv, primary), {}};
    run.t = BuildTimeline(kCatalog, kMission, run.sc, run.lv, run.r, Options(sc));
    const TimelineEvent& last = run.t.back();
    std::printf("%-6s %zu events, reached %s, cause %s, %s\n", label, run.t.size(), last.phaseReached.c_str(),
        last.cause.empty() ? "-" : last.cause.c_str(), last.success ? "success" : "not successful");
    if (g_verbose) for (const auto& e : run.t) std::printf("    %s\n", Describe(e).c_str());
    // Rules every timeline must obey.
    CHECK(last.event == "MISSION_REPORT");
    int reports = 0;
    for (std::size_t i = 0; i < run.t.size(); ++i)
    {
        const auto& e = run.t[i];
        reports += e.event == "MISSION_REPORT";
        CHECK(!e.message.empty() && !e.phase.empty() && !e.camera.empty());
        if (i > 0) CHECK(e.missionTimeS >= run.t[i - 1].missionTimeS && e.displayTimeS >= run.t[i - 1].displayTimeS);
        if (e.propellantLeftKg) CHECK(*e.propellantLeftKg >= 0);
        if (e.batteryWh) CHECK(*e.batteryWh >= 0);
    }
    CHECK(reports == 1);
    CHECK(CameraTag(run.t.front()) == run.t.front().vehicle + "/" + run.t.front().camera);
    CHECK(last.success == run.r.success && last.score == run.r.score && last.risk == run.r.risk &&
          last.block == run.r.block && last.warn == run.r.warn);
    return run;
}
const TimelineEvent* Find(const Timeline& t, const char* event)
{ for (const auto& e : t) if (e.event == event) return &e; return nullptr; }
int Count(const Timeline& t, const char* event)
{ int n = 0; for (const auto& e : t) n += e.event == event; return n; }
SpacecraftDesign Surveyor() { return kSc.at("SC_SURVEYOR"); }

std::set<std::string> tags;
void FullMissions()
{
    const Run a = Fly("TC-A", kSc.at("SC_PROSPECTOR"), kLv.at("LV_LIGHT"));
    const char* order[] = {"LIFTOFF", "STAGE1_SEPARATION", "FAIRING_RELEASE", "PARKING_ORBIT", "TLI_COMPLETE",
        "MIDCOURSE_CORRECTION", "INSERTION_BURN_START", "LUNAR_ORBIT_INSERTION", "FIRST_ECLIPSE", "OPERATIONS_COMPLETE", "MISSION_REPORT"};
    CHECK(a.t.size() == 11);
    CHECK(CameraTag(a.t[0]) == "launcher/pad_wide" && CameraTag(a.t[5]) == "spacecraft/deep_space");
    for (const auto& e : a.t) tags.insert(CameraTag(e));
    for (std::size_t i = 0; i < 11 && i < a.t.size(); ++i) CHECK(a.t[i].event == order[i]);
    CHECK(Near(a.r.score, 33.51111111111111, 1e-9) && a.t.back().phaseReached == "operations" && a.t.back().cause.empty());
    // Launch times: fixture t1_burn, and t_parking / t_tli counted from stage 2 ignition.
    CHECK(Near(Find(a.t, "STAGE1_SEPARATION")->missionTimeS, 157.4225394736842));
    CHECK(Near(Find(a.t, "PARKING_ORBIT")->missionTimeS, 157.4225394736842 + 201.6187984265297));
    CHECK(Near(Find(a.t, "TLI_COMPLETE")->missionTimeS, 157.4225394736842 + 297.74671325855786));
    CHECK(Near(*Find(a.t, "PARKING_ORBIT")->deltaVUsed, 9400.0));
    CHECK(Near(*Find(a.t, "TLI_COMPLETE")->deltaVUsed, 12531.343278360124));
    CHECK(Near(*Find(a.t, "LIFTOFF")->propellantLeftKg, 207000.0) && Near(*Find(a.t, "STAGE1_SEPARATION")->propellantLeftKg, 7000.0));
    // Stage 2 propellant at TLI, cross-checked with the rocket equation instead of flow x time.
    const double wet2 = 1200 + 190 + 150 + 7000 + 315.5;
    const double used = wet2 * (1.0 - std::exp(-(12531.343278360124 - 6198.325658675389) / (450 * 9.80665)));
    CHECK(Near(*Find(a.t, "TLI_COMPLETE")->propellantLeftKg, 7000.0 - used, 1e-6));
    const double cruiseDays = (Find(a.t, "INSERTION_BURN_START")->missionTimeS - Find(a.t, "TLI_COMPLETE")->missionTimeS) / 86400.0;
    CHECK(cruiseDays > 4.9 && cruiseDays < 5.1);
    // End of mission: 1 year, data 0.02 GB/day, propellant left matches years_possible 3.17.
    const TimelineEvent* end = Find(a.t, "OPERATIONS_COMPLETE");
    CHECK(Near(*end->missionYears, 1.0) && Near(*end->dataReturnedGB, 0.02 * 365.25));
    CHECK(Near(*end->deltaVUsed, 1002.7111029483098, 1e-6));      // fixture dv_req
    CHECK(*end->propellantLeftKg > 10.0 && Near(*end->batteryWh, 240.0 - 68.17332125605297));
    CHECK(Near(Find(a.t, "INSERTION_BURN_START")->displayTimeS - Find(a.t, "TLI_COMPLETE")->displayTimeS, 20.0));
    CHECK(Near(Find(a.t, "TLI_COMPLETE")->displayTimeS, 75.0) && Near(Find(a.t, "LIFTOFF")->displayTimeS, 0.0));
    CHECK(Near(end->displayTimeS - Find(a.t, "FIRST_ECLIPSE")->displayTimeS, 15.0));

    const Run b = Fly("TC-B", Surveyor(), kLv.at("LV_MID"));
    CHECK(Near(b.r.score, 80.77777777777777, 1e-9) && Count(b.t, "OPERATIONS_COMPLETE") == 1);
    CHECK(Near(*Find(b.t, "OPERATIONS_COMPLETE")->dataReturnedGB, 30.92 * 365.25));
    CHECK(Near(Find(b.t, "TLI_COMPLETE")->missionTimeS, 157.4225394736842 + 867.919424731917));
    CHECK(Find(b.t, "LUNAR_ORBIT_INSERTION")->altitude == "50 km lunar orbit");
    CHECK(Near(Find(b.t, "TLI_COMPLETE")->displayTimeS, 75.0));   // 1,025 s of flight in 75 s on screen
    // Insertion burn, recomputed here from raw catalog numbers. Same assumption as the propellant
    // readout: start from the mass after the mid-course burn, charge the 10% margin.
    const double ve = 318 * 9.80665, margin = 1.1;
    const double afterMcc = 774 * std::exp(-30.0 * margin / ve), afterLoi = 774 * std::exp(-859.0783916788878 * margin / ve);
    const double usedKg = afterMcc - afterLoi;
    const TimelineEvent* start = Find(b.t, "INSERTION_BURN_START");
    const TimelineEvent* loi = Find(b.t, "LUNAR_ORBIT_INSERTION");
    CHECK(loi->burnDurationS && Near(*loi->burnDurationS, usedKg / (450 / ve), 1e-6));
    CHECK(Near(*start->propellantLeftKg - *loi->propellantLeftKg, usedKg, 1e-6)); // readout drop == burn consumption
    CHECK(Near(*start->propellantLeftKg - *loi->propellantLeftKg, *loi->burnDurationS * 450 / ve, 1e-6));
    CHECK(Near(*loi->deltaVUsed - *start->deltaVUsed, (859.0783916788878 - 30.0) * margin, 1e-6));
    CHECK(Near(loi->missionTimeS - start->missionTimeS, *loi->burnDurationS));
    std::printf("  TC-B insertion burn: %.1f kg, %.0f s\n", usedKg, *loi->burnDurationS);
    // An engine without thrust data gives an empty duration and a zero-length burn.
    Catalog noThrust = kCatalog; noThrust.at("sc_engine_biprop").thrustN = 0;
    const Timeline bare = BuildTimeline(noThrust, kMission, b.sc, b.lv, b.r);
    CHECK(bare.size() == b.t.size() && !Find(bare, "LUNAR_ORBIT_INSERTION")->burnDurationS);
    // Skip button: launch -> TLI, then event by event, then stays at the end.
    CHECK(Near(SkipTarget(b.t, 0.0), 75.0) && Near(SkipTarget(b.t, 30.0), 75.0));
    CHECK(Near(SkipTarget(b.t, 75.0), 85.0) && Near(SkipTarget(b.t, 1e9), b.t.back().displayTimeS));

    // Two planned years on TC-A (years_possible 3.17): a YEAR_COMPLETE then OPERATIONS_COMPLETE.
    SpacecraftDesign two = kSc.at("SC_PROSPECTOR"); two.years = 2;
    const Run a2 = Fly("A-2yr", two, kLv.at("LV_LIGHT"));
    CHECK(Count(a2.t, "YEAR_COMPLETE") == 1 && Near(*Find(a2.t, "YEAR_COMPLETE")->missionYears, 1.0));
    CHECK(Near(*Find(a2.t, "OPERATIONS_COMPLETE")->missionYears, 2.0));
}

void Failures()
{
    SpacecraftDesign d = Surveyor(); d.propKg = 150;
    const Run tcd = Fly("TC-D", d, kLv.at("LV_MID"));
    CHECK(tcd.t.size() == 9 && tcd.t[7].event == "INSERTION_FAILED" && tcd.t[6].event == "INSERTION_BURN_START");
    CHECK(tcd.t.back().phaseReached == "insertion" && tcd.t.back().cause == "R_SC_DELTA_V_INSERTION");
    CHECK(Near(*tcd.t[7].propellantLeftKg, 0.0) && Near(*tcd.t[7].deltaVUsed, 798.5233938859218));
    CHECK(tcd.t[7].burnDurationS && Near(*tcd.t[7].burnDurationS * 450 / (318 * 9.80665), *tcd.t[6].propellantLeftKg, 1e-6)); // burns what is left
    CHECK(Count(tcd.t, "FIRST_ECLIPSE") == 0 && Count(tcd.t, "LUNAR_ORBIT_INSERTION") == 0 && !tcd.t.back().success);

    SpacecraftDesign d2 = Surveyor(); d2.propKg = 205;
    const Run tcd2 = Fly("TC-D2", d2, kLv.at("LV_MID"));
    const TimelineEvent* dry = Find(tcd2.t, "PROPELLANT_DEPLETED");
    CHECK(dry && Near(*dry->missionYears, 0.6164005682686737, 1e-9));
    CHECK(Near(*dry->propellantLeftKg, 0.0, 1e-6));                // tank is empty exactly when the engine says so
    CHECK(Near(*dry->deltaVUsed, 1046.6923246111078, 1e-6));       // all of the spacecraft delta-v
    CHECK(Near(*dry->dataReturnedGB, 30.92 * 365.25 * 0.6164005682686737, 1e-6));
    CHECK(Count(tcd2.t, "OPERATIONS_COMPLETE") == 0 && Count(tcd2.t, "YEAR_COMPLETE") == 0);
    CHECK(tcd2.t.back().cause == "R_SC_DELTA_V_LIFETIME" && tcd2.t.back().success && tcd2.t.back().phaseReached == "operations");
    CHECK(Near(tcd2.t.back().score, 50.08982323927168, 1e-9));

    SpacecraftDesign e = Surveyor(); e.battery = {"battery_s"};
    const Run tce = Fly("TC-E", e, kLv.at("LV_MID"));
    const TimelineEvent* dark = Find(tce.t, "ECLIPSE_FAILED");
    CHECK(dark && tce.t.size() == 10 && tce.t.back().cause == "R_BATTERY_ECLIPSE" && tce.t.back().phaseReached == "eclipse");
    CHECK(Near(*dark->batteryWh, 0.0) && Count(tce.t, "OPERATIONS_COMPLETE") == 0 && Count(tce.t, "FIRST_ECLIPSE") == 0);
    const double sunlitS = (113.04808459140574 - 47.992646522619204) * 60.0;
    CHECK(Near(dark->missionTimeS - Find(tce.t, "LUNAR_ORBIT_INSERTION")->missionTimeS, sunlitS + 240.0 / 477.0 * 3600.0, 1e-3));

    SpacecraftDesign f = Surveyor(); f.solar = {"solar_s"};
    const Run tcf = Fly("TC-F", f, kLv.at("LV_MID"));
    CHECK(Find(tcf.t, "ECLIPSE_FAILED") && tcf.t.back().cause == "R_POWER_GENERATION" && tcf.t.back().phaseReached == "eclipse" && !tcf.t.back().success);
    CHECK(Near(*Find(tcf.t, "ECLIPSE_FAILED")->batteryWh, 960.0) && Near(tcf.t.back().score, 0.0));

    // BLOCK (TC-C: small rocket under the medium probe): no liftoff, report only.
    const Run tcc = Fly("TC-C", Surveyor(), kLv.at("LV_LIGHT"));
    CHECK(tcc.t.size() == 1 && tcc.t[0].phaseReached == "design" && tcc.t[0].cause == "E_LV_DELTA_V");
    CHECK((tcc.t[0].block == Ids{"E_LV_DELTA_V", "E_FAIRING_MASS", "E_FAIRING_SIZE"}));
    const Run empty = Fly("empty", SpacecraftDesign{}, LauncherDesign{});
    CHECK(empty.t.size() == 1 && Count(empty.t, "LIFTOFF") == 0);

    // TC-J: flies the whole mission but the primary objective's instrument is missing.
    const Run tcj = Fly("TC-J", kSc.at("SC_PROSPECTOR"), kLv.at("LV_LIGHT"), "O4_IMAGING");
    CHECK(Count(tcj.t, "OPERATIONS_COMPLETE") == 1 && !tcj.t.back().success &&
          tcj.t.back().cause == "R_PRIMARY_INSTRUMENT_MISSING" && Near(tcj.t.back().score, 22.4, 1e-9));
}

void Determinism()
{
    for (double prop : {150.0, 205.0, 260.0})
    {
        SpacecraftDesign d = Surveyor(); d.propKg = prop;
        std::string first, second;
        for (std::string* out : {&first, &second})
        {
            const MissionResult r = Evaluate(kCatalog, kMission, d, kLv.at("LV_MID"), "O1_ICE");
            for (const auto& e : BuildTimeline(kCatalog, kMission, d, kLv.at("LV_MID"), r, Options(d))) *out += Describe(e) + "\n";
        }
        CHECK(!first.empty() && first == second);
    }
    // Display options change only the display clock.
    const SpacecraftDesign d = Surveyor();
    const MissionResult r = Evaluate(kCatalog, kMission, d, kLv.at("LV_MID"), "O1_ICE");
    TimelineOptions fast; fast.launchDisplayS = 30; fast.cruiseDisplayS = 8; fast.yearDisplayS = 6; fast.cruiseS = 4 * 86400.0;
    const Timeline normal = BuildTimeline(kCatalog, kMission, d, kLv.at("LV_MID"), r);
    const Timeline quick = BuildTimeline(kCatalog, kMission, d, kLv.at("LV_MID"), r, fast);
    CHECK(normal.size() == quick.size() && quick.back().displayTimeS < normal.back().displayTimeS / 2);
    CHECK(Near(Find(quick, "TLI_COMPLETE")->missionTimeS, Find(normal, "TLI_COMPLETE")->missionTimeS));
    CHECK(Near(Find(quick, "INSERTION_BURN_START")->missionTimeS - Find(quick, "TLI_COMPLETE")->missionTimeS, 4 * 86400.0, 1e-6));
    TimelineOptions real; real.launchDisplayS = 0;
    CHECK(Near(Find(BuildTimeline(kCatalog, kMission, d, kLv.at("LV_MID"), r, real), "TLI_COMPLETE")->displayTimeS,
               Find(normal, "TLI_COMPLETE")->missionTimeS));
    std::printf("determinism ok\n");
}
} // namespace
int main(int argc, char** argv)
{
    g_verbose = argc > 1 && std::strcmp(argv[1], "-v") == 0;
    FullMissions(); Failures(); Determinism();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
