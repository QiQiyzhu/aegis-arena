#include "AegisInputProbe.h"

#if !UE_BUILD_SHIPPING
#include "AegisAIController.h"
#include "AegisCharacter.h"
#include "AegisLab.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
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

AAegisInputProbe::AAegisInputProbe()
{
#if !UE_BUILD_SHIPPING
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
#else
    PrimaryActorTick.bCanEverTick = false;
#endif
}
void AAegisInputProbe::Initialize(AAegisScenarioRunner* InRunner, AAegisPlayerController* InController)
{
#if !UE_BUILD_SHIPPING
    Runner = InRunner;
    PC = InController;
    bRunning = true;
    StartedWall = FPlatformTime::Seconds();
    StageWall = StartedWall;
    StageGame = GetWorld()->GetTimeSeconds();
    // RenderOffscreen selects FNullApplication/FNullCursor on Windows. Without it,
    // SetMouseLocation would reposition the user's desktop pointer, so refuse to run.
    const bool Offscreen = FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen"));
    const bool OptIn = FParse::Param(FCommandLine::Get(), TEXT("AegisInputProbe"));
    if (!Offscreen || !OptIn || FParse::Param(FCommandLine::Get(), TEXT("NullRHI")) || !IsValid(Runner) || !IsValid(PC))
    {
        Finish(false, TEXT("Requires explicit AegisInputProbe + RenderOffscreen, rendered game world and local controller"));
        return;
    }
    if (!FParse::Value(FCommandLine::Get(), TEXT("AegisProbeOutput="), OutputDirectory) || OutputDirectory.IsEmpty())
    {
        Finish(false, TEXT("Missing AegisProbeOutput directory"));
        return;
    }
    OutputDirectory = FPaths::ConvertRelativePathToFull(OutputDirectory);
    if (IFileManager::Get().FileExists(*FPaths::Combine(OutputDirectory, TEXT("input-probe.json"))))
    {
        Finish(false, TEXT("Refusing to overwrite existing probe result"));
        return;
    }
    bMayWriteReport = IFileManager::Get().MakeDirectory(*OutputDirectory, true);
    if (!bMayWriteReport)
    {
        Finish(false, TEXT("Cannot create output directory"));
        return;
    }
    ScreenshotDelegate = FScreenshotRequest::OnScreenshotRequestProcessed().AddUObject(this, &AAegisInputProbe::ScreenshotCompleted);
    UE_LOG(LogTemp, Display, TEXT("AEGIS_INPUT_PROBE_BEGIN synthetic=1 offscreen=1 human_usability=0"));
#else
    (void)InRunner;
    (void)InController;
#endif
}
void AAegisInputProbe::EndPlay(const EEndPlayReason::Type Reason)
{
#if !UE_BUILD_SHIPPING
    FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotDelegate);
