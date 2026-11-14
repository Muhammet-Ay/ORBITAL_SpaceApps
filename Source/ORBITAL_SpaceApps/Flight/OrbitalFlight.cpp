#include "OrbitalFlight.h"

#include "../Assembly/OrbitalAssemblyScreenObject.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogOrbitalFlight, Log, All);

static TAutoConsoleVariable<int32> CVarOrbitalFlightDemo(
	TEXT("orbital.FlightDemo"), 1,
	TEXT("1: play the TC-A flight when play starts on the L_Mission map. 0: off."));

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

void AOrbitalFlightPlayback::StartFlight(const Orbital::Catalog& Catalog, const Orbital::SpacecraftDesign& Spacecraft,
	const Orbital::LauncherDesign& Launcher, const Orbital::MissionResult& Result, const Orbital::Timeline& InTimeline)
{
	if (bPlaying || InTimeline.empty())
	{
		return;
	}
	Show = InTimeline;
	for (size_t Index = 1; Index < Show.size(); ++Index)
	{
		Show[Index].displayTimeS = FMath::Max(Show[Index].displayTimeS, Show[Index - 1].displayTimeS + MinEventDwellS);
	}
	LiftoffTwr = Result.lv.firstTwr;
	BuildVehicle(Catalog, Spacecraft, Launcher);

	// The pad is at the world origin (L_Mission). Stand the rocket on whatever is there.
	PadCm = FVector(0, 0, 600);
	FHitResult Hit;
	FCollisionQueryParams Query(FName(TEXT("OrbitalPad")), false, this);
	if (GetWorld() && GetWorld()->LineTraceSingleByChannel(Hit, FVector(0, 0, 50000), FVector(0, 0, -5000), ECC_Visibility, Query))
	{
		PadCm = Hit.Location;
	}
	SetActorLocationAndRotation(PadCm, FRotator::ZeroRotator);

	bTestMode = FParse::Param(FCommandLine::Get(), TEXT("OrbitalFlightTest"));
	DisplayNow = 0;
	NextEvent = 0;
	CurrentEvent = -1;
	bFinished = false;
	bPlaying = true;
	UE_LOG(LogOrbitalFlight, Display, TEXT("Flight started: %d events, rocket %.1f m tall, pad at Z=%.0f cm"),
		static_cast<int32>(Show.size()), TotalHeightM, PadCm.Z);
	for (const Orbital::TimelineEvent& Event : Show)
	{
		UE_LOG(LogOrbitalFlight, Display, TEXT("  show %.2f s  %s  [%s/%s]"), Event.displayTimeS,
			*ToFString(Event.event), *ToFString(Event.vehicle), *ToFString(Event.camera));
	}
}

