#include "AegisPortfolioProbe.h"
#if !UE_BUILD_SHIPPING
#include "AegisPortfolio.h"
#include "AegisLab.h"
#include "AegisAIController.h"
#include "AegisCharacter.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#endif

AAegisPortfolioProbe::AAegisPortfolioProbe()
{
#if !UE_BUILD_SHIPPING
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
#endif
}
void AAegisPortfolioProbe::Initialize(AAegisScenarioRunner* InRunner)
{
#if !UE_BUILD_SHIPPING
    Runner = InRunner;
    PC = GetWorld() ? Cast<AAegisPlayerController>(GetWorld()->GetFirstPlayerController()) : nullptr;
    Portfolio = AAegisPortfolio::Find(GetWorld());
    StartedWall = StageWall = FPlatformTime::Seconds();
    bRunning = true;
    if (!IsValid(Runner) || !IsValid(PC) || !IsValid(Portfolio) || !PC->IsLocalController() ||
        !FParse::Param(FCommandLine::Get(), TEXT("AegisPortfolioProbe")) ||
        !FParse::Param(FCommandLine::Get(), TEXT("AegisPortfolio")) ||
        !FParse::Param(FCommandLine::Get(), TEXT("AegisInputProbe")) ||
        !FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")) ||
        !FParse::Value(FCommandLine::Get(), TEXT("AegisPortfolioProbeOutput="), Output))
    { Finish(false, TEXT("requires explicit portfolio offscreen input fixture and output")); return; }
    Output = FPaths::ConvertRelativePathToFull(Output);
    if (IFileManager::Get().FileExists(*FPaths::Combine(Output, TEXT("portfolio-probe.json"))) ||
        !IFileManager::Get().MakeDirectory(*Output, true))
    { Finish(false, TEXT("output report exists or directory is not writable")); return; }
    bMayWrite = true;
    FreezeAI();
    UE_LOG(LogTemp, Display, TEXT("AEGIS_PORTFOLIO_PROBE_BEGIN syntheticKeyboard=1 fixtureDamage=1 aiFrozen=1 humanPlay=0"));
#else
    (void)InRunner;
#endif
}
void AAegisPortfolioProbe::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
    if (!bRunning) return;
    if (FPlatformTime::Seconds() - StartedWall > 35 || !IsValid(Runner) || !IsValid(PC) || !IsValid(Portfolio))
    { Finish(false, TEXT("deadline or actor lifetime exceeded")); return; }
    for (FKey Key : Releases) PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0));
    Releases.Reset();
    FreezeAI();
    if (!Portfolio->GetTracePath().IsEmpty()) PortfolioTraces.AddUnique(Portfolio->GetTracePath());
    auto* Player = Cast<AAegisPlayerCharacter>(PC->GetPawn());
    if (!IsValid(Player)) { Finish(false, TEXT("player pawn unavailable")); return; }
    const double Age = FPlatformTime::Seconds() - StageWall;
    if (Age < 0.2) return;
    const auto& S = Portfolio->GetStatistics();
    const auto Phase = Runner->GetTrial().phase;
    switch (Stage)
    {
    case 0:
        if (!Check(TEXT("begun_game_world"), GetWorld()->WorldType == EWorldType::Game && GetWorld()->HasBegunPlay())) return;
        if (!Check(TEXT("briefing_preset_and_budget"), Phase == aegis::TrialPhase::Briefing && Portfolio->GetEnergy() == 60 &&
            Player->Health->Current == 140 && Runner->GetCompanion() && Runner->GetCompanion()->Health->Current == 120)) return;
        Tap(EKeys::Q); Tap(EKeys::E); Advance(1); break;
    case 1:
        if (!Check(TEXT("briefing_rejects_pulse_and_repair"), Portfolio->GetEnergy() == 60 && S.EnergySpent == 0 &&
            S.RepairsUsed == 0 && Player->PulseActivations == 0)) return;
        Tap(EKeys::Enter); Advance(2); break;
    case 2:
        if (!Check(TEXT("enter_deploys_active_trial"), Phase == aegis::TrialPhase::Active && Player->bCombatEnabled &&
            Runner->LivingEnemies() > 0 && FrozenControllers.Num() > 1)) return;
        Tap(EKeys::E); Advance(3); break;
    case 3:
    {
        if (!Check(TEXT("full_health_repair_has_no_cost_or_cooldown"), Portfolio->GetEnergy() == 60 &&
            Portfolio->GetRepairCooldown() == 0 && S.RepairsUsed == 0 && Player->Health->Current == 140)) return;
        AAegisAICharacter* Hostile = nullptr;
        for (TActorIterator<AAegisAICharacter> It(GetWorld()); It; ++It)
            if (It->Team == EAegisTeam::Enemy && It->Health->IsAlive()) { Hostile = *It; break; }
        // The damage API rejects nullptr source; a real hostile supplies legal fixture attribution.
        FixtureDamageApplied = Hostile ? Player->Health->ApplyDamage(45.f, Hostile) : 0.f;
        if (!Check(TEXT("disclosed_fixture_damage_applies_45"), FixtureDamageApplied == 45 && Player->Health->Current == 95)) return;
        Tap(EKeys::E); Advance(4); break;
    }
    case 4:
        if (!Check(TEXT("repair_spends_exactly_40"), Portfolio->GetEnergy() == 20 && S.EnergySpent == 40 && S.RepairsUsed == 1)) return;
        if (!Check(TEXT("repair_applies_and_records_actual_30"), Player->Health->Current == 125 && S.PlayerHealing == 30 && S.CompanionHealing == 0)) return;
        if (!Check(TEXT("repair_starts_game_clock_cooldown"), Portfolio->GetRepairCooldown() > 7 && Portfolio->GetRepairCooldown() <= 8)) return;
        Tap(EKeys::E); Advance(5); break;
    case 5:
        if (!Check(TEXT("repair_cooldown_rejects_repeat"), Portfolio->GetEnergy() == 20 && S.RepairsUsed == 1 &&
            Player->Health->Current == 125 && S.PlayerHealing == 30)) return;
        Tap(EKeys::Q); Advance(6); break;
    case 6:
        if (!Check(TEXT("low_energy_pulse_preserves_gameplay_cooldown"), Portfolio->GetEnergy() == 20 &&
            Player->PulseActivations == 0 && Player->GetPulseCooldownRemaining() == 0 && S.PulsesUsed == 0)) return;
        PreviousPawn = Player; Tap(EKeys::R); Advance(7); break;
    case 7:
        if (!Check(TEXT("restart_replaces_player_pawn"), TWeakObjectPtr<AAegisPlayerCharacter>(Player) != PreviousPawn &&
            Phase == aegis::TrialPhase::Briefing)) return;
        if (!Check(TEXT("restart_resets_energy_health_cooldown_and_statistics"), Portfolio->GetEnergy() == 60 &&
            Player->Health->Current == 140 && Portfolio->GetRepairCooldown() == 0 && S.EnergySpent == 0 &&
            S.RepairsUsed == 0 && S.PlayerHealing == 0)) return;
        Tap(EKeys::Enter); Advance(8); break;
    case 8:
        if (!Check(TEXT("second_deploy_uses_normal_enter"), Phase == aegis::TrialPhase::Active && Player->bCombatEnabled)) return;
        Tap(EKeys::Q); Advance(9); break;
    case 9:
        if (!Check(TEXT("accepted_pulse_spends_exactly_35"), Portfolio->GetEnergy() == 25 && S.EnergySpent == 35 && S.PulsesUsed == 1)) return;
        if (!Check(TEXT("accepted_pulse_updates_actual_counter_and_cooldown"), Player->PulseActivations == 1 &&
            Player->GetPulseCooldownRemaining() > 5 && Player->GetPulseCooldownRemaining() <= 6)) return;
        Tap(EKeys::Q); Advance(10); break;
    case 10:
        if (!Check(TEXT("pulse_cooldown_repeat_does_not_double_charge"), Portfolio->GetEnergy() == 25 &&
            S.PulsesUsed == 1 && Player->PulseActivations == 1 && S.EnergySpent == 35)) return;
        PreviousPawn = Player; Tap(EKeys::R); Advance(11); break;
    case 11:
        if (!Check(TEXT("second_restart_resets_both_abilities"), TWeakObjectPtr<AAegisPlayerCharacter>(Player) != PreviousPawn &&
            Phase == aegis::TrialPhase::Briefing && Portfolio->GetEnergy() == 60 && Player->PulseActivations == 0 &&
            Player->GetPulseCooldownRemaining() == 0 && S.EnergySpent == 0)) return;
        Tap(EKeys::Enter); Advance(12); break;
    case 12:
        if (Phase != aegis::TrialPhase::Active) { Finish(false, TEXT("third deploy did not activate")); return; }
        Tap(EKeys::P); Advance(13); break;
    case 13:
        if (!Check(TEXT("normal_pause_menu_opens"), PC->IsPaused() && PC->bMenuOpen)) return;
        PausedGame = GetWorld()->GetTimeSeconds();
        Tap(EKeys::Q); Tap(EKeys::E); Advance(14); break;
    case 14:
        if (Age < 0.4) return;
        if (!Check(TEXT("paused_q_and_e_preserve_time_and_resources"), PC->IsPaused() &&
            FMath::Abs(GetWorld()->GetTimeSeconds() - PausedGame) < 0.001 && Portfolio->GetEnergy() == 60 &&
            S.EnergySpent == 0 && Player->PulseActivations == 0 && Portfolio->GetRepairCooldown() == 0)) return;
        PreviousPawn = Player; Tap(EKeys::R); Advance(15); break;
    case 15:
        if (!Check(TEXT("restart_from_pause_restores_briefing"), !PC->IsPaused() && !PC->bMenuOpen &&
            Phase == aegis::TrialPhase::Briefing && TWeakObjectPtr<AAegisPlayerCharacter>(Player) != PreviousPawn &&
            Portfolio->GetEnergy() == 60 && Player->Health->Current == 140)) return;
        Tap(EKeys::Enter); Advance(16); break;
    case 16:
        if (Phase != aegis::TrialPhase::Active) { Finish(false, TEXT("held-fire deploy did not activate")); return; }
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Pressed, 1));
        bHeldFire = true; Advance(17); break;
    case 17:
        if (Age < 0.6) return;
        if (!Check(TEXT("normal_held_fire_actually_fires"), Player->Combat->RangedShotsFired > 0)) return;
        PreviousPawn = Player; Tap(EKeys::R); Advance(18); break;
    case 18:
        if (!Check(TEXT("restart_during_held_fire_replaces_pawn"), Phase == aegis::TrialPhase::Briefing &&
            TWeakObjectPtr<AAegisPlayerCharacter>(Player) != PreviousPawn && Player->Combat->RangedShotsFired == 0)) return;
        Tap(EKeys::Enter); Advance(19); break;
    case 19:
        if (Age < 0.6) return;
        if (!Check(TEXT("redeploy_does_not_retain_held_fire"), Phase == aegis::TrialPhase::Active &&
            Player->bCombatEnabled && Player->Combat->RangedShotsFired == 0 && Portfolio->GetEnergy() == 60)) return;
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Released, 0));
        bHeldFire = false;
        Tap(EKeys::P); Advance(20); break;
    case 20:
        if (!Check(TEXT("normal_quit_menu_is_available"), PC->IsPaused() && PC->bMenuOpen)) return;
        Finish(true, TEXT("completed 25 transaction and input checks; normal menu X requests quit")); break;
    default: Finish(false, TEXT("invalid probe stage")); break;
    }
