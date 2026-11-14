#include "OrbitalGame.h"

#include "../Assembly/OrbitalAssemblyScreenObject.h"
#include "../Flight/OrbitalFlight.h"
#include "../OrbitalCatalogLoader.h"
#include "Camera/CameraActor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
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
	}
	else if (Flow->Phase() == Orbital::GamePhase::Flight && Flight && Flight->GetReport().bVisible)
	{
		Flow->FlightFinished();
	}
}

// Actions (hit box names):
//   s:<socket>:<index>  select a socket        p:<part id>   put the part in the selected socket
//   x:remove            empty the selected socket             c:<case>   load TC-A, TC-B, TC-D, TC-E, TC-F
//   o:<orbit>  y:<years>  g:<ground>  m:<objective>  d:<difficulty>   mission plan
//   f:<+/-kg>           change propellant      e:<+/-1>      change stage 1 engine count
//   a:launch  a:confirm  a:back                launch, and the design review answers
void AOrbitalGameDirector::HandleAction(const FString& Action)
{
	UOrbitalGameStateSubsystem* State = GetState();
	Orbital::GameFlow* Flow = State ? State->GetFlow() : nullptr;
	FString Kind, Argument;
	if (!Flow || !Action.Split(TEXT(":"), &Kind, &Argument))
	{
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
		}
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

	DrawRect(FLinearColor(0.02f, 0.03f, 0.06f, 0.78f), 0, 0, Width, Height);
	Orbital::GameFlow* Flow = State ? State->GetFlow() : nullptr;
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
	const float Top = Pad * 2;
	DrawText(TEXT("ORBITAL  |  Assembly"), White, Pad * 3, Top, Font, TitleScale);
	Label(FString::Printf(TEXT("Mission: lunar polar survey   Budget: %s M credits"), *OrbitalNumber(Preview.budgetMCr)),
		Dim, Width * 0.34f, Top + TextHeight * 0.2f);
	const float ColumnTop = Top + TextHeight * 2.6f;
	const float LeftX = Width * 0.02f, LeftW = Width * 0.29f;
	const float MidX = Width * 0.33f, MidW = Width * 0.30f;
	const float RightX = Width * 0.65f, RightW = Width * 0.33f;

	// ---- left: sockets
	float Y = ColumnTop;
	bool bRocketHeader = false, bProbeHeader = false;
	for (const Orbital::SocketView& View : Flow->Screen().Sockets())
	{
		if (View.socket == "separator")
		{
			continue; // comes with the stage 1 tank
		}
		const bool bProbe = View.socket.rfind("stage", 0) != 0 && View.socket != "fairing";
		if (!bProbe && !bRocketHeader)
		{
			bRocketHeader = true;
			Label(TEXT("ROCKET"), Accent, LeftX, Y);
			Y += RowHeight;
		}
		if (bProbe && !bProbeHeader)
		{
			bProbeHeader = true;
			Y += RowHeight * 0.3f;
			Label(TEXT("SPACECRAFT"), Accent, LeftX, Y);
			Y += RowHeight;
		}
		const bool bSelected = View.socket == Game->GetSelectedSocket() && View.index == Game->GetSelectedIndex();
		DrawRect(bSelected ? RowSelected : RowColor, LeftX, Y, LeftW, RowHeight - 2.0f);
		const FString Title = OrbitalSocketTitle(View.socket, View.index);
		Label(Title, View.locked ? Dim : White, LeftX + Pad, Y);
		const FString Content = View.locked ? FString(TEXT("needs its parent part"))
			: View.partId.empty() ? FString(TEXT("- empty -")) : State->PartName(View.partId);
		Label(Content, View.locked || View.partId.empty() ? Dim : Green, LeftX + LeftW * 0.46f, Y);
		if (bInteractive && !View.locked)
		{
			AddHitBox(FVector2D(LeftX, Y), FVector2D(LeftW, RowHeight - 2.0f),
				FName(*FString::Printf(TEXT("s:%s:%d"), *OrbitalGameText(View.socket), View.index)), true);
		}
		Y += RowHeight;
	}

	// ---- middle: parts for the selected socket
	Y = ColumnTop;
	const Orbital::Id& Selected = Game->GetSelectedSocket();
	if (Selected.empty())
	{
		Label(TEXT("Click a socket on the left to choose a part for it."), Dim, MidX, Y);
		Y += RowHeight * 1.5f;
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
			FString Text = State->PartName(PartId);
			if (Found != Data->Parts.end())
			{
				Text += FString::Printf(TEXT("   %s kg, %s M"), *OrbitalNumber(Found->second.massKg),
					*OrbitalNumber(Found->second.costMCr));
			}
			// Name and numbers on the first line, the part's description under it.
			const TArray<FString> DescriptionLines = WrapText(State->PartDescription(PartId), MidW - Pad * 3.0f, Font, Scale);
			const float BlockHeight = RowHeight * (1 + DescriptionLines.Num());
			DrawRect(bCurrent ? ButtonOn : RowColor, MidX, Y, MidW, BlockHeight - 2.0f);
			Label(Text, White, MidX + Pad, Y);
			for (int32 LineIndex = 0; LineIndex < DescriptionLines.Num(); ++LineIndex)
			{
				Label(DescriptionLines[LineIndex], bCurrent ? White : Dim, MidX + Pad * 2.0f, Y + RowHeight * (LineIndex + 1));
			}
			if (bInteractive && !bCurrent)
			{
				AddHitBox(FVector2D(MidX, Y), FVector2D(MidW, BlockHeight - 2.0f),
					FName(*(FString(TEXT("p:")) + OrbitalGameText(PartId))), true);
			}
			Y += BlockHeight;
		}
		if (!Current.empty())
		{
			Button(TEXT("Remove this part"), MidX, Y + 2.0f, MidW * 0.5f, FLinearColor(0.55f, 0.20f, 0.18f, 0.95f),
				TEXT("x:remove"), bInteractive);
			Y += RowHeight;
		}
		Y += RowHeight * 0.6f;
	}

	// ---- middle: mission plan
	Label(TEXT("MISSION PLAN"), Accent, MidX, Y);
	Y += RowHeight;
	const auto Choices = [&](const TCHAR* Title, const TArray<FString>& Ids, const FString& CurrentId, const TCHAR* Kind)
	{
		Label(Title, White, MidX, Y);
		const float StartX = MidX + MidW * 0.27f, Gap = 4.0f * Unit;
		const float ChoiceWidth = (MidW * 0.73f - Gap * (Ids.Num() - 1)) / FMath::Max(1, Ids.Num());
		for (int32 Index = 0; Index < Ids.Num(); ++Index)
		{
			const bool bOn = Ids[Index] == CurrentId;
			Button(Ids[Index], StartX + Index * (ChoiceWidth + Gap), Y, ChoiceWidth, bOn ? ButtonOn : ButtonColor,
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
			*OrbitalNumber(CapacityKg)), White, MidX, Y);
		const float StepWidth = MidW * 0.115f, StepX = MidX + MidW * 0.50f, Gap = 4.0f * Unit;
		const TCHAR* Steps[] = {TEXT("-50"), TEXT("-10"), TEXT("+10"), TEXT("+50")};
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Button(Steps[Index], StepX + Index * (StepWidth + Gap), Y, StepWidth, ButtonColor,
				FString(TEXT("f:")) + Steps[Index], bInteractive);
		}
		Y += RowHeight + 3.0f * Unit;
		Label(FString::Printf(TEXT("Stage 1 engines  x%d"), Snapshot.launcher.firstEngineCount), White, MidX, Y);
		Button(TEXT("-1"), StepX, Y, StepWidth, ButtonColor, TEXT("e:-1"), bInteractive);
		Button(TEXT("+1"), StepX + StepWidth + Gap, Y, StepWidth, ButtonColor, TEXT("e:+1"), bInteractive);
		Y += RowHeight + 3.0f * Unit;
	}
	// Last refusal (wrong socket, limit reached, launch refused).
	if (!Flow->LastChange().ok && !Flow->LastChange().reason.empty())
	{
		Y += RowHeight * 0.4f;
		const FString Message = OrbitalGameText(Flow->LastChange().reason)
			+ (Flow->LastChange().code.empty() ? FString() : TEXT("  (") + OrbitalGameText(Flow->LastChange().code) + TEXT(")"));
		for (const FString& Line : WrapText(Message, MidW, Font, Scale))
		{
			Label(Line, Amber, MidX, Y);
			Y += RowHeight;
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
