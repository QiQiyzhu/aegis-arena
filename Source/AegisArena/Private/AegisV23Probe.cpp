#include "AegisV23Probe.h"
#if !UE_BUILD_SHIPPING
#include "AegisAIController.h"
#include "AegisCharacter.h"
#include "AegisLab.h"
#include "AegisPortfolio.h"
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

AAegisV23Probe::AAegisV23Probe()
{
#if !UE_BUILD_SHIPPING
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
#endif
}
void AAegisV23Probe::Initialize(AAegisScenarioRunner* InRunner)
{
#if !UE_BUILD_SHIPPING
    Runner = InRunner;
    PC = GetWorld() ? Cast<AAegisPlayerController>(GetWorld()->GetFirstPlayerController()) : nullptr;
    Portfolio = AAegisPortfolio::Find(GetWorld());
    StartedWall = StageWall = FPlatformTime::Seconds();
    StageGame = GetWorld()->GetTimeSeconds();
    bRunning = true;
    if (!IsValid(Runner) || !IsValid(PC) || !IsValid(Portfolio) || !Portfolio->IsV23() ||
        !FParse::Param(FCommandLine::Get(), TEXT("AegisV23Probe")) ||
        !FParse::Param(FCommandLine::Get(), TEXT("AegisInputProbe")) ||
        !FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")) ||
        !FParse::Value(FCommandLine::Get(), TEXT("AegisV23ProbeOutput="), Output))
    { Finish(false, TEXT("Explicit v2.3 offscreen input fixture and output required")); return; }
    Output = FPaths::ConvertRelativePathToFull(Output);
    if (IFileManager::Get().FileExists(*FPaths::Combine(Output, TEXT("v23-probe.json"))) ||
        !IFileManager::Get().MakeDirectory(*Output, true))
    { Finish(false, TEXT("Report exists or output is not writable")); return; }
    bMayWrite = true;
    FreezeAI();
    UE_LOG(LogTemp, Display, TEXT("AEGIS_V23_PROBE_BEGIN syntheticKeyboard=1 fixtureDamage=1 fixturePlacement=1 aiFrozen=1"));
#else
    (void)InRunner;
#endif
}
void AAegisV23Probe::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
    if (!bRunning) return;
    if (FPlatformTime::Seconds() - StartedWall > 60 || !IsValid(Runner) || !IsValid(PC) || !IsValid(Portfolio))
    { Finish(false, TEXT("Actor lifetime or 60-second deadline exceeded")); return; }
    auto Pending = Releases; Releases.Reset();
    for (const FKey K : Pending) Key(K, false);
    FreezeAI();
    if (!Portfolio->GetTracePath().IsEmpty()) Trace = Portfolio->GetTracePath();
    auto* Player = Cast<AAegisPlayerCharacter>(PC->GetPawn());
    auto* Ally = Runner->GetCompanion();
    if (!Player || !Ally) { Finish(false, TEXT("Player or companion missing")); return; }
    if (FPlatformTime::Seconds() - StageWall < .2) return;
    const auto& S = Portfolio->GetStatistics();
    const double Elapsed = GetWorld()->GetTimeSeconds() - StageGame;
    const bool Active = Runner->GetTrial().phase == aegis::TrialPhase::Active;
    const FVector First = Portfolio->GetSurveyLocation(0) + FVector(0, 0, 100);
    const FVector Second = Portfolio->GetSurveyLocation(1) + FVector(0, 0, 100);
    switch (Stage)
    {
    case 0:
        if (!Check(TEXT("begun_game_world_v23"), GetWorld()->WorldType == EWorldType::Game && GetWorld()->HasBegunPlay() && Portfolio->IsV23())) return;
        if (!Check(TEXT("briefing_survey_defaults"), Runner->GetTrial().phase == aegis::TrialPhase::Briefing && Portfolio->GetEnergy() == 60 &&
            Portfolio->GetSurveyNode() == -1 && Portfolio->GetSurveyClaimedMask() == 0 && !Portfolio->IsSurveySupplySelected())) return;
        Tap(EKeys::G); Tap(EKeys::H); Advance(1); break;
    case 1:
        if (!Check(TEXT("briefing_survey_inputs_rejected"), Portfolio->GetSurveyNode() == -1 && !Portfolio->IsSurveySupplySelected() && S.SurveyKeys == 0 && S.EnergySpent == 0)) return;
        Tap(EKeys::Enter); Advance(2); break;
    case 2:
        if (!Check(TEXT("deploys_frozen_active_trial"), Active && Runner->LivingEnemies() > 0 && FrozenControllers.Num() > 1)) return;
        Place(Player, First + FVector(700, 0, 0)); Key(EKeys::G, true); Advance(3); break;
    case 3:
        if (!Check(TEXT("out_of_range_scan_rejected"), Portfolio->GetSurveyNode() == -1 && S.SurveyKeys == 0)) return;
        Key(EKeys::G, false); Place(Player, First); Place(Ally, First + FVector(120, 0, 0)); Tap(EKeys::H); Advance(4); break;
    case 4:
        if (!Check(TEXT("h_selects_supply"), Portfolio->IsSurveySupplySelected())) return;
        Key(EKeys::G, true); Advance(5); break;
    case 5:
        if (!Check(TEXT("full_health_supply_rejected"), Portfolio->GetSurveyNode() == -1 && S.SurveySupplies == 0 && Portfolio->GetSurveyClaimedMask() == 0)) return;
        Key(EKeys::G, false); Tap(EKeys::H); Advance(6); break;
    case 6:
        Key(EKeys::G, true); Advance(7); break;
    case 7:
        if (Elapsed < .5) return;
        if (!Check(TEXT("held_g_advances_partial_scan"), Portfolio->GetSurveyNode() == 0 && Portfolio->GetSurveyProgress() > .1f && Portfolio->GetSurveyProgress() < .8f)) return;
        Key(EKeys::G, false); Advance(8); break;
    case 8:
        if (!Check(TEXT("release_cancels_without_claim"), Portfolio->GetSurveyNode() == -1 && Portfolio->GetSurveyProgress() == 0 && S.SurveyCancelled == 1 && S.SurveyKeys == 0)) return;
        Key(EKeys::G, true); Advance(9); break;
    case 9:
        if (Elapsed < .4) return;
        Tap(EKeys::P); Advance(10); break;
    case 10:
        if (!Check(TEXT("pause_cancels_scan"), PC->IsPaused() && Portfolio->GetSurveyNode() == -1 && S.SurveyCancelled == 2)) return;
        PausedGame = GetWorld()->GetTimeSeconds(); Key(EKeys::G, false); Tap(EKeys::G); Tap(EKeys::H); Advance(11); break;
    case 11:
        if (FPlatformTime::Seconds() - StageWall < .4) return;
        if (!Check(TEXT("paused_survey_inputs_preserve_state_time"), PC->IsPaused() && FMath::Abs(GetWorld()->GetTimeSeconds() - PausedGame) < .001 &&
            Portfolio->GetSurveyNode() == -1 && !Portfolio->IsSurveySupplySelected() && S.SurveyCancelled == 2 && Portfolio->GetEnergy() == 60)) return;
        Tap(EKeys::P); Advance(12); break;
    case 12:
        Key(EKeys::G, true); Advance(13); break;
    case 13:
        if (Elapsed < .4) return;
        if (!Wound(Player, 5)) return;
        Advance(14); break;
    case 14:
        if (!Check(TEXT("damage_cancels_without_automatic_retry"), Portfolio->GetSurveyNode() == -1 && S.SurveyCancelled == 3 && S.SurveyKeys == 0 && Player->Health->Current == 135)) return;
        Key(EKeys::G, false); Key(EKeys::G, true); Advance(15); break;
    case 15:
        if (Elapsed < .4) return;
        Place(Player, First + FVector(700, 0, 0)); Advance(16); break;
    case 16:
        if (!Check(TEXT("leaving_radius_cancels_scan"), Portfolio->GetSurveyNode() == -1 && S.SurveyCancelled == 4 && Portfolio->GetSurveyClaimedMask() == 0)) return;
        Key(EKeys::G, false); Place(Player, First); Advance(17); break;
    case 17:
        Key(EKeys::G, true); Advance(18); break;
    case 18:
        if (Elapsed < 3) return;
        if (!Check(TEXT("continuous_hold_claims_one_key"), S.SurveyKeys == 1 && Portfolio->HasSurveyKey() && Portfolio->GetSurveyClaimedMask() == 1 && Portfolio->GetSurveyNode() == -1)) return;
        Advance(19); break;
    case 19:
        if (Elapsed < .5) return;
        if (!Check(TEXT("continued_hold_cannot_repeat_claim"), S.SurveyKeys == 1 && S.SurveySupplies == 0 && Portfolio->GetSurveyClaimedMask() == 1)) return;
        Key(EKeys::G, false); Tap(EKeys::G); Advance(20); break;
    case 20:
        if (!Check(TEXT("claimed_cache_repress_rejected"), Portfolio->GetSurveyNode() == -1 && S.SurveyKeys == 1)) return;
        Place(Player, Second); Key(EKeys::G, true); Advance(21); break;
    case 21:
        if (!Check(TEXT("pending_key_cannot_stack_at_second_cache"), Portfolio->GetSurveyNode() == -1 && S.SurveyKeys == 1 && Portfolio->GetSurveyClaimedMask() == 1)) return;
        Key(EKeys::G, false); Tap(EKeys::H); Place(Ally, Second + FVector(120, 0, 0));
        if (!Wound(Player, 30) || !Wound(Ally, 25)) return;
        Advance(22); break;
    case 22:
        if (!Check(TEXT("supply_selection_preserves_pending_key"), Portfolio->IsSurveySupplySelected() && Portfolio->HasSurveyKey() && Player->Health->Current == 105 && Ally->Health->Current == 95)) return;
        Key(EKeys::G, true); Advance(23); break;
    case 23:
        if (Elapsed < 3) return;
        if (!Check(TEXT("supply_heals_actual_twenty_fifteen"), Player->Health->Current == 125 && Ally->Health->Current == 110 && S.SurveyPlayerHealing == 20 && S.SurveyCompanionHealing == 15)) return;
        if (!Check(TEXT("supply_consumes_cache_without_energy_cost"), S.SurveyKeys == 1 && S.SurveySupplies == 1 && Portfolio->GetSurveyClaimedMask() == 3 && Portfolio->HasSurveyKey() && Portfolio->GetEnergy() == 60 && S.EnergySpent == 0)) return;
        Key(EKeys::G, false); Advance(24); break;
    case 24:
        Tap(EKeys::G); Advance(25); break;
    case 25:
        if (!Check(TEXT("claimed_supply_repress_rejected"), Portfolio->GetSurveyNode() == -1 && S.SurveySupplies == 1 && S.SurveyPlayerHealing == 20)) return;
        {
            int32 Index = 0;
            for (TActorIterator<AAegisAICharacter> It(GetWorld()); It; ++It)
                if (It->Team == EAegisTeam::Enemy) Place(*It, Runner->GetActorLocation() + FVector(1400, 900 + 120 * Index++, 100));
        }
        Place(Ally, Runner->GetObjectiveLocation() + FVector(-800, 0, 100));
        Place(Player, Runner->GetObjectiveLocation() + FVector(0, 0, 100)); Advance(26); break;
    case 26:
        if (Elapsed < .4) return;
        if (!Check(TEXT("productive_relay_binds_key_once"), Runner->IsPlayerInObjective() && !Runner->IsCompanionInObjective() && !Runner->IsObjectiveContested() &&
            !Portfolio->HasSurveyKey() && Portfolio->GetSurveyBoostRelayKey() == 10 && Portfolio->IsSurveyBoostActive() && S.SurveyBoosts == 1)) return;
        ChargeBefore = Runner->GetOperation().charge; Advance(27); break;
    case 27:
        if (Elapsed < 1.6) return;
        if (!Check(TEXT("bound_key_accelerates_actual_relay_progress"), (Runner->GetOperation().charge - ChargeBefore) / Elapsed > 1.1 &&
            (Runner->GetOperation().charge - ChargeBefore) / Elapsed < 1.4 && S.SurveyBoosts == 1)) return;
        Advance(28); break;
    case 28:
        if (!Runner->GetOperation().complete) return;
        if (!Check(TEXT("completed_relay_releases_boost_without_energy_change"), Portfolio->GetSurveyBoostRelayKey() == -1 && !Portfolio->IsSurveyBoostActive() &&
            S.SurveyBoosts == 1 && Portfolio->GetEnergy() == 60 && S.EnergySpent == 0 && S.EnergyEarned == 0)) return;
        Tap(EKeys::P); Advance(29); break;
    case 29:
        if (!Check(TEXT("normal_menu_quit_available"), PC->IsPaused() && PC->bMenuOpen)) return;
        Finish(true, TEXT("Completed v2.3 survey input fixture; normal menu X requests quit")); break;
    default: Finish(false, TEXT("Invalid stage")); break;
    }
