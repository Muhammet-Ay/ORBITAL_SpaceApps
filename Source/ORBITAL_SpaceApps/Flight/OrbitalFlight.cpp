#include "OrbitalFlight.h"

#include "../Assembly/OrbitalAssemblyScreenObject.h"
#include "../OrbitalCatalogLoader.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogOrbitalFlight, Log, All);

static TAutoConsoleVariable<int32> CVarOrbitalFlightDemo(
	TEXT("orbital.FlightDemo"), 1,
	TEXT("1: play a reference flight when play starts on the L_Mission map. 0: off."));

static TAutoConsoleVariable<FString> CVarOrbitalFlightCase(
	TEXT("orbital.FlightCase"), TEXT("TC-A"),
	TEXT("Reference design played on L_Mission: TC-A, TC-B, TC-D, TC-E or TC-F (TEST_CASES.md)."));

static TAutoConsoleVariable<float> CVarOrbitalFlightExposure(
	TEXT("orbital.FlightExposureBias"), -1.3f,
	TEXT("Exposure bias (EV) applied to the mission cameras during flight playback."));

namespace
{
// Each event stays on screen at least this long, so events that share a time
// (stage separation and fairing release) are both seen.
const double MinEventDwellS = 4.0;
const double RocketDiameterM = 3.7;
const double AspectRatio = 16.0 / 9.0;

// Ascent view (display only). The scene is 1:100, so these heights are well above the real ones;
// they are chosen so Earth reads as a planet: at 15 km the horizon is curved and the pad is gone,
// at 30 km the view is clearly above Earth. Timeline times and results do not use them.
const double SeparationHeightCm = 1500000.0;
const double FairingHeightCm = 3000000.0;
// Where Earth's limb sits in the frame: this fraction of the half-height above the centre.
const double HorizonAboveCentre = 0.4;

// Earth's centre and radius, from the static mesh actor that uses M_Earth (labels are not kept in
// packaged builds). False if it is missing.
bool FindEarth(UWorld* World, FVector& OutCentre, double& OutRadiusCm)
{
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		const UStaticMeshComponent* Mesh = It->GetStaticMeshComponent();
		const UMaterialInterface* Material = Mesh ? Mesh->GetMaterial(0) : nullptr;
		if (Material && Material->GetName() == TEXT("M_Earth") && Mesh->GetStaticMesh())
		{
			OutCentre = It->GetActorLocation();
			OutRadiusCm = Mesh->GetStaticMesh()->GetBounds().BoxExtent.X * It->GetActorScale3D().X;
			return true;
		}
	}
	return false;
}

// Shows or hides the launch pad (every static mesh actor with an M_Pad_* material).
void SetPadVisible(UWorld* World, bool bVisible)
{
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		const UStaticMeshComponent* Mesh = It->GetStaticMeshComponent();
		const UMaterialInterface* Material = Mesh ? Mesh->GetMaterial(0) : nullptr;
		if (Material && Material->GetName().StartsWith(TEXT("M_Pad_")))
		{
			It->SetActorHiddenInGame(!bVisible);
		}
	}
}

// Source: ORBITAL_TIMELINE_COPY_v2.1.md (approved copy). Keep in sync with that file.
struct FTimelineCopy
{
	const char* Event;
	const TCHAR* Message;
};
const FTimelineCopy TimelineCopy[] = {
	{"LIFTOFF", TEXT("Liftoff! The rocket has left the pad with a thrust-to-weight ratio of {twr}.")},
	{"STAGE1_SEPARATION", TEXT("The first stage has run out of fuel and safely separated from the rocket.")},
	{"FAIRING_RELEASE", TEXT("The protective fairing has opened, exposing the spacecraft to space.")},
	{"PARKING_ORBIT", TEXT("The vehicle has successfully reached a temporary parking orbit around Earth.")},
	{"TLI_COMPLETE", TEXT("The final engine burn is complete, and the spacecraft is now coasting toward the Moon.")},
	{"MIDCOURSE_CORRECTION", TEXT("The spacecraft used a small amount of fuel to perfectly adjust its aim toward the Moon.")},
	{"INSERTION_BURN_START", TEXT("The spacecraft is firing its engine for {minutes} minutes to slow down and enter lunar orbit.")},
	{"LUNAR_ORBIT_INSERTION", TEXT("Success! The spacecraft has captured into a stable orbit around the Moon ({orbit}).")},
	{"INSERTION_FAILED", TEXT("The spacecraft could not slow down enough and has flown past the Moon.")},
	{"FIRST_ECLIPSE", TEXT("The spacecraft is passing through the Moon's shadow and is relying entirely on battery power.")},
	{"ECLIPSE_FAILED", TEXT("The spacecraft lost power in the Moon's shadow and contact was lost.")},
	{"YEAR_COMPLETE", TEXT("Another year complete! The spacecraft has sent {gb} GB of science data back to Earth.")},
	{"PROPELLANT_DEPLETED", TEXT("After {years} years, the spacecraft has used all its fuel and can no longer maintain its orbit.")},
	{"OPERATIONS_COMPLETE", TEXT("The spacecraft reached the end of its planned {years}-year mission and returned {gb} GB of data.")},
	{"MISSION_REPORT", TEXT("The mission has ended; review the final engineering report for your results.")},
};

FString ToFString(const std::string& Text)
{
	return FString(UTF8_TO_TCHAR(Text.c_str()));
}

const Orbital::Part* FindPart(const Orbital::Catalog& Catalog, const Orbital::Id& Id)
{
	const auto Found = Catalog.find(Id);
	return Found == Catalog.end() ? nullptr : &Found->second;
}
} // namespace

// ---------------------------------------------------------------- status copy

FString AOrbitalFlightPlayback::FormatStatus(const Orbital::TimelineEvent& Event, double InLiftoffTwr)
{
	const FString Fallback = ToFString(Event.message);
	for (const FTimelineCopy& Row : TimelineCopy)
	{
		if (Event.event != Row.Event)
		{
			continue;
		}
		FString Text = Row.Message;
		bool bMissing = false;
		const auto Fill = [&](const TCHAR* Token, bool bHasValue, const FString& Value)
		{
			if (Text.Contains(Token))
			{
				bMissing = bMissing || !bHasValue;
				Text.ReplaceInline(Token, *Value);
			}
		};
		Fill(TEXT("{twr}"), InLiftoffTwr > 0, FString::Printf(TEXT("%.2f"), InLiftoffTwr));
		Fill(TEXT("{minutes}"), Event.burnDurationS.has_value(),
			FString::Printf(TEXT("%.0f"), Event.burnDurationS.value_or(0.0) / 60.0));
		Fill(TEXT("{orbit}"), !Event.altitude.empty(), ToFString(Event.altitude));
		Fill(TEXT("{gb}"), Event.dataReturnedGB.has_value(),
			FString::Printf(TEXT("%.1f"), Event.dataReturnedGB.value_or(0.0)));
		const double Years = Event.missionYears.value_or(0.0);
		Fill(TEXT("{years}"), Event.missionYears.has_value(),
			FMath::IsNearlyEqual(Years, FMath::RoundToDouble(Years), 0.005)
				? FString::Printf(TEXT("%.0f"), Years) : FString::Printf(TEXT("%.2f"), Years));
		return bMissing ? Fallback : Text;
	}
	return Fallback;
}

// ---------------------------------------------------------------- setup

AOrbitalFlightPlayback::AOrbitalFlightPlayback()
{
	PrimaryActorTick.bCanEverTick = true;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("VehicleRoot"));
	RootComponent->SetMobility(EComponentMobility::Movable);

	// Small unshadowed fill so the vehicle is readable when the Sun is behind it.
	FillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FillLight"));
	FillLight->SetupAttachment(RootComponent);
	FillLight->SetUsingAbsoluteLocation(true);
	FillLight->SetMobility(EComponentMobility::Movable);
	FillLight->SetCastShadows(false);
	FillLight->SetIntensityUnits(ELightUnits::Candelas);
	FillLight->SetIntensity(0.0f);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CylinderMesh = Cylinder.Object;
	ConeMesh = Cone.Object;
	CubeMesh = Cube.Object;
	SphereMesh = Sphere.Object;
	ShapeMaterial = Material.Object;

	// Optional polish assets. Playback still works if one is missing.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Flame(TEXT("/Game/Materials/FX/M_Flame.M_Flame"));
	static ConstructorHelpers::FObjectFinder<USoundBase> Engine(TEXT("/Game/Audio/S_EngineLoop.S_EngineLoop"));
	static ConstructorHelpers::FObjectFinder<USoundBase> Separation(TEXT("/Game/Audio/S_Separation.S_Separation"));
	static ConstructorHelpers::FObjectFinder<USoundBase> Click(TEXT("/Game/Audio/S_Click.S_Click"));
	FlameMaterial = Flame.Object;
	EngineSound = Engine.Object;
	SeparationSound = Separation.Object;
	ClickSound = Click.Object;
}

USceneComponent* AOrbitalFlightPlayback::MakeGroup(FGroup& Group, const TCHAR* Name, double BottomM)
{
	USceneComponent* Component = NewObject<USceneComponent>(this, Name);
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetupAttachment(RootComponent);
	Component->RegisterComponent();
	AddInstanceComponent(Component);
	Group = FGroup();
	Group.Component = Component;
	Group.HomeCm = FVector(0, 0, BottomM * 100.0);
	Component->SetRelativeLocation(Group.HomeCm);
	return Component;
}

UStaticMeshComponent* AOrbitalFlightPlayback::AddShape(USceneComponent* Parent, EShape Shape,
	const FVector& CenterM, const FVector& SizeM, const FLinearColor& Color, const FRotator& Rotation)
{
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetStaticMesh(Shape == EShape::Cylinder ? CylinderMesh : Shape == EShape::Cone ? ConeMesh
		: Shape == EShape::Box ? CubeMesh : SphereMesh);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetupAttachment(Parent);
	Component->RegisterComponent();
	AddInstanceComponent(Component);
	// Basic shapes are 100 cm across, so the scale is the size in metres.
	Component->SetRelativeLocationAndRotation(CenterM * 100.0, Rotation);
	Component->SetRelativeScale3D(SizeM);
	if (ShapeMaterial)
	{
		UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(ShapeMaterial, Component);
		Dynamic->SetVectorParameterValue(TEXT("Color"), Color);
		Component->SetMaterial(0, Dynamic);
	}
	return Component;
}

