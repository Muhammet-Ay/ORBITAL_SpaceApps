// ORBITAL flight playback: replays Orbital::BuildTimeline() in the L_Mission scene.
// - StartFlight(Data, Snapshot, Result) plays the design it is given. The outcome comes from
//   Orbital::Evaluate(); the timeline only replays it and ends at the failing event.
// - Each event switches to the camera actor whose Actor Tag is Orbital::CameraTag(event)
//   (for example "launcher/pad_wide"). Actor labels do not exist in packaged builds.
// - The rocket and probe are stacked from the design's own part list with the part Blueprints
//   in Content/Parts (BP_<catalog id>, pivot at the bottom, +Z up). If one is missing the
//   vehicle is built from basic shapes instead.
// - The status line uses ORBITAL_TIMELINE_COPY_v2.1.md, looked up by event ID.
// - Skip (button or Space) jumps with Orbital::SkipTarget().
// - The report screen shows the phase reached, the cause and fix text for the result codes
//   (ORBITAL_UI_COPY_v2.1.md), the score, and a Redesign button (OnRedesignRequested).
// All UI is drawn in C++ (AHUD canvas). No Blueprint widgets, no extra module dependencies.
//
// Play on L_Mission starts a reference case until the game state hands over the player's design.
// Console: orbital.FlightCase TC-A|TC-B|TC-D|TC-E|TC-F (before Play), orbital.FlightDemo 0 turns it off.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/HUD.h"
#include "Subsystems/WorldSubsystem.h"
#include "../Assembly/OrbitalTimeline.h"
#include "OrbitalFlight.generated.h"

class ACameraActor;
class APlayerController;
class UAudioComponent;
class UFont;
class USoundBase;
class UMaterialInterface;
class UPointLightComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
struct FOrbitalAssemblyData;
namespace Orbital
{
struct AssemblySnapshot;
}

// What the report screen shows. Filled when the MISSION_REPORT event is reached.
struct FOrbitalFlightReport
{
	bool bVisible = false;
	bool bSuccess = false;
	bool bRedesignRequested = false;
	FString Title;        // the timeline's own report message
	FString PhaseReached; // design, launch, ascent, parking, transfer, cruise, insertion, eclipse, operations
	double Score = 0, Science = 0;
	TArray<FString> Codes, Causes, Fixes; // one row per result code, text from ORBITAL_UI_COPY_v2.1.md
};

UCLASS()
class ORBITAL_SPACEAPPS_API AOrbitalFlightPlayback : public AActor
{
	GENERATED_BODY()

public:
	AOrbitalFlightPlayback();
	virtual void Tick(float DeltaSeconds) override;

	// Plays the design in an assembly snapshot with the Orbital::Evaluate() result for it.
	// Get Data and Snapshot from UOrbitalAssemblyScreen::GetData() / GetSnapshot().
	bool StartFlight(const FOrbitalAssemblyData& Data, const Orbital::AssemblySnapshot& Snapshot,
		const Orbital::MissionResult& Result);
	bool StartFlight(const Orbital::Catalog& Catalog, const Orbital::Mission& Mission,
		const Orbital::SpacecraftDesign& Spacecraft, const Orbital::LauncherDesign& Launcher,
		const Orbital::MissionResult& Result);

	// Reference designs from TEST_CASES.md: TC-A, TC-B, TC-D, TC-E, TC-F.
	bool StartCase(const FString& CaseName);

	// Skip button: before TLI jumps to TLI, afterwards to the next event (Orbital::SkipTarget).
	UFUNCTION(BlueprintCallable, Category = "Orbital")
	void Skip();

	// Report screen "Redesign" button. The game state object binds to this to return to assembly.
	UFUNCTION(BlueprintCallable, Category = "Orbital")
	void RequestRedesign();
	FSimpleMulticastDelegate OnRedesignRequested;

	// Read by the HUD.
	bool IsPlaying() const { return bPlaying; }
	bool IsFinished() const { return bFinished; }
	const FString& GetStatusMessage() const { return StatusMessage; }
	const FString& GetStatusDetail() const { return StatusDetail; }
	const FOrbitalFlightReport& GetReport() const { return Report; }
	FString GetClockText() const;
	FString GetPhaseText() const;

	// Status message for one event from ORBITAL_TIMELINE_COPY_v2.1.md. Falls back to the
	// timeline's own message when the event ID is unknown or a value is missing.
	static FString FormatStatus(const Orbital::TimelineEvent& Event, double LiftoffTwr);

private:
	enum class EShape : uint8 { Cylinder, Cone, Box, Sphere };
	enum class EMode : uint8 { Idle, Ascent, Tracked, Anchored };

	struct FGroup
	{
		USceneComponent* Component = nullptr;
		FVector HomeCm = FVector::ZeroVector;
		bool bDebris = false;
		FVector VelocityCm = FVector::ZeroVector;
		FRotator SpinDeg = FRotator::ZeroRotator;
	};