#endif
}
void AAegisV23Probe::EndPlay(const EEndPlayReason::Type Reason)
{
#if !UE_BUILD_SHIPPING
    if (bRunning) Finish(false, TEXT("World ended before fixture completion"));
#endif
    Super::EndPlay(Reason);
}
#if !UE_BUILD_SHIPPING
void AAegisV23Probe::FreezeAI()
{
    for (TActorIterator<AAegisAIController> It(GetWorld()); It; ++It)
    {
        const TWeakObjectPtr<AAegisAIController> Weak(*It);
        if (!FrozenControllers.Contains(Weak)) { It->ShutdownAI(); FrozenControllers.Add(Weak); }
    }
}
void AAegisV23Probe::Key(FKey K, bool Down)
{
    if (!PC || Held.Contains(K) == Down) return;
    PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, Down ? IE_Pressed : IE_Released, Down ? 1.f : 0.f));
    if (Down) Held.Add(K); else Held.Remove(K);
}
void AAegisV23Probe::Tap(FKey K) { Key(K, true); Releases.AddUnique(K); }
void AAegisV23Probe::Advance(int32 Next) { Stage = Next; StageWall = FPlatformTime::Seconds(); StageGame = GetWorld()->GetTimeSeconds(); }
AAegisAICharacter* AAegisV23Probe::Hostile() const
{
    for (TActorIterator<AAegisAICharacter> It(GetWorld()); It; ++It)
        if (It->Team == EAegisTeam::Enemy && It->Health->IsAlive()) return *It;
    return nullptr;
}
void AAegisV23Probe::Place(AAegisCharacter* Character, const FVector& Location)
{
    if (!IsValid(Character)) return;
    Character->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
    auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("type"), TEXT("fixture_placement"));
    Row->SetStringField(TEXT("actor"), Character->GetName());
    Row->SetArrayField(TEXT("location"), {MakeShared<FJsonValueNumber>(Location.X), MakeShared<FJsonValueNumber>(Location.Y), MakeShared<FJsonValueNumber>(Location.Z)});
    Interventions.Add(MakeShared<FJsonValueObject>(Row));
}
bool AAegisV23Probe::Wound(AAegisCharacter* Character, float Amount)
{
    auto* Enemy = Hostile();
    const float Applied = Character && Enemy ? Character->Health->ApplyDamage(Amount, Enemy) : 0;
    auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("type"), TEXT("fixture_damage"));
    Row->SetStringField(TEXT("actor"), GetNameSafe(Character)); Row->SetStringField(TEXT("source"), GetNameSafe(Enemy));
    Row->SetNumberField(TEXT("requested"), Amount); Row->SetNumberField(TEXT("applied"), Applied);
    Interventions.Add(MakeShared<FJsonValueObject>(Row));
    if (Applied != Amount) { Finish(false, TEXT("Explicit wound was not applied exactly")); return false; }
    return true;
}
bool AAegisV23Probe::Check(const TCHAR* Name, bool Passed)
{
    auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("name"), Name); Row->SetBoolField(TEXT("passed"), Passed);
    Row->SetNumberField(TEXT("wallSeconds"), FPlatformTime::Seconds() - StartedWall);
    Row->SetNumberField(TEXT("gameSeconds"), GetWorld()->GetTimeSeconds());
    Row->SetBoolField(TEXT("paused"), PC->IsPaused());
    auto* Player = Cast<AAegisPlayerCharacter>(PC->GetPawn()); auto* Ally = Runner->GetCompanion();
    Row->SetNumberField(TEXT("playerHealth"), Player ? Player->Health->Current : -1);
    Row->SetNumberField(TEXT("companionHealth"), Ally ? Ally->Health->Current : -1);
    const auto& S = Portfolio->GetStatistics();
    Row->SetNumberField(TEXT("energy"), Portfolio->GetEnergy()); Row->SetNumberField(TEXT("energySpent"), S.EnergySpent);
    Row->SetNumberField(TEXT("surveyKeys"), S.SurveyKeys); Row->SetNumberField(TEXT("surveySupplies"), S.SurveySupplies);
    Row->SetNumberField(TEXT("surveyBoosts"), S.SurveyBoosts); Row->SetNumberField(TEXT("surveyCancelled"), S.SurveyCancelled);
    Row->SetNumberField(TEXT("surveyPlayerActualHealing"), S.SurveyPlayerHealing); Row->SetNumberField(TEXT("surveyCompanionActualHealing"), S.SurveyCompanionHealing);
    Row->SetNumberField(TEXT("surveyClaimedMask"), Portfolio->GetSurveyClaimedMask()); Row->SetNumberField(TEXT("surveyNode"), Portfolio->GetSurveyNode());
    Row->SetNumberField(TEXT("surveyProgress"), Portfolio->GetSurveyProgress()); Row->SetNumberField(TEXT("surveyBoostRelayKey"), Portfolio->GetSurveyBoostRelayKey());
    Row->SetBoolField(TEXT("surveyKeyPending"), Portfolio->HasSurveyKey()); Row->SetBoolField(TEXT("supplySelected"), Portfolio->IsSurveySupplySelected());
    Row->SetNumberField(TEXT("relayCharge"), Runner->GetOperation().charge);
    Assertions.Add(MakeShared<FJsonValueObject>(Row)); bAllPassed &= Passed;
    UE_LOG(LogTemp, Display, TEXT("AEGIS_V23_PROBE_ASSERT_%s %s"), Passed ? TEXT("PASS") : TEXT("FAIL"), Name);
    if (!Passed) Finish(false, Name);
    return Passed;
}
void AAegisV23Probe::Finish(bool Passed, const FString& Reason)
{
    if (!bRunning) return;
    auto Keys = Held; for (const FKey K : Keys) Key(K, false);
    Releases.Reset(); bRunning = false; SetActorTickEnabled(false);
    const double Wall = FPlatformTime::Seconds() - StartedWall;
    Passed &= bAllPassed && Assertions.Num() == 25 && Wall <= 60;
    auto Report = MakeShared<FJsonObject>();
    Report->SetNumberField(TEXT("schemaVersion"), 1); Report->SetBoolField(TEXT("passed"), Passed);
    Report->SetStringField(TEXT("engine"), TEXT("unreal-runtime"));
    Report->SetStringField(TEXT("scope"), TEXT("v2.3 controlled survey input fixture; not natural combat or human evaluation"));
    Report->SetBoolField(TEXT("syntheticKeyboardInput"), true); Report->SetBoolField(TEXT("fixtureDamage"), true);
    Report->SetBoolField(TEXT("fixturePlacement"), true); Report->SetBoolField(TEXT("aiFrozen"), FrozenControllers.Num() > 0);
    Report->SetNumberField(TEXT("frozenControllerCount"), FrozenControllers.Num());
    Report->SetBoolField(TEXT("humanPlaytest"), false); Report->SetBoolField(TEXT("policyPerformanceTest"), false);
    Report->SetBoolField(TEXT("renderOffscreen"), true); Report->SetBoolField(TEXT("nullRHI"), FParse::Param(FCommandLine::Get(), TEXT("NullRHI")));
    Report->SetStringField(TEXT("reason"), Reason); Report->SetStringField(TEXT("quitPath"), Passed ? TEXT("normal PlayerController X from paused menu") : TEXT("fixture failure exit"));
    Report->SetNumberField(TEXT("wallSeconds"), Wall); Report->SetNumberField(TEXT("lastStage"), Stage);
    Report->SetArrayField(TEXT("assertions"), Assertions); Report->SetArrayField(TEXT("interventions"), Interventions);
    Report->SetStringField(TEXT("portfolioTracePath"), Trace);
    TArray<TSharedPtr<FJsonValue>> Limits;
    for (const TCHAR* Item : {TEXT("human input and natural combat"), TEXT("real window focus loss"), TEXT("wave-three key rejection in native play"), TEXT("supply line-of-sight occlusion")})
        Limits.Add(MakeShared<FJsonValueString>(Item));
    Report->SetArrayField(TEXT("notCovered"), Limits);
    FString Json; FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    if (!bMayWrite || !FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Output, TEXT("v23-probe.json")))) Passed = false;
    UE_LOG(LogTemp, Display, TEXT("AEGIS_V23_PROBE_%s checks=%d"), Passed ? TEXT("PASS") : TEXT("FAIL"), Assertions.Num());
    if (Passed && PC) PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::X, IE_Pressed, 1));
    else FPlatformMisc::RequestExitWithStatus(false, 9, TEXT("Aegis v2.3 survey fixture failure"));
}
#endif
