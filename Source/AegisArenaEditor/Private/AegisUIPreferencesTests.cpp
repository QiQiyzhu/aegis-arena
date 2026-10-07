#include "AegisUIPreferences.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAegisUILanguageTest, "Aegis.Core.UILanguagePersistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAegisUILanguageTest::RunTest(const FString& Parameters)
{
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), FGuid::NewGuid().ToString());
    const FString Path = FPaths::Combine(Directory, TEXT("AegisUI.ini"));
    TestFalse(TEXT("New installations default to Chinese"), FAegisUIPreferences::Load(Path, TEXT("")).bEnglish);
    FAegisUIPreferences Settings;
    Settings.bEnglish = true;
    Settings.bMusicMuted = true;
    TestTrue(TEXT("English preference is written to a new directory"), Settings.Save(Path));
    TestTrue(TEXT("A fresh session reads English from disk"), FAegisUIPreferences::Load(Path, TEXT("")).bEnglish);
    TestTrue(TEXT("Music mute survives a fresh session"), FAegisUIPreferences::Load(Path, TEXT("")).bMusicMuted);
    FString Before;
    FFileHelper::LoadFileToString(Before, *Path);
    for (const TCHAR* Flags : {TEXT("-AegisPortfolioCapture"), TEXT("-AegisInputProbe"), TEXT("-RenderOffscreen")})
    {
        auto Isolated = FAegisUIPreferences::Load(Path, Flags);
        TestFalse(TEXT("Automation starts Chinese despite the saved English preference"), Isolated.bEnglish);
        TestFalse(TEXT("Automation suppresses persistence"), Isolated.bPersistent);
        TestFalse(TEXT("Automation keeps deterministic unmuted music"), Isolated.bMusicMuted);
        TestTrue(TEXT("An isolated switch succeeds without a write"), Isolated.Save(Path));
        FString After;
        FFileHelper::LoadFileToString(After, *Path);
        TestEqual(TEXT("Capture/probe preserves the user's file byte for byte"), After, Before);
    }
    Settings.bEnglish = false;
    TestTrue(TEXT("Chinese can replace a saved English preference"), Settings.Save(Path));
    TestFalse(TEXT("A subsequent session reads Chinese"), FAegisUIPreferences::Load(Path, TEXT("")).bEnglish);
    TestTrue(TEXT("Create unknown language fixture"), FFileHelper::SaveStringToFile(TEXT("[Interface]\nLanguage=unknown\n[Other]\nKeep=untouched\n"), *Path));
    TestFalse(TEXT("Unknown values fall back to Chinese"), FAegisUIPreferences::Load(Path, TEXT("")).bEnglish);
    Settings.bEnglish = true;
    TestTrue(TEXT("Saving repairs an unknown language"), Settings.Save(Path));
    FString Updated;
    FFileHelper::LoadFileToString(Updated, *Path);
    TestTrue(TEXT("Saving preserves unrelated settings"), Updated.Contains(TEXT("Keep=untouched")));
    // A file cannot also be the parent directory of a settings file.
    TestFalse(TEXT("Write failure is reported to the caller"), Settings.Save(FPaths::Combine(Path, TEXT("Blocked.ini"))));
    IFileManager::Get().Delete(*Path);
    IFileManager::Get().DeleteDirectory(*Directory);
    return true;
}
#endif
