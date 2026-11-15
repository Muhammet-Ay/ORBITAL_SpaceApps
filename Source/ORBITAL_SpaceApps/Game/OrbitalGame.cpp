#include "OrbitalGame.h"

#include "../Assembly/OrbitalAssemblyScreenObject.h"
#include "../Flight/OrbitalFlight.h"
#include "../OrbitalCatalogLoader.h"
#include "AudioDevice.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Canvas.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogOrbitalGame, Log, All);

static TAutoConsoleVariable<int32> CVarOrbitalGame(
	TEXT("orbital.Game"), 1,
	TEXT("1: L_Mission starts on the assembly screen and flies the player's design. 0: off (reference flights)."));

// Names are prefixed: other .cpp files of this module have their own ToFString helpers,
// and a unity build puts them in one translation unit.
static FString OrbitalGameText(const std::string& Text)
{
	return FString(UTF8_TO_TCHAR(Text.c_str()));
}

static std::string OrbitalGameId(const FString& Text)
{
	return std::string(TCHAR_TO_UTF8(*Text));
}

static bool OrbitalGameEnabled()
{
	// Muhammet's automated flight runs pass these switches; leave them alone.
	FString Ignored;
	return CVarOrbitalGame.GetValueOnGameThread() != 0
		&& !FParse::Param(FCommandLine::Get(), TEXT("OrbitalFlightTest"))
		&& !FParse::Value(FCommandLine::Get(), TEXT("OrbitalFlightCase="), Ignored);
}

static FString OrbitalSocketTitle(const Orbital::Id& Socket, int32 Index)
{
	struct FTitle
	{
		const char* Socket;
		const TCHAR* Title;
		bool bNumbered;
	};
	static const FTitle Titles[] = {
		{"stage1-tank", TEXT("Stage 1 tank"), false}, {"stage1-engine", TEXT("Stage 1 engine"), false},
		{"stage2-tank", TEXT("Stage 2 tank"), false}, {"stage2-engine", TEXT("Stage 2 engine"), false},
		{"fairing", TEXT("Fairing"), false}, {"probe-core", TEXT("Spacecraft bus"), false},
		{"probe-tank", TEXT("Propellant tank"), true}, {"probe-engine", TEXT("Spacecraft engine"), false},
		{"adcs", TEXT("Attitude control"), true}, {"solar", TEXT("Solar array"), true},
		{"battery", TEXT("Battery"), true}, {"antenna", TEXT("Antenna"), false},
		{"avionics", TEXT("Computer"), false}, {"instrument", TEXT("Instrument deck"), true},
	};
	for (const FTitle& Entry : Titles)
	{
		if (Socket == Entry.Socket)
		{
			if (!Entry.bNumbered)
			{
				return Entry.Title;
			}
			if (Socket == "instrument")
			{
				return FString(Entry.Title) + TEXT(" ") + FString::Chr(static_cast<TCHAR>(TEXT('A') + Index));
			}
			return FString::Printf(TEXT("%s %d"), Entry.Title, Index + 1);
		}
	}
	return OrbitalGameText(Orbital::Assembly::SocketName(Socket, Index));
}

// Short button labels for mission plan choices; the action still carries the catalog ID.
// Builder layout, as fractions of the screen. The window shows the 3D model behind the HUD.
static const float OrbitalWindowLeft = 0.43f, OrbitalWindowRight = 0.655f;

// Part categories of the builder. Stage 0 is the spacecraft, stage 1 the rocket.
struct FOrbitalCategory
{
	int32 Stage;
	const TCHAR* Title;
	const char* Sockets[2];
};
static const FOrbitalCategory OrbitalCategories[] = {
	{0, TEXT("Structure"), {"probe-core", ""}},
	{0, TEXT("Propulsion"), {"probe-engine", "probe-tank"}},
	{0, TEXT("Power"), {"solar", "battery"}},
	{0, TEXT("Control"), {"adcs", "avionics"}},
	{0, TEXT("Comms"), {"antenna", ""}},
	{0, TEXT("Instruments"), {"instrument", ""}},
	{1, TEXT("Stage 1"), {"stage1-tank", "stage1-engine"}},
	{1, TEXT("Stage 2"), {"stage2-tank", "stage2-engine"}},
	{1, TEXT("Fairing"), {"fairing", ""}},
};

static bool OrbitalCategoryHas(const FOrbitalCategory& Entry, const Orbital::Id& Socket)
{
	return Socket == Entry.Sockets[0] || Socket == Entry.Sockets[1];
}

// The N-th category of a stage; null past the end.
static const FOrbitalCategory* OrbitalCategoryAt(int32 Stage, int32 Index)
{
	for (const FOrbitalCategory& Entry : OrbitalCategories)
	{
		if (Entry.Stage == Stage && Index-- == 0)
		{
			return &Entry;
		}
	}
	return nullptr;
}

static FString OrbitalChoiceLabel(const FString& Id)
{
	struct FChoiceLabel
	{
		const TCHAR* Id;
		const TCHAR* Label;
	};
	static const FChoiceLabel Labels[] = {
		{TEXT("O1_ICE"), TEXT("Ice")}, {TEXT("O2_TOPO"), TEXT("Terrain")}, {TEXT("O3_THERMAL"), TEXT("Thermal")},
		{TEXT("O4_IMAGING"), TEXT("Imaging")}, {TEXT("O5_RADAR"), TEXT("Radar")},
		{TEXT("LPO_50"), TEXT("50 km")}, {TEXT("LPO_100"), TEXT("100 km")}, {TEXT("ELL_30x216"), TEXT("30 x 216 km")},
		{TEXT("single_station"), TEXT("1 station")}, {TEXT("network_3"), TEXT("3 stations")},
		{TEXT("easy"), TEXT("Easy")}, {TEXT("normal"), TEXT("Normal")}, {TEXT("hard"), TEXT("Hard")},
	};
	for (const FChoiceLabel& Entry : Labels)
	{
		if (Id == Entry.Id)
		{
			return Entry.Label;
		}
	}
	return Id;
}

static FString OrbitalNumber(double Value)
{
	const double Size = FMath::Abs(Value);
	if (Size >= 100.0 || Value == FMath::RoundToDouble(Value))
	{
		return FString::Printf(TEXT("%.0f"), Value);
	}
	return Size >= 10.0 ? FString::Printf(TEXT("%.1f"), Value) : FString::Printf(TEXT("%.2f"), Value);
}

// ---------------------------------------------------------------- game state

void UOrbitalGameStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// The automated loop test and its recording mode drive the assembly screen directly.
	if (FParse::Param(FCommandLine::Get(), TEXT("OrbitalLoopTest")) || FParse::Param(FCommandLine::Get(), TEXT("OrbitalLoopRecord")))
	{
		Screen = EOrbitalScreen::Game;
	}
}

void UOrbitalGameStateSubsystem::SetVolumePercent(int32 Percent, UWorld* World)
{
	VolumePercent = FMath::Clamp(Percent, 0, 100);
	if (FAudioDevice* Device = World ? World->GetAudioDeviceRaw() : nullptr)
	{
		Device->SetTransientPrimaryVolume(VolumePercent / 100.0f);
	}
}

int32 UOrbitalGameStateSubsystem::GetQuality() const
{
	const UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	return Settings ? Settings->GetOverallScalabilityLevel() : -1;
}

void UOrbitalGameStateSubsystem::SetQuality(int32 Level)
{
	if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		Settings->SetOverallScalabilityLevel(FMath::Clamp(Level, 0, 3));
		Settings->ApplyNonResolutionSettings();
		Settings->SaveSettings();
	}
}

void UOrbitalGameStateSubsystem::Deinitialize()
{
	Flow.Reset();
	Data.Reset();
	Super::Deinitialize();
}

