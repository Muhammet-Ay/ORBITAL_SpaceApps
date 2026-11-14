// Tests for Flight/OrbitalAscent.h (gravity-turn launch) with the three preset launchers.
// Build: g++ -std=c++17 -O2 -I../../Source/ORBITAL_SpaceApps -I../../Source/ORBITAL_SpaceApps/Assembly ascent_tests.cpp -o ascent_tests && ./ascent_tests [-v]
#include "Flight/OrbitalAscent.h"
#include "OrbitalBuiltInCatalog.h"
#include <cstdio>
#include <cstring>
using namespace Orbital;
namespace {
int g_checks = 0, g_failures = 0; bool g_verbose = false;
void Check(bool ok, const char* what, int line)
{ ++g_checks; if (!ok) { ++g_failures; std::printf("  FAIL line %d: %s\n", line, what); } }
#define CHECK(e) Check((e), #e, __LINE__)
const Catalog kCatalog = OrbitalData::MakeCatalog();
const Mission kMission = OrbitalData::MakeMission();
const auto kSc = OrbitalData::SpacecraftPresets();
const auto kLv = OrbitalData::LauncherPresets();

void Fly(const char* rocket, const char* probe)
{
    const SpacecraftDesign& sc = kSc.at(probe);
    const LauncherDesign& lv = kLv.at(rocket);
    const MissionResult r = Evaluate(kCatalog, kMission, sc, lv, "O1_ICE");
    const AscentProfile p = SimulateAscent(kCatalog, kMission, lv, r.sc.wetKg);
    std::printf("%-8s kick %.2f deg, orbit %.0f x %.0f km, max-Q %.1f kPa at %.1f km, delta-v %.0f m/s "
        "(gravity loss %.0f, drag loss %.0f), cutoff T+%.0f s\n", rocket, p.kickDeg, p.periapsisAltM / 1e3,
        p.apoapsisAltM / 1e3, p.maxQPa / 1e3, p.maxQAltitudeM / 1e3, p.deltaVSpentMps, p.gravityLossMps,
        p.dragLossMps, p.cutoffS);
    CHECK(!p.samples.empty());
    if (p.samples.empty()) return;

    // Reaches a near-circular parking orbit at the mission's altitude.
    CHECK(p.reachedOrbit);
    CHECK(std::fabs(p.periapsisAltM - kMission.leoAltM) < 30000.0);
    CHECK(std::fabs(p.apoapsisAltM - kMission.leoAltM) < 30000.0);

    // Stage 1 burns for the same time the engineering model reports.
    CHECK(std::fabs(p.stage1BurnoutS - r.lv.firstBurnS) < 0.5);

    // The delta-v the flight costs is in the range the engineering model budgets for
    // (mission.ascentDeltaV), and the losses are the usual size for an orbital launch.
    CHECK(p.deltaVSpentMps > 0.95 * kMission.ascentDeltaV && p.deltaVSpentMps < 1.08 * kMission.ascentDeltaV);
    CHECK(p.gravityLossMps > 700.0 && p.gravityLossMps < 2000.0);
    CHECK(p.dragLossMps > 20.0 && p.dragLossMps < 300.0);

    // Max-Q happens in the lower atmosphere, early in the stage 1 burn.
    CHECK(p.maxQPa > 15000.0 && p.maxQPa < 90000.0);
    CHECK(p.maxQAltitudeM > 7000.0 && p.maxQAltitudeM < 20000.0);
    CHECK(p.maxQTimeS < p.stage1BurnoutS);

    // The shape of a gravity turn: straight up off the pad, then the nose only ever comes down
    // during stage 1, and the rocket is far from vertical by the time stage 1 runs out.
    CHECK(p.samples.front().pitchDeg > 89.9 && p.samples.front().altitudeM < 1.0);
    double lastPitch = 90.0, lastDownrange = -1.0, lastTime = -1.0, burnoutPitch = 90.0;
    bool monotonic = true, forward = true, ordered = true, aboveGround = true;
    for (const AscentSample& a : p.samples)
    {
        if (a.stage == 1)
        {
            monotonic = monotonic && a.pitchDeg <= lastPitch + 1e-6;
            lastPitch = a.pitchDeg;
            burnoutPitch = a.pitchDeg;
        }
        forward = forward && a.downrangeM >= lastDownrange;
        ordered = ordered && a.timeS > lastTime;
        aboveGround = aboveGround && a.altitudeM > -1.0;
        lastDownrange = a.downrangeM;
        lastTime = a.timeS;
        if (g_verbose && static_cast<int>(a.timeS) % 20 == 0)
            std::printf("    T+%4.0f s  stage %d  alt %6.1f km  downrange %7.1f km  %5.0f m/s  pitch %5.1f deg  q %5.1f kPa\n",
                a.timeS, a.stage, a.altitudeM / 1e3, a.downrangeM / 1e3, a.speedMps, a.pitchDeg, a.dynamicPressurePa / 1e3);
    }
    CHECK(monotonic);
    CHECK(forward);
    CHECK(ordered);
    CHECK(aboveGround);
    CHECK(burnoutPitch < 45.0 && burnoutPitch > 0.0);
    CHECK(p.samples.back().speedMps > 7600.0 && p.samples.back().speedMps < 7950.0);

    // Interpolation stays inside the samples.
    const AscentSample mid = SampleAscent(p, p.stage1BurnoutS * 0.5);
    CHECK(mid.altitudeM > 0 && mid.altitudeM < p.samples.back().altitudeM + 50000.0);
    CHECK(SampleAscent(p, -5.0).timeS == p.samples.front().timeS);
    CHECK(SampleAscent(p, 1e9).timeS == p.samples.back().timeS);
}
} // namespace

int main(int argc, char** argv)
{
    g_verbose = argc > 1 && std::strcmp(argv[1], "-v") == 0;
    Fly("LV_LIGHT", "SC_PROSPECTOR");
    Fly("LV_MID", "SC_SURVEYOR");
    Fly("LV_HEAVY", "SC_SURVEYOR");

    // A rocket that cannot lift its own weight returns an empty profile instead of a path.
    LauncherDesign weak = kLv.at("LV_HEAVY");
    weak.firstEngine = "s1_engine_kl190";
    weak.firstEngineCount = 1;
    CHECK(SimulateAscent(kCatalog, kMission, weak, 774.0).samples.empty());
    // Unknown parts do the same.
    LauncherDesign broken = kLv.at("LV_MID");
    broken.firstTank = "no_such_part";
    CHECK(SimulateAscent(kCatalog, kMission, broken, 774.0).samples.empty());

    // Heating estimate: zero without air, and rises steeply with speed.
    CHECK(AscentModel::HeatFluxWm2(0.0, 7800.0) == 0.0);
    CHECK(AscentModel::HeatFluxWm2(1e-4, 7800.0) > 7.9 * AscentModel::HeatFluxWm2(1e-4, 3900.0));

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