bool AOrbitalFlightPlayback::StartTCA()
{
	DemoScreen = NewObject<UOrbitalAssemblyScreen>(this);
	DemoScreen->InitCatalog();
	static const TCHAR* const ProbeParts[] = {
		TEXT("bus_small"), TEXT("sc_tank_s"), TEXT("sc_engine_mono"), TEXT("rcs"), TEXT("solar_s"),
		TEXT("battery_s"), TEXT("antenna_lga"), TEXT("obc_basic"), TEXT("inst_neutron")};
	static const TCHAR* const RocketParts[] = {
		TEXT("s1_tank_200"), TEXT("s1_engine_kl190"), TEXT("sep_system"), TEXT("s2_tank_7"),
		TEXT("s2_engine_hl10"), TEXT("fairing_s")};
	bool bBuilt = true;
	for (const TCHAR* Part : ProbeParts)
	{
		bBuilt = DemoScreen->Attach(Part).bOk && bBuilt;
	}
	bBuilt = DemoScreen->SetPropellant(130.0).bOk && bBuilt;
	bBuilt = DemoScreen->SetOrbit(TEXT("LPO_100")).bOk && bBuilt;
	for (const TCHAR* Part : RocketParts)
	{
		bBuilt = DemoScreen->Attach(Part).bOk && bBuilt;
	}
	bBuilt = DemoScreen->SetStage1EngineCount(2).bOk && bBuilt;
	const Orbital::AssemblySnapshot* Snapshot = DemoScreen->GetSnapshot();
	const FOrbitalAssemblyData* Data = DemoScreen->GetData();
	if (!bBuilt || !Snapshot || !Data)
	{
		UE_LOG(LogOrbitalFlight, Error, TEXT("TC-A could not be assembled; flight not started"));
		return false;
	}
	const Orbital::MissionResult Result = Orbital::Evaluate(
		Data->Parts, Data->Mission, Snapshot->spacecraft, Snapshot->launcher, "O1_ICE", "normal");
	const Orbital::Timeline Timeline = Orbital::BuildTimeline(
		Data->Parts, Data->Mission, Snapshot->spacecraft, Snapshot->launcher, Result);
	UE_LOG(LogOrbitalFlight, Display, TEXT("TC-A score %.4f (expected 33.5111), %d timeline events"),
		Result.score, static_cast<int32>(Timeline.size()));
	StartFlight(Data->Parts, Snapshot->spacecraft, Snapshot->launcher, Result, Timeline);
	return bPlaying;
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
	DisplayNow = FMath::Max(DisplayNow, Target);
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
	const FName Tag(*ToFString(Event.vehicle + "/" + Event.camera));
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

// Point on the vertical line above the pad that the camera looks at.
bool AOrbitalFlightPlayback::AscentAnchor(const ACameraActor& Camera, FVector& OutAnchor) const
{
	const FVector Location = Camera.GetActorLocation();
	const FVector Forward = Camera.GetActorForwardVector();
	const double Denominator = Forward.X * Forward.X + Forward.Y * Forward.Y;
	if (Denominator < 1.0e-6)
	{
		return false;
	}
	const double Along = -((Location.X - PadCm.X) * Forward.X + (Location.Y - PadCm.Y) * Forward.Y) / Denominator;
	if (Along <= 0)
	{
		return false;
	}
	OutAnchor = FVector(PadCm.X, PadCm.Y, Location.Z + Along * Forward.Z);
	return true;
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
	Lens->SetFieldOfView(static_cast<float>(FMath::Clamp(Needed, 5.0, static_cast<double>(*Original))));
}

void AOrbitalFlightPlayback::ApplyEvent(int32 Index, APlayerController& Player)
{
	const Orbital::TimelineEvent& Event = Show[Index];
	const std::string& Name = Event.event;
	CurrentEvent = Index;
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
	FVector Anchor;
	if (bLiftoff)
	{
		SetActorLocationAndRotation(PadCm, FRotator::ZeroRotator);
		AscentEndCm = PadCm + FVector(0, 0, 700000.0);
		if (Index + 1 < static_cast<int32>(Show.size()))
		{
			const ACameraActor* Next = FindCamera(Show[Index + 1]);
			if (Next && AscentAnchor(*Next, Anchor))
			{
				AscentEndCm = Anchor - FVector(0, 0, Stage1TopM * 100.0);
			}
		}
		Mode = EMode::Ascent;
		FocusLocalCm = FVector(0, 0, TotalHeightM * 50.0);
		FocusSizeM = TotalHeightM;
		FocusFraction = 0.3;
		SetFill(false);
	}
	else if (bSeparation)
	{
		if (Camera && AscentAnchor(*Camera, Anchor))
		{
			SetActorLocationAndRotation(Anchor - FVector(0, 0, Stage1TopM * 100.0), FRotator::ZeroRotator);
		}
		MakeDebris(Stage1, FVector(0, 0, -300.0), FRotator(1.5, 0, 0.8));
		Mode = EMode::Tracked;
		DriftCm = FVector(0, 0, 300.0);
		FocusLocalCm = FVector(0, 0, Stage1TopM * 100.0);
		FocusSizeM = TotalHeightM + 10.0;
		FocusFraction = 0.6;
		if (Camera)
		{
			SetFill(true, Camera->GetActorLocation(), FVector::Dist(Camera->GetActorLocation(), GetActorLocation()));
		}
	}
	else if (bFairingEvent)
	{
		if (Camera && AscentAnchor(*Camera, Anchor))
		{
			SetActorLocationAndRotation(Anchor - ProbeFocus, FRotator::ZeroRotator);
		}
		const FVector Sideways = Camera ? Camera->GetActorRightVector() : FVector::RightVector;
		MakeDebris(Fairing, Sideways * 180.0 + FVector(0, 0, -60.0), FRotator(0, 0, 6.0));
		Mode = EMode::Tracked;
		DriftCm = FVector(0, 0, 100.0);
		FocusLocalCm = ProbeFocus;
		FocusSizeM = FairingHeightM + 8.0;
		FocusFraction = 0.6;
		if (Camera)
		{
			SetFill(true, Camera->GetActorLocation(), FVector::Dist(Camera->GetActorLocation(), GetActorLocation()));
		}
	}
	else if (bReport)
	{
		Mode = EMode::Idle;
		SetFill(false);
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
		DisplayNow += DeltaSeconds;
		if (bTestMode && !bTestSkipped && CurrentEvent == 0 && DisplayNow > 6.0
			&& FParse::Param(FCommandLine::Get(), TEXT("OrbitalFlightTestSkip")))
		{
			bTestSkipped = true; // exercise the Skip path once
			Skip();
		}
	}
	while (NextEvent < Count && Show[NextEvent].displayTimeS <= DisplayNow)
	{
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
	double Seconds = Show[CurrentEvent].missionTimeS;
	if (CurrentEvent + 1 < Count)
	{
		const double Start = Show[CurrentEvent].displayTimeS, End = Show[CurrentEvent + 1].displayTimeS;
		if (End > Start)
		{
			Seconds = FMath::Lerp(Seconds, Show[CurrentEvent + 1].missionTimeS,
				FMath::Clamp((DisplayNow - Start) / (End - Start), 0.0, 1.0));
		}
	}
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

void AOrbitalFlightHUD::NotifyHitBoxClick(FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);
	if (BoxName == TEXT("OrbitalSkip") && Playback.IsValid())
	{
		Playback->Skip();
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
		Flight->StartTCA();
	}
}