void AOrbitalFlightPlayback::BuildVehicle(const Orbital::Catalog& Catalog,
	const Orbital::SpacecraftDesign& Spacecraft, const Orbital::LauncherDesign& Launcher)
{
	const FLinearColor White(0.85f, 0.85f, 0.83f), Dark(0.08f, 0.08f, 0.09f), Grey(0.4f, 0.42f, 0.44f);
	const FLinearColor Black(0.03f, 0.03f, 0.03f), Gold(0.8f, 0.55f, 0.15f), Blue(0.03f, 0.06f, 0.3f);
	const FLinearColor Silver(0.7f, 0.71f, 0.73f), Teal(0.05f, 0.38f, 0.4f);
	const double D = RocketDiameterM;
	const double Area = UE_DOUBLE_PI * D * D / 4.0;
	const auto Engine = [&](USceneComponent* Parent, double X, double Y, double Z, double Dia, double Height)
	{
		// Combustion chamber on top, nozzle below with the wide end down.
		AddShape(Parent, EShape::Cylinder, FVector(X, Y, Z + Height * 0.875), FVector(Dia * 0.45, Dia * 0.45, Height * 0.25), Grey);
		AddShape(Parent, EShape::Cone, FVector(X, Y, Z + Height * 0.4), FVector(Dia, Dia, Height * 0.8), Dark);
	};

	// ---- Stage 1: engines, tank, separation ring
	USceneComponent* S1 = MakeGroup(Stage1, TEXT("Stage1"), 0.0);
	const Orbital::Part* FirstEngine = FindPart(Catalog, Launcher.firstEngine);
	const Orbital::Part* FirstTank = FindPart(Catalog, Launcher.firstTank);
	const double ThrustMN = FirstEngine ? FirstEngine->seaLevelThrustN / 1.0e6 : 2.0;
	const double EngineDia = FMath::Clamp(0.85 * FMath::Sqrt(FMath::Max(ThrustMN, 0.1)), 0.8, 1.8);
	const double EngineHeight = EngineDia * 1.8;
	const int32 EngineCount = FMath::Clamp(Launcher.firstEngineCount, 1, 4);
	const double RingRadius = EngineCount > 1 ? FMath::Min(0.95, D / 2.0 - EngineDia / 2.0) : 0.0;
	for (int32 Index = 0; Index < EngineCount; ++Index)
	{
		const double Angle = 2.0 * UE_DOUBLE_PI * Index / EngineCount;
		Engine(S1, RingRadius * FMath::Cos(Angle), RingRadius * FMath::Sin(Angle), 0.0, EngineDia, EngineHeight);
	}
	const double Tank1 = FMath::Clamp((FirstTank ? FirstTank->capacityKg : 200000.0) / 1030.0 / Area, 6.0, 40.0);
	AddShape(S1, EShape::Cylinder, FVector(0, 0, EngineHeight + Tank1 / 2.0), FVector(D, D, Tank1), White);
	AddShape(S1, EShape::Cylinder, FVector(0, 0, EngineHeight + Tank1 + 0.15), FVector(D * 1.01, D * 1.01, 0.3), Black);
	Stage1TopM = EngineHeight + Tank1 + 0.3;

	// ---- Stage 2: engine, tank, payload ring
	USceneComponent* S2 = MakeGroup(Stage2, TEXT("Stage2"), Stage1TopM);
	const Orbital::Part* SecondTank = FindPart(Catalog, Launcher.secondTank);
	const double Engine2Height = 2.0;
	Engine(S2, 0, 0, 0.0, 1.2, Engine2Height);
	// Interstage: a skirt on top of stage 1 that covers the stage 2 engine until separation.
	AddShape(S1, EShape::Cylinder, FVector(0, 0, Stage1TopM + Engine2Height / 2.0), FVector(D, D, Engine2Height), Grey);
	const double Tank2 = FMath::Clamp((SecondTank ? SecondTank->capacityKg : 7000.0) / 330.0 / Area, 2.5, 12.0);
	AddShape(S2, EShape::Cylinder, FVector(0, 0, Engine2Height + Tank2 / 2.0), FVector(D, D, Tank2), White);
	AddShape(S2, EShape::Cylinder, FVector(0, 0, Engine2Height + Tank2 + 0.15), FVector(D * 1.01, D * 1.01, 0.3), Black);
	Stage2TopM = Stage1TopM + Engine2Height + Tank2 + 0.3;

	// ---- Probe: engine, bus, tanks, antenna, instruments, solar panels
	USceneComponent* P = MakeGroup(Probe, TEXT("Probe"), Stage2TopM);
	const Orbital::Part* Bus = FindPart(Catalog, Spacecraft.bus);
	const bool bSmallBus = !Bus || Bus->maxWetMassKg <= 1000.0;
	const FVector BusSize = bSmallBus ? FVector(1.0, 1.0, 1.2) : FVector(1.6, 1.6, 2.0);
	const double BusBottom = 0.45;
	AddShape(P, EShape::Cone, FVector(0, 0, 0.225), FVector(0.3, 0.3, 0.45), Dark);
	AddShape(P, EShape::Box, FVector(0, 0, BusBottom + BusSize.Z / 2.0), BusSize, Gold);
	const int32 TankCount = static_cast<int32>(Spacecraft.tanks.size());
	for (int32 Index = 0; Index < TankCount; ++Index)
	{
		const Orbital::Part* Tank = FindPart(Catalog, Spacecraft.tanks[Index]);
		const double TankDia = FMath::Pow(6.0 * (Tank ? Tank->capacityKg : 150.0) / 1000.0 / UE_DOUBLE_PI, 1.0 / 3.0);
		const double Offset = (Index - (TankCount - 1) / 2.0) * TankDia * 1.05;
		AddShape(P, EShape::Sphere, FVector(Offset, 0, BusBottom), FVector(TankDia), Silver);
	}
	const double DeckZ = BusBottom + BusSize.Z;
	const Orbital::Part* Antenna = FindPart(Catalog, Spacecraft.antenna);
	const double DishDia = !Antenna ? 0.0 : Antenna->dataRateMbps < 0.1 ? 0.3 : Antenna->dataRateMbps < 5.0 ? 0.6 : 1.5;
	if (DishDia > 0)
	{
		AddShape(P, EShape::Cylinder, FVector(0, 0, DeckZ + 0.125), FVector(0.05, 0.05, 0.25), Grey);
		// Dish: a flat cone turned over so the wide end faces +Z.
		AddShape(P, EShape::Cone, FVector(0, 0, DeckZ + 0.25 + DishDia * 0.15),
			FVector(DishDia, DishDia, DishDia * 0.3), White, FRotator(0, 0, 180));
	}
	const FLinearColor InstrumentColors[] = {Teal, FLinearColor(0.1f, 0.45f, 0.12f), FLinearColor(0.35f, 0.12f, 0.5f),
		FLinearColor(0.08f, 0.25f, 0.7f), FLinearColor(0.85f, 0.7f, 0.08f)};
	for (int32 Index = 0; Index < static_cast<int32>(Spacecraft.instruments.size()); ++Index)
	{
		const double Angle = UE_DOUBLE_PI / 4.0 + UE_DOUBLE_PI / 2.0 * Index + (Index >= 4 ? UE_DOUBLE_PI / 4.0 : 0.0);
		AddShape(P, EShape::Box, FVector(FMath::Cos(Angle) * BusSize.X * 0.33, FMath::Sin(Angle) * BusSize.Y * 0.33, DeckZ + 0.15),
			FVector(0.3, 0.3, 0.3), InstrumentColors[Index % 5]);
	}
	ProbeHeightM = DeckZ + 0.25 + DishDia * 0.3;

	// Solar panels are a child group of the probe; hidden under the fairing, shown from TLI.
	USceneComponent* PanelRoot = MakeGroup(Panels, TEXT("Panels"), 0.0);
	PanelRoot->AttachToComponent(P, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	double LongestPanel = 0;
	for (int32 Index = 0; Index < static_cast<int32>(Spacecraft.solar.size()); ++Index)
	{
		const Orbital::Part* Panel = FindPart(Catalog, Spacecraft.solar[Index]);
		const double Length = FMath::Max(0.5, Panel ? Panel->areaM2 : 1.5); // 1 m wide wing
		const double Side = Index % 2 == 0 ? 1.0 : -1.0;
		const double PanelZ = BusBottom + BusSize.Z / 2.0;
		LongestPanel = FMath::Max(LongestPanel, Length);
		if (Index < 2)
		{
			AddShape(PanelRoot, EShape::Box, FVector(0, Side * (BusSize.Y / 2.0 + 0.1 + Length / 2.0), PanelZ), FVector(0.03, Length, 1.0), Blue);
		}
		else
		{
			AddShape(PanelRoot, EShape::Box, FVector(Side * (BusSize.X / 2.0 + 0.1 + Length / 2.0), 0, PanelZ), FVector(Length, 0.03, 1.0), Blue);
		}
	}
	ProbeSpanM = FMath::Max(ProbeHeightM + 0.5, BusSize.Y + 2.0 * LongestPanel + 0.2);

	// ---- Fairing: barrel and nose cone around the probe
	USceneComponent* F = MakeGroup(Fairing, TEXT("Fairing"), Stage2TopM);
	const Orbital::Part* FairingPart = FindPart(Catalog, Launcher.fairing);
	const bool bSmallFairing = !FairingPart || FairingPart->maxPayloadKg <= 1000.0;
	const double Barrel = FMath::Max(bSmallFairing ? 3.0 : 6.0, ProbeHeightM);
	const double Nose = bSmallFairing ? 4.0 : 5.0;
	AddShape(F, EShape::Cylinder, FVector(0, 0, Barrel / 2.0), FVector(D, D, Barrel), White);
	AddShape(F, EShape::Cone, FVector(0, 0, Barrel + Nose / 2.0), FVector(D, D, Nose), White);
	FairingHeightM = Barrel + Nose;
	TotalHeightM = Stage2TopM + FairingHeightM;
}

// ---------------------------------------------------------------- vehicle from the 38 part Blueprints

// Part Blueprints (Content/Parts/BP_<catalog id>): pivot at the bottom centre, body along +Z, 1 unit = 1 cm.
AActor* AOrbitalFlightPlayback::SpawnPart(const Orbital::Id& PartId, USceneComponent* Parent, const FVector& LocationM,
	const FRotator& Rotation, FVector* OutSizeM)
{
	if (OutSizeM)
	{
		*OutSizeM = FVector::ZeroVector;
	}
	UClass* Class = PartId.empty() ? nullptr : PartClass(PartId);
	if (!Class || !Parent || !GetWorld())
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Part = GetWorld()->SpawnActor<AActor>(Class, FTransform::Identity, Params);
	if (!Part)
	{
		return nullptr;
	}
	Part->SetActorEnableCollision(false);
	if (OutSizeM)
	{
		// X, Y: full width. Z: height above the pivot.
		FVector Origin, Extent;
		Part->GetActorBounds(false, Origin, Extent, true);
		*OutSizeM = FVector(Extent.X * 2.0, Extent.Y * 2.0, Origin.Z + Extent.Z) / 100.0;
	}
	if (Part->GetRootComponent())
	{
		Part->GetRootComponent()->SetMobility(EComponentMobility::Movable);
	}
	Part->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform);
	Part->SetActorRelativeLocation(LocationM * 100.0);
	Part->SetActorRelativeRotation(Rotation);
	PartActors.Add(Part);
	return Part;
}

