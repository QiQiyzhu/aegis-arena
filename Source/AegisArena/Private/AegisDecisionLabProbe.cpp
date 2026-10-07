#include "AegisDecisionLabProbe.h"

#if !UE_BUILD_SHIPPING
#include "AegisDecisionLab.h"
#include "AegisAIController.h"
#include "AegisCharacter.h"
#include "AegisLab.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"
#endif

AAegisDecisionLabProbe::AAegisDecisionLabProbe()
{
#if !UE_BUILD_SHIPPING
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
#else
    PrimaryActorTick.bCanEverTick = false;
#endif
}

void AAegisDecisionLabProbe::Initialize(AAegisDecisionLab* InLab, AAegisPlayerController* InController)
{
#if !UE_BUILD_SHIPPING
    Lab = InLab; PC = InController; bRunning = true;
    StartedWall = StageWall = FPlatformTime::Seconds();
    StageGame = GetWorld()->GetTimeSeconds();
    const TCHAR* Cmd = FCommandLine::Get();
    if (!FParse::Param(Cmd, TEXT("AegisDecisionLab")) || !FParse::Param(Cmd, TEXT("AegisLabInputProbe")) ||
        !FParse::Param(Cmd, TEXT("AegisInputProbe")) || !FParse::Param(Cmd, TEXT("RenderOffscreen")) ||
        FParse::Param(Cmd, TEXT("NullRHI")) || FParse::Param(Cmd, TEXT("AegisLabCapture")) ||
        !IsValid(Lab) || !IsValid(PC) || Lab->bAutomated)
    {
        Finish(false, TEXT("Requires manual DecisionLab, LabInputProbe, InputProbe and rendered offscreen; no automatic capture"));
        return;
    }
    if (!FParse::Value(Cmd, TEXT("AegisLabProbeOutput="), OutputDirectory) || OutputDirectory.IsEmpty())
    {
        Finish(false, TEXT("Missing AegisLabProbeOutput")); return;
    }
    OutputDirectory = FPaths::ConvertRelativePathToFull(OutputDirectory);
    if (IFileManager::Get().FileExists(*FPaths::Combine(OutputDirectory, TEXT("lab-input.json"))))
    {
        Finish(false, TEXT("Refusing to overwrite a prior result")); return;
    }
    bMayWriteReport = IFileManager::Get().MakeDirectory(*OutputDirectory, true);
    if (!bMayWriteReport) { Finish(false, TEXT("Cannot create output directory")); return; }
    InitialSeed = Lab->Seed;
    ScreenshotDelegate = FScreenshotRequest::OnScreenshotRequestProcessed().AddUObject(this, &AAegisDecisionLabProbe::ScreenshotCompleted);
    UE_LOG(LogTemp, Display, TEXT("AEGIS_LAB_INPUT_BEGIN synthetic=1 offscreen=1 human_usability=0"));
#else
    (void)InLab; (void)InController;
#endif
}

void AAegisDecisionLabProbe::EndPlay(const EEndPlayReason::Type Reason)
{
#if !UE_BUILD_SHIPPING
    FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotDelegate);
#endif
    Super::EndPlay(Reason);
}