Orbital::GameFlow* UOrbitalGameStateSubsystem::GetFlow()
{
	if (Flow.IsValid() || bLoadTried)
	{
		return Flow.Get();
	}
	bLoadTried = true;

	FOrbitalCatalog Catalog;
	const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/CLAUDE_RESEARCH/PART_CATALOG.json"));
	if (!LoadOrbitalCatalog(Path, Catalog, LoadError))
	{
		UE_LOG(LogOrbitalGame, Error, TEXT("Could not load %s: %s"), *Path, *LoadError);
		return nullptr;
	}

	// Part display names from the catalog JSON (the engineering Part struct has no name).
	if (Catalog.Source.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
		if (Catalog.Source->TryGetArrayField(TEXT("parts"), Parts) && Parts)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Parts)
			{
				if (!Value.IsValid() || Value->Type != EJson::Object)
				{
					continue;
				}
				const TSharedPtr<FJsonObject> Object = Value->AsObject();
				FString Id, Name;
				if (Object.IsValid() && Object->TryGetStringField(TEXT("id"), Id)
					&& Object->TryGetStringField(TEXT("name"), Name))
				{
					PartNames.Add(Id, Name);
				}
			}
		}
	}

	// Cause and Fix columns of the validation table in ORBITAL_UI_COPY_v2.1.md, keyed by code.
	FString Copy;
	for (const TCHAR* Relative : {TEXT("Docs/CLAUDE_RESEARCH/ORBITAL_UI_COPY_v2.1.md"), TEXT("Source/ORBITAL_SpaceApps/ORBITAL_UI_COPY_v2.1.md")})
	{
		if (FFileHelper::LoadFileToString(Copy, *FPaths::Combine(FPaths::ProjectDir(), Relative)))
		{
			break;
		}
	}
	TArray<FString> Lines;
	Copy.ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		TArray<FString> Cells;
		Line.ParseIntoArray(Cells, TEXT("|"), false);
		// "Part tooltips" table: | part id | tooltip |
		if (Cells.Num() == 4)
		{
			const FString PartId = Cells[1].TrimStartAndEnd();
			if (Catalog.Parts.count(OrbitalGameId(PartId)) > 0)
			{
				PartDescriptions.Add(PartId, Cells[2].TrimStartAndEnd());
			}
		}
		if (Cells.Num() >= 6)
		{
			const FString Code = Cells[1].TrimStartAndEnd();
			if (Code.Len() > 2 && Code[1] == TEXT('_') && Code == Code.ToUpper())
			{
				Causes.Add(Code, Cells[3].TrimStartAndEnd());
				Fixes.Add(Code, Cells[4].TrimStartAndEnd());
			}
		}
	}

	Orbital::FlowPresets Presets;
	for (const TPair<FString, Orbital::SpacecraftDesign>& Pair : Catalog.SpacecraftPresets)
	{
		Presets.spacecraft.emplace(OrbitalGameId(Pair.Key), Pair.Value);
	}
	for (const TPair<FString, Orbital::LauncherDesign>& Pair : Catalog.LauncherPresets)
	{
		Presets.launchers.emplace(OrbitalGameId(Pair.Key), Pair.Value);
	}

	TSharedRef<FOrbitalAssemblyData> Loaded = MakeShared<FOrbitalAssemblyData>();
	Loaded->Parts = MoveTemp(Catalog.Parts);
	Loaded->Mission = MoveTemp(Catalog.Mission);
	Loaded->Rules.socketTree = MoveTemp(Catalog.SocketTree);
	Loaded->Rules.defaultOrbit = MoveTemp(Catalog.DefaultOrbit);
	Data = Loaded;
	Flow = MakeUnique<Orbital::GameFlow>(Loaded->Parts, Loaded->Mission, Loaded->Rules, MoveTemp(Presets));
	UE_LOG(LogOrbitalGame, Display, TEXT("Game state ready: %d parts, %d part names, %d part descriptions, %d UI copy rows"),
		static_cast<int32>(Loaded->Parts.size()), PartNames.Num(), PartDescriptions.Num(), Causes.Num());
	return Flow.Get();
}

FString UOrbitalGameStateSubsystem::PartName(const Orbital::Id& PartId) const
{
	const FString Id = OrbitalGameText(PartId);
	const FString* Name = PartNames.Find(Id);
	return Name ? *Name : Id;
}

FString UOrbitalGameStateSubsystem::PartDescription(const Orbital::Id& PartId) const
{
	const FString* Text = PartDescriptions.Find(OrbitalGameText(PartId));
	return Text ? *Text : FString();
}

FString UOrbitalGameStateSubsystem::CauseText(const Orbital::Id& Code) const
{
	const FString* Text = Causes.Find(OrbitalGameText(Code));
	return Text ? *Text : FString();
}

FString UOrbitalGameStateSubsystem::FixText(const Orbital::Id& Code) const
{
	const FString* Text = Fixes.Find(OrbitalGameText(Code));
	return Text ? *Text : FString();
}

// ---------------------------------------------------------------- director

AOrbitalGameDirector::AOrbitalGameDirector()
{
	PrimaryActorTick.bCanEverTick = true;
}

UOrbitalGameStateSubsystem* AOrbitalGameDirector::GetState() const
{
	const UWorld* World = GetWorld();
	UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
	return Instance ? Instance->GetSubsystem<UOrbitalGameStateSubsystem>() : nullptr;
}

void AOrbitalGameDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
	UOrbitalGameStateSubsystem* State = GetState();
	if (!Player || !State)
	{
		return;
	}
	UpdateSun(Player);
	Orbital::GameFlow* Flow = State->GetFlow();

	// A world that starts in the flight or report phase has no flight actor (level reload
	// or a failed start): go back to assembly with the same design.
	if (Flow && !Flight && (Flow->Phase() == Orbital::GamePhase::Flight || Flow->Phase() == Orbital::GamePhase::Report))
	{
		Flow->Redesign();
	}

	const bool bDesigning = !Flow || Flow->Phase() == Orbital::GamePhase::Assembly
		|| Flow->Phase() == Orbital::GamePhase::Review;
	if (bDesigning)
	{
		if (!bAssemblyHudSet)
		{
			bAssemblyHudSet = true;
			Player->ClientSetHUD(AOrbitalAssemblyHUD::StaticClass());
			Player->bShowMouseCursor = true;
			Player->bEnableClickEvents = true;
			if (GEngine)
			{
				GEngine->ClearOnScreenDebugMessages();
			}
			State->SetVolumePercent(State->GetVolumePercent(), World); // a new level has a fresh audio device state
			// The pad camera makes a calm background for the assembly screen.
			const FName PadTag(TEXT("launcher/pad_wide"));
			for (TActorIterator<ACameraActor> It(World); It; ++It)
			{
				if (It->ActorHasTag(PadTag))
				{
					Player->SetViewTarget(*It);
					break;
				}
			}
		}
		if (AOrbitalAssemblyHUD* Hud = Cast<AOrbitalAssemblyHUD>(Player->GetHUD()))
		{
			Hud->Director = this;
		}
		if (Flow && State->GetScreen() == EOrbitalScreen::Game)
		{
			UpdatePreview(*Player, *Flow, DeltaSeconds);
		}
	}
	else if (Flow->Phase() == Orbital::GamePhase::Flight && Flight && Flight->GetReport().bVisible)
	{
		Flow->FlightFinished();
	}
}

// The level lights the scene with a directional light but shows no Sun. This puts a glowing
// sphere where that light comes from. It follows the camera at a fixed distance, so it stays
// in the same direction from every camera, behind Earth and the Moon and inside the star sphere
// (radius 1.0e8 cm). Material: M_Flame (unlit, additive), which the flight code already uses.
void AOrbitalGameDirector::UpdateSun(APlayerController* Player)
{
	const double DistanceCm = 5.0e7;      // farther than Earth or the Moon from any camera
	const double AngularRadiusDeg = 0.5;  // the real Sun is 0.27; a little larger so it reads on screen
	const float Glow = 50.0f;             // brightness; raise or lower after looking at it in play

	UWorld* World = GetWorld();
	if (!SunDisc && !bSunTried && World)
	{
		bSunTried = true;
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			SunLight = *It;
			break;
		}
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/FX/M_Flame.M_Flame"));
		AStaticMeshActor* Actor = (SunLight.IsValid() && Mesh && Material) ? World->SpawnActor<AStaticMeshActor>() : nullptr;
		UStaticMeshComponent* Component = Actor ? Actor->GetStaticMeshComponent() : nullptr;
		if (!Component)
		{
			UE_LOG(LogOrbitalGame, Warning, TEXT("Sun disc not created (light, mesh or material missing)."));
			return;
		}
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(Mesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		UMaterialInstanceDynamic* Dynamic = UMaterialInstanceDynamic::Create(Material, Actor);
		Dynamic->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.93f, 0.80f));
		Dynamic->SetScalarParameterValue(TEXT("Glow"), Glow);
		Component->SetMaterial(0, Dynamic);
		// The engine sphere has a 50 cm radius.
		Actor->SetActorScale3D(FVector(DistanceCm * FMath::Tan(FMath::DegreesToRadians(AngularRadiusDeg)) / 50.0));
		SunDisc = Actor;
	}
	if (SunDisc && SunLight.IsValid() && Player && Player->PlayerCameraManager)
	{
		// The light's forward vector is the direction the light travels, so the Sun is the other way.
		SunDisc->SetActorLocation(Player->PlayerCameraManager->GetCameraLocation()
			- SunLight->GetActorForwardVector() * DistanceCm);
	}
}

