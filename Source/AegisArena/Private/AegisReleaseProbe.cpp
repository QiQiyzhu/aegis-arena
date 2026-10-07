#include "AegisReleaseProbe.h"
#if !UE_BUILD_SHIPPING
#include "AegisLab.h"
#include "AegisPortfolio.h"
#include "AegisPortfolioMusic.h"
#include "AegisTacticalFX.h"
#include "AegisCharacter.h"
#include "AegisAIController.h"
#include "EngineUtils.h"
#include "InputKeyEventArgs.h"
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
    bArt = FParse::Param(FCommandLine::Get(),TEXT("AegisArtProbe"));
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
    auto* FX = AAegisTacticalFX::Find(GetWorld());
    auto* Player = Cast<AAegisPlayerCharacter>(PC->GetPawn());
    auto Shot = [&](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::GetPath(Output),Name),false,false); };
    auto Key = [&](FKey K,bool Down) { PC->InputKey(FInputKeyEventArgs::CreateSimulated(K,Down?IE_Pressed:IE_Released,Down?1.f:0.f)); };
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
        if(!bArt) { Finish(true); break; }
        PC->MenuAction(TEXT("Language")); Shot(TEXT("briefing-en.png")); Stage=8; break;
    case 8:
        PC->MenuAction(TEXT("Language")); PC->MenuAction(TEXT("Deploy")); Stage=10; break;
    case 10:
        for(TActorIterator<AAegisAIController> It(GetWorld());It;++It) It->ShutdownAI();
        if(!Check(TEXT("tactical_pool_has_seven_fixed_components"),FX && FX->GetComponentCount()==7 && FX->GetActiveCount()<=AAegisTacticalFX::TransientBudget)) return;
        Shot(TEXT("tactical-hud.png")); Key(EKeys::RightMouseButton,true); break;
    case 11:
        if(!Check(TEXT("real_charge_input_drives_semantic_windup"),Player && Player->IsCharging() && Player->GetChargeFraction()>.2f && FX->HasSemanticCharge())) return;
        Shot(TEXT("charge-full.png")); break;
    case 12:
        PC->MenuAction(TEXT("Effects")); break;
    case 13:
        if(!Check(TEXT("reduced_effects_retains_semantic_charge"),PC->bReducedEffects && Player->IsCharging() && FX->HasSemanticCharge())) return;
        Shot(TEXT("charge-reduced.png")); break;
    case 14:
        EffectsBefore=FX->GetEmittedCount(); Key(EKeys::RightMouseButton,false); Next=Now+.08; break;
    case 15:
        if(!Check(TEXT("accepted_charge_release_emits_cue"),Portfolio->GetStatistics().ChargedShots==1 && FX->GetEmittedCount()>EffectsBefore && Portfolio->GetEnergy()==48)) return;
        Shot(TEXT("charge-impact.png")); PC->MenuAction(TEXT("Effects")); Next=Now+1.0; break;
    case 16:
        if(!Check(TEXT("transient_cues_decay_to_zero"),FX->GetActiveCount()==0 && !FX->HasSemanticCharge())) return;
        SnapshotEnergy=Portfolio->GetEnergy(); SnapshotHealth=Player->Health->Current;
        EffectsBefore=FX->GetEmittedCount();
        AAegisTacticalFX::Emit(this,EAegisTacticalCue::Repair,Player->GetActorLocation());
        FullEmission=FX->GetEmittedCount()-EffectsBefore; Next=Now+.12; break;
    case 17:
        if(!Check(TEXT("standard_cue_has_bounded_attack_and_decay"),FX->GetActiveCount()>0 && FX->GetActiveCount()<=AAegisTacticalFX::TransientBudget && FullEmission>8)) return;
        Shot(TEXT("repair-full.png")); Next=Now+1.0; break;
    case 18:
        PC->MenuAction(TEXT("Effects")); EffectsBefore=FX->GetEmittedCount();
        AAegisTacticalFX::Emit(this,EAegisTacticalCue::Repair,Player->GetActorLocation()); Next=Now+.12; break;
    case 19:
        if(!Check(TEXT("reduced_cue_emits_fewer_decorations"),FX->GetActiveCount()>0 && FX->GetEmittedCount()-EffectsBefore<FullEmission)) return;
        if(!Check(TEXT("presentation_does_not_change_health_or_energy"),Portfolio->GetEnergy()==SnapshotEnergy && Player->Health->Current==SnapshotHealth)) return;
        Shot(TEXT("repair-reduced.png")); Next=Now+1.0; break;
    case 20:
        for(int32 I=0;I<100;++I) AAegisTacticalFX::Emit(this,EAegisTacticalCue::Repair,Player->GetActorLocation());
        Next=Now+.08; break;
    case 21:
        if(!Check(TEXT("stress_drops_decorations_without_allocating_components"),FX->GetDroppedCount()>0 && FX->GetComponentCount()==7 && FX->GetPeakCount()<=AAegisTacticalFX::TransientBudget)) return;
        Next=Now+1.0; break;
    case 22:
        if(!Check(TEXT("stress_cues_expire_and_leave_semantic_layer"),FX->GetActiveCount()==0 && FX->GetComponentCount()==7)) return;
        PC->MenuAction(TEXT("Effects")); EffectsBefore=FX->GetEmittedCount(); Key(EKeys::SpaceBar,true); Next=Now+.07; break;
    case 23:
        Key(EKeys::SpaceBar,false);
        if(!Check(TEXT("real_dash_input_emits_directional_trail"),Player->GetDashCooldownRemaining()>0 && FX->GetEmittedCount()>EffectsBefore)) return;
        Shot(TEXT("dash.png")); break;
    case 24:
        PC->MenuAction(TEXT("Menu")); Finish(true); break;
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
    const FString Report = FString::Printf(TEXT("{\"passed\":%s,\"checks\":%d,\"artFixture\":%s,\"aiFrozenForArt\":%s,\"syntheticVisualCues\":%s,\"syntheticMenuActions\":true,\"simulatedFocusCallback\":true,\"realWindowFocusTest\":false,\"humanPlaytest\":false,\"shippingRuntime\":false}\n"), bPassed ? TEXT("true") : TEXT("false"), Checks,bArt?TEXT("true"):TEXT("false"),bArt?TEXT("true"):TEXT("false"),bArt?TEXT("true"):TEXT("false"));
    const bool Written = FFileHelper::SaveStringToFile(Report, *Output);
    if (bPassed && Written) PC->MenuAction(TEXT("Quit"));
    else FPlatformMisc::RequestExitWithStatus(false, 2, TEXT("Release flow probe failed"));
}
#endif
