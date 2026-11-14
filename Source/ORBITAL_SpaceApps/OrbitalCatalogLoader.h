#pragma once

#include "CoreMinimal.h"
#include "OrbitalEngineering_v2.1.h"

class FJsonObject;

struct FOrbitalCatalog
{
    Orbital::Catalog Parts;
    Orbital::Mission Mission;
    std::unordered_map<Orbital::Id, Orbital::Ids> SocketTree;
    Orbital::Id DefaultOrbit;
    TMap<FString, Orbital::SpacecraftDesign> SpacecraftPresets;
    TMap<FString, Orbital::LauncherDesign> LauncherPresets;
    TSharedPtr<const FJsonObject> Source;
};

// Load the catalog from an explicit path. Out is updated only on success.
bool LoadOrbitalCatalog(const FString& Filename, FOrbitalCatalog& Out, FString& Error);

// Decode Isa's assembly snapshot using the reference_builds field names.
bool ReadOrbitalSpacecraft(const FJsonObject& Json, Orbital::SpacecraftDesign& Out, FString& Error);
bool ReadOrbitalLauncher(const FJsonObject& Json, Orbital::LauncherDesign& Out, FString& Error);
