#include "OrbitalClouds.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogOrbitalClouds, Log, All);

namespace
{
	// True proportion: Earth's radius in the level is 6 371 000 cm for 6371 km, so 10 km of real
	// altitude (typical cloud tops) is R * 10 / 6371 = 100 m in the scene. The rocket and pad are
	// drawn about 100x true size, so the layer sits just above the tower top.
	const double CloudAltitudeKm = 10.0;
	const double RealEarthRadiusKm = 6371.0;
	const double DriftDegPerSecond = 0.15; // slow drift; display only
	// Latitude of the cloud map that sits over the pad. The equator and poles of the NASA map are
	// often clear there; the mid-latitude storm belt (about 55 degrees) is cloudy at most longitudes.
	const double PadLatitudeDeg = -55.0;
	const float CloudOpacity = 1.2f; // multiplies the map's brightness; values above 1 thicken thin cloud
	// Seen from below, the shell's underside gets almost no sunlight and blends into the blue sky.
	// While the camera is under the layer the clouds get a soft grey-white glow so they read as clouds.
	const float UnderGlow = 1.2f;
}

bool UOrbitalCloudsSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UOrbitalCloudsSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.GetMapName().Contains(TEXT("L_Mission")))
	{
		return;
	}

	// Labels are stripped in packaged builds, so find Earth by its material.
	AStaticMeshActor* Earth = nullptr;
	for (TActorIterator<AStaticMeshActor> It(&InWorld); It; ++It)
	{
		const UStaticMeshComponent* Mesh = It->GetStaticMeshComponent();
		const UMaterialInterface* Material = Mesh ? Mesh->GetMaterial(0) : nullptr;
		if (Material && Material->GetName() == TEXT("M_Earth"))
		{
			Earth = *It;
			break;
		}
	}

	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Meshes/SM_Sphere_HiPoly.SM_Sphere_HiPoly"));
	UMaterialInterface* CloudMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/FX/M_Clouds.M_Clouds"));
	if (!Earth || !Sphere || !CloudMaterial)
	{
		UE_LOG(LogOrbitalClouds, Warning, TEXT("Clouds not created (Earth, sphere mesh or M_Clouds missing)."));
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// The pad is on top of Earth. Turn the shell so the chosen map latitude is straight above the
	// pad (not the map's pole, where the texture is pinched). The drift spins the shell about its
	// own axis, so that latitude stays over the pad and clouds move across the sky.
	const double Lat = FMath::DegreesToRadians(PadLatitudeDeg);
	const FVector PadOnMap(FMath::Cos(Lat), 0.0, FMath::Sin(Lat));
	const FRotator ShellRotation = FQuat::FindBetweenNormals(PadOnMap, FVector::UpVector).Rotator();
	CloudShell = InWorld.SpawnActor<AStaticMeshActor>(Earth->GetActorLocation(), ShellRotation, Params);
	UStaticMeshComponent* Component = CloudShell ? CloudShell->GetStaticMeshComponent() : nullptr;
	if (!Component)
	{
		return;
	}
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetStaticMesh(Sphere);
	UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(CloudMaterial, CloudShell);
	Dynamic->SetScalarParameterValue(TEXT("Opacity"), CloudOpacity);
	Component->SetMaterial(0, Dynamic);
	CloudMaterialInstance = Dynamic;
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(false);

	// Match the shell to Earth's real size, whatever the two sphere meshes' native radii are.
	const double EarthRadius = Earth->GetStaticMeshComponent()->GetStaticMesh()->GetBounds().BoxExtent.X
		* Earth->GetActorScale3D().X;
	const double SphereRadius = Sphere->GetBounds().BoxExtent.X;
	ShellRadius = EarthRadius * (1.0 + CloudAltitudeKm / RealEarthRadiusKm);
	CloudShell->SetActorScale3D(FVector(ShellRadius / SphereRadius));
}

void UOrbitalCloudsSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (CloudShell)
	{
		CloudShell->AddActorLocalRotation(FRotator(0.0, DriftDegPerSecond * DeltaTime, 0.0));
		const APlayerController* Player = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		if (CloudMaterialInstance && Player && Player->PlayerCameraManager)
		{
			const bool bBelow = FVector::Dist(Player->PlayerCameraManager->GetCameraLocation(),
				CloudShell->GetActorLocation()) < ShellRadius;
			if (bBelow != bGlowOn)
			{
				bGlowOn = bBelow;
				CloudMaterialInstance->SetScalarParameterValue(TEXT("UnderGlow"), bBelow ? UnderGlow : 0.0f);
			}
		}
	}
}

TStatId UOrbitalCloudsSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UOrbitalCloudsSubsystem, STATGROUP_Tickables);
}