void AAegisDecisionLabProbe::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
    if (!bRunning) return;
    const double Wall = FPlatformTime::Seconds();
    if (Wall-StartedWall > 30) { Finish(false, TEXT("Exceeded 30 wall seconds")); return; }
    if (!IsValid(Lab) || !IsValid(PC)) { Finish(false, TEXT("Lab or controller destroyed")); return; }
    for (const FKey& Input : PendingReleases) Key(Input, IE_Released);
    PendingReleases.Reset();
    const double Now = GetWorld()->GetTimeSeconds(), Age = Now-StageGame, WallAge = Wall-StageWall;
    auto* Player = Cast<AAegisPlayerCharacter>(PC->GetPawn());
    if (Stage >= EStage::Walk && Stage <= EStage::RestartHeld && !IsValid(Player))
    {
        Finish(false, TEXT("Lost the possessed player during the input sequence")); return;
    }
    auto Require = [&](const TCHAR* Name, bool Passed, const FString& Detail) {
        if (Check(Name, Passed, Detail)) return true;
        Finish(false, Name); return false;
    };
    switch (Stage)
    {
    case EStage::Warmup:
        if (WallAge < .5) return;
        if (!Require(TEXT("game_world_begun"), GetWorld()->IsGameWorld() && GetWorld()->HasBegunPlay(), TEXT("Rendered Unreal game world"))) return;
        if (!Require(TEXT("manual_lab_briefing"), Lab->Phase == TEXT("Briefing") && !Lab->bAutomated, TEXT("Manual lab, no bot player or forced fixture outcome"))) return;
        Capture(TEXT("lab-briefing.png"));
        Tap(EKeys::One); Advance(EStage::PolicyOne); break;
    case EStage::PolicyOne:
        if (WallAge < .15) return;
        if (!Require(TEXT("policy_one_baseline"), Lab->Policy == TEXT("baseline"), TEXT("1 binding selects baseline"))) return;
        Tap(EKeys::Two); Advance(EStage::PolicyTwo); break;
    case EStage::PolicyTwo:
        if (WallAge < .15) return;
        if (!Require(TEXT("policy_two_improved"), Lab->Policy == TEXT("improved"), TEXT("2 binding selects improved label; this is not an efficacy claim"))) return;
        Tap(EKeys::Three); Advance(EStage::PolicyThree); break;
    case EStage::PolicyThree:
        if (WallAge < .15) return;
        if (!Require(TEXT("policy_three_priority"), Lab->Policy == TEXT("priority"), TEXT("3 binding selects priority"))) return;
        Tap(EKeys::One); Advance(EStage::PolicyReset); break;
    case EStage::PolicyReset:
        if (WallAge < .15 || ScreenshotProcessed != ScreenshotRequests) return;
        if (!Require(TEXT("policy_one_restores_baseline"), Lab->Policy == TEXT("baseline"), TEXT("1 restores baseline before deploy"))) return;
        Tap(EKeys::Enter); Advance(EStage::Deploy); break;
    case EStage::Deploy:
        if (Age < .25) return;
        if (!Require(TEXT("enter_starts_manual_encounter"), Lab->Phase == TEXT("Active") && IsValid(Player) && Lab->Player == Player && Player->bCombatEnabled,
            TEXT("Enter spawned and possessed a playable character"))) return;
        {
            auto* AI = Lab->GetCompanionController();
            if (!Require(TEXT("companion_uses_utility_bt"), AI && !AI->IsTacticalTrialEnabled() && AI->Decision->bUtilityPolicy && !AI->Decision->bImprovedPolicy,
                TEXT("Companion uses baseline Utility/BT path, not tactical dispatch"))) return;
            TArray<AAegisAICharacter*> Enemies;
            for (TActorIterator<AAegisAICharacter> It(GetWorld()); It; ++It)
                if (IsValid(*It) && !It->IsActorBeingDestroyed() && It->Team == EAegisTeam::Enemy && It->Health->IsAlive())
                    Enemies.Add(*It);
            const bool TwoEnemies = Enemies.Num() == 2 && Enemies.Num() == Lab->EnemyCount && Enemies[0] != Enemies[1];
            const bool RejectHostile = TwoEnemies && !AI->SetFollowPositionLink(Enemies[0]);
            auto* EnemyAI = TwoEnemies ? Cast<AAegisAIController>(Enemies[0]->GetController()) : nullptr;
            const bool RejectEnemyLink = EnemyAI && !EnemyAI->SetFollowPositionLink(Enemies[1]);
            // Restore the authorized link before any assertion can exit this
            // fixture. These API checks never alter health or actor positions.
            const bool AcceptPlayer = AI->SetFollowPositionLink(Player);
            if (!Require(TEXT("position_link_rejects_hostile"), RejectHostile,
                FString::Printf(TEXT("Direct API: companion rejects hostile; observedEnemies=%d expectedEnemies=%d distinctPair=%d"),
                    Enemies.Num(), Lab->EnemyCount, TwoEnemies ? 1 : 0))) return;
            if (!Require(TEXT("position_link_rejects_enemy_controller"), RejectEnemyLink,
                FString::Printf(TEXT("Direct API: first enemy controller rejects second same-team enemy; observedEnemies=%d controllerValid=%d"),
                    Enemies.Num(), EnemyAI ? 1 : 0))) return;
            if (!Require(TEXT("position_link_accepts_player"), AcceptPlayer,
                TEXT("Direct API: authorized player link restored after invalid registrations"))) return;
            const auto& Telemetry = AI->GetDecisionTelemetry();
            if (!Require(TEXT("baseline_does_not_sample_registered_link"), Telemetry.FollowLinkAvailable &&
                Telemetry.FollowLinkSampleGameSeconds < 0 && Telemetry.FollowLinkDistance < 0,
                TEXT("Baseline records registration but does not consume linked position or distance"))) return;
        }
        PreviousPlayerId = Player->GetUniqueID(); PreviousCompanion = Lab->Companion;
        StartPosition = Player->GetActorLocation(); Key(EKeys::W, IE_Pressed); Advance(EStage::Walk); break;
    case EStage::Walk:
        if (Age < .3) return;
        WalkDistance = FVector::Dist2D(StartPosition, Player->GetActorLocation());
        if (!Require(TEXT("w_moves_player"), WalkDistance > 40, FString::Printf(TEXT("distanceCm=%.3f"), WalkDistance))) return;
        DashPosition = Player->GetActorLocation(); Tap(EKeys::SpaceBar); Advance(EStage::Dash); break;
    case EStage::Dash:
        if (Age < .1) return;
        if (!Require(TEXT("space_starts_dash_cooldown"), Player->GetDashCooldownRemaining() > 2.f,
            FString::Printf(TEXT("cooldownSeconds=%.3f"), Player->GetDashCooldownRemaining()))) return;
        DashReadyAt = Now+Player->GetDashCooldownRemaining(); Tap(EKeys::SpaceBar); Advance(EStage::DashRepeat); break;
    case EStage::DashRepeat:
        if (Age < .25) return;
        DashDistance = FVector::Dist2D(DashPosition, Player->GetActorLocation());
        if (!Require(TEXT("dash_moves_player"), DashDistance > 180, FString::Printf(TEXT("distanceCm=%.3f"), DashDistance))) return;
        if (!Require(TEXT("dash_cooldown_blocks_repeat"), FMath::Abs(Now+Player->GetDashCooldownRemaining()-DashReadyAt) < .05,
            TEXT("A second Space press did not reset the cooldown deadline"))) return;
        Key(EKeys::W, IE_Released); Advance(EStage::Settle); break;
    case EStage::Settle:
        if (Age < .3) return;
        {
            ExpectedAim = Player->GetActorLocation()+FVector(350, 250, -Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            FVector2D Screen = FVector2D::ZeroVector;
            const bool Projected = PC->ProjectWorldLocationToScreen(ExpectedAim, Screen);
            int32 W = 0, H = 0; PC->GetViewportSize(W, H);
            const FVector2D Pixel(FMath::RoundToInt(Screen.X), FMath::RoundToInt(Screen.Y));
            const bool Safe = Projected && Pixel.X >= 0 && Pixel.X < W && Pixel.Y >= 0 && Pixel.Y < H &&
                PC->GetHUD() && !PC->GetHUD()->GetHitBoxAtCoordinates(Pixel, true);
            if (!Require(TEXT("ground_aim_projects_outside_hud"), Safe, TEXT("Projected real ground point avoids all clickable HUD hitboxes"))) return;
            PC->SetMouseLocation(static_cast<int32>(Pixel.X), static_cast<int32>(Pixel.Y));
        }
        Advance(EStage::Aim); break;
    case EStage::Aim:
        if (Age < .15) return;
        if (!Require(TEXT("cursor_matches_ground_point"), FVector::Dist2D(Player->GetAimPoint(), ExpectedAim) < 8,
            FString::Printf(TEXT("groundErrorCm=%.3f"), FVector::Dist2D(Player->GetAimPoint(), ExpectedAim)))) return;
        ShotsBefore = Player->Combat->RangedShotsFired;
        Key(EKeys::LeftMouseButton, IE_Pressed); Advance(EStage::Fire); break;
    case EStage::Fire:
        if (!bCapturedActive && Player->Combat->RangedShotsFired > ShotsBefore)
        {
            Capture(TEXT("lab-active.png")); bCapturedActive = true;
        }
        if (Age < 1.6) return;
        HeldShots = Player->Combat->RangedShotsFired-ShotsBefore;
        if (!Require(TEXT("held_lmb_fires_repeatedly"), HeldShots >= 2, FString::Printf(TEXT("shots=%d heldSeconds=%.3f"), HeldShots, Age))) return;
        Key(EKeys::LeftMouseButton, IE_Released); ShotsAfter = Player->Combat->RangedShotsFired;
        Advance(EStage::FireReleased); break;
    case EStage::FireReleased:
        if (Age < .8 || ScreenshotRequests != ScreenshotProcessed) return;
        if (!Require(TEXT("released_lmb_stops_fire"), Player->Combat->RangedShotsFired == ShotsAfter, TEXT("No additional accepted shots after releasing LMB"))) return;
        Tap(EKeys::F1); Advance(EStage::Debug); break;
    case EStage::Debug:
        if (Age < .2) return;
        if (!Require(TEXT("f1_opens_debug"), PC->bShowDiagnostics, TEXT("F1 toggles the inspector through the actual input binding"))) return;
        if (!Require(TEXT("f1_preserves_lit_view_mode"), GetWorld()->GetGameViewport() && GetWorld()->GetGameViewport()->ViewModeIndex == VMI_Lit,
            TEXT("F1 did not invoke the engine's default wireframe debug binding"))) return;
        {
            const auto* AI = Lab->GetCompanionController();
            const bool Matching = AI && AI->GetDecisionTelemetry().DecisionSequence > 0 &&
                AI->GetDecisionTelemetry().Selected == AI->Decision->Selected &&
                AI->GetDecisionTelemetry().Scores == AI->Decision->Scores &&
                !AI->GetDecisionTelemetry().bTacticalTrial;
            if (!Require(TEXT("debug_snapshot_matches_controller"), Matching, TEXT("HUD snapshot carries the controller's real policy selection and score array"))) return;
        }
        Capture(TEXT("lab-debug.png")); Tap(EKeys::Escape); Advance(EStage::Pause); break;
    case EStage::Pause:
        if (WallAge < .2 || ScreenshotRequests != ScreenshotProcessed) return;
        if (!Require(TEXT("escape_pauses"), PC->IsPaused() && PC->bMenuOpen, TEXT("Escape enters the actual paused menu"))) return;
        PausedGame = Now; Capture(TEXT("lab-paused.png")); Advance(EStage::Frozen); break;
    case EStage::Frozen:
        if (WallAge < .4 || ScreenshotRequests != ScreenshotProcessed) return;
        if (!Require(TEXT("pause_freezes_world"), FMath::Abs(Now-PausedGame) < .001, FString::Printf(TEXT("wallSeconds=%.3f worldDelta=%.6f"), WallAge, Now-PausedGame))) return;
        Tap(EKeys::F1); Advance(EStage::PausedToggle); break;
    case EStage::PausedToggle:
        if (WallAge < .15) return;
        if (!Require(TEXT("f1_toggles_while_paused"), !PC->bShowDiagnostics && PC->IsPaused(), TEXT("Inspector closes while simulation remains paused"))) return;
        if (!Require(TEXT("paused_f1_preserves_lit_view_mode"), GetWorld()->GetGameViewport() && GetWorld()->GetGameViewport()->ViewModeIndex == VMI_Lit,
            TEXT("Toggling diagnostics while paused also leaves the renderer in Lit mode"))) return;
        Tap(EKeys::Escape); Advance(EStage::Resume); break;
    case EStage::Resume:
        if (WallAge < .2) return;
        if (!Require(TEXT("escape_resumes"), !PC->IsPaused() && !PC->bMenuOpen && Now > PausedGame+.05, TEXT("Escape resumes the same game clock"))) return;
        Key(EKeys::W, IE_Pressed); Key(EKeys::LeftMouseButton, IE_Pressed); Advance(EStage::RestartHeld); break;
    case EStage::RestartHeld:
        if (Age < .15) return;
        if (!Require(TEXT("restart_begins_with_held_inputs"), PC->IsInputKeyDown(EKeys::W) && PC->IsInputKeyDown(EKeys::LeftMouseButton), TEXT("W and LMB deliberately held across R"))) return;
        Tap(EKeys::R); Advance(EStage::Restarted); break;
    case EStage::Restarted:
        if (WallAge < .25) return;
        if (!Require(TEXT("r_restores_briefing_same_seed"), Lab->Phase == TEXT("Briefing") && Lab->Seed == InitialSeed && !PC->IsPaused(), TEXT("R reset preserves seed and returns to briefing"))) return;
        if (!Require(TEXT("restart_flushes_held_inputs"), !PC->IsInputKeyDown(EKeys::W) && !PC->IsInputKeyDown(EKeys::LeftMouseButton), TEXT("PlayerInput no longer retains W or LMB"))) return;
        if (!Require(TEXT("restart_retires_old_companion"), !PreviousCompanion.IsValid() || PreviousCompanion->IsActorBeingDestroyed(), TEXT("Old encounter companion was retired"))) return;
        Tap(EKeys::Enter); Advance(EStage::SecondDeploy); break;
    case EStage::SecondDeploy:
        if (Age < .2) return;
        if (!Require(TEXT("enter_restarts_fresh_pawn"), Lab->Phase == TEXT("Active") && IsValid(Player) && Player->GetUniqueID() != PreviousPlayerId && Lab->Player == Player,
            TEXT("Second Enter possesses a new player instance"))) return;
        if (!Require(TEXT("restart_resets_combat_state"), Player->Combat->RangedShotsFired == 0 && Player->Combat->CooldownRemaining() == 0 && Player->GetDashCooldownRemaining() == 0,
            TEXT("Fresh pawn has no inherited shot or dash cooldown"))) return;
        Advance(EStage::Screenshots); break;
    case EStage::Screenshots:
        {
            bool Saved = ScreenshotPaths.Num() == 4 && ScreenshotRequests == 4 && ScreenshotProcessed == 4;
            for (const FString& Path : ScreenshotPaths) Saved &= IFileManager::Get().FileSize(*Path) > 1024;
            if (!Saved) return;
            if (!Require(TEXT("four_completed_screenshots"), Saved, TEXT("Briefing, active, diagnostics and paused screenshots processed and saved"))) return;
            Finish(true, TEXT("Synthetic manual-input lifecycle completed; no policy-quality or human-usability claim"));
        }
        break;
    }
#endif
}