UClass* AOrbitalFlightPlayback::PartClass(const Orbital::Id& PartId)
{
	const FString Name = ToFString(PartId);
	return LoadClass<AActor>(nullptr, *FString::Printf(TEXT("/Game/Parts/BP_%s.BP_%s_C"), *Name, *Name));
}

// Stacks the rocket and the probe from the design's own part list. Returns false, without
// spawning anything, when a part has no Blueprint; the caller then falls back to basic shapes.
bool AOrbitalFlightPlayback::BuildVehicleFromParts(const Orbital::SpacecraftDesign& Sc, const Orbital::LauncherDesign& Lv)
{
	Orbital::Ids Needed = {Lv.firstTank, Lv.firstEngine, Lv.secondTank, Lv.secondEngine, Lv.fairing, "sep_system",
		Sc.bus, Sc.engine, Sc.antenna, Sc.computer};
	for (const Orbital::Ids* List : {&Sc.tanks, &Sc.adcs, &Sc.solar, &Sc.battery, &Sc.instruments})
	{
		Needed.insert(Needed.end(), List->begin(), List->end());
	}
	for (const Orbital::Id& Id : Needed)
	{
		if (!Id.empty() && !PartClass(Id))
		{
			UE_LOG(LogOrbitalFlight, Warning, TEXT("No part Blueprint /Game/Parts/BP_%s; using basic shapes"), *ToFString(Id));
			return false;
		}
	}
	FVector Size;

	// ---- Stage 1: engines, tank, separation system
	USceneComponent* S1 = MakeGroup(Stage1, TEXT("Stage1"), 0.0);
	const int32 EngineCount = FMath::Clamp(Lv.firstEngineCount, 1, 4);
	TArray<AActor*> Engines;
	FVector EngineSize = FVector::ZeroVector;
	for (int32 Index = 0; Index < EngineCount; ++Index)
	{
		Engines.Add(SpawnPart(Lv.firstEngine, S1, FVector::ZeroVector, FRotator::ZeroRotator, &EngineSize));
	}
	const double Ring = EngineCount > 1 ? FMath::Min(0.95, RocketDiameterM / 2.0 - EngineSize.X / 2.0) : 0.0;
	for (int32 Index = 0; Index < EngineCount; ++Index)
	{
		const double Angle = 2.0 * UE_DOUBLE_PI * Index / EngineCount;
		if (Engines[Index])
		{
			Engines[Index]->SetActorRelativeLocation(FVector(Ring * FMath::Cos(Angle), Ring * FMath::Sin(Angle), 0.0) * 100.0);
		}
	}
	double Z = EngineSize.Z;
	SpawnPart(Lv.firstTank, S1, FVector(0, 0, Z), FRotator::ZeroRotator, &Size);
	Z += Size.Z;
	SpawnPart("sep_system", S1, FVector(0, 0, Z), FRotator::ZeroRotator, &Size);
	Stage1TopM = Z + Size.Z;

	// ---- Stage 2: engine, tank, separation system
	USceneComponent* S2 = MakeGroup(Stage2, TEXT("Stage2"), Stage1TopM);
	SpawnPart(Lv.secondEngine, S2, FVector::ZeroVector, FRotator::ZeroRotator, &Size);
	Z = Size.Z;
	if (Z > 0 && !Lv.firstTank.empty())
	{
		// Interstage: a skirt on top of stage 1 that covers the stage 2 engine until separation.
		AddShape(S1, EShape::Cylinder, FVector(0, 0, Stage1TopM + Z / 2.0), FVector(RocketDiameterM, RocketDiameterM, Z),
			FLinearColor(0.22f, 0.23f, 0.25f));
	}
	SpawnPart(Lv.secondTank, S2, FVector(0, 0, Z), FRotator::ZeroRotator, &Size);
	Z += Size.Z;
	SpawnPart("sep_system", S2, FVector(0, 0, Z), FRotator::ZeroRotator, &Size);
	Stage2TopM = Stage1TopM + Z + Size.Z;

	// ---- Probe: bus with everything attached to it
	USceneComponent* P = MakeGroup(Probe, TEXT("Probe"), Stage2TopM);
	USceneComponent* PanelRoot = MakeGroup(Panels, TEXT("Panels"), 0.0);
	PanelRoot->AttachToComponent(P, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	struct FPlaced
	{
		AActor* Actor = nullptr;
		FVector Size = FVector::ZeroVector;
	};
	const auto Measure = [&](const Orbital::Id& Id)
	{
		FPlaced Placed;
		Placed.Actor = SpawnPart(Id, P, FVector::ZeroVector, FRotator::ZeroRotator, &Placed.Size);
		return Placed;
	};
	const auto MoveTo = [](const FPlaced& Placed, const FVector& LocationM)
	{
		if (Placed.Actor)
		{
			Placed.Actor->SetActorRelativeLocation(LocationM * 100.0);
		}
	};
	const FPlaced Bus = Measure(Sc.bus);
	const FPlaced Engine = Measure(Sc.engine);
	TArray<FPlaced> Tanks, Equipment;
	for (const Orbital::Id& Id : Sc.tanks)
	{
		Tanks.Add(Measure(Id));
	}
	Orbital::Ids EquipmentIds = Sc.adcs;
	EquipmentIds.insert(EquipmentIds.end(), Sc.battery.begin(), Sc.battery.end());
	EquipmentIds.push_back(Sc.computer);
	for (const Orbital::Id& Id : EquipmentIds)
	{
		if (!Id.empty())
		{
			Equipment.Add(Measure(Id));
		}
	}
	// Tanks sit on the +X/-X faces unless solar arrays need those faces; then they hang under the bus.
	const bool bTanksOnSides = Sc.solar.size() <= 2;
	double BaseZ = Engine.Size.Z; // bus bottom: room for whatever hangs underneath
	for (const FPlaced& Item : Equipment)
	{
		BaseZ = FMath::Max(BaseZ, Item.Size.Z);
	}
	for (const FPlaced& Tank : Tanks)
	{
		BaseZ = bTanksOnSides ? BaseZ : FMath::Max(BaseZ, Tank.Size.Z);
	}
	const FVector BusSize = Bus.Size.IsNearlyZero() ? FVector(1.0, 1.0, 1.2) : Bus.Size;
	MoveTo(Bus, FVector(0, 0, BaseZ));
	MoveTo(Engine, FVector(0, 0, BaseZ - Engine.Size.Z));
	for (int32 Index = 0; Index < Equipment.Num(); ++Index)
	{
		const double Angle = UE_DOUBLE_PI / 4.0 + 2.0 * UE_DOUBLE_PI * Index / Equipment.Num();
		const double Radius = 0.33 * FMath::Min(BusSize.X, BusSize.Y);
		MoveTo(Equipment[Index], FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), BaseZ - Equipment[Index].Size.Z));
	}
	for (int32 Index = 0; Index < Tanks.Num(); ++Index)
	{
		const double Side = Index % 2 == 0 ? 1.0 : -1.0;
		const FVector& TankSize = Tanks[Index].Size;
		MoveTo(Tanks[Index], bTanksOnSides
			? FVector(Side * (BusSize.X / 2.0 + TankSize.X / 2.0), 0, BaseZ + BusSize.Z / 2.0 - TankSize.Z / 2.0)
			: FVector(Side * (Engine.Size.X / 2.0 + TankSize.X / 2.0 + 0.05), 0, BaseZ - TankSize.Z));
	}
	// Named mount points on the bus Blueprints: top, bottom, side_a..side_d, deck_a..deck_e.
	const auto Socket = [&](const FString& Name, FVector& OutLocationM, FRotator& OutRotation)
	{
		if (Bus.Actor)
		{
			for (UActorComponent* Component : Bus.Actor->GetComponents())
			{
				const USceneComponent* Scene = Cast<USceneComponent>(Component);
				if (Scene && Scene->GetName().Equals(Name, ESearchCase::IgnoreCase))
				{
					OutLocationM = Scene->GetRelativeLocation() / 100.0 + FVector(0, 0, BaseZ);
					OutRotation = Scene->GetRelativeRotation();
					return true;
				}
			}
		}
		return false;
	};
	const double DeckZ = BaseZ + BusSize.Z;
	FVector Location;
	FRotator Rotation;
	FVector AntennaSize = FVector::ZeroVector;
	if (!Socket(TEXT("top"), Location, Rotation))
	{
		Location = FVector(0, 0, DeckZ);
	}
	SpawnPart(Sc.antenna, P, Location, FRotator::ZeroRotator, &AntennaSize);
	for (int32 Index = 0; Index < static_cast<int32>(Sc.instruments.size()); ++Index)
	{
		if (!Socket(FString::Printf(TEXT("deck_%c"), TEXT('a') + Index), Location, Rotation))
		{
			const double Angle = UE_DOUBLE_PI / 4.0 + UE_DOUBLE_PI / 2.0 * Index;
			Location = FVector(FMath::Cos(Angle) * BusSize.X * 0.33, FMath::Sin(Angle) * BusSize.Y * 0.33, DeckZ);
		}
		SpawnPart(Sc.instruments[Index], P, Location, FRotator::ZeroRotator, &Size);
	}
	// Solar arrays: hinge on a side face, wing along the socket's +Y. Hidden under the fairing.
	const TCHAR* const SideNames[] = {TEXT("side_a"), TEXT("side_b"), TEXT("side_c"), TEXT("side_d")};
	const FVector SideFallback[] = {FVector(0, BusSize.Y / 2.0, 0), FVector(0, -BusSize.Y / 2.0, 0),
		FVector(BusSize.X / 2.0, 0, 0), FVector(-BusSize.X / 2.0, 0, 0)};
	const double SideYaw[] = {0.0, 180.0, -90.0, 90.0};
	double LongestPanel = 0;
	for (int32 Index = 0; Index < FMath::Min(4, static_cast<int32>(Sc.solar.size())); ++Index)
	{
		if (!Socket(SideNames[Index], Location, Rotation))
		{
			Location = SideFallback[Index] + FVector(0, 0, BaseZ + BusSize.Z / 2.0);
			Rotation = FRotator(0, SideYaw[Index], 0);
		}
		SpawnPart(Sc.solar[Index], PanelRoot, Location - FVector(0, 0, 0.5), Rotation, &Size); // wing is 1 m wide
		LongestPanel = FMath::Max(LongestPanel, Size.Y);
	}
	ProbeHeightM = DeckZ + FMath::Max(AntennaSize.Z, 0.3);
	ProbeSpanM = FMath::Max(ProbeHeightM + 0.5, BusSize.Y + 2.0 * LongestPanel + 0.2);

	// ---- Fairing around the probe
	USceneComponent* F = MakeGroup(Fairing, TEXT("Fairing"), Stage2TopM);
	SpawnPart(Lv.fairing, F, FVector::ZeroVector, FRotator::ZeroRotator, &Size);
	FairingHeightM = Size.Z;
	TotalHeightM = Stage2TopM + FairingHeightM;
	return true;
}

