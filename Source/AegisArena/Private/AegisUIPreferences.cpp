#include "AegisUIPreferences.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

FString FAegisUIPreferences::DefaultPath()
{
    return FPaths::Combine(FPaths::GeneratedConfigDir(), TEXT("AegisUI.ini"));
}

FAegisUIPreferences FAegisUIPreferences::Load(const FString& Path, const TCHAR* CommandLine)
{
    FAegisUIPreferences Result;
    // Automated recordings/probes must neither inherit nor overwrite a user's
    // preferred language. A normal game always reads the local preference.
    Result.bPersistent = !FParse::Param(CommandLine, TEXT("AegisPortfolioCapture"))
        && !FParse::Param(CommandLine, TEXT("AegisInputProbe"))
        && !FParse::Param(CommandLine, TEXT("RenderOffscreen"));
    if (Result.bPersistent)
    {
        FConfigFile Config;
        Config.Read(Path);
        FString Language;
        Config.GetString(TEXT("Interface"), TEXT("Language"), Language);
        Result.bEnglish = Language.Equals(TEXT("en"), ESearchCase::IgnoreCase);
        Config.GetBool(TEXT("Audio"), TEXT("MusicMuted"), Result.bMusicMuted);
        Config.GetBool(TEXT("Accessibility"), TEXT("ReducedEffects"), Result.bReducedEffects);
    }
    return Result;
}

bool FAegisUIPreferences::Save(const FString& Path) const
{
    if (!bPersistent) return true;
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true)) return false;
    FConfigFile Config;
    Config.Read(Path);
    Config.SetString(TEXT("Interface"), TEXT("Language"), bEnglish ? TEXT("en") : TEXT("zh-CN"));
    Config.SetBool(TEXT("Audio"), TEXT("MusicMuted"), bMusicMuted);
    Config.SetBool(TEXT("Accessibility"), TEXT("ReducedEffects"), bReducedEffects);
    return Config.Write(Path, false);
}
