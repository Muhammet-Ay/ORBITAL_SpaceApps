// ORBITAL: screen language (English or Turkish).
//
// The screens are written in English. OrbitalTr() turns a finished line into the chosen language
// just before it is drawn; the rules and the table are in OrbitalText.h (plain C++, tested).
// The choice is kept in the user's settings file.
#pragma once

#include "CoreMinimal.h"

ORBITAL_SPACEAPPS_API bool OrbitalIsTurkish();
ORBITAL_SPACEAPPS_API void OrbitalSetTurkish(bool bTurkish);
ORBITAL_SPACEAPPS_API FString OrbitalTr(const FString& English);