// Actions (hit box names):
//   s:<socket>:<index>  select a socket        p:<part id>   put the part in the selected socket
//   x:remove            empty the selected socket             c:<case>   load TC-A, TC-B, TC-D, TC-E, TC-F
//   o:<orbit>  y:<years>  g:<ground>  m:<objective>  d:<difficulty>   mission plan
//   f:<+/-kg>           change propellant      e:<+/-1>      change stage 1 engine count
//   a:launch  a:confirm  a:back                launch, and the design review answers
//   n:menu  n:missions  n:settings  n:credits  n:game  n:quit     menu navigation
//   k:<mission card id>                        start a mission from its card
//   v:<+/-percent>  q:<0..3>                   settings: sound volume, graphics quality
void AOrbitalGameDirector::HandleAction(const FString& Action)
{
	UOrbitalGameStateSubsystem* State = GetState();
	Orbital::GameFlow* Flow = State ? State->GetFlow() : nullptr;
	FString Kind, Argument;
	if (!State || !Action.Split(TEXT(":"), &Kind, &Argument))
	{
		return;
	}
	if (Kind == TEXT("n"))
	{
		if (Argument == TEXT("quit"))
		{
			// Closes the game; in Play in Editor it ends the play session.
			UWorld* World = GetWorld();
			UKismetSystemLibrary::QuitGame(this, World ? World->GetFirstPlayerController() : nullptr, EQuitPreference::Quit, false);
			return;
		}
		State->SetScreen(Argument == TEXT("missions") ? EOrbitalScreen::Missions
			: Argument == TEXT("settings") ? EOrbitalScreen::Settings
			: Argument == TEXT("credits") ? EOrbitalScreen::Credits
			: Argument == TEXT("game") ? EOrbitalScreen::Game : EOrbitalScreen::MainMenu);
		return;
	}
	if (Kind == TEXT("v"))
	{
		State->SetVolumePercent(State->GetVolumePercent() + FCString::Atoi(*Argument), GetWorld());
		return;
	}
	if (Kind == TEXT("q"))
	{
		State->SetQuality(FCString::Atoi(*Argument));
		return;
	}
	if (!Flow)
	{
		return;
	}
	if (Kind == TEXT("k"))
	{
		for (const Orbital::MissionCard& Card : Orbital::MissionCards())
		{
			if (OrbitalGameText(Card.id) == Argument && Flow->StartMission(Card))
			{
				State->SetMissionTitle(OrbitalGameText(Card.title));
				State->SetScreen(EOrbitalScreen::Game);
			}
		}
		return;
	}
	const Orbital::AssemblySnapshot& Snapshot = Flow->Screen().Snapshot();
	if (Kind == TEXT("s"))
	{
		FString Socket, Index;
		if (Argument.Split(TEXT(":"), &Socket, &Index, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			SelectedSocket = OrbitalGameId(Socket);
			SelectedIndex = FCString::Atoi(*Index);
			// Open the tab the socket belongs to.
			for (int32 Stage = 0; Stage < 2; ++Stage)
			{
				for (int32 Index2 = 0; const FOrbitalCategory* Entry = OrbitalCategoryAt(Stage, Index2); ++Index2)
				{
					if (OrbitalCategoryHas(*Entry, SelectedSocket))
					{
						BuildStage = Stage;
						Category = Index2;
					}
				}
			}
		}
	}
	else if (Kind == TEXT("t") || Kind == TEXT("u"))
	{
		if (Kind == TEXT("t"))
		{
			BuildStage = FMath::Clamp(FCString::Atoi(*Argument), 0, 2);
			Category = 0;
		}
		else
		{
			Category = FMath::Max(0, FCString::Atoi(*Argument));
		}
		// Select the first socket of the category, so its parts show at once.
		SelectedSocket.clear();
		SelectedIndex = -1;
		if (const FOrbitalCategory* Entry = OrbitalCategoryAt(BuildStage, Category))
		{
			SelectedSocket = Entry->Sockets[0];
			SelectedIndex = 0;
		}
		return;
	}
	else if (Kind == TEXT("p"))
	{
		Flow->Replace(SelectedSocket, SelectedIndex, OrbitalGameId(Argument));
	}
	else if (Kind == TEXT("x"))
	{
		Flow->Remove(SelectedSocket, SelectedIndex);
	}
	else if (Kind == TEXT("c"))
	{
		if (Flow->LoadCase(OrbitalGameId(Argument)))
		{
			SelectedSocket.clear();
			SelectedIndex = -1;
		}
	}
	else if (Kind == TEXT("o"))
	{
		Flow->SetOrbit(OrbitalGameId(Argument));
	}
	else if (Kind == TEXT("y"))
	{
		Flow->SetYears(FCString::Atod(*Argument));
	}
	else if (Kind == TEXT("g"))
	{
		Flow->SetGround(OrbitalGameId(Argument));
	}
	else if (Kind == TEXT("m"))
	{
		Flow->SetPrimary(OrbitalGameId(Argument));
	}
	else if (Kind == TEXT("d"))
	{
		Flow->SetDifficulty(OrbitalGameId(Argument));
	}
	else if (Kind == TEXT("f"))
	{
		Flow->SetPropellant(FMath::Max(0.0, Snapshot.spacecraft.propKg + FCString::Atod(*Argument)));
	}
	else if (Kind == TEXT("e"))
	{
		Flow->SetStage1EngineCount(Snapshot.launcher.firstEngineCount + FCString::Atoi(*Argument));
	}
	else if (Kind == TEXT("a"))
	{
		if (Argument == TEXT("launch"))
		{
			if (Flow->RequestLaunch() == Orbital::LaunchAnswer::Launched)
			{
				BeginFlight();
			}
		}
		else if (Argument == TEXT("confirm"))
		{
			if (Flow->ConfirmLaunch())
			{
				BeginFlight();
			}
		}
		else if (Argument == TEXT("back"))
		{
			Flow->CancelReview();
		}
	}
	UE_LOG(LogOrbitalGame, Display, TEXT("Action %s -> phase %d, score %.4f, last change %s %s"), *Action,
		static_cast<int32>(Flow->Phase()), Flow->Preview().score,
		Flow->LastChange().ok ? TEXT("ok") : TEXT("refused"), *OrbitalGameText(Flow->LastChange().code));
}

void AOrbitalGameDirector::BeginFlight()
{
	UOrbitalGameStateSubsystem* State = GetState();
	Orbital::GameFlow* Flow = State ? State->GetFlow() : nullptr;
	const FOrbitalAssemblyData* Data = State ? State->GetData() : nullptr;
	UWorld* World = GetWorld();
	if (!Flow || !Data || !World || !Flow->LaunchSnapshot().has_value())
	{
		return;
	}
	const Orbital::SessionLaunch& Shot = *Flow->LaunchSnapshot();
	ClearPreview();
	Flight = World->SpawnActor<AOrbitalFlightPlayback>();
	if (!Flight || !Flight->StartFlight(Data->Parts, Data->Mission, Shot.design.spacecraft, Shot.design.launcher, Shot.result))
	{
		UE_LOG(LogOrbitalGame, Error, TEXT("The flight view did not start; returning to assembly."));
		Flight = nullptr;
		Flow->Redesign();
		return;
	}
	Flight->OnRedesignRequested.AddUObject(this, &AOrbitalGameDirector::HandleRedesign);
	bAssemblyHudSet = false; // the flight actor installs its own HUD
	UE_LOG(LogOrbitalGame, Display, TEXT("Launch: score %.4f, success %s, fail phase '%s'"), Shot.result.score,
		Shot.result.success ? TEXT("yes") : TEXT("no"), *OrbitalGameText(Shot.result.failPhase));
}

void AOrbitalGameDirector::HandleRedesign()
{
	UOrbitalGameStateSubsystem* State = GetState();
	Orbital::GameFlow* Flow = State ? State->GetFlow() : nullptr;
	if (!Flow)
	{
		return;
	}
	Flow->Redesign();
	UE_LOG(LogOrbitalGame, Display, TEXT("Redesign: back to assembly with the same design"));
	// Reload the level to put the pad, cameras and vehicle back. The game state object lives
	// in the GameInstance, so the design is still there when the level comes back.
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this, true)));
}

