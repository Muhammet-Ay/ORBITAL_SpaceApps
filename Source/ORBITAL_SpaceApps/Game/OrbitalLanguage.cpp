#include "OrbitalLanguage.h"

#include "Misc/ConfigCacheIni.h"
#include "OrbitalText.h"

static int32 GOrbitalTurkish = -1; // -1: not read from the settings file yet
static TMap<FString, FString> GOrbitalTextMemo;

bool OrbitalIsTurkish()
{
	if (GOrbitalTurkish < 0)
	{
		bool bSaved = false;
		if (GConfig)
		{
			GConfig->GetBool(TEXT("Orbital"), TEXT("Turkish"), bSaved, GGameUserSettingsIni);
		}
		GOrbitalTurkish = bSaved ? 1 : 0;
	}
	return GOrbitalTurkish == 1;
}

void OrbitalSetTurkish(bool bTurkish)
{
	GOrbitalTurkish = bTurkish ? 1 : 0;
	GOrbitalTextMemo.Reset();
	if (GConfig)
	{
		GConfig->SetBool(TEXT("Orbital"), TEXT("Turkish"), bTurkish, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

FString OrbitalTr(const FString& English)
{
	if (English.IsEmpty() || !OrbitalIsTurkish())
	{
		return English;
	}
	if (const FString* Known = GOrbitalTextMemo.Find(English))
	{
		return *Known;
	}
	const std::wstring Source(TCHAR_TO_WCHAR(*English));
	const std::wstring Translated = Orbital::TurkishText().Translate(Source);
	const FString Result(WCHAR_TO_TCHAR(Translated.c_str()));
	if (GOrbitalTextMemo.Num() > 4000)
	{
		GOrbitalTextMemo.Reset(); // lines with running numbers (clocks) would grow it without end
	}
	GOrbitalTextMemo.Add(English, Result);
	return Result;
}
