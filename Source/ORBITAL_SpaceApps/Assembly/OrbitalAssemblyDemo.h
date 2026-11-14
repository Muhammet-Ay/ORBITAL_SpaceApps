// First Play in Editor demo for the assembly screen (TEMPORARY, remove once the real UI exists).
// On BeginPlay of a game world it loads PART_CATALOG.json, builds TC-A through UOrbitalAssemblyScreen, tries a battery
// in a solar socket, removes the bus, and prints each step on screen and in the Output Log
// (category LogOrbitalAssembly). Turn off with the console variable: orbital.AssemblyDemo 0
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "OrbitalAssemblyDemo.generated.h"

class UOrbitalAssemblyScreen;

// Runs the demo script on a freshly initialised screen. Returns true when every step
// behaved as expected. Lines receives one text line per step.
ORBITAL_SPACEAPPS_API bool RunOrbitalAssemblyDemo(UOrbitalAssemblyScreen& Screen, TArray<FString>& Lines);

UCLASS()
class ORBITAL_SPACEAPPS_API UOrbitalAssemblyDemoSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	UPROPERTY()
	TObjectPtr<UOrbitalAssemblyScreen> Screen;
};
