// Unattended end-to-end check of the game loop. Only active with the -OrbitalLoopTest switch:
// TC-E -> launch (confirm the risk) -> flight fails in the first eclipse -> report -> Redesign
// -> same design still loaded -> large battery -> relaunch -> report with score 80.7778.
// It drives the game through AOrbitalGameDirector::HandleAction and the flight's public
// functions, takes screenshots (Saved/Screenshots/LoopTest), logs average FPS at liftoff
// and near the Moon, then quits. It changes no game code.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "OrbitalLoopTest.generated.h"

class AOrbitalFlightPlayback;
class AOrbitalGameDirector;

UCLASS()
class AOrbitalLoopTestDriver : public AActor
{
	GENERATED_BODY()

public:
	AOrbitalLoopTestDriver();
	virtual void Tick(float DeltaSeconds) override;

private:
	void Next(const TCHAR* What);
	void Shot(const TCHAR* Name);
	AOrbitalGameDirector* Director() const;
	AOrbitalFlightPlayback* Flight() const;

	int32 Step = 0;
	double StepTime = 0, SinceSkip = 0;
	bool bAllOk = true;
	FString DesignBefore;
	double LiftoffTime = 0, MoonTime = 0;
	int32 LiftoffFrames = 0, MoonFrames = 0;
};

UCLASS()
class UOrbitalLoopTestSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
