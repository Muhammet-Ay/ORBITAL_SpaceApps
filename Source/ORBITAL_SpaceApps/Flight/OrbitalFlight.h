// ORBITAL flight playback: replays Orbital::BuildTimeline() in the L_Mission scene.
// - Each event switches to the camera actor whose Actor Tag is "<vehicle>/<camera>"
//   (for example "launcher/pad_wide"). Actor labels do not exist in packaged builds.
// - The rocket is stacked from basic shapes (+Z up): it lifts off, drops stage 1 at
//   STAGE1_SEPARATION, releases the fairing, and leaves stage 2 at TLI_COMPLETE so the
//   probe is visible.
// - The status line at the bottom uses ORBITAL_TIMELINE_COPY_v2.1.md, looked up by event ID.
// - Skip (button or Space) jumps with Orbital::SkipTarget().
// All UI is drawn in C++ (AHUD canvas). No Blueprint widgets, no extra module dependencies.
//
// Play in Editor on L_Mission starts TC-A automatically. Turn off with: orbital.FlightDemo 0
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/HUD.h"
#include "Subsystems/WorldSubsystem.h"
#include "../Assembly/OrbitalTimeline.h"
#include "OrbitalFlight.generated.h"

class ACameraActor;
class APlayerController;
class UMaterialInterface;
class UOrbitalAssemblyScreen;
class UPointLightComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS()
class ORBITAL_SPACEAPPS_API AOrbitalFlightPlayback : public AActor
{
	GENERATED_BODY()

public:
	AOrbitalFlightPlayback();
	virtual void Tick(float DeltaSeconds) override;

	// Replays a timeline that Orbital::BuildTimeline() produced for this design and result.
	void StartFlight(const Orbital::Catalog& Catalog, const Orbital::SpacecraftDesign& Spacecraft,
		const Orbital::LauncherDesign& Launcher, const Orbital::MissionResult& Result,
		const Orbital::Timeline& InTimeline);

	// Builds TC-A through the assembly screen (same parts as the assembly demo) and plays it.
	bool StartTCA();

	// Skip button: before TLI jumps to TLI, afterwards to the next event (Orbital::SkipTarget).
	UFUNCTION(BlueprintCallable, Category = "Orbital")
	void Skip();

	// Read by the HUD.
	bool IsPlaying() const { return bPlaying; }
	bool IsFinished() const { return bFinished; }
	const FString& GetStatusMessage() const { return StatusMessage; }
	const FString& GetStatusDetail() const { return StatusDetail; }
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
	UPROPERTY() TObjectPtr<UOrbitalAssemblyScreen> DemoScreen;
	UPROPERTY() TObjectPtr<ACameraActor> ActiveCamera;

	FGroup Stage1, Stage2, Fairing, Probe, Panels;
	double Stage1TopM = 0, Stage2TopM = 0, ProbeHeightM = 0, ProbeSpanM = 0, FairingHeightM = 0, TotalHeightM = 0;

	Orbital::Timeline Show; // BuildTimeline() events; display times spaced so each event is seen
	double LiftoffTwr = 0;
	double DisplayNow = 0;
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
};

// Canvas HUD: status line, mission clock and the Skip button. Created by the playback actor.
UCLASS()
class ORBITAL_SPACEAPPS_API AOrbitalFlightHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
	virtual void NotifyHitBoxClick(FName BoxName) override;

	TWeakObjectPtr<AOrbitalFlightPlayback> Playback;
};

// Starts the TC-A flight when a game world begins play on the L_Mission map.
UCLASS()
class ORBITAL_SPACEAPPS_API UOrbitalFlightSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