// ---------------------------------------------------------------- assembly preview

bool AOrbitalFlightPlayback::BuildPreview(const Orbital::Catalog& Catalog, const Orbital::SpacecraftDesign& Spacecraft,
	const Orbital::LauncherDesign& Launcher, bool bProbeOnly)
{
	if (bPlaying || Stage1.Component)
	{
		return false; // one build per actor
	}
	PadCm = FVector(0, 0, 600);
	FHitResult Hit;
	FCollisionQueryParams Query(FName(TEXT("OrbitalPad")), false, this);
	if (GetWorld() && GetWorld()->LineTraceSingleByChannel(Hit, FVector(0, 0, 50000), FVector(0, 0, -5000), ECC_Visibility, Query))
	{
		PadCm = Hit.Location;
	}
	SetActorLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!BuildVehicleFromParts(Spacecraft, Launcher))
	{
		BuildVehicle(Catalog, Spacecraft, Launcher);
	}
	// The spacecraft alone stands on the pad; the stack stands on its engines.
	const FVector Base = bProbeOnly ? PadCm - FVector(0, 0, Stage2TopM * 100.0) : PadCm;
	SetActorLocationAndRotation(Base, FRotator::ZeroRotator);
	for (FGroup* Group : {&Stage1, &Stage2, &Fairing})
	{
		if (Group->Component)
		{
			Group->Component->SetVisibility(!bProbeOnly, true);
		}
	}
	if (Panels.Component)
	{
		Panels.Component->SetVisibility(bProbeOnly, true);
	}
	if (bProbeOnly)
	{
		PreviewFocusCm = Base + FVector(0, 0, (Stage2TopM + ProbeHeightM / 2.0) * 100.0);
		PreviewSizeM = FMath::Max(ProbeSpanM, 2.5);
	}
	else
	{
		PreviewFocusCm = Base + FVector(0, 0, TotalHeightM * 50.0);
		PreviewSizeM = FMath::Max(TotalHeightM, 6.0);
	}
	return true;
}

void AOrbitalFlightPlayback::LightPreview(const FVector& From)
{
	if (FillLight)
	{
		const double DistanceCm = FMath::Max(FVector::Dist(From, PreviewFocusCm), 100.0);
		const double DistanceM = DistanceCm / 100.0;
		FillLight->SetWorldLocation(From);
		FillLight->SetAttenuationRadius(static_cast<float>(DistanceCm * 5.0));
		FillLight->SetIntensity(static_cast<float>(3.0 * DistanceM * DistanceM));
	}
}

void AOrbitalFlightPlayback::ClearPreview()
{
	for (AActor* Part : PartActors)
	{
		if (Part)
		{
			Part->Destroy();
		}
	}
	PartActors.Reset();
	Destroy();
}

// ---------------------------------------------------------------- start

bool AOrbitalFlightPlayback::StartFlight(const FOrbitalAssemblyData& Data, const Orbital::AssemblySnapshot& Snapshot,
	const Orbital::MissionResult& Result)
{
	return StartFlight(Data.Parts, Data.Mission, Snapshot.spacecraft, Snapshot.launcher, Result);
}

bool AOrbitalFlightPlayback::StartFlight(const Orbital::Catalog& Catalog, const Orbital::Mission& Mission,
	const Orbital::SpacecraftDesign& Spacecraft, const Orbital::LauncherDesign& Launcher, const Orbital::MissionResult& Result)
{
	if (bPlaying)
	{
		return false;
	}
	// The outcome was decided by Evaluate(); the timeline only replays it (and ends at the failing event).
	Show = Orbital::BuildTimeline(Catalog, Mission, Spacecraft, Launcher, Result);
	if (Show.empty())
	{
		return false;
	}
	for (size_t Index = 1; Index < Show.size(); ++Index)
	{
		Show[Index].displayTimeS = FMath::Max(Show[Index].displayTimeS, Show[Index - 1].displayTimeS + MinEventDwellS);
	}
	LiftoffTwr = Result.lv.firstTwr;

	// The pad is at the world origin (L_Mission). Stand the rocket on whatever is there.
	PadCm = FVector(0, 0, 600);
	FHitResult Hit;
	FCollisionQueryParams Query(FName(TEXT("OrbitalPad")), false, this);
	if (GetWorld() && GetWorld()->LineTraceSingleByChannel(Hit, FVector(0, 0, 50000), FVector(0, 0, -5000), ECC_Visibility, Query))
	{
		PadCm = Hit.Location;
	}
	SetActorLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
	bool bFromParts = false;
	if (Show.size() > 1) // a refused launch has only the report event and no vehicle
	{
		bFromParts = BuildVehicleFromParts(Spacecraft, Launcher);
		if (!bFromParts)
		{
			BuildVehicle(Catalog, Spacecraft, Launcher);
		}
	}
	SetActorLocationAndRotation(PadCm, FRotator::ZeroRotator);

	if (Show.size() > 1)
	{
		AddFlames();
	}
	bTestMode = FParse::Param(FCommandLine::Get(), TEXT("OrbitalFlightTest"));
	Report = FOrbitalFlightReport();
	DisplayNow = 0;
	PadHoldLeft = bTestMode ? 0.0 : 3.0; // engines light, then the vehicle leaves the pad
	NextEvent = 0;
	CurrentEvent = -1;
	bFinished = false;
	bPlaying = true;
	UE_LOG(LogOrbitalFlight, Display, TEXT("Flight started: %d events, rocket %.1f m tall (%s, %d part actors), pad at Z=%.0f cm"),
		static_cast<int32>(Show.size()), TotalHeightM, bFromParts ? TEXT("part Blueprints") : TEXT("basic shapes"),
		PartActors.Num(), PadCm.Z);
	for (const Orbital::TimelineEvent& Event : Show)
	{
		UE_LOG(LogOrbitalFlight, Display, TEXT("  show %.2f s  T+%.0f s  %s  [%s]"), Event.displayTimeS, Event.missionTimeS,
			*ToFString(Event.event), *ToFString(Orbital::CameraTag(Event)));
	}
	return true;
}

// Reference designs from TEST_CASES.md, built from the catalog presets. Used until the game
// state object hands over the player's own design through StartFlight().
bool AOrbitalFlightPlayback::StartCase(const FString& CaseName)
{
	FOrbitalCatalog Catalog;
	FString Error;
	const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/CLAUDE_RESEARCH/PART_CATALOG.json"));
	if (!LoadOrbitalCatalog(Path, Catalog, Error))
	{
		UE_LOG(LogOrbitalFlight, Error, TEXT("Could not load %s: %s"), *Path, *Error);
		return false;
	}
	const FString Case = CaseName.ToUpper();
	const bool bCaseA = Case == TEXT("TC-A");
	const Orbital::SpacecraftDesign* Preset = Catalog.SpacecraftPresets.Find(bCaseA ? TEXT("SC_PROSPECTOR") : TEXT("SC_SURVEYOR"));
	const Orbital::LauncherDesign* Rocket = Catalog.LauncherPresets.Find(bCaseA ? TEXT("LV_LIGHT") : TEXT("LV_MID"));
	if (!Preset || !Rocket)
	{
		UE_LOG(LogOrbitalFlight, Error, TEXT("%s: catalog preset missing"), *Case);
		return false;
	}
	Orbital::SpacecraftDesign Design = *Preset;
	if (Case == TEXT("TC-D"))
	{
		Design.propKg = 150.0;
	}
	else if (Case == TEXT("TC-E"))
	{
		Design.battery = {"battery_s"};
	}
	else if (Case == TEXT("TC-F"))
	{
		Design.solar = {"solar_s"};
	}
	else if (!bCaseA && Case != TEXT("TC-B"))
	{
		UE_LOG(LogOrbitalFlight, Error, TEXT("Unknown flight case '%s' (use TC-A, TC-B, TC-D, TC-E or TC-F)"), *CaseName);
		return false;
	}
	const Orbital::MissionResult Result = Orbital::Evaluate(Catalog.Parts, Catalog.Mission, Design, *Rocket, "O1_ICE", "normal");
	UE_LOG(LogOrbitalFlight, Display, TEXT("%s: score %.4f, success %s, fail phase '%s'"), *Case, Result.score,
		Result.success ? TEXT("yes") : TEXT("no"), *ToFString(Result.failPhase));
	return StartFlight(Catalog.Parts, Catalog.Mission, Design, *Rocket, Result);
}

void AOrbitalFlightPlayback::RequestRedesign()
{
	if (!Report.bVisible || Report.bRedesignRequested)
	{
		return;
	}
	Report.bRedesignRequested = true;
	PlayOneShot(ClickSound, 0.8f);
	UE_LOG(LogOrbitalFlight, Display, TEXT("Redesign requested from the report screen (%d listener(s))"),
		OnRedesignRequested.IsBound() ? 1 : 0);
	OnRedesignRequested.Broadcast();
}

