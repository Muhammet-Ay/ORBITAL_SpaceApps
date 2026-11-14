#include "OrbitalAssemblyDemo.h"

#include "OrbitalAssemblyScreenObject.h"
#include "OrbitalTimeline.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

DEFINE_LOG_CATEGORY_STATIC(LogOrbitalAssembly, Log, All);

static TAutoConsoleVariable<int32> CVarOrbitalAssemblyDemo(
	TEXT("orbital.AssemblyDemo"), 0,
	TEXT("1: print the TC-A assembly demo when Play in Editor starts. 0: off (default). The same script is the automation test ORBITAL.Assembly.DemoTCA."));

bool RunOrbitalAssemblyDemo(UOrbitalAssemblyScreen& Screen, TArray<FString>& Lines)
{
	bool bAllOk = true;
	const auto Step = [&](bool bOk, const FString& Text)
	{
		bAllOk = bAllOk && bOk;
		Lines.Add(FString::Printf(TEXT("%s %s"), bOk ? TEXT("[OK]") : TEXT("[FAIL]"), *Text));
	};

	// 1. Build TC-A: the probe first, with no rocket present, then the rocket.
	static const TCHAR* const ProbeParts[] = {
		TEXT("bus_small"), TEXT("sc_tank_s"), TEXT("sc_engine_mono"), TEXT("rcs"), TEXT("solar_s"),
		TEXT("battery_s"), TEXT("antenna_lga"), TEXT("obc_basic"), TEXT("inst_neutron")};
	static const TCHAR* const RocketParts[] = {
		TEXT("s1_tank_200"), TEXT("s1_engine_kl190"), TEXT("sep_system"), TEXT("s2_tank_7"),
		TEXT("s2_engine_hl10"), TEXT("fairing_s")};
	bool bBuilt = true;
	for (const TCHAR* Part : ProbeParts)
	{
		bBuilt = Screen.Attach(Part).bOk && bBuilt;
	}
	bBuilt = Screen.SetPropellant(130.0).bOk && bBuilt;
	bBuilt = Screen.SetOrbit(TEXT("LPO_100")).bOk && bBuilt;
	for (const TCHAR* Part : RocketParts)
	{
		bBuilt = Screen.Attach(Part).bOk && bBuilt;
	}
	bBuilt = Screen.SetStage1EngineCount(2).bOk && bBuilt;
	Step(bBuilt, TEXT("TC-A assembled: 9 probe parts, then 6 rocket parts"));

	const Orbital::AssemblySnapshot* Snapshot = Screen.GetSnapshot();
	const FOrbitalAssemblyData* Data = Screen.GetData();
	if (!Snapshot || !Data)
	{
		Step(false, TEXT("Screen is not initialised"));
		return false;
	}
	const Orbital::MissionResult Result = Orbital::Evaluate(
		Data->Parts, Data->Mission, Snapshot->spacecraft, Snapshot->launcher, "O1_ICE", "normal");
	Step(Result.success && FMath::IsNearlyEqual(Result.score, 33.5111, 0.0001),
		FString::Printf(TEXT("TC-A score %.4f (expected 33.5111)"), Result.score));
	const Orbital::Timeline Timeline = Orbital::BuildTimeline(
		Data->Parts, Data->Mission, Snapshot->spacecraft, Snapshot->launcher, Result);
	Step(Timeline.size() == 11 && Timeline.back().event == "MISSION_REPORT",
		FString::Printf(TEXT("Timeline: %d events, last is the mission report"), static_cast<int32>(Timeline.size())));

	// 2. A battery in a solar socket must be refused and change nothing.
	const FString Before = FString(UTF8_TO_TCHAR(Snapshot->spacecraftJson.c_str()));
	const FOrbitalChangeResult Wrong = Screen.Replace(TEXT("solar"), 1, TEXT("battery_s"));
	const FString After = FString(UTF8_TO_TCHAR(Screen.GetSnapshot()->spacecraftJson.c_str()));
	Step(!Wrong.bOk && Wrong.Code == TEXT("A_SLOT_MISMATCH") && Before == After,
		FString::Printf(TEXT("Battery into solar socket refused: %s (%s)"), *Wrong.Reason, *Wrong.Code));

	// 3. Removing the bus removes its 8 attached parts (9 in total); the rocket stays.
	const FOrbitalChangeResult Removed = Screen.Remove(TEXT("probe-core"), 0);
	int32 ProbeLeft = 0, RocketLeft = 0;
	for (const FOrbitalSocketRow& Row : Screen.GetSockets(FString()))
	{
		if (!Row.PartId.IsEmpty())
		{
			const bool bRocket = Row.Socket.StartsWith(TEXT("stage")) || Row.Socket == TEXT("separator") ||
				Row.Socket == TEXT("fairing");
			(bRocket ? RocketLeft : ProbeLeft) += 1;
		}
	}
	Step(Removed.bOk && Removed.Removed == 9 && ProbeLeft == 0 && RocketLeft == 6,
		FString::Printf(TEXT("Bus removed: %d parts gone (bus + 8 attached), probe parts left %d, rocket parts left %d"),
			Removed.Removed, ProbeLeft, RocketLeft));
	return bAllOk;
}

void UOrbitalAssemblyDemoSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.IsGameWorld() || CVarOrbitalAssemblyDemo.GetValueOnGameThread() == 0)
	{
		return;
	}
	Screen = NewObject<UOrbitalAssemblyScreen>(this);
	TArray<FString> Lines;
	FString CatalogError;
	bool bOk = Screen->InitFromProjectCatalog(CatalogError);
	if (!bOk)
	{
		Lines.Add(FString::Printf(TEXT("[FAIL] Catalog did not load: %s"), *CatalogError));
	}
	bOk = bOk && RunOrbitalAssemblyDemo(*Screen, Lines);
	Lines.Add(bOk ? TEXT("ORBITAL assembly demo: ALL STEPS PASSED") : TEXT("ORBITAL assembly demo: FAILED"));
	for (int32 Index = Lines.Num() - 1; Index >= 0; --Index)
	{
		const bool bLineOk = !Lines[Index].Contains(TEXT("FAIL"));
		if (bLineOk)
		{
			UE_LOG(LogOrbitalAssembly, Display, TEXT("%s"), *Lines[Index]);
		}
		else
		{
			UE_LOG(LogOrbitalAssembly, Error, TEXT("%s"), *Lines[Index]);
		}
		if (GEngine)
		{
			// Newest message is drawn on top, so add in reverse to read top to bottom.
			GEngine->AddOnScreenDebugMessage(-1, 60.0f, bLineOk ? FColor::Green : FColor::Red, Lines[Index]);
		}
	}
}

#if WITH_DEV_AUTOMATION_TESTS

// Same script without Play in Editor: Session Frontend > Automation > ORBITAL.Assembly.DemoTCA
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrbitalAssemblyDemoTest,
	"ORBITAL.Assembly.DemoTCA",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOrbitalAssemblyDemoTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	UOrbitalAssemblyScreen* TestScreen = NewObject<UOrbitalAssemblyScreen>();
	TestScreen->AddToRoot();
	TArray<FString> Lines;
	FString CatalogError;
	bool bOk = TestScreen->InitFromProjectCatalog(CatalogError);
	if (!bOk)
	{
		AddError(FString::Printf(TEXT("Catalog did not load: %s"), *CatalogError));
	}
	bOk = bOk && RunOrbitalAssemblyDemo(*TestScreen, Lines);
	TestScreen->RemoveFromRoot();
	for (const FString& Line : Lines)
	{
		AddInfo(Line);
	}
	TestTrue(TEXT("Assembly demo steps"), bOk);
	return bOk;
}

#endif