#endif
    Super::EndPlay(Reason);
}
void AAegisInputProbe::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
    if (!bRunning) return;
    const double Wall = FPlatformTime::Seconds();
    if (Wall - StartedWall > 30)
    {
        Check(TEXT("bounded_completion"), false, FString::Printf(TEXT("stage=%d exceeded 30 wall seconds"), static_cast<int32>(Stage)));
        Finish(false, TEXT("Bounded wall-time deadline exceeded"));
        return;
    }
    if (!IsValid(PC) || !IsValid(Runner))
    {
        Finish(false, TEXT("Runner or controller destroyed during probe"));
        return;
    }
    // Pulses span one complete PlayerInput processing frame; held keys get one press,
    // then remain down until a real release is injected through the same API.
    for (const FKey& Released : PendingReleases) Key(Released, IE_Released);
    PendingReleases.Reset();
    auto* Player = Cast<AAegisPlayerCharacter>(PC->GetPawn());
    if (!Player)
    {
        Finish(false, TEXT("Expected possessed Aegis player"));
        return;
    }
    const double Now = GetWorld()->GetTimeSeconds(), Age = Now - StageGame, WallAge = Wall - StageWall;
    const auto& Trial = Runner->GetTrial();
    auto Require = [&](const TCHAR* Name, bool Passed, const FString& Detail) {
        if (Check(Name, Passed, Detail)) return true;
        Finish(false, Name);
        return false;
    };
    auto CountCharacters = [&]() {
        int32 Count = 0;
        for (TActorIterator<AAegisCharacter> It(GetWorld()); It; ++It)
            if (IsValid(*It) && !It->IsActorBeingDestroyed()) ++Count;
        return Count;
    };
    auto CountControllers = [&]() {
        int32 Count = 0;
        for (TActorIterator<AAegisAIController> It(GetWorld()); It; ++It)
            if (IsValid(*It) && !It->IsActorBeingDestroyed()) ++Count;
        return Count;
    };
    switch (Stage)
    {
    case EStage::Warmup:
        if (Age < 0.6) return;
        if (!Require(TEXT("game_world_begun"), GetWorld()->IsGameWorld() && GetWorld()->HasBegunPlay(),
                     FString::Printf(TEXT("worldType=%d begun=%d"), static_cast<int32>(GetWorld()->WorldType), GetWorld()->HasBegunPlay()))) return;
        if (!Require(TEXT("briefing_initial_population"), Trial.phase == aegis::TrialPhase::Briefing && CountCharacters() == 2 && CountControllers() == 1,
                     FString::Printf(TEXT("characters=%d aiControllers=%d"), CountCharacters(), CountControllers()))) return;
        StartPosition = Player->GetActorLocation();
        Capture(TEXT("briefing.png"));
        Key(EKeys::W, IE_Pressed); Key(EKeys::LeftMouseButton, IE_Pressed); Tap(EKeys::SpaceBar);
        Advance(EStage::Briefing);
        break;
    case EStage::Briefing:
        if (Age < 0.35) return;
        if (!Require(TEXT("briefing_blocks_movement"), FVector::Dist2D(StartPosition, Player->GetActorLocation()) < 3,
                     FString::Printf(TEXT("distanceCm=%.3f"), FVector::Dist2D(StartPosition, Player->GetActorLocation())))) return;
        if (!Require(TEXT("briefing_blocks_fire"), Player->Combat->RangedShotsFired == 0, TEXT("held LMB delivered through PlayerController"))) return;
        if (!Require(TEXT("briefing_blocks_dash"), Player->GetDashCooldownRemaining() == 0, TEXT("Space delivered through PlayerController"))) return;
        Key(EKeys::W, IE_Released); Key(EKeys::LeftMouseButton, IE_Released); Tap(EKeys::Enter);
        Advance(EStage::Deploy);
        break;
    case EStage::Deploy:
        if (Age < 0.25) return;
        if (!Require(TEXT("enter_deploys_guided_wave"), Trial.phase == aegis::TrialPhase::Active && Trial.wave == 1 && Runner->LivingEnemies() == 3 && Player->bCombatEnabled,
                     FString::Printf(TEXT("wave=%d enemies=%d combatEnabled=%d"), Trial.wave, Runner->LivingEnemies(), Player->bCombatEnabled))) return;
        StartPosition = Player->GetActorLocation();
        Key(EKeys::W, IE_Pressed);
        Advance(EStage::Walk);
        break;
    case EStage::Walk:
        if (Age < 0.65) return;
        {
            const FVector Delta = Player->GetActorLocation() - StartPosition;
            const FVector ScreenForward = FRotationMatrix(FRotator(0, Player->SpringArm->GetComponentRotation().Yaw, 0)).GetUnitAxis(EAxis::X);
            if (!Require(TEXT("w_moves_screen_forward"), FVector::DotProduct(Delta, ScreenForward) > 100 && FMath::Abs(Delta.Y) < 30,
                         FString::Printf(TEXT("delta=%s"), *Delta.ToCompactString()))) return;
        }
        DashPosition = Player->GetActorLocation();
        Tap(EKeys::SpaceBar);
        Advance(EStage::DashPressed);
        break;
    case EStage::DashPressed:
        if (Age < 0.10) return;
        if (!Require(TEXT("space_starts_dash_cooldown"), Player->GetDashCooldownRemaining() > 2.0f,
                     FString::Printf(TEXT("remaining=%.3f"), Player->GetDashCooldownRemaining()))) return;
        DashReadyTime = Now + Player->GetDashCooldownRemaining();
        Tap(EKeys::SpaceBar);
        Advance(EStage::DashBlocked);
        break;
    case EStage::DashBlocked:
        if (Age < 0.24) return;
        if (!Require(TEXT("dash_moves_character"), FVector::Dist2D(DashPosition, Player->GetActorLocation()) > 180,
                     FString::Printf(TEXT("distanceCm=%.3f"), FVector::Dist2D(DashPosition, Player->GetActorLocation())))) return;
        if (!Require(TEXT("dash_cooldown_blocks_repeat"), FMath::Abs(Now + Player->GetDashCooldownRemaining() - DashReadyTime) < 0.05,
                     FString::Printf(TEXT("readyTimeError=%.4f"), Now + Player->GetDashCooldownRemaining() - DashReadyTime))) return;
        if (!Require(TEXT("dash_restores_walk_speed"), FMath::Abs(Player->GetCharacterMovement()->MaxWalkSpeed - 420.f) < 0.01f,
                     FString::Printf(TEXT("walkSpeed=%.1f"), Player->GetCharacterMovement()->MaxWalkSpeed))) return;
        Key(EKeys::W, IE_Released);
        Advance(EStage::Settle);
        break;
    case EStage::Settle:
        if (Age < 0.3) return;
        {
            ExpectedAim = Player->GetActorLocation() + FVector(350, 250, -Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            FVector2D Screen;
            const bool Projected = PC->ProjectWorldLocationToScreen(ExpectedAim, Screen);
            if (!Require(TEXT("ground_point_projects_to_viewport"), Projected, TEXT("fixed ground offset, independent of enemies"))) return;
            PC->SetMouseLocation(FMath::RoundToInt(Screen.X), FMath::RoundToInt(Screen.Y));
        }
        Advance(EStage::Aim);
        break;
    case EStage::Aim:
        if (Age < 0.12) return;
        if (!Require(TEXT("cursor_deprojects_to_ground_xy"), FVector::Dist2D(Player->GetAimPoint(), ExpectedAim) < 8,
                     FString::Printf(TEXT("errorCm=%.3f aim=%s"), FVector::Dist2D(Player->GetAimPoint(), ExpectedAim), *Player->GetAimPoint().ToCompactString()))) return;
        if (!Require(TEXT("cursor_sets_character_heading"), FVector::DotProduct(Player->GetActorForwardVector(), (ExpectedAim - Player->GetActorLocation()).GetSafeNormal2D()) > 0.995,
                     TEXT("heading follows projected ground target"))) return;
        ShotsBeforeHold = Player->Combat->RangedShotsFired;
        Key(EKeys::LeftMouseButton, IE_Pressed);
        Advance(EStage::FireHeld);
        break;
    case EStage::FireHeld:
        if (!bActiveCaptured && Player->Combat->RangedShotsFired > ShotsBeforeHold)
        {
            Capture(TEXT("active.png"));
            bActiveCaptured = true;
        }
        if (Age < 1.6) return;
        if (!Require(TEXT("held_lmb_repeats_after_cooldown"), Player->Combat->RangedShotsFired - ShotsBeforeHold >= 2,
                     FString::Printf(TEXT("acceptedShots=%d heldSeconds=%.3f"), Player->Combat->RangedShotsFired - ShotsBeforeHold, Age))) return;
        Key(EKeys::LeftMouseButton, IE_Released);
        ShotsAfterRelease = Player->Combat->RangedShotsFired;
        Advance(EStage::FireReleased);
        break;
    case EStage::FireReleased:
        if (Age < 0.85) return;
        if (!Require(TEXT("lmb_release_stops_repeat"), Player->Combat->RangedShotsFired == ShotsAfterRelease,
                     FString::Printf(TEXT("acceptedShotsAfterRelease=%d"), Player->Combat->RangedShotsFired - ShotsAfterRelease))) return;
        Tap(EKeys::P);
        Advance(EStage::PauseRequested);
        break;
    case EStage::PauseRequested:
        if (WallAge < 0.15) return;
        if (!Require(TEXT("p_enters_pause"), PC->IsPaused(), TEXT("P binding invoked through PlayerController"))) return;
        PausedGame = Now;
        PausePosition = Player->GetActorLocation();
        ShotsAfterRelease = Player->Combat->RangedShotsFired;
        Key(EKeys::W, IE_Pressed); Key(EKeys::LeftMouseButton, IE_Pressed); Tap(EKeys::SpaceBar);
        Capture(TEXT("paused.png"));
        Advance(EStage::Paused);
        break;
    case EStage::Paused:
        if (WallAge < 0.55) return;
        if (!Require(TEXT("pause_freezes_game_clock"), FMath::Abs(Now - PausedGame) < 0.001,
                     FString::Printf(TEXT("wallDelta=%.3f gameDelta=%.6f"), WallAge, Now - PausedGame))) return;
        if (!Require(TEXT("pause_blocks_movement_and_fire"), FVector::Dist2D(PausePosition, Player->GetActorLocation()) < 1 && Player->Combat->RangedShotsFired == ShotsAfterRelease,
                     TEXT("W/LMB/Space injected while paused"))) return;
        Key(EKeys::W, IE_Released); Key(EKeys::LeftMouseButton, IE_Released); Tap(EKeys::P);
        Advance(EStage::ResumeRequested);
        break;
    case EStage::ResumeRequested:
        if (WallAge < 0.2) return;
        if (!Require(TEXT("p_resumes_game_clock"), !PC->IsPaused() && Now > PausedGame + 0.03,
                     FString::Printf(TEXT("paused=%d gameDelta=%.3f"), PC->IsPaused(), Now - PausedGame))) return;
        PreviousPawnId = Player->GetUniqueID();
        Key(EKeys::W, IE_Pressed); Key(EKeys::LeftMouseButton, IE_Pressed);
        Advance(EStage::RestartHeld);
        break;
    case EStage::RestartHeld:
        if (Age < 0.1) return;
        if (!Require(TEXT("restart_fixture_has_held_inputs"), PC->IsInputKeyDown(EKeys::W) && PC->IsInputKeyDown(EKeys::LeftMouseButton),
                     TEXT("held keys intentionally cross R restart"))) return;
        Tap(EKeys::R);
        Advance(EStage::Restarted);
        break;
    case EStage::Restarted:
        if (Age < 0.4) return;
        if (!Require(TEXT("r_creates_fresh_briefing_pawn"), Trial.phase == aegis::TrialPhase::Briefing && Player->GetUniqueID() != PreviousPawnId && !Player->bCombatEnabled,
                     FString::Printf(TEXT("oldId=%u newId=%u"), PreviousPawnId, Player->GetUniqueID()))) return;
        if (!Require(TEXT("restart_resets_cooldowns_and_shots"), Player->GetDashCooldownRemaining() == 0 && Player->Combat->CooldownRemaining() == 0 && Player->Combat->RangedShotsFired == 0,
                     TEXT("new pawn owns fresh combat and dash state"))) return;
        if (!Require(TEXT("restart_flushes_held_inputs"), !PC->IsInputKeyDown(EKeys::W) && !PC->IsInputKeyDown(EKeys::LeftMouseButton),
                     TEXT("no inherited W/LMB state after R"))) return;
        if (!Require(TEXT("restart_retires_old_population"), CountCharacters() == 2 && CountControllers() == 1,
                     FString::Printf(TEXT("characters=%d aiControllers=%d"), CountCharacters(), CountControllers()))) return;
        Capture(TEXT("newattempt.png"));
        Advance(EStage::Screenshots);
        break;
    case EStage::Screenshots:
        {
            bool Saved = ScreenshotPaths.Num() == 4 && ScreenshotProcessed == 4;
            for (const FString& Path : ScreenshotPaths) Saved &= IFileManager::Get().FileSize(*Path) > 1024;
            if (!Saved) return;
            if (!Require(TEXT("four_rendered_screenshots_saved"), Saved,
                         FString::Printf(TEXT("requests=%d processed=%d files=%d"), ScreenshotRequests, ScreenshotProcessed, ScreenshotPaths.Num()))) return;
            if (!Require(TEXT("briefing_has_clickable_quit"), PC->GetHUD() && PC->GetHUD()->GetHitBoxWithName(TEXT("Quit")), TEXT("Visible Canvas Quit button owns an input hitbox"))) return;
            Tap(EKeys::Escape);
            Advance(EStage::ExitMenu);
        }
        break;
    case EStage::ExitMenu:
        if (WallAge < 0.2) return;
        if (!Require(TEXT("escape_opens_exit_menu"), PC->bMenuOpen && PC->IsPaused(), TEXT("Escape also opens menu from initial briefing"))) return;
        if (!Require(TEXT("menu_has_resume_restart_quit"), PC->GetHUD() && PC->GetHUD()->GetHitBoxWithName(TEXT("Menu")) && PC->GetHUD()->GetHitBoxWithName(TEXT("Restart")) && PC->GetHUD()->GetHitBoxWithName(TEXT("Quit")), TEXT("All three visible menu buttons are clickable"))) return;
        bExitThroughMenu = true;
        Finish(true, TEXT("Synthetic input, rendered lifecycle and normal menu exit completed"));
        break;
    }
#endif
}

