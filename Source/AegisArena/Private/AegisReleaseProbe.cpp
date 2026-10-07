#include "AegisReleaseProbe.h"
#if !UE_BUILD_SHIPPING
#include "AegisLab.h"
#include "AegisPortfolio.h"
#include "AegisPortfolioMusic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#endif

AAegisReleaseProbe::AAegisReleaseProbe()
{
#if !UE_BUILD_SHIPPING
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
#endif
}
void AAegisReleaseProbe::Initialize(AAegisScenarioRunner* InRunner)
{
#if !UE_BUILD_SHIPPING
    Runner = InRunner;
    Stage = -1;
    PC = Cast<AAegisPlayerController>(GetWorld()->GetFirstPlayerController());
    Started = FPlatformTime::Seconds();
    Next = Started + 1;
    bRunning = PC && Runner && FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")) &&
        FParse::Value(FCommandLine::Get(), TEXT("AegisReleaseProbeOutput="), Output);
    if (!bRunning) { FPlatformMisc::RequestExitWithStatus(false, 2, TEXT("Release probe requires isolated offscreen output")); return; }
    Output = FPaths::ConvertRelativePathToFull(Output);
    if (IFileManager::Get().FileExists(*Output)) { bRunning = false; FPlatformMisc::RequestExitWithStatus(false, 2, TEXT("Release probe refuses stale report")); }
#else
    (void)InRunner;
#endif
}
void AAegisReleaseProbe::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
    if (!bRunning) return;
    const double Now = FPlatformTime::Seconds();
    if (Now - Started > 180 || !PC || !Runner) { Finish(false); return; }
    if (Now < Next) return;
    Next = Now + .4;
    auto* Portfolio = AAegisPortfolio::Find(GetWorld());
    auto* Music = AAegisPortfolioMusic::Find(GetWorld());
    switch (Stage++)
    {
    case -1:
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::GetPath(Output), TEXT("briefing.png")), false, false);
        break;
    case 0:
        if (!Check(TEXT("release_profile_opens_latest_briefing"), Portfolio && Portfolio->IsV23() && Runner->GetTrial().phase == aegis::TrialPhase::Briefing)) return;
        if (!Check(TEXT("isolated_preferences_default_chinese_music_on"), !PC->bEnglishUI && Music && !Music->IsMuted())) return;
        PC->MenuAction(TEXT("Language")); PC->MenuAction(TEXT("Music"));
        if (!Check(TEXT("clickable_language_and_music_controls"), PC->bEnglishUI && Music->IsMuted())) return;
        PC->MenuAction(TEXT("Language")); PC->MenuAction(TEXT("Music"));
        PC->MenuAction(TEXT("Deploy")); break;
    case 1:
        if (!Check(TEXT("deploy_starts_active_operation"), Runner->GetTrial().phase == aegis::TrialPhase::Active)) return;
        PC->OnApplicationActivationChanged(false);
        if (!Check(TEXT("focus_callback_pauses_active_game"), PC->bMenuOpen && PC->bPausedForFocus && GetWorld()->IsPaused())) return;
        PausedClock = GetWorld()->GetTimeSeconds();
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::GetPath(Output), TEXT("focus-pause.png")), false, false);
        PC->OnApplicationActivationChanged(true); break;
    case 2:
        if (!Check(TEXT("focus_return_does_not_resume_or_advance_time"), PC->bMenuOpen && GetWorld()->IsPaused() && FMath::Abs(GetWorld()->GetTimeSeconds()-PausedClock) < .001)) return;
        PC->MenuAction(TEXT("Menu"));
        if (!Check(TEXT("manual_resume_clears_focus_pause"), !PC->bMenuOpen && !PC->bPausedForFocus && !GetWorld()->IsPaused())) return;
        PC->MenuAction(TEXT("Restart"));
        if (!Check(TEXT("restart_requires_confirmation_and_preserves_attempt"), PC->bMenuOpen && PC->bRestartConfirmation && GetWorld()->IsPaused() && Runner->GetTrial().phase == aegis::TrialPhase::Active)) return;
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::GetPath(Output), TEXT("restart-confirm.png")), false, false);
        break;
    case 3:
        PC->MenuAction(TEXT("Menu"));
        if (!Check(TEXT("cancel_restart_keeps_existing_attempt_paused"), !PC->bRestartConfirmation && PC->bMenuOpen && GetWorld()->IsPaused() && Runner->GetTrial().phase == aegis::TrialPhase::Active)) return;
        PC->MenuAction(TEXT("Restart")); PC->MenuAction(TEXT("Restart")); break;
    case 4:
        if (!Check(TEXT("confirmed_restart_returns_clean_briefing"), !PC->bRestartConfirmation && !PC->bMenuOpen && !GetWorld()->IsPaused() && Runner->GetTrial().phase == aegis::TrialPhase::Briefing && Portfolio && Portfolio->GetEnergy() == 60 && Portfolio->GetSurveyClaimedMask() == 0)) return;
        Finish(true); break;
    }
#endif
}
#if !UE_BUILD_SHIPPING
bool AAegisReleaseProbe::Check(const TCHAR* Name, bool bPassed)
{
    UE_LOG(LogTemp, Display, TEXT("AEGIS_RELEASE_PROBE_CHECK %s %s"), Name, bPassed ? TEXT("PASS") : TEXT("FAIL"));
    if (!bPassed) { Finish(false); return false; }
    ++Checks; return true;
}
void AAegisReleaseProbe::Finish(bool bPassed)
{
    bRunning = false;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output), true);
    const FString Report = FString::Printf(TEXT("{\"passed\":%s,\"checks\":%d,\"syntheticMenuActions\":true,\"simulatedFocusCallback\":true,\"realWindowFocusTest\":false,\"humanPlaytest\":false,\"shippingRuntime\":false}\n"), bPassed ? TEXT("true") : TEXT("false"), Checks);
    const bool Written = FFileHelper::SaveStringToFile(Report, *Output);
    if (bPassed && Written) PC->MenuAction(TEXT("Quit"));
    else FPlatformMisc::RequestExitWithStatus(false, 2, TEXT("Release flow probe failed"));
}
#endif