// Cause and Fix columns of the validation table in ORBITAL_UI_COPY_v2.1.md, keyed by code.
void AOrbitalFlightPlayback::FillReport(const Orbital::TimelineEvent& Event)
{
	TMap<FString, TPair<FString, FString>> Copy;
	FString Text;
	for (const TCHAR* Relative : {TEXT("Docs/CLAUDE_RESEARCH/ORBITAL_UI_COPY_v2.1.md"), TEXT("Source/ORBITAL_SpaceApps/ORBITAL_UI_COPY_v2.1.md")})
	{
		if (FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), Relative)))
		{
			break;
		}
	}
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		TArray<FString> Cells;
		Line.ParseIntoArray(Cells, TEXT("|"), false);
		if (Cells.Num() >= 6)
		{
			const FString Code = Cells[1].TrimStartAndEnd();
			if (Code.Len() > 2 && Code[1] == TEXT('_') && Code == Code.ToUpper())
			{
				Copy.Add(Code, TPair<FString, FString>(Cells[3].TrimStartAndEnd(), Cells[4].TrimStartAndEnd()));
			}
		}
	}

	Report = FOrbitalFlightReport();
	Report.bVisible = true;
	Report.bSuccess = Event.success;
	Report.Title = ToFString(Event.message);
	Report.PhaseReached = ToFString(Event.phaseReached);
	Report.Score = Event.score;
	Report.Science = Event.science;
	TArray<FString> Codes;
	if (!Event.cause.empty())
	{
		Codes.Add(ToFString(Event.cause));
	}
	for (const Orbital::Id& Code : Event.block)
	{
		Codes.AddUnique(ToFString(Code));
	}
	for (int32 Index = 0; Index < FMath::Min(3, Codes.Num()); ++Index)
	{
		const TPair<FString, FString>* Row = Copy.Find(Codes[Index]);
		Report.Codes.Add(Codes[Index]);
		Report.Causes.Add(Row ? Row->Key : FString::Printf(TEXT("%s (no text found in ORBITAL_UI_COPY_v2.1.md)"), *Codes[Index]));
		Report.Fixes.Add(Row ? Row->Value : FString());
	}
	UE_LOG(LogOrbitalFlight, Display, TEXT("Report: %s | phase reached '%s' | score %.1f | science %.1f | codes %s"),
		*Report.Title, *Report.PhaseReached, Report.Score, Report.Science, *FString::Join(Report.Codes, TEXT(",")));
	for (int32 Index = 0; Index < Report.Codes.Num(); ++Index)
	{
		UE_LOG(LogOrbitalFlight, Display, TEXT("Report %s | Cause: %s | Fix: %s"), *Report.Codes[Index], *Report.Causes[Index], *Report.Fixes[Index]);
	}
}

// ---------------------------------------------------------------- playback

void AOrbitalFlightPlayback::Skip()
{
	if (!bPlaying || bFinished)
	{
		return;
	}
	const double Target = Orbital::SkipTarget(Show, DisplayNow);
	UE_LOG(LogOrbitalFlight, Display, TEXT("Skip: %.2f s -> %.2f s"), DisplayNow, Target);
	PadHoldLeft = 0;
	DisplayNow = FMath::Max(DisplayNow, Target);
	PlayOneShot(ClickSound, 0.8f);
}

void AOrbitalFlightPlayback::SetAttached(FGroup& Group, bool bAttached)
{
	Group.bDebris = false;
	if (!Group.Component)
	{
		return;
	}
	if (Group.Component->GetAttachParent() != RootComponent)
	{
		Group.Component->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
	}
	Group.Component->SetRelativeLocationAndRotation(Group.HomeCm, FRotator::ZeroRotator);
	Group.Component->SetVisibility(bAttached, true);
}

void AOrbitalFlightPlayback::MakeDebris(FGroup& Group, const FVector& VelocityCm, const FRotator& SpinDeg)
{
	if (!Group.Component)
	{
		return;
	}
	Group.Component->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	Group.Component->SetVisibility(true, true);
	Group.bDebris = true;
	Group.VelocityCm = VelocityCm;
	Group.SpinDeg = SpinDeg;
}

ACameraActor* AOrbitalFlightPlayback::FindCamera(const Orbital::TimelineEvent& Event) const
{
	const FName Tag(*ToFString(Orbital::CameraTag(Event)));
	for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(Tag))
		{
			return *It;
		}
	}
	UE_LOG(LogOrbitalFlight, Warning, TEXT("No camera actor with tag '%s' for event %s"),
		*Tag.ToString(), *ToFString(Event.event));
	return nullptr;
}

// Ascent view: moves a level camera (at runtime only) so it looks at the vehicle with Earth's curved
// limb in the upper part of the frame. Keeps the camera's compass direction from the level and a
// distance at which FrameSubject keeps the level's field of view. Display only.
void AOrbitalFlightPlayback::PlaceAboveEarth(ACameraActor& Camera, const FVector& FocusWorld)
{
	FVector EarthCentre;
	double EarthRadiusCm = 0.0;
	FVector Across = Camera.GetActorForwardVector();
	Across.Z = 0.0;
	if (!FindEarth(GetWorld(), EarthCentre, EarthRadiusCm) || !Across.Normalize())
	{
		return;
	}
	UCameraComponent* Lens = Camera.GetCameraComponent();
	const float* Original = OriginalFov.Find(&Camera);
	const double Fov = FMath::DegreesToRadians(Original ? *Original : Lens->FieldOfView);
	const double HalfVertical = FMath::Atan(FMath::Tan(Fov / 2.0) / AspectRatio);
	const double RadiusHere = FMath::Max(FVector::Dist(FocusWorld, EarthCentre), EarthRadiusCm);
	const double Dip = FMath::Acos(EarthRadiusCm / RadiusHere); // horizon angle below level
	const double Pitch = -(Dip + HorizonAboveCentre * HalfVertical);
	const FVector Look = Across * FMath::Cos(Pitch) + FVector::UpVector * FMath::Sin(Pitch);
	const double Distance = (FocusSizeM * 100.0 / FocusFraction / 2.0) * AspectRatio / FMath::Tan(Fov / 2.0);
	if (Camera.GetRootComponent())
	{
		Camera.GetRootComponent()->SetMobility(EComponentMobility::Movable);
	}
	Camera.SetActorLocationAndRotation(FocusWorld - Look * Distance, Look.Rotation());
}

void AOrbitalFlightPlayback::SetFill(bool bOn, const FVector& Location, double DistanceCm)
{
	if (!bOn || DistanceCm <= 0)
	{
		FillLight->SetIntensity(0.0f);
		return;
	}
	const double DistanceM = DistanceCm / 100.0;
	FillLight->SetWorldLocation(Location);
	FillLight->SetAttenuationRadius(static_cast<float>(DistanceCm * 5.0));
	FillLight->SetIntensity(static_cast<float>(0.8 * DistanceM * DistanceM)); // about 0.8 lux on the vehicle
}

// Camera stays where the level author put it; the vehicle is placed in its view and drifts across.
void AOrbitalFlightPlayback::PlaceInFrontOfCamera(const ACameraActor& Camera, const FVector& InFocusLocalCm,
	double SizeM, double Fraction)
{
	const FVector CameraLocation = Camera.GetActorLocation();
	const FVector Forward = Camera.GetActorForwardVector(), Right = Camera.GetActorRightVector(), Up = Camera.GetActorUpVector();
	FocusLocalCm = InFocusLocalCm; // the player camera orbits this point
	FocusSizeM = SizeM;
	const double HalfFov = FMath::DegreesToRadians(Camera.GetCameraComponent()->FieldOfView * 0.5);
	const double TanHalf = FMath::Tan(HalfFov);
	const double Distance = (SizeM * 100.0 / Fraction / 2.0) / TanHalf;
	const FVector Axis = (Right * 0.95 + Up * 0.2 - Forward * 0.25).GetSafeNormal();
	const FRotator Rotation = FRotationMatrix::MakeFromZX(Axis, Forward).Rotator();
	const FVector FocusWorld = CameraLocation + Forward * Distance - Right * (0.2 * Distance * TanHalf)
		- Up * (0.1 * Distance * TanHalf);
	SetActorLocationAndRotation(FocusWorld - Rotation.RotateVector(InFocusLocalCm), Rotation);
	DriftCm = Axis * (0.5 * Distance * TanHalf / 12.0);
	Mode = EMode::Anchored;
	SetFill(true, CameraLocation + Up * (0.3 * Distance) - Right * (0.3 * Distance), Distance);
}

// Turns a level camera towards the vehicle and narrows its field of view to keep the vehicle readable.
void AOrbitalFlightPlayback::FrameSubject(ACameraActor& Camera, const FVector& FocusWorld, double SizeM,
	double VerticalFraction)
{
	UCameraComponent* Lens = Camera.GetCameraComponent();
	float* Original = OriginalFov.Find(&Camera);
	if (!Original)
	{
		if (Camera.GetRootComponent())
		{
			Camera.GetRootComponent()->SetMobility(EComponentMobility::Movable);
		}
		Original = &OriginalFov.Add(&Camera, Lens->FieldOfView);
	}
	const FVector ToFocus = FocusWorld - Camera.GetActorLocation();
	const double Distance = FMath::Max(ToFocus.Size(), 100.0);
	Camera.SetActorRotation(ToFocus.Rotation());
	const double TanHalf = (SizeM * 100.0 / VerticalFraction / 2.0) * AspectRatio / Distance;
	const double Needed = FMath::RadiansToDegrees(2.0 * FMath::Atan(TanHalf));
	Lens->SetFieldOfView(static_cast<float>(FMath::Clamp(Needed, 14.0, static_cast<double>(*Original))));
}

