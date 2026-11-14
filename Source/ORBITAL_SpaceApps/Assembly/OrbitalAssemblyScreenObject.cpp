#include "OrbitalAssemblyScreenObject.h"

#include "../OrbitalCatalogLoader.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogOrbitalAssemblyScreen, Log, All);

namespace
{
FString ToFString(const std::string& Value)
{
	return FString(UTF8_TO_TCHAR(Value.c_str()));
}

std::string ToStd(const FString& Value)
{
	return std::string(TCHAR_TO_UTF8(*Value));
}

FOrbitalChangeResult Convert(const Orbital::ChangeResult& In)
{
	FOrbitalChangeResult Out;
	Out.bOk = In.ok;
	Out.Code = ToFString(In.code);
	Out.Reason = ToFString(In.reason);
	Out.SocketName = ToFString(In.socketName);
	Out.Removed = In.removed;
	return Out;
}
} // namespace

TSharedPtr<const FOrbitalAssemblyData> FOrbitalAssemblyData::LoadFile(const FString& Filename, FString& OutError)
{
	FOrbitalCatalog Catalog;
	if (!LoadOrbitalCatalog(Filename, Catalog, OutError))
	{
		return nullptr;
	}
	TSharedRef<FOrbitalAssemblyData> Result = MakeShared<FOrbitalAssemblyData>();
	Result->Parts = MoveTemp(Catalog.Parts);
	Result->Mission = MoveTemp(Catalog.Mission);
	Result->Rules.socketTree = MoveTemp(Catalog.SocketTree);
	Result->Rules.defaultOrbit = MoveTemp(Catalog.DefaultOrbit);
	// Rules.probeRequiresFairing stays false: the probe is assembled independently of the rocket.
	return Result;
}

TSharedPtr<const FOrbitalAssemblyData> FOrbitalAssemblyData::LoadFromProject(FString& OutError)
{
	return LoadFile(FPaths::Combine(FPaths::ProjectDir(), TEXT("Docs/CLAUDE_RESEARCH/PART_CATALOG.json")), OutError);
}

void UOrbitalAssemblyScreen::Init(TSharedRef<const FOrbitalAssemblyData> InData)
{
	Screen.Reset();
	Data = InData;
	Screen = MakeUnique<Orbital::AssemblyScreen>(InData->Parts, InData->Mission, InData->Rules);
	Screen->SetListener([this](const Orbital::AssemblySnapshot& Snapshot)
	{
		OnSnapshotChanged.Broadcast(ToFString(Snapshot.spacecraftJson), ToFString(Snapshot.launcherJson));
	});
}

bool UOrbitalAssemblyScreen::InitFromProjectCatalog(FString& OutError)
{
	const TSharedPtr<const FOrbitalAssemblyData> Loaded = FOrbitalAssemblyData::LoadFromProject(OutError);
	if (!Loaded.IsValid())
	{
		return false;
	}
	Init(Loaded.ToSharedRef());
	return true;
}

void UOrbitalAssemblyScreen::InitBuiltInCatalog()
{
	FString Error;
	if (!InitFromProjectCatalog(Error))
	{
		UE_LOG(LogOrbitalAssemblyScreen, Error, TEXT("PART_CATALOG.json did not load: %s"), *Error);
	}
}

TArray<FOrbitalSocketRow> UOrbitalAssemblyScreen::GetSockets(const FString& SelectedPart) const
{
	TArray<FOrbitalSocketRow> Rows;
	if (!Screen)
	{
		return Rows;
	}
	for (const Orbital::SocketView& View : Screen->Sockets(ToStd(SelectedPart)))
	{
		FOrbitalSocketRow& Row = Rows.AddDefaulted_GetRef();
		Row.Socket = ToFString(View.socket);
		Row.Index = View.index;
		Row.Name = ToFString(View.name);
		Row.PartId = ToFString(View.partId);
		Row.bLocked = View.locked;
		Row.bCanAccept = View.canAccept;
		Row.bWouldReplace = View.wouldReplace;
		Row.RejectCode = ToFString(View.rejectCode);
		Row.RejectReason = ToFString(View.rejectReason);
	}
	return Rows;
}

FOrbitalChangeResult UOrbitalAssemblyScreen::Attach(const FString& PartId)
{
	return Screen ? Convert(Screen->Attach(ToStd(PartId))) : FOrbitalChangeResult();
}

FOrbitalChangeResult UOrbitalAssemblyScreen::Replace(const FString& Socket, int32 Index, const FString& PartId)
{
	return Screen ? Convert(Screen->Replace(ToStd(Socket), Index, ToStd(PartId))) : FOrbitalChangeResult();
}

FOrbitalChangeResult UOrbitalAssemblyScreen::Remove(const FString& Socket, int32 Index)
{
	return Screen ? Convert(Screen->Remove(ToStd(Socket), Index)) : FOrbitalChangeResult();
}

FOrbitalChangeResult UOrbitalAssemblyScreen::SetStage1EngineCount(int32 Count)
{
	return Screen ? Convert(Screen->SetStage1EngineCount(Count)) : FOrbitalChangeResult();
}

FOrbitalChangeResult UOrbitalAssemblyScreen::SetOrbit(const FString& Orbit)
{
	return Screen ? Convert(Screen->SetOrbit(ToStd(Orbit))) : FOrbitalChangeResult();
}

FOrbitalChangeResult UOrbitalAssemblyScreen::SetYears(double Years)
{
	return Screen ? Convert(Screen->SetYears(Years)) : FOrbitalChangeResult();
}

FOrbitalChangeResult UOrbitalAssemblyScreen::SetGround(const FString& Ground)
{
	return Screen ? Convert(Screen->SetGround(ToStd(Ground))) : FOrbitalChangeResult();
}

FOrbitalChangeResult UOrbitalAssemblyScreen::SetPropellant(double Kg)
{
	return Screen ? Convert(Screen->SetPropellant(Kg)) : FOrbitalChangeResult();
}

TArray<FString> UOrbitalAssemblyScreen::GetFairingIssues() const
{
	TArray<FString> Issues;
	if (Screen)
	{
		for (const Orbital::Id& Code : Screen->FairingIssues())
		{
			Issues.Add(ToFString(Code));
		}
	}
	return Issues;
}