#if !UE_BUILD_SHIPPING
void AAegisDecisionLabProbe::Key(const FKey& Input, EInputEvent Event)
{
    if (IsValid(PC)) PC->InputKey(FInputKeyEventArgs::CreateSimulated(Input, Event, Event == IE_Released ? 0.f : 1.f));
}
void AAegisDecisionLabProbe::Tap(const FKey& Input)
{
    Key(Input, IE_Pressed); PendingReleases.AddUnique(Input);
}
void AAegisDecisionLabProbe::Advance(EStage Next)
{
    Stage = Next; StageWall = FPlatformTime::Seconds(); StageGame = GetWorld()->GetTimeSeconds();
}
bool AAegisDecisionLabProbe::Check(const TCHAR* Name, bool Passed, const FString& Detail)
{
    auto Entry = MakeShared<FJsonObject>();
    Entry->SetStringField(TEXT("name"), Name); Entry->SetBoolField(TEXT("passed"), Passed); Entry->SetStringField(TEXT("detail"), Detail);
    Assertions.Add(MakeShared<FJsonValueObject>(Entry));
    UE_LOG(LogTemp, Display, TEXT("AEGIS_LAB_INPUT_CHECK %s pass=%d %s"), Name, Passed ? 1 : 0, *Detail);
    return Passed;
}
void AAegisDecisionLabProbe::Capture(const TCHAR* Name)
{
    const FString Path = FPaths::Combine(OutputDirectory, Name);
    if (ScreenshotRequests != ScreenshotProcessed || IFileManager::Get().FileExists(*Path))
    {
        Finish(false, TEXT("Overlapping screenshot request or existing output file")); return;
    }
    ScreenshotPaths.Add(Path); ++ScreenshotRequests;
    FScreenshotRequest::RequestScreenshot(Path, true, false);
}
void AAegisDecisionLabProbe::ScreenshotCompleted()
{
    if (bRunning) ++ScreenshotProcessed;
}
void AAegisDecisionLabProbe::Finish(bool Passed, const FString& Reason)
{
    if (!bRunning) return;
    bRunning = false; SetActorTickEnabled(false);
    for (const FKey& Input : {EKeys::W, EKeys::LeftMouseButton, EKeys::SpaceBar, EKeys::F1, EKeys::Escape,
        EKeys::R, EKeys::Enter, EKeys::One, EKeys::Two, EKeys::Three}) Key(Input, IE_Released);
    const double WallSeconds = FPlatformTime::Seconds()-StartedWall;
    auto Report = MakeShared<FJsonObject>();
    Report->SetNumberField(TEXT("schemaVersion"), 1);
    Report->SetStringField(TEXT("engine"), TEXT("unreal-runtime"));
    Report->SetBoolField(TEXT("syntheticInput"), true);
    Report->SetBoolField(TEXT("humanUsabilityTest"), false);
    Report->SetBoolField(TEXT("policyQualityTest"), false);
    Report->SetBoolField(TEXT("fixtureDamage"), false);
    Report->SetBoolField(TEXT("renderOffscreen"), FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")));
    Report->SetBoolField(TEXT("renderingEnabled"), !FParse::Param(FCommandLine::Get(), TEXT("NullRHI")));
    Report->SetBoolField(TEXT("passed"), Passed);
    Report->SetStringField(TEXT("reason"), Reason);
    Report->SetNumberField(TEXT("worldType"), static_cast<int32>(GetWorld()->WorldType));
    Report->SetBoolField(TEXT("begunPlay"), GetWorld()->HasBegunPlay());
    Report->SetNumberField(TEXT("wallSeconds"), WallSeconds);
    Report->SetNumberField(TEXT("seed"), InitialSeed);
    Report->SetNumberField(TEXT("walkDistanceCm"), WalkDistance);
    Report->SetNumberField(TEXT("dashDistanceCm"), DashDistance);
    Report->SetNumberField(TEXT("heldShots"), HeldShots);
    Report->SetNumberField(TEXT("screenshotRequests"), ScreenshotRequests);
    Report->SetNumberField(TEXT("screenshotProcessed"), ScreenshotProcessed);
    Report->SetArrayField(TEXT("assertions"), Assertions);
    TArray<TSharedPtr<FJsonValue>> Paths;
    for (const FString& Path : ScreenshotPaths) Paths.Add(MakeShared<FJsonValueString>(Path));
    Report->SetArrayField(TEXT("screenshots"), Paths);
    FString Json; FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    if (!bMayWriteReport || !FFileHelper::SaveStringToFile(Json, *FPaths::Combine(OutputDirectory, TEXT("lab-input.json")))) Passed = false;
    if (Passed) { UE_LOG(LogTemp, Display, TEXT("AEGIS_LAB_INPUT_COMPLETE checks=%d"), Assertions.Num()); }
    else { UE_LOG(LogTemp, Error, TEXT("AEGIS_LAB_INPUT_FAIL checks=%d reason=%s"), Assertions.Num(), *Reason); }
    FPlatformMisc::RequestExitWithStatus(false, Passed ? 0 : 2, TEXT("Aegis Decision Lab input probe"));
}
#endif