// ---------------------------------------------------------------- builder preview

void AOrbitalGameDirector::ClearPreview()
{
	if (PreviewVehicle)
	{
		PreviewVehicle->ClearPreview();
		PreviewVehicle = nullptr;
	}
	PreviewKey.clear();
}

// Rebuilds the model when the design or the build stage changes, and turns a camera slowly
// around it. The camera looks a little to the side, so the model sits in the builder window.
void AOrbitalGameDirector::UpdatePreview(APlayerController& Player, Orbital::GameFlow& Flow, float DeltaSeconds)
{
	UWorld* World = GetWorld();
	UOrbitalGameStateSubsystem* State = GetState();
	const FOrbitalAssemblyData* Data = State ? State->GetData() : nullptr;
	if (!World || !Data)
	{
		return;
	}
	const bool bProbeOnly = BuildStage == 0;
	const Orbital::AssemblySnapshot& Snapshot = Flow.Screen().Snapshot();
	const std::string Key = (bProbeOnly ? "P" : "R") + Snapshot.spacecraftJson + Snapshot.launcherJson;
	if (Key != PreviewKey || !PreviewVehicle)
	{
		ClearPreview();
		PreviewKey = Key;
		PreviewVehicle = World->SpawnActor<AOrbitalFlightPlayback>();
		if (PreviewVehicle)
		{
			PreviewVehicle->BuildPreview(Data->Parts, Snapshot.spacecraft, Snapshot.launcher, bProbeOnly);
		}
	}
	if (!PreviewVehicle)
	{
		return;
	}
	if (!PreviewCamera)
	{
		PreviewCamera = World->SpawnActor<ACameraActor>();
		if (!PreviewCamera)
		{
			return;
		}
		UCameraComponent* Lens = PreviewCamera->GetCameraComponent();
		Lens->SetFieldOfView(40.0f);
		Lens->SetConstraintAspectRatio(false);
		// The project runs with fixed exposure; use the same bias as the flight cameras.
		const IConsoleVariable* Bias = IConsoleManager::Get().FindConsoleVariable(TEXT("orbital.FlightExposureBias"));
		Lens->PostProcessBlendWeight = 1.0f;
		Lens->PostProcessSettings.bOverride_AutoExposureBias = true;
		Lens->PostProcessSettings.AutoExposureBias = Bias ? Bias->GetFloat() : -1.3f;
	}
	if (Player.GetViewTarget() != PreviewCamera)
	{
		Player.SetViewTarget(PreviewCamera);
	}
	PreviewYaw = FMath::Fmod(PreviewYaw + 10.0 * DeltaSeconds, 360.0);

	int32 SizeX = 16, SizeY = 9;
	Player.GetViewportSize(SizeX, SizeY);
	const double Aspect = SizeY > 0 ? static_cast<double>(SizeX) / SizeY : 16.0 / 9.0;
	const double TanHalfWide = FMath::Tan(FMath::DegreesToRadians(20.0));
	const double TanHalfTall = TanHalfWide / Aspect;
	const double WindowWide = OrbitalWindowRight - OrbitalWindowLeft;
	const double SizeCm = PreviewVehicle->GetPreviewSizeM() * 100.0;
	// A rocket must fit the window's height; a spacecraft with its arrays out must fit the width.
	const double Distance = bProbeOnly
		? SizeCm / (2.0 * TanHalfWide * WindowWide * 0.85)
		: FMath::Max(SizeCm / (2.0 * TanHalfTall * 0.62), 600.0 / (2.0 * TanHalfWide * WindowWide * 0.7));
	const FVector Focus = PreviewVehicle->GetPreviewFocus();
	const FRotator Around(bProbeOnly ? -18.0 : -6.0, PreviewYaw, 0.0);
	const FVector Location = Focus - Around.Vector() * Distance;
	FRotator Look = (Focus - Location).Rotation();
	const double WindowCentre = (OrbitalWindowLeft + OrbitalWindowRight) * 0.5;
	Look.Yaw -= FMath::RadiansToDegrees(FMath::Atan((WindowCentre - 0.5) * 2.0 * TanHalfWide));
	PreviewCamera->SetActorLocationAndRotation(Location, Look);
	PreviewVehicle->LightPreview(Location + FVector(0, 0, Distance * 0.3));
}

// ---------------------------------------------------------------- assembly screen

TArray<FString> AOrbitalAssemblyHUD::WrapText(const FString& Text, float MaxWidth, UFont* Font, float Scale) const
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

void AOrbitalAssemblyHUD::NotifyHitBoxClick(FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);
	if (Director.IsValid())
	{
		Director->HandleAction(BoxName.ToString());
	}
}

