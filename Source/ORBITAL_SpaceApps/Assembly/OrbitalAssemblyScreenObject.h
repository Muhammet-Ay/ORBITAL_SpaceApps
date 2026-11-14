// Blueprint/UMG wrapper around Orbital::AssemblyScreen. All rules live in the plain C++
// headers in this folder; this file only converts types.
// The owner (widget, game instance, subsystem) must hold this object in a UPROPERTY.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "OrbitalAssemblyScreen.h"
#include "OrbitalAssemblyScreenObject.generated.h"

// Catalog, mission and socket rules the screen works on. Shared so it outlives the screen.
struct FOrbitalAssemblyData
{
	Orbital::Catalog Parts;
	Orbital::Mission Mission;
	Orbital::AssemblyRules Rules;

	// Loads Docs/CLAUDE_RESEARCH/PART_CATALOG.json from the project folder with LoadOrbitalCatalog
	// and builds the socket rules from its socket_tree and default_orbit. Null on failure.
	// Packaged builds must stage the catalog and use LoadFile with its real path.
	static ORBITAL_SPACEAPPS_API TSharedPtr<const FOrbitalAssemblyData> LoadFromProject(FString& OutError);
	static ORBITAL_SPACEAPPS_API TSharedPtr<const FOrbitalAssemblyData> LoadFile(const FString& Filename, FString& OutError);
};

USTRUCT(BlueprintType)
struct FOrbitalSocketRow
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Orbital") FString Socket;
	UPROPERTY(BlueprintReadOnly, Category = "Orbital") int32 Index = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Orbital") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "Orbital") FString PartId;
	UPROPERTY(BlueprintReadOnly, Category = "Orbital") bool bLocked = false;
	UPROPERTY(BlueprintReadOnly, Category = "Orbital") bool bCanAccept = false;
	UPROPERTY(BlueprintReadOnly, Category = "Orbital") bool bWouldReplace = false;
	UPROPERTY(BlueprintReadOnly, Category = "Orbital") FString RejectCode;
	UPROPERTY(BlueprintReadOnly, Category = "Orbital") FString RejectReason;
};

USTRUCT(BlueprintType)
struct FOrbitalChangeResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Orbital") bool bOk = false;
	UPROPERTY(BlueprintReadOnly, Category = "Orbital") FString Code;
	UPROPERTY(BlueprintReadOnly, Category = "Orbital") FString Reason;
	UPROPERTY(BlueprintReadOnly, Category = "Orbital") FString SocketName;
	UPROPERTY(BlueprintReadOnly, Category = "Orbital") int32 Removed = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOrbitalSnapshotChanged,
	const FString&, SpacecraftJson, const FString&, LauncherJson);

UCLASS(BlueprintType)
class ORBITAL_SPACEAPPS_API UOrbitalAssemblyScreen : public UObject
{
	GENERATED_BODY()

public:
	void Init(TSharedRef<const FOrbitalAssemblyData> InData);

	// Start from the project's PART_CATALOG.json. Returns false and leaves the screen
	// uninitialised if the catalog cannot be loaded; OutError says why.
	UFUNCTION(BlueprintCallable, Category = "Orbital")
	bool InitFromProjectCatalog(FString& OutError);

	// Old name, kept so callers written against the first version (Flight/OrbitalFlight.cpp)
	// still compile. There is no built-in catalog any more: this loads PART_CATALOG.json
	// like InitFromProjectCatalog and logs an error if that fails. Prefer InitFromProjectCatalog.
	UFUNCTION(BlueprintCallable, Category = "Orbital")
	void InitBuiltInCatalog();

	// Fires after every successful change with the new snapshot JSON (dashboard refresh).
	UPROPERTY(BlueprintAssignable, Category = "Orbital")
	FOrbitalSnapshotChanged OnSnapshotChanged;

	// One row per socket. Pass the part selected in the palette, or an empty string.
	UFUNCTION(BlueprintCallable, Category = "Orbital")
	TArray<FOrbitalSocketRow> GetSockets(const FString& SelectedPart) const;

	UFUNCTION(BlueprintCallable, Category = "Orbital")
	FOrbitalChangeResult Attach(const FString& PartId);

	UFUNCTION(BlueprintCallable, Category = "Orbital")
	FOrbitalChangeResult Replace(const FString& Socket, int32 Index, const FString& PartId);

	UFUNCTION(BlueprintCallable, Category = "Orbital")
	FOrbitalChangeResult Remove(const FString& Socket, int32 Index);

	UFUNCTION(BlueprintCallable, Category = "Orbital")
	FOrbitalChangeResult SetStage1EngineCount(int32 Count);

	UFUNCTION(BlueprintCallable, Category = "Orbital")
	FOrbitalChangeResult SetOrbit(const FString& Orbit);

	UFUNCTION(BlueprintCallable, Category = "Orbital")
	FOrbitalChangeResult SetYears(double Years);

	UFUNCTION(BlueprintCallable, Category = "Orbital")
	FOrbitalChangeResult SetGround(const FString& Ground);

	UFUNCTION(BlueprintCallable, Category = "Orbital")
	FOrbitalChangeResult SetPropellant(double Kg);

	// E_FAIRING_MASS / E_FAIRING_SIZE once a bus and a fairing both exist; empty otherwise.
	UFUNCTION(BlueprintCallable, Category = "Orbital")
	TArray<FString> GetFairingIssues() const;

	// For C++ callers (dashboard, timeline): pass to Orbital::Evaluate / BuildTimeline.
	const Orbital::AssemblySnapshot* GetSnapshot() const { return Screen ? &Screen->Snapshot() : nullptr; }
	const FOrbitalAssemblyData* GetData() const { return Data.Get(); }

private:
	TSharedPtr<const FOrbitalAssemblyData> Data; // declared first: must outlive Screen
	TUniquePtr<Orbital::AssemblyScreen> Screen;
};