#endif
}
void AAegisPortfolioProbe::EndPlay(const EEndPlayReason::Type Reason)
{
#if !UE_BUILD_SHIPPING
    if (bRunning) Finish(false, TEXT("world ended before probe completion"));
#endif
    Super::EndPlay(Reason);
}
#if !UE_BUILD_SHIPPING
void AAegisPortfolioProbe::FreezeAI()
{
    for (TActorIterator<AAegisAIController> It(GetWorld()); It; ++It)
    {
        const TWeakObjectPtr<AAegisAIController> Weak(*It);
        if (!FrozenControllers.Contains(Weak))
        {
            It->ShutdownAI();
            FrozenControllers.Add(Weak);
        }
    }
}
void AAegisPortfolioProbe::Tap(FKey Key)
{
    PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1));
    Releases.AddUnique(Key);
}
void AAegisPortfolioProbe::Advance(int32 Next) { Stage = Next; StageWall = FPlatformTime::Seconds(); }
bool AAegisPortfolioProbe::Check(const TCHAR* Name, bool Passed)
{
    auto Row = MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("name"), Name);
    Row->SetBoolField(TEXT("passed"), Passed);
    Row->SetNumberField(TEXT("wallSeconds"), FPlatformTime::Seconds() - StartedWall);
    Row->SetNumberField(TEXT("gameSeconds"), GetWorld()->GetTimeSeconds());
    Row->SetNumberField(TEXT("energy"), IsValid(Portfolio) ? Portfolio->GetEnergy() : -1);
    if (auto* Player = Cast<AAegisPlayerCharacter>(PC->GetPawn()))
    {
        Row->SetNumberField(TEXT("playerHealth"), Player->Health->Current);
        Row->SetNumberField(TEXT("pulseActivations"), Player->PulseActivations);
        Row->SetNumberField(TEXT("actualPlayerShots"), Player->Combat->RangedShotsFired);
    }
    Assertions.Add(MakeShared<FJsonValueObject>(Row));
    bAllPassed &= Passed;
    UE_LOG(LogTemp, Display, TEXT("AEGIS_PORTFOLIO_PROBE_ASSERT_%s %s"), Passed ? TEXT("PASS") : TEXT("FAIL"), Name);
    if (!Passed) Finish(false, Name);
    return Passed;
}
void AAegisPortfolioProbe::Finish(bool Passed, const FString& Reason)
{
    if (!bRunning) return;
    if (IsValid(PC))
    {
        for (FKey Key : Releases) PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0));
        if (bHeldFire) PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Released, 0));
    }
    Releases.Reset(); bHeldFire = false; bRunning = false;
    SetActorTickEnabled(false);
    const double Age = FPlatformTime::Seconds() - StartedWall;
    Passed &= bAllPassed && Assertions.Num() == 25 && Age <= 35;
    auto Report = MakeShared<FJsonObject>();
    Report->SetNumberField(TEXT("schemaVersion"), 1);
    Report->SetStringField(TEXT("engine"), TEXT("unreal-runtime"));
    Report->SetBoolField(TEXT("passed"), Passed);
    Report->SetStringField(TEXT("scope"), TEXT("portfolio transaction/input fixture; not natural combat, policy or human evaluation"));
    Report->SetBoolField(TEXT("fixtureDamage"), FixtureDamageApplied > 0);
    Report->SetNumberField(TEXT("fixtureDamageApplied"), FixtureDamageApplied);
    Report->SetStringField(TEXT("fixtureDamageSource"), TEXT("existing hostile actor; direct Health.ApplyDamage API"));
    Report->SetBoolField(TEXT("aiFrozen"), FrozenControllers.Num() > 0);
    Report->SetNumberField(TEXT("frozenControllerCount"), FrozenControllers.Num());
    Report->SetBoolField(TEXT("syntheticKeyboardInput"), true);
    Report->SetBoolField(TEXT("humanUsabilityTest"), false);
    Report->SetBoolField(TEXT("policyPerformanceTest"), false);
    Report->SetBoolField(TEXT("renderOffscreen"), true);
    Report->SetBoolField(TEXT("nullRHI"), FParse::Param(FCommandLine::Get(), TEXT("NullRHI")));
    Report->SetStringField(TEXT("reason"), Reason);
    Report->SetStringField(TEXT("quitPath"), Passed ? TEXT("normal PlayerController X from paused menu") : TEXT("fixture failure exit"));
    Report->SetNumberField(TEXT("wallSeconds"), Age);
    Report->SetNumberField(TEXT("lastStage"), Stage);
    Report->SetArrayField(TEXT("assertions"), Assertions);
    TArray<TSharedPtr<FJsonValue>> Traces;
    for (const FString& Trace : PortfolioTraces) Traces.Add(MakeShared<FJsonValueString>(Trace));
    Report->SetArrayField(TEXT("portfolioTracePaths"), Traces);
    FString Json;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    if (!bMayWrite || !FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Output, TEXT("portfolio-probe.json")))) Passed = false;
    UE_LOG(LogTemp, Display, TEXT("AEGIS_PORTFOLIO_PROBE_%s checks=%d"), Passed ? TEXT("PASS") : TEXT("FAIL"), Assertions.Num());
    if (Passed && IsValid(PC))
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::X, IE_Pressed, 1));
    else FPlatformMisc::RequestExitWithStatus(false, 9, TEXT("Aegis portfolio probe failure"));
}
#endif