void AOrbitalAssemblyHUD::DrawHUD()
{
	Super::DrawHUD();
	AOrbitalGameDirector* Game = Director.Get();
	if (!Game || !Canvas || !GEngine)
	{
		return;
	}
	UOrbitalGameStateSubsystem* State = Game->GetState();
	const float Width = Canvas->SizeX, Height = Canvas->SizeY;
	const float Unit = FMath::Max(0.75f, Height / 720.0f);
	UFont* Font = GEngine->GetLargeFont();
	const float Scale = 1.0f * Unit, TitleScale = 1.5f * Unit;
	float MeasureWidth = 0, TextHeight = 0;
	GetTextSize(TEXT("Ag"), MeasureWidth, TextHeight, Font, Scale);
	const float RowHeight = TextHeight * 1.32f, TextInset = TextHeight * 0.16f, Pad = 8.0f * Unit;

	const FLinearColor White = FLinearColor::White;
	const FLinearColor Dim(0.62f, 0.66f, 0.72f);
	const FLinearColor Accent(0.7f, 0.85f, 1.0f);
	const FLinearColor Green(0.30f, 0.85f, 0.40f), Amber(1.0f, 0.75f, 0.20f), Red(1.0f, 0.35f, 0.30f);
	const FLinearColor ButtonColor(0.10f, 0.35f, 0.75f, 0.95f), ButtonOn(0.15f, 0.55f, 0.35f, 0.95f);
	const FLinearColor RowColor(1.0f, 1.0f, 1.0f, 0.06f), RowSelected(0.10f, 0.35f, 0.75f, 0.55f);

	const FLinearColor Backdrop(0.02f, 0.03f, 0.06f, 0.78f), Panel(0.02f, 0.03f, 0.06f, 0.90f);
	Orbital::GameFlow* Flow = State ? State->GetFlow() : nullptr;
	const float Top = Pad * 2;
	const float ColumnTop = Top + TextHeight * 2.6f;
	const float WindowX = Width * OrbitalWindowLeft, WindowW = Width * (OrbitalWindowRight - OrbitalWindowLeft);
	const float WindowY = ColumnTop + RowHeight * 1.3f, WindowH = Height - WindowY - Pad * 2;
	if (State && State->GetScreen() == EOrbitalScreen::Game && Flow)
	{
		// Panels around a clear window: the 3D model of the design shows through it.
		DrawRect(Panel, 0, 0, Width, WindowY);
		DrawRect(Panel, 0, WindowY, WindowX, Height - WindowY);
		DrawRect(Panel, WindowX + WindowW, WindowY, Width - WindowX - WindowW, Height - WindowY);
		DrawRect(Panel, WindowX, WindowY + WindowH, WindowW, Height - WindowY - WindowH);
		const float Edge = FMath::Max(1.0f, 2.0f * Unit);
		DrawRect(Accent, WindowX, WindowY, WindowW, Edge);
		DrawRect(Accent, WindowX, WindowY + WindowH - Edge, WindowW, Edge);
		DrawRect(Accent, WindowX, WindowY, Edge, WindowH);
		DrawRect(Accent, WindowX + WindowW - Edge, WindowY, Edge, WindowH);
	}
	else
	{
		DrawRect(Backdrop, 0, 0, Width, Height);
	}
	if (State && DrawMenu(*State, Width, Height, Unit, Font))
	{
		return;
	}
	if (!Flow)
	{
		DrawText(TEXT("ORBITAL could not load PART_CATALOG.json"), Red, Pad * 3, Pad * 3, Font, TitleScale);
		DrawText(State ? State->GetLoadError() : FString(TEXT("No game state object.")), White, Pad * 3,
			Pad * 3 + TextHeight * 2.4f, Font, Scale);
		return;
	}
	const FOrbitalAssemblyData* Data = State->GetData();
	const bool bReview = Flow->Phase() == Orbital::GamePhase::Review;
	const bool bInteractive = !bReview;
	const Orbital::AssemblySnapshot& Snapshot = Flow->Screen().Snapshot();
	const Orbital::MissionResult& Preview = Flow->Preview();

	const auto Label = [&](const FString& Text, const FLinearColor& Color, float X, float Y)
	{
		DrawText(Text, Color, X, Y + TextInset, Font, Scale);
	};
	const auto Button = [&](const FString& Text, float X, float Y, float ButtonWidth, const FLinearColor& Color,
		const FString& Action, bool bActive)
	{
		DrawRect(Color, X, Y, ButtonWidth, RowHeight);
		float LabelWidth = 0, LabelHeight = 0;
		GetTextSize(Text, LabelWidth, LabelHeight, Font, Scale);
		DrawText(Text, White, X + FMath::Max(4.0f * Unit, (ButtonWidth - LabelWidth) * 0.5f), Y + TextInset, Font, Scale);
		if (bActive)
		{
			AddHitBox(FVector2D(X, Y), FVector2D(ButtonWidth, RowHeight), FName(*Action), true);
		}
	};

	// ---- header
	DrawText(TEXT("ORBITAL  |  Builder"), White, Pad * 3, Top, Font, TitleScale);
	Label(FString::Printf(TEXT("Mission: %s   Budget: %s M credits"),
		State->GetMissionTitle().IsEmpty() ? TEXT("lunar polar survey") : *State->GetMissionTitle(),
		*OrbitalNumber(Preview.budgetMCr)), Dim, Width * 0.34f, Top + TextHeight * 0.2f);
	Button(TEXT("Menu"), Width * 0.90f, Top, Width * 0.08f, ButtonColor, TEXT("n:menu"), bInteractive);
	const float LeftX = Width * 0.02f, LeftW = Width * 0.125f;
	const float MidX = Width * 0.155f, MidW = Width * 0.265f;
	const float RightX = Width * 0.665f, RightW = Width * 0.325f;
	const float BuildW = MidX + MidW - LeftX, ContentTop = ColumnTop + RowHeight * 1.5f;
	const float ContentBottom = Height - RowHeight * 3.2f;
	const int32 Stage = Game->GetBuildStage();

	// ---- build stages: spacecraft first, then the rocket, then the mission plan
	{
		const TCHAR* StageTitles[] = {TEXT("1  SPACECRAFT"), TEXT("2  ROCKET"), TEXT("3  MISSION PLAN")};
		const float Gap = 4.0f * Unit, TabW = (BuildW - Gap * 2) / 3.0f;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			Button(StageTitles[Index], LeftX + Index * (TabW + Gap), ColumnTop, TabW, Index == Stage ? ButtonOn : ButtonColor,
				FString::Printf(TEXT("t:%d"), Index), bInteractive && Index != Stage);
		}
		Label(Stage == 0 ? TEXT("SPACECRAFT") : TEXT("ROCKET WITH SPACECRAFT INSIDE"), Accent, WindowX, ColumnTop);
	}

	float Y = ContentTop;
	const std::vector<Orbital::SocketView> Views = Flow->Screen().Sockets();
	if (Stage < 2)
	{
		// ---- left: part categories of this stage, with the number of parts fitted
		const FOrbitalCategory* Open = nullptr;
		for (int32 Index = 0; const FOrbitalCategory* Entry = OrbitalCategoryAt(Stage, Index); ++Index)
		{
			int32 Fitted = 0;
			for (const Orbital::SocketView& View : Views)
			{
				Fitted += OrbitalCategoryHas(*Entry, View.socket) && !View.partId.empty() ? 1 : 0;
			}
			const bool bOpen = Index == Game->GetCategory();
			Open = bOpen ? Entry : Open;
			const float TabH = RowHeight * 1.5f;
			DrawRect(bOpen ? RowSelected : RowColor, LeftX, Y, LeftW, TabH - 3.0f);
			Label(Entry->Title, White, LeftX + Pad, Y + (TabH - RowHeight) * 0.5f);
			float CountW = 0, CountH = 0;
			const FString Count = FString::FromInt(Fitted);
			GetTextSize(Count, CountW, CountH, Font, Scale);
			Label(Count, Fitted > 0 ? Green : Dim, LeftX + LeftW - CountW - Pad, Y + (TabH - RowHeight) * 0.5f);
			if (bInteractive && !bOpen)
			{
				AddHitBox(FVector2D(LeftX, Y), FVector2D(LeftW, TabH - 3.0f), FName(*FString::Printf(TEXT("u:%d"), Index)), true);
			}
			Y += TabH;
		}

		// ---- middle: sockets of the open category. Fitted ones, and one free place per kind.
		Y = ContentTop;
		const Orbital::Id& Selected = Game->GetSelectedSocket();
		bool bSelectedHere = false;
		if (Open)
		{
			Orbital::Id FreeShown[2];
			for (const Orbital::SocketView& View : Views)
			{
				if (!OrbitalCategoryHas(*Open, View.socket))
				{
					continue;
				}
				const bool bSelected = View.socket == Selected && View.index == Game->GetSelectedIndex();
				if (View.partId.empty() && !bSelected)
				{
					if (View.socket == FreeShown[0] || View.socket == FreeShown[1])
					{
						continue;
					}
					(FreeShown[0].empty() ? FreeShown[0] : FreeShown[1]) = View.socket;
				}
				bSelectedHere = bSelectedHere || bSelected;
				DrawRect(bSelected ? RowSelected : RowColor, MidX, Y, MidW, RowHeight - 2.0f);
				Label(OrbitalSocketTitle(View.socket, View.index), View.locked ? Dim : White, MidX + Pad, Y);
				const FString Content = View.locked ? FString(TEXT("needs its parent part"))
					: View.partId.empty() ? FString(TEXT("+ add")) : State->PartName(View.partId);
				Label(Content, View.locked || View.partId.empty() ? Dim : Green, MidX + MidW * 0.50f, Y);
				if (bInteractive && !View.locked)
				{
					AddHitBox(FVector2D(MidX, Y), FVector2D(MidW, RowHeight - 2.0f),
						FName(*FString::Printf(TEXT("s:%s:%d"), *OrbitalGameText(View.socket), View.index)), true);
				}
				Y += RowHeight;
			}
		}
		Y += RowHeight * 0.4f;

		// ---- middle: part cards for the selected socket
		if (!bSelectedHere)
		{
			Label(TEXT("Click a place above to see its parts."), Dim, MidX, Y);
		}
		else
		{
			Label(FString::Printf(TEXT("PARTS FOR: %s"), *OrbitalSocketTitle(Selected, Game->GetSelectedIndex()).ToUpper()),
				Accent, MidX, Y);
			Y += RowHeight;
			const Orbital::Id Current = Flow->Screen().Model().Get(Selected, Game->GetSelectedIndex());
			for (const Orbital::Id& PartId : Flow->Screen().PartsForSocket(Selected))
			{
				const bool bCurrent = PartId == Current;
				const auto Found = Data->Parts.find(PartId);
				// Name, then mass and cost, then up to two lines of description.
				TArray<FString> DescriptionLines = WrapText(State->PartDescription(PartId), MidW - Pad * 3.0f, Font, Scale);
				if (DescriptionLines.Num() > 2)
				{
					DescriptionLines.SetNum(2);
					DescriptionLines[1] += TEXT(" ...");
				}
				const float BlockHeight = RowHeight * (2 + DescriptionLines.Num()) + 4.0f * Unit;
				if (Y + BlockHeight > ContentBottom)
				{
					Label(TEXT("... more parts"), Dim, MidX, Y);
					Y += RowHeight;
					break;
				}
				DrawRect(bCurrent ? ButtonOn : RowColor, MidX, Y, MidW, BlockHeight - 4.0f);
				Label(State->PartName(PartId), White, MidX + Pad, Y);
				if (Found != Data->Parts.end())
				{
					Label(FString::Printf(TEXT("%s kg    %s M credits%s"), *OrbitalNumber(Found->second.massKg),
						*OrbitalNumber(Found->second.costMCr), bCurrent ? TEXT("    FITTED") : TEXT("")),
						bCurrent ? White : Accent, MidX + Pad, Y + RowHeight);
				}
				for (int32 LineIndex = 0; LineIndex < DescriptionLines.Num(); ++LineIndex)
				{
					Label(DescriptionLines[LineIndex], bCurrent ? White : Dim, MidX + Pad, Y + RowHeight * (LineIndex + 2));
				}
				if (bInteractive && !bCurrent)
				{
					AddHitBox(FVector2D(MidX, Y), FVector2D(MidW, BlockHeight - 4.0f),
						FName(*(FString(TEXT("p:")) + OrbitalGameText(PartId))), true);
				}
				Y += BlockHeight;
			}
			if (!Current.empty() && Y + RowHeight <= ContentBottom + RowHeight)
			{
				Button(TEXT("Remove this part"), MidX, Y + 2.0f, MidW * 0.6f, FLinearColor(0.55f, 0.20f, 0.18f, 0.95f),
					TEXT("x:remove"), bInteractive);
				Y += RowHeight;
			}
		}
	}
	else
	{
		// ---- mission plan: uses the whole build area
		const float PlanX = LeftX, PlanW = BuildW;
		const auto Choices = [&](const TCHAR* Title, const TArray<FString>& Ids, const FString& CurrentId, const TCHAR* Kind)
		{
			Label(Title, White, PlanX, Y);
			const float StartX = PlanX + PlanW * 0.27f, Gap = 4.0f * Unit;
			const float ChoiceWidth = (PlanW * 0.73f - Gap * (Ids.Num() - 1)) / FMath::Max(1, Ids.Num());
			for (int32 Index = 0; Index < Ids.Num(); ++Index)
			{
				const bool bOn = Ids[Index] == CurrentId;
				Button(OrbitalChoiceLabel(Ids[Index]), StartX + Index * (ChoiceWidth + Gap), Y, ChoiceWidth, bOn ? ButtonOn : ButtonColor,
					FString(Kind) + TEXT(":") + Ids[Index], bInteractive && !bOn);
			}
			Y += RowHeight + 3.0f * Unit;
		};
		const auto SortedKeys = [](const auto& Map)
		{
			TArray<FString> Keys;
			for (const auto& Entry : Map)
			{
				Keys.Add(OrbitalGameText(Entry.first));
			}
			Keys.Sort();
			return Keys;
		};
		Choices(TEXT("Orbit"), SortedKeys(Data->Mission.orbits), OrbitalGameText(Snapshot.spacecraft.orbit), TEXT("o"));
		TArray<FString> YearOptions;
		for (const double Years : Data->Mission.plannedYearsOptions)
		{
			YearOptions.Add(OrbitalNumber(Years));
		}
		Choices(TEXT("Years"), YearOptions, OrbitalNumber(Snapshot.spacecraft.years), TEXT("y"));
		Choices(TEXT("Ground"), SortedKeys(Data->Mission.ground), OrbitalGameText(Snapshot.spacecraft.ground), TEXT("g"));
		Choices(TEXT("Primary goal"), SortedKeys(Data->Mission.objectiveWeights), OrbitalGameText(Flow->Primary()), TEXT("m"));
		Choices(TEXT("Difficulty"), SortedKeys(Data->Mission.budgets), OrbitalGameText(Flow->Difficulty()), TEXT("d"));
		{
			double CapacityKg = 0;
			for (const Orbital::Id& Tank : Snapshot.spacecraft.tanks)
			{
				const auto Found = Data->Parts.find(Tank);
				CapacityKg += Found != Data->Parts.end() ? Found->second.capacityKg : 0.0;
			}
			Label(FString::Printf(TEXT("Propellant  %s / %s kg"), *OrbitalNumber(Snapshot.spacecraft.propKg),
				*OrbitalNumber(CapacityKg)), White, PlanX, Y);
			const float StepWidth = PlanW * 0.115f, StepX = PlanX + PlanW * 0.50f, Gap = 4.0f * Unit;
			const TCHAR* Steps[] = {TEXT("-50"), TEXT("-10"), TEXT("+10"), TEXT("+50")};
			for (int32 Index = 0; Index < 4; ++Index)
			{
				Button(Steps[Index], StepX + Index * (StepWidth + Gap), Y, StepWidth, ButtonColor,
					FString(TEXT("f:")) + Steps[Index], bInteractive);
			}
			Y += RowHeight + 3.0f * Unit;
			Label(FString::Printf(TEXT("Stage 1 engines  x%d"), Snapshot.launcher.firstEngineCount), White, PlanX, Y);
			Button(TEXT("-1"), StepX, Y, StepWidth, ButtonColor, TEXT("e:-1"), bInteractive);
			Button(TEXT("+1"), StepX + StepWidth + Gap, Y, StepWidth, ButtonColor, TEXT("e:+1"), bInteractive);
			Y += RowHeight + 3.0f * Unit;
		}
	}

	// Last refusal (wrong socket, limit reached, launch refused), under the build area.
	if (!Flow->LastChange().ok && !Flow->LastChange().reason.empty())
	{
		const FString Message = OrbitalGameText(Flow->LastChange().reason)
			+ (Flow->LastChange().code.empty() ? FString() : TEXT("  (") + OrbitalGameText(Flow->LastChange().code) + TEXT(")"));
		float NoteY = ContentBottom + RowHeight * 0.3f;
		for (const FString& Line : WrapText(Message, BuildW, Font, Scale))
		{
			Label(Line, Amber, LeftX, NoteY);
			NoteY += RowHeight;
		}
	}

	// ---- right: dashboard rows
	Y = ColumnTop;
	Label(TEXT("DASHBOARD"), Accent, RightX, Y);
	Y += RowHeight;
	const Orbital::DashboardReview* Review = Flow->Review();
	if (Review)
	{
		for (const Orbital::ResourceRow& Row : Review->rows)
		{
			const FLinearColor StateColor = !Row.current.has_value() ? Dim
				: Row.state == Orbital::DashboardState::Green ? Green
				: Row.state == Orbital::DashboardState::Amber ? Amber : Red;
			DrawRect(RowColor, RightX, Y, RightW, RowHeight - 2.0f);
			DrawRect(StateColor, RightX, Y, 6.0f * Unit, RowHeight - 2.0f);
			Label(OrbitalGameText(Row.tooltipKey), White, RightX + Pad * 1.6f, Y);
			FString Value = TEXT("not available yet");
			if (Row.current.has_value() && Row.limit.has_value())
			{
				Value = FString::Printf(TEXT("%s  %s / %s %s"), *OrbitalGameText(Row.measure), *OrbitalNumber(*Row.current),
					*OrbitalNumber(*Row.limit), *OrbitalGameText(Row.unit));
				if (Row.marginPercent.has_value())
				{
					Value += FString::Printf(TEXT("  (%+.0f%%)"), *Row.marginPercent);
				}
			}
			Label(Value, StateColor, RightX + RightW * 0.27f, Y);
			Y += RowHeight;
		}
	}
	Y += RowHeight * 0.3f;
	Label(FString::Printf(TEXT("Predicted score %.1f    Science %.1f    Cost %s / %s M"), Preview.score, Preview.science,
		*OrbitalNumber(Preview.totalCostMCr), *OrbitalNumber(Preview.budgetMCr)), White, RightX, Y);
	Y += RowHeight * 1.3f;

	// ---- right: review list
	const float ButtonsTop = Height - RowHeight * 3.4f;
	Label(TEXT("DESIGN REVIEW"), Accent, RightX, Y);
	Y += RowHeight;
	if (Review && Review->issues.empty())
	{
		Label(TEXT("No open items. Ready to launch."), Green, RightX, Y);
	}
	else if (Review)
	{
		for (const Orbital::ReviewIssue& Issue : Review->issues)
		{
			if (Y > ButtonsTop - RowHeight * 2.0f)
			{
				Label(TEXT("... more items"), Dim, RightX, Y);
				break;
			}
			const bool bBlock = Issue.severity == Orbital::ReviewSeverity::Block;
			const FLinearColor IssueColor = bBlock ? Red : Amber;
			const TCHAR* Severity = bBlock ? TEXT("BLOCK") : Issue.severity == Orbital::ReviewSeverity::Risk ? TEXT("RISK") : TEXT("WARN");
			Label(FString::Printf(TEXT("%s  %s"), Severity, *OrbitalGameText(Issue.code)), IssueColor, RightX, Y);
			Y += RowHeight;
			const FString Cause = State->CauseText(Issue.code), Fix = State->FixText(Issue.code);
			const FString Text = Cause.IsEmpty() ? FString() : Fix.IsEmpty() ? Cause : Cause + TEXT("  Fix: ") + Fix;
			for (const FString& Line : WrapText(Text, RightW - Pad, Font, Scale))
			{
				if (Y > ButtonsTop - RowHeight)
				{
					break;
				}
				Label(Line, White, RightX + Pad, Y);
				Y += RowHeight;
			}
		}
	}

	// ---- bottom right: reference cases and Launch
	{
		const float Gap = 6.0f * Unit, CaseWidth = (RightW - Gap * 3) / 4.0f;
		const TCHAR* Cases[] = {TEXT("TC-A"), TEXT("TC-B"), TEXT("TC-E"), TEXT("TC-F")};
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Button(FString(TEXT("Load ")) + Cases[Index], RightX + Index * (CaseWidth + Gap), ButtonsTop, CaseWidth,
				ButtonColor, FString(TEXT("c:")) + Cases[Index], bInteractive);
		}
		const bool bBlocked = !Preview.block.empty();
		const float LaunchTop = ButtonsTop + RowHeight + Gap, LaunchHeight = RowHeight * 1.6f;
		const FString LaunchLabel = bBlocked ? TEXT("LAUNCH  (blocked: fix the red items)") : TEXT("LAUNCH");
		DrawRect(bBlocked ? FLinearColor(0.30f, 0.30f, 0.32f, 0.95f) : FLinearColor(0.15f, 0.55f, 0.35f, 0.98f),
			RightX, LaunchTop, RightW, LaunchHeight);
		float LaunchWidth = 0, LaunchTextHeight = 0;
		GetTextSize(LaunchLabel, LaunchWidth, LaunchTextHeight, Font, Scale * 1.25f);
		DrawText(LaunchLabel, White, RightX + (RightW - LaunchWidth) * 0.5f, LaunchTop + (LaunchHeight - LaunchTextHeight) * 0.5f,
			Font, Scale * 1.25f);
		if (bInteractive)
		{
			AddHitBox(FVector2D(RightX, LaunchTop), FVector2D(RightW, LaunchHeight), FName(TEXT("a:launch")), true);
		}
	}

	// ---- design review confirmation (RISK or WARN present)
	if (bReview)
	{
		const float PanelW = Width * 0.56f, PanelX = (Width - PanelW) * 0.5f;
		struct FPanelLine
		{
			FString Text;
			FLinearColor Color = FLinearColor::White;
		};
		TArray<FPanelLine> PanelLines;
		const auto AddPanelLine = [&PanelLines](const FString& Text, const FLinearColor& Color)
		{
			FPanelLine Entry;
			Entry.Text = Text;
			Entry.Color = Color;
			PanelLines.Add(Entry);
		};
		AddPanelLine(TEXT("DESIGN REVIEW: this design can launch, but it carries risks."), Amber);
		AddPanelLine(FString(), White);
		if (Review)
		{
			for (const Orbital::ReviewIssue& Issue : Review->issues)
			{
				AddPanelLine(OrbitalGameText(Issue.code), Accent);
				const FString Cause = State->CauseText(Issue.code);
				for (const FString& Line : WrapText(Cause, PanelW - Pad * 6, Font, Scale))
				{
					AddPanelLine(Line, White);
				}
				if (PanelLines.Num() > 16)
				{
					break;
				}
			}
		}
		const float PanelH = (PanelLines.Num() + 4) * RowHeight + Pad * 4;
		const float PanelY = FMath::Max(Pad, (Height - PanelH) * 0.5f);
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.93f), PanelX, PanelY, PanelW, PanelH);
		float LineY = PanelY + Pad * 2;
		for (const FPanelLine& Entry : PanelLines)
		{
			Label(Entry.Text, Entry.Color, PanelX + Pad * 3, LineY);
			LineY += RowHeight;
		}
		LineY += RowHeight;
		const float ChoiceW = PanelW * 0.36f;
		Button(TEXT("Back to assembly"), PanelX + PanelW * 0.10f, LineY, ChoiceW, ButtonColor, TEXT("a:back"), true);
		Button(TEXT("Launch anyway"), PanelX + PanelW * 0.54f, LineY, ChoiceW, FLinearColor(0.70f, 0.45f, 0.10f, 0.98f),
			TEXT("a:confirm"), true);
	}
}

