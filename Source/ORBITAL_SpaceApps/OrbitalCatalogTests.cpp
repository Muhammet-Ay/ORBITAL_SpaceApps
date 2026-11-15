#if WITH_DEV_AUTOMATION_TESTS

#include "OrbitalCatalogLoader.h"
#include "OrbitalDashboard.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrbitalCatalogParity,
    "ORBITAL.Engineering.CatalogParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOrbitalCatalogParity::RunTest(const FString& Parameters)
{
    static_cast<void>(Parameters);
    FOrbitalCatalog Data;
    FString Error;
    const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/CLAUDE_RESEARCH/PART_CATALOG.json"));
    if (!LoadOrbitalCatalog(Path, Data, Error))
    {
        AddError(Error);
        return false;
    }
    TestEqual(TEXT("v2.1 catalog part count"), static_cast<int32>(Data.Parts.size()), 39);
    const auto Mono = Data.Parts.find("sc_engine_mono");
    const auto Biprop = Data.Parts.find("sc_engine_biprop");
    TestTrue(TEXT("Monopropellant engine loads thrust_n in newtons"),
        Mono != Data.Parts.end() && Mono->second.thrustN == 440.0);
    TestTrue(TEXT("Bipropellant engine loads thrust_n in newtons"),
        Biprop != Data.Parts.end() && Biprop->second.thrustN == 450.0);
    TestTrue(TEXT("Default orbit is LPO_100"), Data.DefaultOrbit == "LPO_100");
    const auto ProbeSockets = Data.SocketTree.find("probe-core");
    TestTrue(TEXT("Probe core accepts instruments"),
        ProbeSockets != Data.SocketTree.end() &&
        std::find(ProbeSockets->second.begin(), ProbeSockets->second.end(), "instrument") != ProbeSockets->second.end());

    const auto RunCase = [&](const TCHAR* Label, const TCHAR* Probe, const TCHAR* Vehicle, double Score)
    {
        const auto* Design = Data.SpacecraftPresets.Find(Probe);
        const auto* Launcher = Data.LauncherPresets.Find(Vehicle);
        if (!Design || !Launcher)
        {
            AddError(FString::Printf(TEXT("%s: preset missing"), Label));
            return;
        }
        const auto Result = Orbital::Evaluate(Data.Parts, Data.Mission, *Design, *Launcher, "O1_ICE");
        const auto Panel = Orbital::MakeDashboardData(Data.Parts, Data.Mission, *Design, Result);
        TestTrue(FString::Printf(TEXT("%s succeeds"), Label), Result.success);
        TestTrue(FString::Printf(TEXT("%s score"), Label), FMath::IsNearlyEqual(Result.score, Score, 0.0001));
        TestTrue(FString::Printf(TEXT("%s dashboard metrics exist"), Label),
            Panel.hasSpacecraftMetrics && Panel.hasLauncherMetrics && Panel.hasCommunicationsMetrics);
        TestTrue(FString::Printf(TEXT("%s returned data"), Label),
            FMath::IsNearlyEqual(Panel.expectedReturnedGBDay, Result.sc.generatedGBPerDay, 0.0001));
    };
    RunCase(TEXT("TC-A"), TEXT("SC_PROSPECTOR"), TEXT("LV_LIGHT"), 33.5111);
    RunCase(TEXT("TC-B"), TEXT("SC_SURVEYOR"), TEXT("LV_MID"), 80.7778);
    return !HasAnyErrors();
}

#endif