void AOrbitalFlightPlayback::ApplyEvent(int32 Index, APlayerController& Player)
{
	const Orbital::TimelineEvent& Event = Show[Index];
	const std::string& Name = Event.event;
	CurrentEvent = Index;
	bOrbiting = false; // every event starts on its own camera
	StatusMessage = FormatStatus(Event, LiftoffTwr);
	StatusDetail = Name == "MISSION_REPORT" ? ToFString(Event.message) : FString();

	ACameraActor* Camera = FindCamera(Event);
	UE_LOG(LogOrbitalFlight, Display, TEXT("Event %d %s at show %.2f s, camera %s: %s"), Index, *ToFString(Name),
		DisplayNow, Camera ? *ToFString(Event.vehicle + "/" + Event.camera) : TEXT("(missing)"), *StatusMessage);
	if (Camera)
	{
		Player.SetViewTargetWithBlend(Camera, 0.0f);
		if (Player.PlayerCameraManager && Camera != ActiveCamera)
		{
			Player.PlayerCameraManager->StartCameraFade(1.0f, 0.0f, 0.5f, FLinearColor::Black, false, false);
		}
		ActiveCamera = Camera;
		// The project runs with fixed exposure; without this the sunlit vehicle and Moon clip to white.
		UCameraComponent* Lens = Camera->GetCameraComponent();
		Lens->PostProcessBlendWeight = 1.0f;
		Lens->PostProcessSettings.bOverride_AutoExposureBias = true;
		Lens->PostProcessSettings.AutoExposureBias = CVarOrbitalFlightExposure.GetValueOnGameThread();
	}

	// What is still part of the vehicle when this event starts.
	const bool bLiftoff = Name == "LIFTOFF", bSeparation = Name == "STAGE1_SEPARATION";
	const bool bFairingEvent = Name == "FAIRING_RELEASE", bParking = Name == "PARKING_ORBIT";
	const bool bTli = Name == "TLI_COMPLETE", bReport = Name == "MISSION_REPORT";
	SetAttached(Stage1, bLiftoff || bSeparation);
	SetAttached(Fairing, bLiftoff || bSeparation || bFairingEvent);
	SetAttached(Stage2, bLiftoff || bSeparation || bFairingEvent || bParking || bTli);
	SetAttached(Probe, !bReport);
	const bool bPanelsOut = !(bLiftoff || bSeparation || bFairingEvent || bParking || bReport);
	if (Panels.Component)
	{
		Panels.Component->SetVisibility(bPanelsOut, true);
	}

	const FVector ProbeFocus(0, 0, (Stage2TopM + ProbeHeightM / 2.0) * 100.0);
	if (bLiftoff)
	{
		SetActorLocationAndRotation(PadCm, FRotator::ZeroRotator);
		SetPadVisible(GetWorld(), true);
		// Ends where stage separation shows the vehicle, so the cut does not jump.
		AscentEndCm = PadCm + FVector(0, 0, SeparationHeightCm - Stage1TopM * 100.0);
		Mode = EMode::Ascent;
		FocusLocalCm = FVector(0, 0, TotalHeightM * 50.0);
		FocusSizeM = TotalHeightM;
		FocusFraction = 0.3;
		SetFill(false);
	}
	else if (bSeparation)
	{
		SetPadVisible(GetWorld(), false);
		const FVector Focus = PadCm + FVector(0, 0, SeparationHeightCm);
		SetActorLocationAndRotation(Focus - FVector(0, 0, Stage1TopM * 100.0), FRotator::ZeroRotator);
		MakeDebris(Stage1, FVector(0, 0, -300.0), FRotator(1.5, 0, 0.8));
		Mode = EMode::Tracked;
		DriftCm = FVector(0, 0, 300.0);
		FocusLocalCm = FVector(0, 0, Stage1TopM * 100.0);
		FocusSizeM = TotalHeightM + 10.0;
		FocusFraction = 0.6;
		if (Camera)
		{
			PlaceAboveEarth(*Camera, Focus);
			SetFill(true, Camera->GetActorLocation(), FVector::Dist(Camera->GetActorLocation(), GetActorLocation()));
		}
	}
	else if (bFairingEvent)
	{
		SetPadVisible(GetWorld(), false);
		const FVector Focus = PadCm + FVector(0, 0, FairingHeightCm);
		SetActorLocationAndRotation(Focus - ProbeFocus, FRotator::ZeroRotator);
		const FVector Sideways = Camera ? Camera->GetActorRightVector() : FVector::RightVector;
		MakeDebris(Fairing, Sideways * 180.0 + FVector(0, 0, -60.0), FRotator(0, 0, 6.0));
		Mode = EMode::Tracked;
		DriftCm = FVector(0, 0, 100.0);
		FocusLocalCm = ProbeFocus;
		FocusSizeM = FairingHeightM + 8.0;
		FocusFraction = 0.6;
		if (Camera)
		{
			PlaceAboveEarth(*Camera, Focus);
			SetFill(true, Camera->GetActorLocation(), FVector::Dist(Camera->GetActorLocation(), GetActorLocation()));
		}
	}
	else if (bReport)
	{
		Mode = EMode::Idle;
		SetFill(false);
		FillReport(Event);
	}
	else if (Camera)
	{
		if (bParking)
		{
			const double UpperLength = Stage2TopM + ProbeHeightM - Stage1TopM;
			PlaceInFrontOfCamera(*Camera, FVector(0, 0, (Stage1TopM + UpperLength / 2.0) * 100.0), UpperLength, 0.3);
		}
		else
		{
			PlaceInFrontOfCamera(*Camera, ProbeFocus, ProbeSpanM, bTli ? 0.2 : 0.25);
			if (bTli)
			{
				MakeDebris(Stage2, GetActorUpVector() * -220.0, FRotator(1.5, 0, 0));
			}
		}
	}

	// Engines: stage 1 from liftoff to separation, stage 2 from its ignition to TLI, probe engine at insertion.
	SetBurning(bLiftoff, bFairingEvent || bParking, Name == "INSERTION_BURN_START", bQuietEvent);
	if ((bSeparation || bFairingEvent || bTli) && !bQuietEvent)
	{
		PlayOneShot(SeparationSound, 0.9f);
	}
}

void AOrbitalFlightPlayback::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
	if (!bPlaying || !Player)
	{
		return;
	}
	if (!bPlayerReady)
	{
		bPlayerReady = true;
		Player->ClientSetHUD(AOrbitalFlightHUD::StaticClass());
		Player->bShowMouseCursor = true;
		Player->bEnableClickEvents = true;
		if (GEngine)
		{
			GEngine->ClearOnScreenDebugMessages(); // the assembly demo prints over the status line
		}
	}
	if (AOrbitalFlightHUD* Hud = Cast<AOrbitalFlightHUD>(Player->GetHUD()))
	{
		Hud->Playback = this;
	}
	if (Player->WasInputKeyJustPressed(EKeys::SpaceBar))
	{
		Skip();
	}

	const int32 Count = static_cast<int32>(Show.size());
	if (!bFinished)
	{
		if (PadHoldLeft > 0)
		{
			PadHoldLeft -= DeltaSeconds; // show clock stays at zero: display only, no result changes
		}
		else
		{
			DisplayNow += DeltaSeconds;
		}
		if (bTestMode && !bTestSkipped && CurrentEvent == 0 && DisplayNow > 6.0
			&& FParse::Param(FCommandLine::Get(), TEXT("OrbitalFlightTestSkip")))
		{
			bTestSkipped = true; // exercise the Skip path once
			Skip();
		}
	}
	while (NextEvent < Count && Show[NextEvent].displayTimeS <= DisplayNow)
	{
		bQuietEvent = NextEvent + 1 < Count && Show[NextEvent + 1].displayTimeS <= DisplayNow;
		ApplyEvent(NextEvent, *Player);
		++NextEvent;
	}
	if (NextEvent >= Count && !bFinished)
	{
		bFinished = true;
		DisplayNow = Show.back().displayTimeS;
		UE_LOG(LogOrbitalFlight, Display, TEXT("Flight playback finished at show %.2f s"), DisplayNow);
	}

	// Vehicle motion.
	if (Mode == EMode::Ascent && CurrentEvent >= 0)
	{
		const double Start = Show[CurrentEvent].displayTimeS;
		const double End = CurrentEvent + 1 < Count ? Show[CurrentEvent + 1].displayTimeS : Start + 20.0;
		const double U = FMath::Clamp((DisplayNow - Start) / FMath::Max(End - Start, 0.1), 0.0, 1.0);
		SetActorLocation(FMath::Lerp(PadCm, AscentEndCm, U * U * U));
	}
	else if (Mode == EMode::Tracked || Mode == EMode::Anchored)
	{
		AddActorWorldOffset(DriftCm * DeltaSeconds);
	}
	for (FGroup* Group : {&Stage1, &Stage2, &Fairing})
	{
		if (Group->bDebris && Group->Component)
		{
			Group->Component->AddWorldOffset(Group->VelocityCm * DeltaSeconds);
			Group->Component->AddLocalRotation(Group->SpinDeg * DeltaSeconds);
		}
	}
	if ((Mode == EMode::Ascent || Mode == EMode::Tracked) && ActiveCamera)
	{
		FrameSubject(*ActiveCamera, GetActorTransform().TransformPosition(FocusLocalCm), FocusSizeM, FocusFraction);
	}

	// Flame flicker.
	UStaticMeshComponent* Flames[] = {Stage1Flame, Stage2Flame, ProbeFlame};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (Flames[Index] && Flames[Index]->IsVisible())
		{
			const double Flicker = 1.0 + 0.10 * FMath::Sin(DisplayNow * 43.0 + Index) + 0.06 * FMath::Sin(DisplayNow * 71.0);
			Flames[Index]->SetRelativeScale3D(FlameBaseScale[Index] * FVector(1.0, 1.0, Flicker));
		}
	}
	// Heating glow: grows to a peak in the middle of the first-stage burn, gone before staging. Display only.
	if (HeatGlow)
	{
		double Heat = 0.0;
		if (Mode == EMode::Ascent && CurrentEvent == 0 && Count > 1)
		{
			const double Span = FMath::Max(Show[1].displayTimeS - Show[0].displayTimeS, 0.1);
			const double Phase = FMath::Clamp(((DisplayNow - Show[0].displayTimeS) / Span - 0.30) / 0.60, 0.0, 1.0);
			Heat = FMath::Sin(Phase * UE_DOUBLE_PI);
		}
		HeatGlow->SetVisibility(Heat > 0.03);
		if (Heat > 0.03)
		{
			const double Shimmer = 1.0 + 0.05 * FMath::Sin(DisplayNow * 37.0) + 0.04 * FMath::Sin(DisplayNow * 59.0);
			const double Wide = 4.6 * (0.55 + 0.45 * Heat) * Shimmer;
			HeatGlow->SetRelativeScale3D(FVector(Wide, Wide, 7.0 * (0.6 + 0.4 * Heat)));
		}
	}
	bTestOrbitDrag = bTestMode && CurrentEvent == 3 && DisplayNow - Show[3].displayTimeS > 5.0 && Count > 4;
	UpdateOrbitCamera(*Player);

	// -OrbitalFlightTest: one screenshot per event, then quit. Used to check a full run unattended.
	if (bTestMode && CurrentEvent >= 0)
	{
		if (bFinished)
		{
			FinishedForS += DeltaSeconds;
		}
		const double InEvent = bFinished ? FinishedForS : DisplayNow - Show[CurrentEvent].displayTimeS;
		const int32 ShotKey = CurrentEvent * 2 + (InEvent >= 9.0 ? 1 : 0);
		if (InEvent >= 2.5 && ShotKey > LastShotKey)
		{
			LastShotKey = ShotKey;
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots/FlightTest") /
				FString::Printf(TEXT("%02d_%s_%d.png"), CurrentEvent, *ToFString(Show[CurrentEvent].event), ShotKey % 2), false, false);
		}
		if (bFinished && FinishedForS > 3.5 && !Report.bRedesignRequested)
		{
			RequestRedesign(); // exercise the report button path
		}
		if (bFinished && FinishedForS > 5.0)
		{
			UKismetSystemLibrary::QuitGame(this, Player, EQuitPreference::Quit, false);
		}
	}
}

