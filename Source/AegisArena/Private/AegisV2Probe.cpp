#include "AegisV2Probe.h"
#if !UE_BUILD_SHIPPING
#include "AegisAIController.h"
#include "AegisCharacter.h"
#include "AegisLab.h"
#include "AegisPortfolio.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMeshActor.h"
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

AAegisV2Probe::AAegisV2Probe()
{
#if !UE_BUILD_SHIPPING
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
#endif
}
void AAegisV2Probe::Initialize(AAegisScenarioRunner* InRunner)
{
#if !UE_BUILD_SHIPPING
    Runner = InRunner;
    PC = GetWorld() ? Cast<AAegisPlayerController>(GetWorld()->GetFirstPlayerController()) : nullptr;
    Portfolio = AAegisPortfolio::Find(GetWorld());
    StartedWall = StageWall = FPlatformTime::Seconds();
    StageGame = GetWorld()->GetTimeSeconds();
    bRunning = true;
    if (!IsValid(Runner) || !IsValid(PC) || !IsValid(Portfolio) || !Portfolio->IsV2() ||
        !FParse::Param(FCommandLine::Get(), TEXT("AegisV2Probe")) ||
        !FParse::Param(FCommandLine::Get(), TEXT("AegisInputProbe")) ||
        !FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")) ||
        !FParse::Value(FCommandLine::Get(), TEXT("AegisV2ProbeOutput="), Output))
    { Finish(false, TEXT("Explicit v2 offscreen input fixture and output required")); return; }
    Output = FPaths::ConvertRelativePathToFull(Output);
    if (IFileManager::Get().FileExists(*FPaths::Combine(Output, TEXT("v2-probe.json"))) ||
        !IFileManager::Get().MakeDirectory(*Output, true))
    { Finish(false, TEXT("Report exists or output is not writable")); return; }
    bMayWrite = true;
    FreezeAI();
    UE_LOG(LogTemp, Display, TEXT("AEGIS_V2_PROBE_BEGIN syntheticKeyboard=1 fixtureDamage=1 fixturePlacement=1 aiFrozen=1"));
#else
    (void)InRunner;
#endif
}
void AAegisV2Probe::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
    if (!bRunning) return;
    if (FPlatformTime::Seconds() - StartedWall > 45 || !IsValid(Runner) || !IsValid(PC) || !IsValid(Portfolio))
    { Finish(false, TEXT("Actor lifetime or 45-second deadline exceeded")); return; }
    auto Pending = Releases; Releases.Reset();
    for (const FKey K : Pending) Key(K, false);
    FreezeAI();
    if (!Portfolio->GetTracePath().IsEmpty()) Traces.AddUnique(Portfolio->GetTracePath());
    auto* Player = Cast<AAegisPlayerCharacter>(PC->GetPawn());
    auto* Ally = Runner->GetCompanion();
    if (!Player || !Ally) { Finish(false, TEXT("Player or companion missing")); return; }
    // The camera continues its normal interpolation after fixture placements.
    // Track the same world target through ordinary screen mouse coordinates,
    // including the final release frame; a fixed pixel would drift off the row.
    if ((Stage == 16 || Stage == 18) && !AimAt(FirstTarget.Get())) return;
    if (FPlatformTime::Seconds() - StageWall < 0.2) return;
    const auto& S = Portfolio->GetStatistics();
    const auto Phase = Runner->GetTrial().phase;
    switch (Stage)
    {
    case 0:
        if (!Check(TEXT("begun_game_world_v2"), GetWorld()->WorldType == EWorldType::Game && GetWorld()->HasBegunPlay() && Portfolio->IsV2())) return;
        if (!Check(TEXT("briefing_defaults_route_energy"), Phase == aegis::TrialPhase::Briefing && !Runner->IsNorthRouteFirst() && Portfolio->GetEnergy() == 60)) return;
        Tap(EKeys::V); Tap(EKeys::Q); Tap(EKeys::E); Tap(EKeys::F); Key(EKeys::RightMouseButton, true); Advance(1); break;
    case 1:
        if (!Check(TEXT("briefing_actions_have_no_cost"), Portfolio->GetEnergy() == 60 && S.EnergySpent == 0 && !Player->IsCharging())) return;
        if (!Check(TEXT("v_toggles_route_in_briefing"), Runner->IsNorthRouteFirst())) return;
        Key(EKeys::RightMouseButton, false); Tap(EKeys::Enter); Advance(2); break;
    case 2:
        if (!Check(TEXT("enter_deploys_frozen_active_trial"), Phase == aegis::TrialPhase::Active && Runner->LivingEnemies() > 0 && FrozenControllers.Num() > 1)) return;
        Tap(EKeys::V); Tap(EKeys::E); Advance(3); break;
    case 3:
        if (!Check(TEXT("active_v_cannot_change_route"), Runner->IsNorthRouteFirst())) return;
        if (!Check(TEXT("full_health_repair_has_no_cost"), Portfolio->GetRepairQuote() == 0 && Portfolio->GetEnergy() == 60 && S.RepairsUsed == 0)) return;
        Key(EKeys::RightMouseButton, true); Advance(4); break;
    case 4:
        if (!Check(TEXT("partial_charge_starts_without_spending"), Player->IsCharging() && Player->GetChargeFraction() > 0 && Player->GetChargeFraction() < 1 && Portfolio->GetEnergy() == 60)) return;
        Key(EKeys::RightMouseButton, false); Advance(5); break;
    case 5:
        if (!Check(TEXT("partial_charge_release_is_free"), !Player->IsCharging() && S.ChargedShots == 0 && Portfolio->GetEnergy() == 60)) return;
        Key(EKeys::RightMouseButton, true); Advance(6); break;
    case 6:
        if (!Player->IsCharging()) { Finish(false,TEXT("Charge did not start before primary input")); return; }
        Key(EKeys::LeftMouseButton,true); Advance(36); break;
    case 36:
        if (!Check(TEXT("charging_blocks_primary_fire"), Player->IsCharging() && Player->Combat->RangedShotsFired == 0)) return;
        Tap(EKeys::P); Advance(7); break;
    case 7:
        if (!Check(TEXT("pause_cancels_charge_without_cost"), PC->IsPaused() && PC->bMenuOpen && !Player->IsCharging() && Portfolio->GetEnergy() == 60 && S.ChargedShots == 0)) return;
        PausedGame = GetWorld()->GetTimeSeconds();
        Key(EKeys::RightMouseButton, false); Key(EKeys::LeftMouseButton, false);
        Tap(EKeys::Q); Tap(EKeys::E); Tap(EKeys::F); Advance(8); break;
    case 8:
        if (FPlatformTime::Seconds() - StageWall < 0.4) return;
        if (!Check(TEXT("paused_actions_preserve_time_energy"), PC->IsPaused() &&
            FMath::Abs(GetWorld()->GetTimeSeconds()-PausedGame) < 0.001 && Portfolio->GetEnergy() == 60 && S.EnergySpent == 0)) return;
        Tap(EKeys::P); Advance(9); break;
    case 9:
        if (!Wound(Player, 10)) return;
        if (!Check(TEXT("small_repair_quote_is_8"), Player->Health->Current == 130 && Portfolio->GetRepairQuote() == 8)) return;
        Tap(EKeys::E); Advance(10); break;
    case 10:
        if (!Check(TEXT("small_repair_spends_8_heals_10"), Portfolio->GetEnergy() == 52 && Player->Health->Current == 140 && S.RepairsUsed == 1 && S.PlayerHealing == 10 && Portfolio->GetRepairCooldown() > 7)) return;
        Tap(EKeys::E); Advance(11); break;
    case 11:
        if (!Check(TEXT("small_repair_cooldown_rejects_repeat"), Portfolio->GetEnergy() == 52 && S.RepairsUsed == 1 && S.EnergySpent == 8)) return;
        // Release profile requires a second real R press to confirm the restart.
        PreviousPawn = Player; Key(EKeys::R,true); Key(EKeys::R,false); Tap(EKeys::R); Advance(12); break;
    case 12:
        if (!Check(TEXT("restart_resets_route_energy_charge"), TWeakObjectPtr<AAegisPlayerCharacter>(Player) != PreviousPawn &&
            Phase == aegis::TrialPhase::Briefing && !Runner->IsNorthRouteFirst() && !Player->IsCharging() && Portfolio->GetEnergy() == 60 && S.RepairsUsed == 0)) return;
        Tap(EKeys::Enter); Advance(13); break;
    case 13:
        if (Phase != aegis::TrialPhase::Active || !Hostile(1)) { Finish(false, TEXT("Second deployment unavailable")); return; }
        FirstTarget = Hostile(0); SecondTarget = Hostile(1);
        Place(Player, FVector(-1200,-900,100)); Place(Ally, FVector(-1200,-650,100));
        Place(FirstTarget.Get(), FVector(-800,-900,100)); Place(SecondTarget.Get(), FVector(-500,-900,100));
        if (!Wound(Player,45) || !Wound(Ally,35)) return;
        Advance(14); break;
    case 14:
        if (!Check(TEXT("large_repair_quote_is_40"), Portfolio->GetRepairQuote() == 40)) return;
        Tap(EKeys::E); Advance(15); break;
    case 15:
        if (!Check(TEXT("large_repair_spends_40"), Portfolio->GetEnergy() == 20 && S.EnergySpent == 40 && S.RepairsUsed == 1)) return;
        if (!Check(TEXT("large_repair_actual_30_20"), Player->Health->Current == 125 && Ally->Health->Current == 105 && S.PlayerHealing == 30 && S.CompanionHealing == 20)) return;
        if (!AimAt(FirstTarget.Get())) return;
        Key(EKeys::RightMouseButton,true); Advance(16); break;
    case 16:
        if (GetWorld()->GetTimeSeconds() - StageGame < 0.8) return;
        if (!Check(TEXT("full_charge_reaches_ready_without_spending"), Player->IsCharging() && Player->GetChargeFraction() >= 1 && Portfolio->GetEnergy() == 20)) return;
        Key(EKeys::RightMouseButton,false); Advance(17); break;
    case 17:
        if (!Check(TEXT("charged_shot_spends_12"), Portfolio->GetEnergy() == 8 && S.ChargedShots == 1 && S.EnergySpent == 52)) return;
        if (!Check(TEXT("charged_shot_hits_two_for_52"), FirstTarget.IsValid() && SecondTarget.IsValid() &&
            FirstTarget->Health->Current == 48 && SecondTarget->Health->Current == 48 && Player->Combat->RangedShotsFired == 1)) return;
        Key(EKeys::RightMouseButton,true); Advance(18); break;
    case 18:
        if (GetWorld()->GetTimeSeconds() - StageGame < 0.8) return;
        Key(EKeys::RightMouseButton,false); Advance(19); break;
    case 19:
        if (!Check(TEXT("insufficient_energy_full_charge_release_is_free"), !Player->IsCharging() && Portfolio->GetEnergy() == 8 &&
            S.ChargedShots == 1 && FirstTarget->Health->Current == 48 && SecondTarget->Health->Current == 48)) return;
        // Release profile requires a second real R press to confirm the restart.
        PreviousPawn = Player; Key(EKeys::R,true); Key(EKeys::R,false); Tap(EKeys::R); Advance(20); break;
    case 20:
        if (!Check(TEXT("second_restart_resets_all_v2_state"), TWeakObjectPtr<AAegisPlayerCharacter>(Player) != PreviousPawn &&
            Phase == aegis::TrialPhase::Briefing && Portfolio->GetEnergy() == 60 && S.ChargedShots == 0 && S.Overclocks == 0 && !Portfolio->IsOverclockUsed())) return;
        Tap(EKeys::Enter); Advance(21); break;
    case 21:
        if (Phase != aegis::TrialPhase::Active) { Finish(false,TEXT("Third deployment unavailable")); return; }
        Tap(EKeys::F); Advance(22); break;
    case 22:
        if (!Check(TEXT("outside_relay_overclock_rejected"), !Runner->IsPlayerInObjective() && Portfolio->GetEnergy() == 60 && S.Overclocks == 0)) return;
        Place(Player,Runner->GetObjectiveLocation()+FVector(0,0,100));
        Place(Hostile(),Runner->GetObjectiveLocation()+FVector(110,0,100));
        Advance(23); break;
    case 23:
        if (!Runner->IsPlayerInObjective() || !Runner->IsObjectiveContested()) { Finish(false,TEXT("Contested relay placement failed")); return; }
        Tap(EKeys::F); Advance(24); break;
    case 24:
        if (!Check(TEXT("relay_overclock_spends_35"), Portfolio->GetEnergy() == 25 && S.EnergySpent == 35 && S.Overclocks == 1)) return;
        if (!Check(TEXT("overclock_is_2x_and_used"), Portfolio->GetOverclockMultiplier() == 2 && Portfolio->IsOverclockUsed() &&
            Portfolio->GetOverclockRemaining() > 5 && Portfolio->GetOverclockRemaining() <= 6)) return;
        OverclockBefore = Portfolio->GetOverclockRemaining(); ChargeBefore = Runner->GetOperation().charge;
        Tap(EKeys::F); Advance(25); break;
    case 25:
        if (!Check(TEXT("overclock_repeat_has_no_cost"), Portfolio->GetEnergy() == 25 && S.Overclocks == 1)) return;
        Advance(26); break;
    case 26:
        if (GetWorld()->GetTimeSeconds()-StageGame < 0.8) return;
        if (!Check(TEXT("contested_relay_consumes_duration_without_progress"), Runner->IsObjectiveContested() &&
            FMath::Abs(Runner->GetOperation().charge-ChargeBefore) < 0.001 && Portfolio->GetOverclockRemaining() < OverclockBefore-0.7f)) return;
        Tap(EKeys::P); Advance(27); break;
    case 27:
        if (!PC->IsPaused()) { Finish(false,TEXT("Overclock pause failed")); return; }
        OverclockBefore = Portfolio->GetOverclockRemaining(); PausedGame = GetWorld()->GetTimeSeconds(); Advance(28); break;
    case 28:
        if (FPlatformTime::Seconds()-StageWall < 0.4) return;
        if (!Check(TEXT("overclock_pause_freezes_timer"), PC->IsPaused() &&
            FMath::Abs(GetWorld()->GetTimeSeconds()-PausedGame) < 0.001 && FMath::Abs(Portfolio->GetOverclockRemaining()-OverclockBefore) < 0.001)) return;
        // Release profile requires a second real R press to confirm the restart.
        PreviousPawn = Player; Key(EKeys::R,true); Key(EKeys::R,false); Tap(EKeys::R); Advance(29); break;
    case 29:
        if (!Check(TEXT("overclock_restart_clears_used_route_and_energy"), !PC->IsPaused() && TWeakObjectPtr<AAegisPlayerCharacter>(Player) != PreviousPawn &&
            Phase == aegis::TrialPhase::Briefing && !Runner->IsNorthRouteFirst() && Portfolio->GetEnergy() == 60 &&
            !Portfolio->IsOverclockUsed() && Portfolio->GetOverclockRemaining() == 0 && S.Overclocks == 0)) return;
        Tap(EKeys::Enter); Advance(30); break;
    case 30:
        if (Phase != aegis::TrialPhase::Active) { Finish(false,TEXT("Fourth deployment unavailable")); return; }
        Place(Player,FVector(-1200,-900,100)); Place(Ally,FVector(-600,-900,100));
        if (!Wound(Ally,20)) return;
        Tap(EKeys::E); Advance(31); break;
    case 31:
        if (!Check(TEXT("out_of_range_ally_repair_rejected"), Portfolio->GetRepairQuote() == 0 && Portfolio->GetEnergy() == 60 && S.RepairsUsed == 0)) return;
        Place(Ally,FVector(-950,-900,100));
        {
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Cover = GetWorld()->SpawnActor<AStaticMeshActor>(FVector(-1075,-900,150),FRotator::ZeroRotator,Params);
            if (!Cover) { Finish(false,TEXT("Fixture cover spawn failed")); return; }
            Cover->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
            Cover->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            Cover->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
            Cover->GetStaticMeshComponent()->SetCanEverAffectNavigation(false);
            Cover->SetActorScale3D(FVector(0.4,1.5,3));
            auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("type"),TEXT("spawn_opaque_cover"));
            Row->SetStringField(TEXT("actor"),Cover->GetName()); Interventions.Add(MakeShared<FJsonValueObject>(Row));
        }
        Tap(EKeys::E); Advance(32); break;
    case 32:
        if (!Check(TEXT("covered_ally_repair_rejected"), Portfolio->GetRepairQuote() == 0 && Portfolio->GetEnergy() == 60 && S.RepairsUsed == 0 && Ally->Health->Current == 100)) return;
        Cover->Destroy(); Cover=nullptr; Advance(33); break;
    case 33:
        if (!Check(TEXT("clear_ally_repair_quote_is_16"), Portfolio->GetRepairQuote() == 16)) return;
        Tap(EKeys::E); Advance(34); break;
    case 34:
        if (!Check(TEXT("clear_ally_repair_charges_16_heals_20"), Portfolio->GetEnergy() == 44 && Player->Health->Current == 140 && Ally->Health->Current == 120 &&
            S.CompanionHealing == 20 && S.PlayerHealing == 0 && S.RepairsUsed == 1 && S.EnergySpent == 16)) return;
        Tap(EKeys::P); Advance(35); break;
    case 35:
        if (!Check(TEXT("normal_menu_quit_available"), PC->IsPaused() && PC->bMenuOpen)) return;
        Finish(true,TEXT("Completed v2 control fixture; normal menu X requests quit")); break;
    default: Finish(false,TEXT("Invalid stage")); break;
    }