	USceneComponent* MakeGroup(FGroup& Group, const TCHAR* Name, double BottomM);
	UStaticMeshComponent* AddShape(USceneComponent* Parent, EShape Shape, const FVector& CenterM,
		const FVector& SizeM, const FLinearColor& Color, const FRotator& Rotation = FRotator::ZeroRotator);
	void BuildVehicle(const Orbital::Catalog& Catalog, const Orbital::SpacecraftDesign& Spacecraft,
		const Orbital::LauncherDesign& Launcher);
	bool BuildVehicleFromParts(const Orbital::SpacecraftDesign& Spacecraft, const Orbital::LauncherDesign& Launcher);
	AActor* SpawnPart(const Orbital::Id& PartId, USceneComponent* Parent, const FVector& LocationM,
		const FRotator& Rotation, FVector* OutSizeM);
	static UClass* PartClass(const Orbital::Id& PartId);
	void FillReport(const Orbital::TimelineEvent& Event);
	void AddFlames();
	void SetBurning(bool bStage1, bool bStage2, bool bProbe, bool bQuiet);
	void PlayOneShot(USoundBase* Sound, float Volume);
	void UpdateOrbitCamera(APlayerController& Player);

	void ApplyEvent(int32 Index, APlayerController& Player);
	void SetAttached(FGroup& Group, bool bAttached);
	void MakeDebris(FGroup& Group, const FVector& VelocityCm, const FRotator& SpinDeg);
	ACameraActor* FindCamera(const Orbital::TimelineEvent& Event) const;
	bool AscentAnchor(const ACameraActor& Camera, FVector& OutAnchor) const;
	void PlaceInFrontOfCamera(const ACameraActor& Camera, const FVector& FocusLocalCm, double SizeM, double Fraction);
	void FrameSubject(ACameraActor& Camera, const FVector& FocusWorld, double SizeM, double VerticalFraction);
	void SetFill(bool bOn, const FVector& Location = FVector::ZeroVector, double DistanceCm = 0);

	UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> ConeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> ShapeMaterial;
	UPROPERTY() TObjectPtr<UPointLightComponent> FillLight;
	UPROPERTY() TObjectPtr<ACameraActor> ActiveCamera;
	UPROPERTY() TArray<TObjectPtr<AActor>> PartActors;
	UPROPERTY() TObjectPtr<UMaterialInterface> FlameMaterial;
	UPROPERTY() TObjectPtr<USoundBase> EngineSound;
	UPROPERTY() TObjectPtr<USoundBase> SeparationSound;
	UPROPERTY() TObjectPtr<USoundBase> ClickSound;
	// NASA recording played once when the first stage ignites; null if it has not been imported.
	UPROPERTY() TObjectPtr<USoundBase> IgnitionSound;
	bool bNasaSoundsChecked = false;
	bool bStage1Burning = false;
	UPROPERTY() TObjectPtr<UAudioComponent> EngineAudio;
	UPROPERTY() TObjectPtr<ACameraActor> OrbitCamera;

	FGroup Stage1, Stage2, Fairing, Probe, Panels;
	double Stage1TopM = 0, Stage2TopM = 0, ProbeHeightM = 0, ProbeSpanM = 0, FairingHeightM = 0, TotalHeightM = 0;

	Orbital::Timeline Show; // BuildTimeline() events; display times spaced so each event is seen
	FOrbitalFlightReport Report;
	double LiftoffTwr = 0;
	double DisplayNow = 0;
	double PadHoldLeft = 0; // seconds of ignition on the pad before the show clock starts
	int32 NextEvent = 0, CurrentEvent = -1;
	bool bPlaying = false, bFinished = false, bPlayerReady = false, bTestMode = false;
	double FinishedForS = 0;
	int32 LastShotKey = -1;
	bool bTestSkipped = false;

	EMode Mode = EMode::Idle;
	FVector PadCm = FVector::ZeroVector;
	FVector AscentEndCm = FVector::ZeroVector;
	FVector DriftCm = FVector::ZeroVector;
	FVector FocusLocalCm = FVector::ZeroVector;
	double FocusSizeM = 30, FocusFraction = 0.5;
	TMap<TWeakObjectPtr<ACameraActor>, float> OriginalFov;

	FString StatusMessage, StatusDetail;

	UStaticMeshComponent* Stage1Flame = nullptr;
	UStaticMeshComponent* Stage2Flame = nullptr;
	UStaticMeshComponent* ProbeFlame = nullptr;
	FVector FlameBaseScale[3];
	bool bQuietEvent = false; // several events applied in one frame (Skip): no one-shot sounds for the earlier ones
	bool bOrbiting = false, bTestOrbitDrag = false;
	double OrbitYaw = 0, OrbitPitch = 0, OrbitDistanceCm = 0;
};

// Canvas HUD: status line, mission clock, Skip button and the report screen.
UCLASS()
class ORBITAL_SPACEAPPS_API AOrbitalFlightHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
	virtual void NotifyHitBoxClick(FName BoxName) override;

	TWeakObjectPtr<AOrbitalFlightPlayback> Playback;

private:
	TArray<FString> Wrap(const FString& Text, float MaxWidth, UFont* Font, float Scale) const;
	void DrawReport(const FOrbitalFlightReport& Report, float Width, float Height, float Unit, UFont* Font);
};

// Starts a reference flight when a game world begins play on the L_Mission map.
UCLASS()
class ORBITAL_SPACEAPPS_API UOrbitalFlightSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