FString AOrbitalFlightPlayback::GetClockText() const
{
	const int32 Count = static_cast<int32>(Show.size());
	if (CurrentEvent < 0 || CurrentEvent >= Count)
	{
		return FString();
	}
	// Each event shows its own mission time; the display clock is compressed, so nothing is interpolated.
	const double Seconds = Show[CurrentEvent].missionTimeS;
	if (Seconds < 3600.0)
	{
		return FString::Printf(TEXT("T+ %02d:%02d"), static_cast<int32>(Seconds) / 60, static_cast<int32>(Seconds) % 60);
	}
	if (Seconds < 2.0 * 86400.0)
	{
		return FString::Printf(TEXT("T+ %.1f h"), Seconds / 3600.0);
	}
	if (Seconds < 60.0 * 86400.0)
	{
		return FString::Printf(TEXT("T+ %.1f days"), Seconds / 86400.0);
	}
	return FString::Printf(TEXT("T+ %.2f years"), Seconds / (365.25 * 86400.0));
}

FString AOrbitalFlightPlayback::GetPhaseText() const
{
	if (CurrentEvent < 0 || CurrentEvent >= static_cast<int32>(Show.size()))
	{
		return FString();
	}
	return ToFString(Show[CurrentEvent].phase).ToUpper() + TEXT("  |  ") + ToFString(Show[CurrentEvent].event);
}

// ---------------------------------------------------------------- flame, sound, player camera

// Bright additive cones under the engines (Content/Materials/FX/M_Flame). Shown only while that engine burns.
void AOrbitalFlightPlayback::AddFlames()
{
	if (!FlameMaterial)
	{
		return;
	}
	const auto Flame = [&](FGroup& Group, double WidthM, double LengthM) -> UStaticMeshComponent*
	{
		if (!Group.Component)
		{
			return nullptr;
		}
		// Cone turned over: wide end at the engines, tip pointing down.
		UStaticMeshComponent* Component = AddShape(Group.Component, EShape::Cone, FVector(0, 0, -LengthM / 2.0),
			FVector(WidthM, WidthM, LengthM), FLinearColor::White, FRotator(0, 0, 180));
		Component->SetMaterial(0, FlameMaterial);
		Component->SetCastShadow(false);
		Component->SetVisibility(false);
		return Component;
	};
	Stage1Flame = Flame(Stage1, 2.6, 16.0);
	Stage2Flame = Flame(Stage2, 1.2, 7.0);
	ProbeFlame = Flame(Probe, 0.3, 1.4);
	FlameBaseScale[0] = FVector(2.6, 2.6, 16.0);
	FlameBaseScale[1] = FVector(1.2, 1.2, 7.0);
	FlameBaseScale[2] = FVector(0.3, 0.3, 1.4);

	// Aerodynamic heating glow: a cone over the nose, shown only in the dense part of the ascent.
	if (Fairing.Component)
	{
		const double LengthM = 7.0;
		HeatGlow = AddShape(Fairing.Component, EShape::Cone, FVector(0, 0, FairingHeightM + 1.0 - LengthM / 2.0),
			FVector(4.6, 4.6, LengthM), FLinearColor::White, FRotator::ZeroRotator);
		HeatGlow->SetMaterial(0, FlameMaterial);
		HeatGlow->SetCastShadow(false);
		HeatGlow->SetVisibility(false);
	}
}

