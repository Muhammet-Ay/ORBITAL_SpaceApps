#include "OrbitalLoopTest.h"

#include "OrbitalFlight.h"
#include "../Game/OrbitalGame.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogOrbitalLoopTest, Log, All);

namespace
{
// Redesign reloads the level, which recreates the driver. The test position lives here so it carries on.
struct FLoopTestSaved
{
	int32 Step = 0;
	bool bAllOk = true;
	FString DesignBefore;
	double LiftoffTime = 0, MoonTime = 0;
	int32 LiftoffFrames = 0, MoonFrames = 0;
} GLoopTestSaved;
}

AOrbitalLoopTestDriver::AOrbitalLoopTestDriver()
{
	PrimaryActorTick.bCanEverTick = true;
}

AOrbitalGameDirector* AOrbitalLoopTestDriver::Director() const
{
	for (TActorIterator<AOrbitalGameDirector> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

AOrbitalFlightPlayback* AOrbitalLoopTestDriver::Flight() const
{
	for (TActorIterator<AOrbitalFlightPlayback> It(GetWorld()); It; ++It)
	{
		if (It->IsPlaying())
		{
			return *It;
		}
	}
	return nullptr;
}

void AOrbitalLoopTestDriver::Next(const TCHAR* What)
{
	UE_LOG(LogOrbitalLoopTest, Display, TEXT("STEP %d done: %s"), Step, What);
	++Step;
	StepTime = 0;
	SinceSkip = 0;
}

void AOrbitalLoopTestDriver::Shot(const TCHAR* Name)
{
	FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/LoopTest") /
		FString::Printf(TEXT("%s.png"), Name), false, false);
}

void AOrbitalLoopTestDriver::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Step = GLoopTestSaved.Step;
	bAllOk = GLoopTestSaved.bAllOk;
	DesignBefore = GLoopTestSaved.DesignBefore;
	LiftoffTime = GLoopTestSaved.LiftoffTime;
	MoonTime = GLoopTestSaved.MoonTime;
	LiftoffFrames = GLoopTestSaved.LiftoffFrames;
	MoonFrames = GLoopTestSaved.MoonFrames;
	StepTime += DeltaSeconds;
	SinceSkip += DeltaSeconds;
	AOrbitalGameDirector* Game = Director();
	UOrbitalGameStateSubsystem* State = Game ? Game->GetState() : nullptr;
	Orbital::GameFlow* Flow = State ? State->GetFlow() : nullptr;
	if (!Flow)
	{
		if (StepTime > 20.0)
		{
			UE_LOG(LogOrbitalLoopTest, Error, TEXT("LOOP TEST FAILED: no game director or game flow"));
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
		}
		return;
	}
	const auto Check = [&](bool bOk, const FString& Text)
	{
		bAllOk = bAllOk && bOk;
		if (bOk)
		{
			UE_LOG(LogOrbitalLoopTest, Display, TEXT("[OK] %s"), *Text);
		}
		else
		{
			UE_LOG(LogOrbitalLoopTest, Error, TEXT("[FAIL] %s"), *Text);
		}
	};
	const auto Json = [&]() { return FString(UTF8_TO_TCHAR(Flow->Screen().Snapshot().spacecraftJson.c_str())); };
	AOrbitalFlightPlayback* Playing = Flight();

	// FPS, from frame times, while the flight shows liftoff and the lunar events.
	if (Playing && !Playing->GetReport().bVisible)
	{
		const FString Phase = Playing->GetPhaseText();
		if (Phase.Contains(TEXT("LIFTOFF")))
		{
			LiftoffTime += DeltaSeconds;
			++LiftoffFrames;
		}
		else if (Phase.Contains(TEXT("INSERTION")) || Phase.Contains(TEXT("ECLIPSE")))
		{
			MoonTime += DeltaSeconds;
			++MoonFrames;
		}
	}

	switch (Step)
	{
	case 0: // assembly screen is up
		if (StepTime > 4.0)
		{
			if (GEngine)
			{
				GEngine->Exec(GetWorld(), TEXT("stat fps"));
			}
			Game->HandleAction(TEXT("c:TC-E"));
			Check(Flow->Phase() == Orbital::GamePhase::Assembly, TEXT("TC-E loaded on the assembly screen"));
			DesignBefore = Json();
			Next(TEXT("TC-E loaded"));
		}
		break;
	case 1:
		if (StepTime > 2.0)
		{
			Shot(TEXT("1_assembly_TC-E"));
			Next(TEXT("assembly screenshot"));
		}
		break;
	case 2:
		if (StepTime > 1.0)
		{
			Game->HandleAction(TEXT("a:launch"));
			Check(Flow->Phase() == Orbital::GamePhase::Review, TEXT("Launch with a battery risk opens the design review (warning)"));
			Next(TEXT("launch requested"));
		}
		break;
	case 3:
		if (StepTime > 2.0)
		{
			Shot(TEXT("2_review_warning"));
			Next(TEXT("review screenshot"));
		}
		break;
	case 4:
		if (StepTime > 1.0)
		{
			Game->HandleAction(TEXT("a:confirm"));
			Check(Flow->Phase() == Orbital::GamePhase::Flight, TEXT("Confirmed: flight started"));
			Next(TEXT("launch confirmed"));
		}
		break;
	case 5: // first flight: watch liftoff, skip to TLI, then let it play to the eclipse failure
		if (Playing && Playing->GetReport().bVisible)
		{
			const FOrbitalFlightReport& Report = Playing->GetReport();
			Check(!Report.bSuccess && Report.PhaseReached == TEXT("eclipse") && Report.Codes.Contains(TEXT("R_BATTERY_ECLIPSE")),
				FString::Printf(TEXT("Flight 1 report: %s phase '%s' codes %s"), *Report.Title, *Report.PhaseReached,
					*FString::Join(Report.Codes, TEXT(","))));
			Next(TEXT("flight 1 reached the report"));
		}
		else if (Playing && StepTime > 8.0 && Playing->GetPhaseText().Contains(TEXT("LIFTOFF")))
		{
			Shot(TEXT("3_liftoff"));
			Playing->Skip();
		}
		else if (Playing && Playing->GetPhaseText().Contains(TEXT("ECLIPSE_FAILED")))
		{
			static double InEclipse = 0.0;
			const double Before = InEclipse;
			InEclipse += DeltaSeconds;
			if (Before < 2.0 && InEclipse >= 2.0)
			{
				Shot(TEXT("4_eclipse_failed"));
			}
		}
		break;
	case 6:
		if (StepTime > 2.5)
		{
			Shot(TEXT("5_report_failed"));
			Next(TEXT("report screenshot"));
		}
		break;
	case 7:
		if (StepTime > 1.0 && Playing)
		{
			Playing->RequestRedesign();
			Next(TEXT("Redesign pressed"));
		}
		break;
	case 8:
		if (StepTime > 2.0)
		{
			Check(Flow->Phase() == Orbital::GamePhase::Assembly, TEXT("Redesign returned to the assembly screen"));
			Check(Json() == DesignBefore, TEXT("Redesign kept the same design (spacecraft JSON identical)"));
			Shot(TEXT("6_back_in_assembly"));
			Next(TEXT("back in assembly"));
		}
		break;
	case 9:
		if (StepTime > 1.5)
		{
			Game->HandleAction(TEXT("s:battery:0"));
			Game->HandleAction(TEXT("p:battery_l"));
			Check(Flow->LastChange().ok, TEXT("Large battery fitted in battery socket 0"));
			Check(FMath::IsNearlyEqual(Flow->Preview().score, 80.7778, 0.0001),
				FString::Printf(TEXT("Preview score with the large battery: %.4f (expected 80.7778)"), Flow->Preview().score));
			Next(TEXT("battery replaced"));
		}
		break;
	case 10:
		if (StepTime > 2.0)
		{
			Shot(TEXT("7_assembly_large_battery"));
			Next(TEXT("assembly screenshot 2"));
		}
		break;
	case 11:
		if (StepTime > 1.0)
		{
			Game->HandleAction(TEXT("a:launch"));
			if (Flow->Phase() == Orbital::GamePhase::Review)
			{
				Game->HandleAction(TEXT("a:confirm"));
			}
			Check(Flow->Phase() == Orbital::GamePhase::Flight, TEXT("Relaunch: flight 2 started"));
			Next(TEXT("relaunch"));
		}
		break;
	case 12: // second flight: skip through
		if (Playing && Playing->GetReport().bVisible)
		{
			const FOrbitalFlightReport& Report = Playing->GetReport();
			Check(Report.bSuccess && FMath::IsNearlyEqual(Report.Score, 80.7778, 0.0001),
				FString::Printf(TEXT("Flight 2 report: %s score %.4f (expected 80.7778)"), *Report.Title, Report.Score));
			Next(TEXT("flight 2 reached the report"));
		}
		else if (Playing && SinceSkip > 3.0)
		{
			Playing->Skip();
			SinceSkip = 0;
		}
		break;
	case 13:
		if (StepTime > 2.5)
		{
			Shot(TEXT("8_report_success"));
			Next(TEXT("report screenshot 2"));
		}
		break;
	case 14:
		if (StepTime > 1.5)
		{
			UE_LOG(LogOrbitalLoopTest, Display, TEXT("FPS liftoff: %.1f (%d frames, %.1f s)   FPS near the Moon: %.1f (%d frames, %.1f s)"),
				LiftoffTime > 0 ? LiftoffFrames / LiftoffTime : 0.0, LiftoffFrames, LiftoffTime,
				MoonTime > 0 ? MoonFrames / MoonTime : 0.0, MoonFrames, MoonTime);
			if (bAllOk)
			{
				UE_LOG(LogOrbitalLoopTest, Display, TEXT("LOOP TEST: ALL STEPS PASSED"));
			}
			else
			{
				UE_LOG(LogOrbitalLoopTest, Error, TEXT("LOOP TEST: FAILED"));
			}
			UKismetSystemLibrary::QuitGame(this, GetWorld()->GetFirstPlayerController(), EQuitPreference::Quit, false);
			++Step;
		}
		break;
	default:
		break;
	}
	if (Step < 14 && StepTime > 150.0)
	{
		UE_LOG(LogOrbitalLoopTest, Error, TEXT("LOOP TEST FAILED: step %d timed out"), Step);
		UKismetSystemLibrary::QuitGame(this, GetWorld()->GetFirstPlayerController(), EQuitPreference::Quit, false);
		Step = 99;
	}
	GLoopTestSaved.Step = Step;
	GLoopTestSaved.bAllOk = bAllOk;
	GLoopTestSaved.DesignBefore = DesignBefore;
	GLoopTestSaved.LiftoffTime = LiftoffTime;
	GLoopTestSaved.MoonTime = MoonTime;
	GLoopTestSaved.LiftoffFrames = LiftoffFrames;
	GLoopTestSaved.MoonFrames = MoonFrames;
}

void UOrbitalLoopTestSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (InWorld.IsGameWorld() && FParse::Param(FCommandLine::Get(), TEXT("OrbitalLoopTest")))
	{
		InWorld.SpawnActor<AOrbitalLoopTestDriver>();
	}
}