// ---------------------------------------------------------------- main menu

void AOrbitalAssemblyHUD::MenuButton(const FString& Text, float X, float Y, float ButtonWidth, float ButtonHeight,
	const FLinearColor& Color, const FString& Action, UFont* Font, float Scale)
{
	DrawRect(Color, X, Y, ButtonWidth, ButtonHeight);
	float LabelWidth = 0, LabelHeight = 0;
	GetTextSize(Text, LabelWidth, LabelHeight, Font, Scale);
	DrawText(Text, FLinearColor::White, X + (ButtonWidth - LabelWidth) * 0.5f, Y + (ButtonHeight - LabelHeight) * 0.5f, Font, Scale);
	if (!Action.IsEmpty())
	{
		AddHitBox(FVector2D(X, Y), FVector2D(ButtonWidth, ButtonHeight), FName(*Action), true);
	}
}

bool AOrbitalAssemblyHUD::DrawMenu(UOrbitalGameStateSubsystem& State, float Width, float Height, float Unit, UFont* Font)
{
	const EOrbitalScreen Screen = State.GetScreen();
	if (Screen == EOrbitalScreen::Game)
	{
		return false;
	}
	const float Scale = 1.3f * Unit, BigScale = 4.0f * Unit, HeadScale = 2.0f * Unit;
	float MeasureWidth = 0, TextHeight = 0;
	GetTextSize(TEXT("Ag"), MeasureWidth, TextHeight, Font, Scale);
	const float RowHeight = TextHeight * 1.35f, ButtonHeight = TextHeight * 2.4f;
	const FLinearColor Dim(0.62f, 0.66f, 0.72f), Accent(0.7f, 0.85f, 1.0f);
	const FLinearColor ButtonColor(0.10f, 0.35f, 0.75f, 0.95f), ButtonOn(0.15f, 0.55f, 0.35f, 0.95f);
	const FLinearColor CardColor(0.0f, 0.0f, 0.0f, 0.60f);
	const auto Centered = [&](const FString& Text, const FLinearColor& Color, float Y, float TextScale)
	{
		float TextWidth = 0, LineHeight = 0;
		GetTextSize(Text, TextWidth, LineHeight, Font, TextScale);
		DrawText(Text, Color, (Width - TextWidth) * 0.5f, Y, Font, TextScale);
	};

	if (Screen == EOrbitalScreen::MainMenu)
	{
		Centered(TEXT("ORBITAL"), FLinearColor::White, Height * 0.20f, BigScale);
		Centered(TEXT("Design a mission to the Moon's poles. Build it, launch it, and learn why it worked."), Dim,
			Height * 0.36f, Scale);
		const float ButtonWidth = Width * 0.26f, X = (Width - ButtonWidth) * 0.5f;
		const float FirstY = Height * 0.44f, Step = ButtonHeight * 1.22f;
		MenuButton(TEXT("Missions"), X, FirstY, ButtonWidth, ButtonHeight, ButtonColor, TEXT("n:missions"), Font, HeadScale * 0.8f);
		MenuButton(TEXT("Settings"), X, FirstY + Step, ButtonWidth, ButtonHeight, ButtonColor, TEXT("n:settings"), Font, HeadScale * 0.8f);
		MenuButton(TEXT("Credits"), X, FirstY + Step * 2.0f, ButtonWidth, ButtonHeight, ButtonColor, TEXT("n:credits"), Font, HeadScale * 0.8f);
		MenuButton(TEXT("Quit"), X, FirstY + Step * 3.0f, ButtonWidth, ButtonHeight, FLinearColor(0.30f, 0.30f, 0.34f, 0.95f), TEXT("n:quit"), Font, HeadScale * 0.8f);
		Centered(TEXT("NASA Space Apps Challenge 2026  |  Team Three-Body Problem."), Dim, Height * 0.90f, Scale * 0.85f);
		return true;
	}

	if (Screen == EOrbitalScreen::Missions)
	{
		Centered(TEXT("CHOOSE A MISSION"), FLinearColor::White, Height * 0.10f, HeadScale);
		Centered(TEXT("Same Moon, same parts. Each mission has a different goal and budget."), Dim, Height * 0.10f + TextHeight * 2.4f, Scale);
		Orbital::GameFlow* Flow = State.GetFlow();
		const FOrbitalAssemblyData* Data = State.GetData();
		if (!Flow || !Data)
		{
			Centered(TEXT("Could not load PART_CATALOG.json: ") + State.GetLoadError(), FLinearColor(1.0f, 0.35f, 0.30f), Height * 0.4f, Scale);
		}
		else
		{
			const std::vector<Orbital::MissionCard> Cards = Orbital::MissionCards();
			const int32 Count = static_cast<int32>(Cards.size());
			const float Gap = Width * 0.025f, CardWidth = (Width * 0.90f - Gap * (Count - 1)) / FMath::Max(1, Count);
			const float CardTop = Height * 0.26f, CardHeight = Height * 0.52f, Inset = 16.0f * Unit;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const Orbital::MissionCard& Card = Cards[Index];
				const float X = Width * 0.05f + Index * (CardWidth + Gap);
				DrawRect(CardColor, X, CardTop, CardWidth, CardHeight);
				DrawText(OrbitalGameText(Card.title), FLinearColor::White, X + Inset, CardTop + Inset, Font, HeadScale * 0.85f);
				float Y = CardTop + Inset + TextHeight * 2.6f;
				for (const FString& Line : WrapText(OrbitalGameText(Card.summary), CardWidth - Inset * 2.0f, Font, Scale))
				{
					DrawText(Line, FLinearColor::White, X + Inset, Y, Font, Scale);
					Y += RowHeight;
				}
				Y += RowHeight * 0.6f;
				DrawText(FString::Printf(TEXT("Primary goal: %s"), *OrbitalGameText(Card.primary)), Accent, X + Inset, Y, Font, Scale);
				Y += RowHeight;
				const auto Budget = Data->Mission.budgets.find(Card.difficulty);
				if (Budget != Data->Mission.budgets.end())
				{
					DrawText(FString::Printf(TEXT("Budget: %s M credits"), *OrbitalNumber(Budget->second)), Accent, X + Inset, Y, Font, Scale);
				}
				MenuButton(TEXT("Start"), X + Inset, CardTop + CardHeight - ButtonHeight - Inset, CardWidth - Inset * 2.0f, ButtonHeight,
					ButtonOn, FString(TEXT("k:")) + OrbitalGameText(Card.id), Font, Scale * 1.2f);
			}
		}
		MenuButton(TEXT("Back"), Width * 0.05f, Height * 0.86f, Width * 0.14f, ButtonHeight, ButtonColor, TEXT("n:menu"), Font, Scale);
		return true;
	}

	if (Screen == EOrbitalScreen::Credits)
	{
		// NASA data sources as listed in README.md and Docs/CHALLENGE_TRACEABILITY.md.
		struct FCreditLine
		{
			const TCHAR* Text;
			int32 Style; // 0 body, 1 section heading, 2 gap
		};
		static const FCreditLine CreditLines[] = {
			{TEXT("NASA DATA AND RESOURCES"), 1},
			{TEXT("NSSDC planetary fact sheets: Earth and Moon constants for the engineering model."), 0},
			{TEXT("Blue Marble Next Generation: Earth texture. NASA Earth Observatory / NASA Visible Earth."), 0},
			{TEXT("CGI Moon Kit: Moon texture from LRO/LROC data. NASA's Scientific Visualization Studio."), 0},
			{TEXT("Deep Star Maps 2020: star background. NASA/Goddard Space Flight Center Scientific Visualization Studio. Gaia DR2: ESA/Gaia/DPAC."), 0},
			{TEXT("Credit: NASA"), 0},
			{TEXT(""), 2},
			{TEXT("TEAM THREE-BODY PROBLEM."), 1},
			{TEXT("Muhammet: Unreal project setup, scene, lighting, flight view and submission materials."), 0},
			{TEXT("Isa: vehicle assembly, game screens and mission timeline."), 0},
			{TEXT("Ram: engineering integration, JSON loader, dashboard, session flow and documentation."), 0},
			{TEXT(""), 2},
			{TEXT("NOTES"), 1},
			{TEXT("Launch sounds: NASA Historical Sounds (Atlas V: Launch, SLS Test Fire). Other sounds were generated for this project."), 0},
			{TEXT("The team used Claude, ChatGPT/Codex and Gemini to help write code, tests, documentation and UI text, and directed and reviewed that work."), 0},
			{TEXT("ORBITAL uses a simplified educational model. Its estimates are unsuitable for real flight planning."), 0},
		};
		Centered(TEXT("CREDITS"), FLinearColor::White, Height * 0.07f, HeadScale);
		const float Left = Width * 0.12f, TextWidth = Width * 0.76f;
		float Y = Height * 0.07f + TextHeight * 3.2f;
		for (const FCreditLine& Entry : CreditLines)
		{
			if (Entry.Style == 2)
			{
				Y += RowHeight * 0.6f;
				continue;
			}
			for (const FString& Line : WrapText(Entry.Text, TextWidth, Font, Scale))
			{
				DrawText(Line, Entry.Style == 1 ? Accent : FLinearColor::White, Left, Y, Font, Scale);
				Y += RowHeight;
			}
		}
		MenuButton(TEXT("Back"), Width * 0.05f, Height * 0.86f, Width * 0.14f, ButtonHeight, ButtonColor, TEXT("n:menu"), Font, Scale);
		return true;
	}

	// Settings
	Centered(TEXT("SETTINGS"), FLinearColor::White, Height * 0.12f, HeadScale);
	const float Left = Width * 0.24f, ControlX = Width * 0.46f, Small = Width * 0.06f, Gap = 8.0f * Unit;
	float Y = Height * 0.32f;
	DrawText(FString::Printf(TEXT("Sound volume:  %d%%"), State.GetVolumePercent()), FLinearColor::White, Left, Y + ButtonHeight * 0.25f, Font, Scale);
	MenuButton(TEXT("-"), ControlX, Y, Small, ButtonHeight, ButtonColor, TEXT("v:-10"), Font, Scale * 1.2f);
	MenuButton(TEXT("+"), ControlX + Small + Gap, Y, Small, ButtonHeight, ButtonColor, TEXT("v:+10"), Font, Scale * 1.2f);
	Y += ButtonHeight * 1.6f;
	DrawText(TEXT("Graphics quality:"), FLinearColor::White, Left, Y + ButtonHeight * 0.25f, Font, Scale);
	const TCHAR* Levels[] = {TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic")};
	const int32 Quality = State.GetQuality();
	const float LevelWidth = Width * 0.085f;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const bool bOn = Index == Quality;
		MenuButton(Levels[Index], ControlX + Index * (LevelWidth + Gap), Y, LevelWidth, ButtonHeight, bOn ? ButtonOn : ButtonColor,
			bOn ? FString() : FString::Printf(TEXT("q:%d"), Index), Font, Scale);
	}
	Y += ButtonHeight * 1.6f;
	DrawText(TEXT("Lower the quality if the flight view stutters on your computer."), Dim, Left, Y, Font, Scale * 0.9f);
	MenuButton(TEXT("Back"), Width * 0.05f, Height * 0.86f, Width * 0.14f, ButtonHeight, ButtonColor, TEXT("n:menu"), Font, Scale);
	return true;
}

// ---------------------------------------------------------------- start

void UOrbitalGameWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// The flight subsystem starts a reference case on L_Mission when orbital.FlightDemo is 1.
	// While the game is on, the player's own launch starts the flight instead.
	if (IConsoleVariable* FlightDemo = IConsoleManager::Get().FindConsoleVariable(TEXT("orbital.FlightDemo")))
	{
		FlightDemo->Set(OrbitalGameEnabled() ? 0 : 1, ECVF_SetByCode);
	}
}

void UOrbitalGameWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.IsGameWorld() || !OrbitalGameEnabled() || !InWorld.GetMapName().Contains(TEXT("L_Mission")))
	{
		return;
	}
	InWorld.SpawnActor<AOrbitalGameDirector>();
}