void AOrbitalFlightPlayback::SetBurning(bool bStage1, bool bStage2, bool bProbe, bool bQuiet)
{
	UStaticMeshComponent* Flames[] = {Stage1Flame, Stage2Flame, ProbeFlame};
	const bool bOn[] = {bStage1, bStage2, bProbe};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (Flames[Index])
		{
			Flames[Index]->SetVisibility(bOn[Index]);
		}
	}
	// NASA recordings (Content/Audio/NASA, sources in Docs/CHALLENGE_TRACEABILITY.md) replace the
	// synthesized engine loop and add an ignition sound. If they have not been imported, the
	// synthesized sounds stay in use, so the flight works either way.
	if (!bNasaSoundsChecked)
	{
		bNasaSoundsChecked = true;
		if (USoundBase* NasaEngine = LoadObject<USoundBase>(nullptr,
			TEXT("/Game/Audio/NASA/S_NASA_EngineLoop.S_NASA_EngineLoop"), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			EngineSound = NasaEngine;
		}
		IgnitionSound = LoadObject<USoundBase>(nullptr,
			TEXT("/Game/Audio/NASA/S_NASA_Ignition.S_NASA_Ignition"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (bStage1 && !bStage1Burning && !bQuiet)
	{
		PlayOneShot(IgnitionSound, 1.0f);
	}
	bStage1Burning = bStage1;

	const bool bAny = bStage1 || bStage2 || bProbe;
	if (!EngineAudio && EngineSound && bAny && !bQuiet)
	{
		EngineAudio = UGameplayStatics::CreateSound2D(this, EngineSound, 1.0f, 1.0f, 0.0f, nullptr, false, false);
	}
	if (EngineAudio)
	{
		if (bAny && !bQuiet)
		{
			EngineAudio->SetVolumeMultiplier(bStage1 ? 0.9f : bStage2 ? 0.5f : 0.3f);
			EngineAudio->SetPitchMultiplier(bStage1 ? 1.0f : bStage2 ? 1.25f : 1.6f);
			if (!EngineAudio->IsPlaying())
			{
				EngineAudio->Play();
			}
		}
		else if (EngineAudio->IsPlaying())
		{
			EngineAudio->FadeOut(0.6f, 0.0f);
		}
	}
}

void AOrbitalFlightPlayback::PlayOneShot(USoundBase* Sound, float Volume)
{
	if (Sound)
	{
		UGameplayStatics::PlaySound2D(this, Sound, Volume);
	}
}

// Player camera: hold the right mouse button and drag to orbit the vehicle, wheel to zoom.
// The next timeline event returns to its own camera.
void AOrbitalFlightPlayback::UpdateOrbitCamera(APlayerController& Player)
{
	if (Report.bVisible || Mode == EMode::Idle || !ActiveCamera)
	{
		bOrbiting = false;
		return;
	}
	const bool bDrag = Player.IsInputKeyDown(EKeys::RightMouseButton) || bTestOrbitDrag;
	const double Wheel = (Player.WasInputKeyJustPressed(EKeys::MouseScrollUp) ? 1.0 : 0.0)
		- (Player.WasInputKeyJustPressed(EKeys::MouseScrollDown) ? 1.0 : 0.0);
	const FVector Focus = GetActorTransform().TransformPosition(FocusLocalCm);
	const double MinDistance = FocusSizeM * 100.0 * 0.9, MaxDistance = FocusSizeM * 100.0 * 40.0;
	if (!bOrbiting && (bDrag || Wheel != 0.0))
	{
		// Start from where the event camera is, so the view does not jump.
		const FVector Offset = ActiveCamera->GetActorLocation() - Focus;
		const FRotator Direction = Offset.Rotation();
		OrbitYaw = Direction.Yaw;
		OrbitPitch = Direction.Pitch;
		OrbitDistanceCm = FMath::Clamp(Offset.Size(), MinDistance, MaxDistance);
		if (!OrbitCamera && GetWorld())
		{
			OrbitCamera = GetWorld()->SpawnActor<ACameraActor>();
		}
		if (!OrbitCamera)
		{
			return;
		}
		UCameraComponent* Lens = OrbitCamera->GetCameraComponent();
		Lens->SetConstraintAspectRatio(false);
		Lens->SetFieldOfView(ActiveCamera->GetCameraComponent()->FieldOfView);
		Lens->PostProcessBlendWeight = 1.0f;
		Lens->PostProcessSettings.bOverride_AutoExposureBias = true;
		Lens->PostProcessSettings.AutoExposureBias = CVarOrbitalFlightExposure.GetValueOnGameThread();
		Player.SetViewTargetWithBlend(OrbitCamera, 0.0f);
		bOrbiting = true;
		UE_LOG(LogOrbitalFlight, Display, TEXT("Player camera: orbit started at %.0f m"), OrbitDistanceCm / 100.0);
	}
	if (!bOrbiting || !OrbitCamera)
	{
		return;
	}
	if (bDrag)
	{
		float DeltaX = 0.0f, DeltaY = 0.0f;
		Player.GetInputMouseDelta(DeltaX, DeltaY);
		if (bTestOrbitDrag)
		{
			DeltaX = 0.5f;
			DeltaY = 0.1f;
		}
		OrbitYaw += DeltaX * 2.0;
		OrbitPitch = FMath::Clamp(OrbitPitch + DeltaY * 2.0, -85.0, 85.0);
	}
	if (Wheel != 0.0)
	{
		OrbitDistanceCm = FMath::Clamp(OrbitDistanceCm * (Wheel > 0 ? 0.85 : 1.0 / 0.85), MinDistance, MaxDistance);
	}
	const FVector Location = Focus + FRotator(OrbitPitch, OrbitYaw, 0.0).Vector() * OrbitDistanceCm;
	OrbitCamera->SetActorLocationAndRotation(Location, (Focus - Location).Rotation());
}

// ---------------------------------------------------------------- HUD

void AOrbitalFlightHUD::DrawHUD()
{
	Super::DrawHUD();
	const AOrbitalFlightPlayback* Flight = Playback.Get();
	if (!Flight || !Flight->IsPlaying() || !Canvas || !GEngine)
	{
		return;
	}
	const float Width = Canvas->SizeX, Height = Canvas->SizeY;
	const float Unit = FMath::Max(0.75f, Height / 720.0f);
	UFont* Font = GEngine->GetLargeFont();
	const float TextScale = 1.9f * Unit, SmallScale = 1.3f * Unit;
	const FLinearColor Panel(0.0f, 0.0f, 0.0f, 0.65f);
	if (Flight->GetReport().bVisible)
	{
		DrawReport(Flight->GetReport(), Width, Height, Unit, Font);
		return;
	}

	// Status line: word-wrapped and centred at the bottom.
	TArray<FString> Lines;
	{
		TArray<FString> Words;
		Flight->GetStatusMessage().ParseIntoArray(Words, TEXT(" "));
		FString Line;
		for (const FString& Word : Words)
		{
			const FString Candidate = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
			float CandidateWidth = 0, CandidateHeight = 0;
			GetTextSize(Candidate, CandidateWidth, CandidateHeight, Font, TextScale);
			if (CandidateWidth > Width * 0.8f && !Line.IsEmpty())
			{
				Lines.Add(Line);
				Line = Word;
			}
			else
			{
				Line = Candidate;
			}
		}
		if (!Line.IsEmpty())
		{
			Lines.Add(Line);
		}
		if (!Flight->GetStatusDetail().IsEmpty())
		{
			Lines.Add(Flight->GetStatusDetail());
		}
	}
	float LineWidth = 0, LineHeight = 0;
	GetTextSize(TEXT("Ag"), LineWidth, LineHeight, Font, TextScale);
	const float Padding = 14.0f * Unit;
	const float BarHeight = Lines.Num() * LineHeight * 1.25f + Padding * 2.0f;
	const float BarTop = Height - BarHeight - 18.0f * Unit;
	DrawRect(Panel, Width * 0.06f, BarTop, Width * 0.88f, BarHeight);
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		float TextWidth = 0, TextHeight = 0;
		GetTextSize(Lines[Index], TextWidth, TextHeight, Font, TextScale);
		DrawText(Lines[Index], FLinearColor::White, (Width - TextWidth) * 0.5f,
			BarTop + Padding + Index * LineHeight * 1.25f, Font, TextScale);
	}

	// Mission clock and phase, top left.
	const FString Clock = Flight->GetClockText(), Phase = Flight->GetPhaseText();
	float ClockWidth = 0, ClockHeight = 0, PhaseWidth = 0, PhaseHeight = 0;
	GetTextSize(Clock, ClockWidth, ClockHeight, Font, TextScale);
	GetTextSize(Phase, PhaseWidth, PhaseHeight, Font, SmallScale);
	const float InfoX = 24.0f * Unit, InfoY = 20.0f * Unit;
	DrawRect(Panel, InfoX - 10.0f * Unit, InfoY - 8.0f * Unit, FMath::Max(ClockWidth, PhaseWidth) + 20.0f * Unit,
		ClockHeight + PhaseHeight + 22.0f * Unit);
	DrawText(Clock, FLinearColor::White, InfoX, InfoY, Font, TextScale);
	DrawText(Phase, FLinearColor(0.7f, 0.85f, 1.0f), InfoX, InfoY + ClockHeight + 6.0f * Unit, Font, SmallScale);
	DrawText(TEXT("Right mouse: orbit   Wheel: zoom"), FLinearColor(0.75f, 0.75f, 0.75f), InfoX,
		InfoY + ClockHeight + PhaseHeight + 30.0f * Unit, Font, SmallScale * 0.9f);

	// Skip button, bottom right above the status line.
	if (!Flight->IsFinished())
	{
		const FString Label = TEXT("Skip  >>");
		float LabelWidth = 0, LabelHeight = 0;
		GetTextSize(Label, LabelWidth, LabelHeight, Font, TextScale);
		const FVector2D Size(LabelWidth + 36.0f * Unit, LabelHeight + 18.0f * Unit);
		const FVector2D Position(Width * 0.94f - Size.X, BarTop - Size.Y - 10.0f * Unit);
		DrawRect(FLinearColor(0.1f, 0.35f, 0.75f, 0.9f), Position.X, Position.Y, Size.X, Size.Y);
		DrawText(Label, FLinearColor::White, Position.X + 18.0f * Unit, Position.Y + 9.0f * Unit, Font, TextScale);
		AddHitBox(Position, Size, TEXT("OrbitalSkip"), true);
	}
}

TArray<FString> AOrbitalFlightHUD::Wrap(const FString& Text, float MaxWidth, UFont* Font, float Scale) const
{
	TArray<FString> Lines, Words;
	Text.ParseIntoArray(Words, TEXT(" "));
	FString Line;
	for (const FString& Word : Words)
	{
		const FString Candidate = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
		float CandidateWidth = 0, CandidateHeight = 0;
		GetTextSize(Candidate, CandidateWidth, CandidateHeight, Font, Scale);
		if (CandidateWidth > MaxWidth && !Line.IsEmpty())
		{
			Lines.Add(Line);
			Line = Word;
		}
		else
		{
			Line = Candidate;
		}
	}
	if (!Line.IsEmpty())
	{
		Lines.Add(Line);
	}
	return Lines;
}

// Report screen: result, phase reached, score, cause and fix for each result code, Redesign button.
void AOrbitalFlightHUD::DrawReport(const FOrbitalFlightReport& Report, float Width, float Height, float Unit, UFont* Font)
{
	struct FRow
	{
		FString Text;
		float Scale = 1.0f;
		FLinearColor Color = FLinearColor::White;
		float Gap = 0.0f;
	};
	const float TitleScale = 2.6f * Unit, TextScale = 1.7f * Unit;
	const float PanelWidth = Width * 0.72f, Left = (Width - PanelWidth) * 0.5f, Padding = 26.0f * Unit;
	TArray<FRow> Rows;
	const auto Add = [&](const FString& Text, float Scale, const FLinearColor& Color, float Gap)
	{
		bool bFirst = true;
		for (const FString& Line : Wrap(Text, PanelWidth - Padding * 2.0f, Font, Scale))
		{
			FRow Row;
			Row.Text = Line;
			Row.Scale = Scale;
			Row.Color = Color;
			Row.Gap = bFirst ? Gap : 0.0f;
			Rows.Add(Row);
			bFirst = false;
		}
	};
	Add(Report.Title, TitleScale, Report.bSuccess ? FLinearColor(0.45f, 1.0f, 0.5f) : FLinearColor(1.0f, 0.6f, 0.3f), 0.0f);
	Add(FString::Printf(TEXT("Phase reached: %s"), *Report.PhaseReached.ToUpper()), TextScale, FLinearColor::White, 16.0f * Unit);
	Add(FString::Printf(TEXT("Score: %.1f     Science: %.1f"), Report.Score, Report.Science), TextScale, FLinearColor::White, 4.0f * Unit);
	for (int32 Index = 0; Index < Report.Codes.Num(); ++Index)
	{
		Add(Report.Codes[Index], TextScale, FLinearColor(0.7f, 0.85f, 1.0f), 18.0f * Unit);
		Add(TEXT("Cause: ") + Report.Causes[Index], TextScale, FLinearColor::White, 4.0f * Unit);
		if (!Report.Fixes[Index].IsEmpty())
		{
			Add(TEXT("Fix: ") + Report.Fixes[Index], TextScale, FLinearColor::White, 4.0f * Unit);
		}
	}

	const FString Label = Report.bRedesignRequested ? TEXT("Redesign requested") : TEXT("Redesign");
	float LabelWidth = 0, LabelHeight = 0;
	GetTextSize(Label, LabelWidth, LabelHeight, Font, TextScale);
	const FVector2D ButtonSize(LabelWidth + 48.0f * Unit, LabelHeight + 22.0f * Unit);
	float ContentHeight = 0;
	TArray<float> RowHeights;
	for (const FRow& Row : Rows)
	{
		float RowWidth = 0, RowHeight = 0;
		GetTextSize(TEXT("Ag"), RowWidth, RowHeight, Font, Row.Scale);
		RowHeights.Add(RowHeight * 1.2f);
		ContentHeight += Row.Gap + RowHeight * 1.2f;
	}
	const float PanelHeight = ContentHeight + Padding * 3.0f + ButtonSize.Y;
	const float Top = FMath::Max(8.0f * Unit, (Height - PanelHeight) * 0.5f);
	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.82f), Left, Top, PanelWidth, PanelHeight);
	float Y = Top + Padding;
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		Y += Rows[Index].Gap;
		DrawText(Rows[Index].Text, Rows[Index].Color, Left + Padding, Y, Font, Rows[Index].Scale);
		Y += RowHeights[Index];
	}
	const FVector2D ButtonPosition(Left + (PanelWidth - ButtonSize.X) * 0.5f, Top + PanelHeight - Padding - ButtonSize.Y);
	DrawRect(Report.bRedesignRequested ? FLinearColor(0.25f, 0.25f, 0.25f, 0.9f) : FLinearColor(0.1f, 0.35f, 0.75f, 0.95f),
		ButtonPosition.X, ButtonPosition.Y, ButtonSize.X, ButtonSize.Y);
	DrawText(Label, FLinearColor::White, ButtonPosition.X + 24.0f * Unit, ButtonPosition.Y + 11.0f * Unit, Font, TextScale);
	if (!Report.bRedesignRequested)
	{
		AddHitBox(ButtonPosition, ButtonSize, TEXT("OrbitalRedesign"), true);
	}
}

void AOrbitalFlightHUD::NotifyHitBoxClick(FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);
	if (BoxName == TEXT("OrbitalSkip") && Playback.IsValid())
	{
		Playback->Skip();
	}
	if (BoxName == TEXT("OrbitalRedesign") && Playback.IsValid())
	{
		Playback->RequestRedesign();
	}
}

// ---------------------------------------------------------------- auto start

void UOrbitalFlightSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.IsGameWorld() || CVarOrbitalFlightDemo.GetValueOnGameThread() == 0
		|| !InWorld.GetMapName().Contains(TEXT("L_Mission")))
	{
		return;
	}
	if (AOrbitalFlightPlayback* Flight = InWorld.SpawnActor<AOrbitalFlightPlayback>())
	{
		FString Case = CVarOrbitalFlightCase.GetValueOnGameThread();
		FParse::Value(FCommandLine::Get(), TEXT("OrbitalFlightCase="), Case);
		Flight->StartCase(Case);
	}
}
