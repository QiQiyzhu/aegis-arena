#pragma once

#include "CoreMinimal.h"

// Local presentation settings only; never part of the run's gameplay state.
struct AEGISARENA_API FAegisUIPreferences
{
    bool bEnglish = false;
    bool bMusicMuted = false;
    bool bPersistent = true;

    static FString DefaultPath();
    static FAegisUIPreferences Load(const FString& Path, const TCHAR* CommandLine);
    bool Save(const FString& Path) const;
};