#if !UE_BUILD_SHIPPING
void AAegisInputProbe::Key(const FKey& Input, EInputEvent Event)
{
    if (IsValid(PC)) PC->InputKey(FInputKeyEventArgs::CreateSimulated(Input, Event, Event == IE_Released ? 0.f : 1.f));
}
void AAegisInputProbe::Tap(const FKey& Input)
{
    Key(Input, IE_Pressed);
    PendingReleases.AddUnique(Input);
}
void AAegisInputProbe::Advance(EStage Next)
{
    Stage = Next;
    StageWall = FPlatformTime::Seconds();
    StageGame = GetWorld()->GetTimeSeconds();
}
bool AAegisInputProbe::Check(const TCHAR* Name, bool Passed, const FString& Detail)
{
    auto Entry = MakeShared<FJsonObject>();
    Entry->SetStringField(TEXT("name"), Name);
    Entry->SetBoolField(TEXT("passed"), Passed);
    Entry->SetStringField(TEXT("detail"), Detail);
    Assertions.Add(MakeShared<FJsonValueObject>(Entry));
    UE_LOG(LogTemp, Display, TEXT("AEGIS_INPUT_ASSERT %s | pass=%d | %s"), Name, Passed ? 1 : 0, *Detail);
    return Passed;
}
void AAegisInputProbe::Capture(const TCHAR* Name)
{
    const FString Path = FPaths::Combine(OutputDirectory, Name);
    if (IFileManager::Get().FileExists(*Path))
    {
        Finish(false, TEXT("Refusing to overwrite an existing screenshot"));
        return;
    }
    ScreenshotPaths.Add(Path);
    ++ScreenshotRequests;
    FScreenshotRequest::RequestScreenshot(Path, true, false);
}
void AAegisInputProbe::ScreenshotCompleted()
{
    if (bRunning) ++ScreenshotProcessed;
}
void AAegisInputProbe::Finish(bool Passed, const FString& Reason)
{
    if (!bRunning) return;
    bRunning = false;
    SetActorTickEnabled(false);
    for (const FKey& Input : {EKeys::W, EKeys::LeftMouseButton, EKeys::SpaceBar, EKeys::P, EKeys::R, EKeys::Enter}) Key(Input, IE_Released);
    if (IsValid(PC) && PC->IsPaused()) PC->SetPause(false);
    if (Passed && bExitThroughMenu)
    {
        PC->GetHUD()->NotifyHitBoxClick(TEXT("Quit"));
        Passed = Check(TEXT("quit_button_requests_normal_engine_exit"), IsEngineExitRequested(), TEXT("Canvas click dispatched to normal PlayerController QuitGame path"));
    }
    const double WallSeconds = FPlatformTime::Seconds() - StartedWall;
    auto Report = MakeShared<FJsonObject>();
    Report->SetNumberField(TEXT("schemaVersion"), 1);
    Report->SetStringField(TEXT("engine"), TEXT("unreal-runtime"));
    Report->SetBoolField(TEXT("syntheticInput"), true);
    Report->SetBoolField(TEXT("renderOffscreen"), FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")));
    Report->SetBoolField(TEXT("renderingEnabled"), !FParse::Param(FCommandLine::Get(), TEXT("NullRHI")));
    Report->SetBoolField(TEXT("humanUsabilityTest"), false);
    Report->SetBoolField(TEXT("fixtureDamage"), false);
    Report->SetBoolField(TEXT("passed"), Passed);
    Report->SetStringField(TEXT("reason"), Reason);
    Report->SetNumberField(TEXT("worldType"), static_cast<int32>(GetWorld()->WorldType));
    Report->SetBoolField(TEXT("begunPlay"), GetWorld()->HasBegunPlay());
    Report->SetNumberField(TEXT("wallSeconds"), WallSeconds);
    Report->SetNumberField(TEXT("screenshotRequests"), ScreenshotRequests);
    Report->SetNumberField(TEXT("screenshotProcessed"), ScreenshotProcessed);
    Report->SetArrayField(TEXT("assertions"), Assertions);
    TArray<TSharedPtr<FJsonValue>> Paths;
    for (const FString& Path : ScreenshotPaths) Paths.Add(MakeShared<FJsonValueString>(Path));
    Report->SetArrayField(TEXT("screenshots"), Paths);
    FString Json;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    if (!bMayWriteReport || !FFileHelper::SaveStringToFile(Json, *FPaths::Combine(OutputDirectory, TEXT("input-probe.json")))) Passed = false;
    UE_LOG(LogTemp, Display, TEXT("%s checks=%d screenshots=%d wall=%.3f reason=%s"),
           Passed ? TEXT("AEGIS_INPUT_PROBE_PASS") : TEXT("AEGIS_INPUT_PROBE_FAIL"), Assertions.Num(), ScreenshotProcessed, WallSeconds, *Reason);
    if (!bExitThroughMenu || !Passed)
        FPlatformMisc::RequestExitWithStatus(false, Passed ? 0 : 5, TEXT("Aegis synthetic input probe"));
}
#endif
