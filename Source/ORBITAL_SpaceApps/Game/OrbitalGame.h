// ORBITAL game loop inside Unreal: assemble -> design review -> launch -> flight -> report -> redesign.
// - UOrbitalGameStateSubsystem (GameInstance): the single game state object. It owns
//   Orbital::GameFlow, which wraps Ram's Orbital::Session and Isa's assembly screen model:
//   current design, live dashboard review, launch snapshot, Evaluate() result, phase.
//   It survives level reloads, so "Redesign" comes back to the same design.
// - AOrbitalGameDirector: one per L_Mission world. Shows the assembly screen, starts
//   Muhammet's AOrbitalFlightPlayback::StartFlight with the launch snapshot, and listens to
//   OnRedesignRequested to return to assembly.
// - AOrbitalAssemblyHUD: the assembly screen, drawn in C++ on the HUD canvas (same approach as
//   the flight HUD): socket list, parts for the selected socket, mission plan, dashboard rows
//   (green / amber / red), review list, reference-case buttons and Launch.
// All rules and numbers come from the plain C++ headers; this file only draws and forwards clicks.
//
// Console: orbital.Game 0 turns this off and gives the reference flights back (orbital.FlightCase).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/HUD.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "../Assembly/OrbitalGameFlow.h"
#include "OrbitalGame.generated.h"

class AOrbitalFlightPlayback;
class APlayerController;
class UFont;
struct FOrbitalAssemblyData;

// Which screen the assembly HUD draws. Kept in the game state so that a level reload
// (Redesign) returns to assembly and not to the menu.
enum class EOrbitalScreen : uint8 { MainMenu, Missions, Settings, Credits, Game };

UCLASS()
class ORBITAL_SPACEAPPS_API UOrbitalGameStateSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Main menu state and settings.
	EOrbitalScreen GetScreen() const { return Screen; }
	void SetScreen(EOrbitalScreen InScreen) { Screen = InScreen; }
	const FString& GetMissionTitle() const { return MissionTitle; }
	void SetMissionTitle(const FString& Title) { MissionTitle = Title; }
	int32 GetVolumePercent() const { return VolumePercent; }
	void SetVolumePercent(int32 Percent, UWorld* World); // 0..100, applied to the audio device
	int32 GetQuality() const;                             // 0 low .. 3 epic, -1 custom
	void SetQuality(int32 Level);

	// The game flow, created on first use from Docs/CLAUDE_RESEARCH/PART_CATALOG.json.
	// Null if the catalog did not load; GetLoadError() says why.
	Orbital::GameFlow* GetFlow();
	const FOrbitalAssemblyData* GetData() const { return Data.Get(); }
	const FString& GetLoadError() const { return LoadError; }

	// Display name of a part from the catalog ("Probe Bus S"); the ID if unknown.
	FString PartName(const Orbital::Id& PartId) const;
	// One-sentence description of a part from the "Part tooltips" table in ORBITAL_UI_COPY_v2.1.md; empty if none.
	FString PartDescription(const Orbital::Id& PartId) const;
	// Cause / Fix text for a result code from ORBITAL_UI_COPY_v2.1.md; empty if not found.
	FString CauseText(const Orbital::Id& Code) const;
	FString FixText(const Orbital::Id& Code) const;

private:
	TSharedPtr<const FOrbitalAssemblyData> Data; // declared first: must outlive Flow
	TUniquePtr<Orbital::GameFlow> Flow;
	TMap<FString, FString> PartNames, PartDescriptions;
	TMap<FString, FString> Causes, Fixes;
	FString LoadError, MissionTitle;
	EOrbitalScreen Screen = EOrbitalScreen::MainMenu;
	int32 VolumePercent = 100;
	bool bLoadTried = false;
};

UCLASS()
class ORBITAL_SPACEAPPS_API AOrbitalGameDirector : public AActor
{
	GENERATED_BODY()

public:
	AOrbitalGameDirector();
	virtual void Tick(float DeltaSeconds) override;

	UOrbitalGameStateSubsystem* GetState() const;

	// A click on the assembly screen. Actions are "<kind>:<argument>", see OrbitalGame.cpp.
	void HandleAction(const FString& Action);

	// Socket selected in the left column ("" = none).
	const Orbital::Id& GetSelectedSocket() const { return SelectedSocket; }
	int32 GetSelectedIndex() const { return SelectedIndex; }

private:
	void BeginFlight();
	void HandleRedesign();

	UPROPERTY()
	TObjectPtr<AOrbitalFlightPlayback> Flight;

	Orbital::Id SelectedSocket;
	int32 SelectedIndex = -1;
	bool bAssemblyHudSet = false;
};

UCLASS()
class ORBITAL_SPACEAPPS_API AOrbitalAssemblyHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
	virtual void NotifyHitBoxClick(FName BoxName) override;

	TWeakObjectPtr<AOrbitalGameDirector> Director;

private:
	TArray<FString> WrapText(const FString& Text, float MaxWidth, UFont* Font, float Scale) const;
	// Main menu, mission cards and settings. Returns true if it drew a menu screen.
	bool DrawMenu(UOrbitalGameStateSubsystem& State, float Width, float Height, float Unit, UFont* Font);
	void MenuButton(const FString& Text, float X, float Y, float ButtonWidth, float ButtonHeight,
		const FLinearColor& Color, const FString& Action, UFont* Font, float Scale);
};

// Spawns the director when a game world begins play on L_Mission, and keeps the
// reference-flight auto start (orbital.FlightDemo) out of the way while the game is on.
UCLASS()
class ORBITAL_SPACEAPPS_API UOrbitalGameWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