#endif
}
void AAegisV2Probe::EndPlay(const EEndPlayReason::Type Reason)
{
#if !UE_BUILD_SHIPPING
    if (bRunning) Finish(false,TEXT("World ended before fixture completion"));
    if (IsValid(Cover)) Cover->Destroy();
#endif
    Super::EndPlay(Reason);
}
#if !UE_BUILD_SHIPPING
void AAegisV2Probe::FreezeAI()
{
    for (TActorIterator<AAegisAIController> It(GetWorld()); It; ++It)
    {
        const TWeakObjectPtr<AAegisAIController> Weak(*It);
        if (!FrozenControllers.Contains(Weak)) { It->ShutdownAI(); FrozenControllers.Add(Weak); }
    }
}
void AAegisV2Probe::Key(FKey K,bool Down)
{
    if (!PC || Held.Contains(K)==Down) return;
    PC->InputKey(FInputKeyEventArgs::CreateSimulated(K,Down?IE_Pressed:IE_Released,Down?1.f:0.f));
    if (Down) Held.Add(K); else Held.Remove(K);
}
void AAegisV2Probe::Tap(FKey K) { Key(K,true); Releases.AddUnique(K); }
void AAegisV2Probe::Advance(int32 Next) { Stage=Next; StageWall=FPlatformTime::Seconds(); StageGame=GetWorld()->GetTimeSeconds(); }
AAegisAICharacter* AAegisV2Probe::Hostile(int32 Index) const
{
    for (TActorIterator<AAegisAICharacter> It(GetWorld());It;++It)
        if (It->Team==EAegisTeam::Enemy && It->Health->IsAlive()) { if (Index--==0) return *It; }
    return nullptr;
}
void AAegisV2Probe::Place(AAegisCharacter* Character,const FVector& Location)
{
    if (!IsValid(Character)) return;
    Character->SetActorLocation(Location,false,nullptr,ETeleportType::TeleportPhysics);
    auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("type"),TEXT("fixture_placement"));
    Row->SetStringField(TEXT("actor"),Character->GetName());
    Row->SetArrayField(TEXT("location"),{MakeShared<FJsonValueNumber>(Location.X),MakeShared<FJsonValueNumber>(Location.Y),MakeShared<FJsonValueNumber>(Location.Z)});
    Interventions.Add(MakeShared<FJsonValueObject>(Row));
}
bool AAegisV2Probe::Wound(AAegisCharacter* Character,float Amount)
{
    auto* Enemy=Hostile();
    const float Applied=Character && Enemy ? Character->Health->ApplyDamage(Amount,Enemy) : 0;
    auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("type"),TEXT("fixture_damage"));
    Row->SetStringField(TEXT("actor"),GetNameSafe(Character)); Row->SetStringField(TEXT("source"),GetNameSafe(Enemy));
    Row->SetNumberField(TEXT("requested"),Amount); Row->SetNumberField(TEXT("applied"),Applied);
    Interventions.Add(MakeShared<FJsonValueObject>(Row));
    if (Applied!=Amount) { Finish(false,TEXT("Explicit wound was not applied exactly")); return false; }
    return true;
}
bool AAegisV2Probe::AimAt(AAegisCharacter* Character)
{
    auto* Player=Cast<AAegisPlayerCharacter>(PC->GetPawn());
    FVector2D Screen;
    FVector Point=Character?Character->GetActorLocation():FVector::ZeroVector;
    if (Player) Point.Z=Player->GetActorLocation().Z-Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    if (!Player || !Character || !PC->ProjectWorldLocationToScreen(Point,Screen))
    { Finish(false,TEXT("Fixture aiming projection failed")); return false; }
    PC->SetMouseLocation(FMath::RoundToInt(Screen.X),FMath::RoundToInt(Screen.Y));
    return true;
}
bool AAegisV2Probe::Check(const TCHAR* Name,bool Passed)
{
    auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("name"),Name); Row->SetBoolField(TEXT("passed"),Passed);
    Row->SetNumberField(TEXT("wallSeconds"),FPlatformTime::Seconds()-StartedWall);
    Row->SetNumberField(TEXT("gameSeconds"),GetWorld()->GetTimeSeconds());
    Row->SetNumberField(TEXT("energy"),Portfolio?Portfolio->GetEnergy():-1);
    if (auto* Player=Cast<AAegisPlayerCharacter>(PC->GetPawn()))
    {
        Row->SetNumberField(TEXT("playerHealth"),Player->Health->Current);
        Row->SetBoolField(TEXT("charging"),Player->IsCharging());
        Row->SetNumberField(TEXT("actualPlayerShots"),Player->Combat->RangedShotsFired);
        Row->SetStringField(TEXT("playerPosition"),Player->GetActorLocation().ToString());
        Row->SetStringField(TEXT("aimPoint"),Player->GetAimPoint().ToString());
    }
    for (int32 I=0; I<2; ++I)
    {
        const auto Target = I == 0 ? FirstTarget : SecondTarget;
        if (!Target.IsValid()) continue;
        auto Data=MakeShared<FJsonObject>();
        Data->SetStringField(TEXT("actor"),Target->GetName());
        Data->SetStringField(TEXT("position"),Target->GetActorLocation().ToString());
        Data->SetNumberField(TEXT("health"),Target->Health->Current);
        Row->SetObjectField(I == 0 ? TEXT("firstTarget") : TEXT("secondTarget"),Data);
    }
    if (Portfolio)
    {
        Row->SetNumberField(TEXT("chargedShots"),Portfolio->GetStatistics().ChargedShots);
        Row->SetNumberField(TEXT("repairs"),Portfolio->GetStatistics().RepairsUsed);
        Row->SetNumberField(TEXT("overclocks"),Portfolio->GetStatistics().Overclocks);
    }
    Assertions.Add(MakeShared<FJsonValueObject>(Row)); bAllPassed&=Passed;
    UE_LOG(LogTemp,Display,TEXT("AEGIS_V2_PROBE_ASSERT_%s %s"),Passed?TEXT("PASS"):TEXT("FAIL"),Name);
    if (!Passed) Finish(false,Name);
    return Passed;
}
void AAegisV2Probe::Finish(bool Passed,const FString& Reason)
{
    if (!bRunning) return;
    auto Keys=Held; for(const FKey K:Keys) Key(K,false);
    Releases.Reset(); bRunning=false; SetActorTickEnabled(false);
    const double Wall=FPlatformTime::Seconds()-StartedWall;
    Passed &= bAllPassed && Assertions.Num()==36 && Wall<=45;
    auto Report=MakeShared<FJsonObject>();
    Report->SetNumberField(TEXT("schemaVersion"),1); Report->SetBoolField(TEXT("passed"),Passed);
    Report->SetStringField(TEXT("engine"),TEXT("unreal-runtime"));
    Report->SetStringField(TEXT("scope"),TEXT("v2 controlled input/transaction fixture; not natural combat or human evaluation"));
    Report->SetBoolField(TEXT("syntheticKeyboardInput"),true); Report->SetBoolField(TEXT("fixtureDamage"),true);
    Report->SetBoolField(TEXT("fixturePlacement"),true); Report->SetBoolField(TEXT("aiFrozen"),FrozenControllers.Num()>0);
    Report->SetNumberField(TEXT("frozenControllerCount"),FrozenControllers.Num());
    Report->SetBoolField(TEXT("humanPlaytest"),false); Report->SetBoolField(TEXT("policyPerformanceTest"),false);
    Report->SetBoolField(TEXT("renderOffscreen"),true); Report->SetBoolField(TEXT("nullRHI"),FParse::Param(FCommandLine::Get(),TEXT("NullRHI")));
    Report->SetStringField(TEXT("reason"),Reason); Report->SetStringField(TEXT("quitPath"),Passed?TEXT("normal PlayerController X from paused menu"):TEXT("fixture failure exit"));
    Report->SetNumberField(TEXT("wallSeconds"),Wall); Report->SetNumberField(TEXT("lastStage"),Stage);
    Report->SetArrayField(TEXT("assertions"),Assertions); Report->SetArrayField(TEXT("interventions"),Interventions);
    TArray<TSharedPtr<FJsonValue>> Paths; for(const FString& Path:Traces) Paths.Add(MakeShared<FJsonValueString>(Path));
    Report->SetArrayField(TEXT("portfolioTracePaths"),Paths);
    TArray<TSharedPtr<FJsonValue>> Limits;
    for(const TCHAR* Item:{TEXT("real window focus loss"),TEXT("charge cancellation on natural stage transition"),TEXT("upgrade penetration of three targets"),
        TEXT("V on first upgrade page"),TEXT("F rejection during extraction"),TEXT("F expiration and next-relay eligibility"),TEXT("symbiosis healing")})
        Limits.Add(MakeShared<FJsonValueString>(Item));
    Report->SetArrayField(TEXT("notCovered"),Limits);
    FString Json; FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));
    if (!bMayWrite || !FFileHelper::SaveStringToFile(Json,*FPaths::Combine(Output,TEXT("v2-probe.json")))) Passed=false;
    UE_LOG(LogTemp,Display,TEXT("AEGIS_V2_PROBE_%s checks=%d"),Passed?TEXT("PASS"):TEXT("FAIL"),Assertions.Num());
    if (Passed && PC) PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::X,IE_Pressed,1));
    else FPlatformMisc::RequestExitWithStatus(false,9,TEXT("Aegis v2 control fixture failure"));
}
#endif
