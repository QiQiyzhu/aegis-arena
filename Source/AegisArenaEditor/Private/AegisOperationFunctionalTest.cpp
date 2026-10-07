#include "AegisOperationFunctionalTest.h"
#include "AegisLab.h"
#include "AegisAIController.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"

AAegisOperationFunctionalTest::AAegisOperationFunctionalTest()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    PrimaryActorTick.TickInterval = 0;
    SetTimeLimit(85, EFunctionalTestResult::Failed);
}

void AAegisOperationFunctionalTest::StartTest()
{
    Super::StartTest();
    Stage = Assertions = 0;
    bFixtureRunning = true;
    StartedWall = FPlatformTime::Seconds();
    DeadlineWall = StartedWall + 35;
    Runner = nullptr;
    Player = nullptr;
    Companion = nullptr;
    DamageSource = nullptr;
    SetActorTickEnabled(true);
    if (!Verify(GetWorld() && GetWorld()->IsGameWorld() && GetWorld()->HasBegunPlay() &&
                    GetWorld()->WorldType == EWorldType::PIE, TEXT("begun_pie_world"))) return;
    int32 Count = 0;
    for (TActorIterator<AAegisScenarioRunner> It(GetWorld()); It; ++It)
    {
        Runner = *It;
        ++Count;
    }
    if (!Verify(Count == 1 && Runner->bObjectiveTrial, TEXT("one_objective_runner"))) return;
}

void AAegisOperationFunctionalTest::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bFixtureRunning && IsRunning()) Step();
}

bool AAegisOperationFunctionalTest::Verify(bool Condition, const TCHAR* Name)
{
    ++Assertions;
    if (!AssertTrue(Condition, Name))
    {
        Complete(false, Name);
        return false;
    }
    UE_LOG(LogTemp, Display, TEXT("AEGIS_OPERATION_ASSERT_PASS %s"), Name);
    return true;
}

void AAegisOperationFunctionalTest::StopAI()
{
    for (TActorIterator<AAegisAIController> It(GetWorld()); It; ++It) It->ShutdownAI();
}

void AAegisOperationFunctionalTest::Place(AAegisCharacter* Character, const FVector& GroundLocation)
{
    if (!IsValid(Character))
    {
        Complete(false, TEXT("Fixture occupant was destroyed"));
        return;
    }
    Character->GetCharacterMovement()->StopMovementImmediately();
    const FVector Location = GroundLocation + FVector(0, 0, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 10);
    if (!Character->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics))
        Complete(false, TEXT("Fixture occupant placement failed"));
}

AAegisAICharacter* AAegisOperationFunctionalTest::FirstEnemy() const
{
    for (TActorIterator<AAegisAICharacter> It(GetWorld()); It; ++It)
        if (It->Team == EAegisTeam::Enemy && It->Health->IsAlive()) return *It;
    return nullptr;
}

void AAegisOperationFunctionalTest::ClearEnemies()
{
    for (TActorIterator<AAegisAICharacter> It(GetWorld()); It; ++It)
        if (It->Team == EAegisTeam::Enemy && It->Health->IsAlive())
            It->Health->ApplyDamage(It->Health->Maximum + 1, Player);
}

void AAegisOperationFunctionalTest::Step()
{
    const double Wall = FPlatformTime::Seconds();
    if (!IsValid(Runner) || Wall > DeadlineWall)
    {
        Complete(false, FString::Printf(TEXT("Operation fixture failed or exceeded wall deadline at stage %d"), Stage));
        return;
    }
    // Newly spawned waves are stopped as well as the initial population.
    StopAI();
    const double Now = GetWorld()->GetTimeSeconds();
    const auto& Trial = Runner->GetTrial();
    const auto& Operation = Runner->GetOperation();
    const FVector Goal = Runner->GetObjectiveLocation();
    const FVector PlayerPark = Runner->GetActorLocation() + FVector(-1700, -1100, 0);
    const FVector CompanionPark = Runner->GetActorLocation() + FVector(1700, -1100, 0);
    using aegis::TrialPhase;
    switch (Stage)
    {
    case 0:
        if (!Runner->IsInteractive()) return;
        DeadlineWall = Wall + 45;
        Player = Cast<AAegisPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
        Companion = Runner->GetCompanion();
        if (!Verify(IsValid(Player) && IsValid(Companion) && Trial.phase == TrialPhase::Briefing &&
                        Operation.wave == 0 && Operation.charge == 0 && Operation.relay == 0 && !Operation.complete,
                    TEXT("briefing_objectives_reset"))) return;
        if (!Verify(!Runner->ChooseUpgrade(1) && !Runner->IsUpgradePending() && !Runner->HasUpgrade(1),
                    TEXT("briefing_cannot_choose_upgrade"))) return;
        Runner->DeployTrial(false);
        StopAI();
        if (!Verify(Trial.phase == TrialPhase::Active && Trial.wave == 1 && Operation.wave == 1 &&
                        Operation.requiredRelays() == 1 && Runner->LivingEnemies() > 0 && FirstEnemy(),
                    TEXT("opening_deploy_has_objective_and_hostiles"))) return;
        Place(Player, Runner->GetObjectiveLocation() + FVector(-80, 0, 0));
        Place(Companion, CompanionPark);
        Place(FirstEnemy(), Runner->GetObjectiveLocation() + FVector(80, 0, 0));
        CheckAt = Now + 0.7;
        Stage = 1;
        break;
    case 1:
        if (Now < CheckAt) return;
        if (!Verify(Runner->IsObjectiveContested() && Operation.charge == 0 && !Operation.complete,
                    TEXT("living_hostile_contests_actual_ring"))) return;
        ClearEnemies();
        Place(Player, PlayerPark);
        CheckAt = Now + 0.7;
        Stage = 2;
        break;
    case 2:
        if (Now < CheckAt) return;
        if (!Verify(Runner->LivingEnemies() == 0 && !Runner->IsObjectiveContested() && Operation.charge == 0 &&
                        Trial.phase == TrialPhase::Active && !Runner->IsUpgradePending(),
                    TEXT("clearing_enemies_does_not_complete_empty_ring"))) return;
        Place(Companion, Goal + FVector(80, 0, 0));
        CheckAt = Now + 0.7;
        Stage = 3;
        break;
    case 3:
        if (Now < CheckAt) return;
        if (!Verify(Operation.charge >= 0.5 && Operation.charge < 1.1 && !Operation.complete &&
                        FVector::DistSquared2D(Player->GetActorLocation(), Goal) > FMath::Square(260.f),
                    TEXT("companion_alone_charges_actual_ring"))) return;
        SampleAt = Now;
        SampleCharge = Operation.charge;
        Place(Player, Goal + FVector(-80, 0, 0));
        CheckAt = Now + 1;
        Stage = 4;
        break;
    case 4:
        if (Now < CheckAt) return;
        if (!Verify(FMath::Abs((Operation.charge - SampleCharge) - (Now - SampleAt) * 1.5) < 0.23 &&
                        !Operation.complete, TEXT("dual_occupancy_applies_one_point_five_rate"))) return;
        Stage = 5;
        break;
    case 5:
        if (!Runner->IsUpgradePending()) return;
        if (!Verify(Operation.complete && Trial.phase == TrialPhase::Intermission &&
                        UGameplayStatics::IsGamePaused(GetWorld()), TEXT("opening_completion_pauses_for_upgrade"))) return;
        PauseGameAt = Now;
        CheckAt = Wall + 0.4;
        Stage = 6;
        break;
    case 6:
        if (Wall < CheckAt) return;
        if (!Verify(Now == PauseGameAt && Trial.wave == 1 && Runner->IsUpgradePending(),
                    TEXT("upgrade_pause_freezes_game_clock"))) return;
        if (!Verify(!Runner->ChooseUpgrade(0) && !Runner->ChooseUpgrade(4) && Runner->IsUpgradePending() &&
                        UGameplayStatics::IsGamePaused(GetWorld()), TEXT("invalid_upgrade_keeps_selection_paused"))) return;
        if (!Verify(Runner->ChooseUpgrade(1) && Runner->HasUpgrade(1) && Player->Combat->bPiercingRounds &&
                        !Runner->IsUpgradePending() && !UGameplayStatics::IsGamePaused(GetWorld()),
                    TEXT("piercing_upgrade_applies_and_resumes"))) return;
        if (!Verify(!Runner->ChooseUpgrade(2) && !Runner->HasUpgrade(2),
                    TEXT("second_choice_cannot_apply_without_pending_upgrade"))) return;
        Place(Player, PlayerPark);
        Place(Companion, CompanionPark);
        Stage = 7;
        break;
    case 7:
        if (Trial.phase != TrialPhase::Active || Trial.wave != 2) return;
        if (!Verify(Operation.wave == 2 && Operation.requiredRelays() == 2 && Operation.relay == 0 &&
                        Operation.charge == 0 && Runner->HasUpgrade(1) && Now > PauseGameAt,
                    TEXT("middle_wave_starts_two_fresh_relays"))) return;
        FirstRelay = Goal;
        Place(Player, Goal + FVector(-80, 0, 0));
        Place(Companion, Goal + FVector(80, 0, 0));
        Stage = 8;
        break;
    case 8:
        if (Operation.relay == 0) return;
        if (!Verify(Operation.relay == 1 && Operation.charge == 0 && !Operation.complete &&
                        FVector::DistSquared2D(Goal, FirstRelay) > FMath::Square(520.f),
                    TEXT("first_middle_relay_activates_distinct_second_ring"))) return;
        CheckAt = Now + 0.7;
        Stage = 9;
        break;
    case 9:
        if (Now < CheckAt) return;
        if (!Verify(Operation.relay == 1 && Operation.charge == 0 && Trial.phase == TrialPhase::Active,
                    TEXT("occupying_old_ring_cannot_charge_new_relay"))) return;
        Place(Player, Goal + FVector(-80, 0, 0));
        Place(Companion, Goal + FVector(80, 0, 0));
        Stage = 10;
        break;
    case 10:
        if (!Operation.complete) return;
        if (!Verify(Operation.relay == 2 && Runner->LivingEnemies() > 0 && Trial.phase == TrialPhase::Active &&
                        !Runner->IsUpgradePending(), TEXT("two_relays_complete_but_living_enemies_block_transition"))) return;
        ClearEnemies();
        Stage = 11;
        break;
    case 11:
        if (!Runner->IsUpgradePending()) return;
        if (!Verify(Trial.phase == TrialPhase::Intermission && Trial.wave == 2 && UGameplayStatics::IsGamePaused(GetWorld()),
                    TEXT("middle_clear_opens_second_paused_upgrade"))) return;
        if (!Verify(!Runner->ChooseUpgrade(1) && Runner->IsUpgradePending() && UGameplayStatics::IsGamePaused(GetWorld()),
                    TEXT("owned_upgrade_cannot_be_selected_again"))) return;
        if (!Verify(Runner->ChooseUpgrade(2) && Runner->HasUpgrade(1) && Runner->HasUpgrade(2) &&
                        Player->bRestorativePulse && !Runner->IsUpgradePending() && !UGameplayStatics::IsGamePaused(GetWorld()),
                    TEXT("restorative_upgrade_applies_and_resumes"))) return;
        Place(Player, PlayerPark);
        Place(Companion, CompanionPark);
        Stage = 12;
        break;
    case 12:
        if (Trial.phase != TrialPhase::Active || Trial.wave != 3) return;
        Place(Player, Goal + FVector(-80, 0, 0));
        Place(Companion, Goal + FVector(80, 0, 0));
        CheckAt = Now + 0.7;
        Stage = 13;
        break;
    case 13:
        if (Now < CheckAt) return;
        if (!Verify(Operation.wave == 3 && Runner->LivingEnemies() > 0 && !Runner->IsObjectiveContested() &&
                        Operation.charge == 0 && !Operation.complete, TEXT("extraction_requires_enemies_cleared_before_charging"))) return;
        DamageSource = FirstEnemy();
        if (!IsValid(DamageSource)) { Complete(false, TEXT("Missing hostile source for fixture damage")); return; }
        ClearEnemies();
        Place(Player, PlayerPark);
        CheckAt = Now + 0.7;
        Stage = 14;
        break;
    case 14:
        if (Now < CheckAt) return;
        if (!Verify(Runner->LivingEnemies() == 0 && Companion->Health->IsAlive() && Operation.charge == 0 &&
                        Trial.phase == TrialPhase::Active, TEXT("living_companion_cannot_extract_without_player"))) return;
        // Health accepts a faction-bearing source independently of its life state.
        // This intentionally authored intervention is not an attack by a dead AI.
        Companion->Health->ApplyDamage(Companion->Health->Maximum + 1, DamageSource);
        if (!Verify(!Companion->Health->IsAlive() && IsValid(Companion), TEXT("authored_damage_creates_real_companion_death"))) return;
        CheckAt = Now + 0.7;
        Stage = 15;
        break;
    case 15:
        if (Now < CheckAt) return;
        if (!Verify(Runner->LivingEnemies() == 0 && Operation.charge == 0 && Trial.phase == TrialPhase::Active &&
                        FVector::DistSquared2D(Companion->GetActorLocation(), Goal) < FMath::Square(260.f),
                    TEXT("dead_companion_inside_cleared_ring_cannot_charge"))) return;
        Place(Player, Goal + FVector(-80, 0, 0));
        SampleAt = Now;
        CheckAt = Now + 1;
        Stage = 16;
        break;
    case 16:
        if (Now < CheckAt) return;
        if (!Verify(FMath::Abs(Operation.charge - (Now - SampleAt)) < 0.18 && !Operation.complete,
                    TEXT("dead_companion_adds_no_capture_bonus"))) return;
        Stage = 17;
        break;
    case 17:
        if (Trial.phase != TrialPhase::Won) return;
        if (!Verify(Operation.complete && Operation.relay == 1 && Operation.charge == 3 && Player->Health->IsAlive() &&
                        !Player->bCombatEnabled && !Runner->IsUpgradePending(), TEXT("living_player_finishes_extraction_and_wins"))) return;
        CheckAt = Now + 0.7;
        Stage = 18;
        break;
    case 18:
        if (Now < CheckAt) return;
        if (!Verify(Trial.phase == TrialPhase::Won && Trial.wave == 3 && Operation.charge == 3 &&
                        Operation.relay == 1 && !Runner->ChooseUpgrade(3), TEXT("completed_operation_stays_terminal"))) return;
        {
            AAegisPlayerCharacter* Previous = Player;
            AAegisAICharacter* PreviousCompanion = Companion;
            Runner->StartInteractive();
            StopAI();
            Player = Cast<AAegisPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
            Companion = Runner->GetCompanion();
            if (!Verify(IsValid(Player) && IsValid(Companion) && Player != Previous && Companion != PreviousCompanion &&
                            !IsValid(Previous) && !IsValid(PreviousCompanion) && Player->Health->IsAlive() && Companion->Health->IsAlive() &&
                            Trial.phase == TrialPhase::Briefing && Operation.wave == 0 && Operation.relay == 0 &&
                            Operation.charge == 0 && !Operation.complete, TEXT("restart_replaces_pawns_and_resets_objective"))) return;
            if (!Verify(!Runner->HasUpgrade(1) && !Runner->HasUpgrade(2) && !Runner->HasUpgrade(3) &&
                            !Player->Combat->bPiercingRounds && !Player->bRestorativePulse && Runner->LastUpgrade.IsEmpty() &&
                            !Runner->IsUpgradePending() && !Runner->IsObjectiveContested() && !UGameplayStatics::IsGamePaused(GetWorld()),
                        TEXT("restart_clears_upgrades_contest_and_pause"))) return;
        }
        Runner->DeployTrial(false);
        StopAI();
        if (!Verify(Trial.wave == 1 && Trial.phase == TrialPhase::Active && Operation.wave == 1 &&
                        Operation.relay == 0 && Operation.charge == 0 && !Operation.complete,
                    TEXT("redeploy_begins_fresh_opening_objective"))) return;
        Complete(true, TEXT("Operation integration verified with AI stopped, authored damage, and placed occupants"));
        break;
    default:
        Complete(false, TEXT("Unknown operation fixture stage"));
        break;
    }
}

void AAegisOperationFunctionalTest::Complete(bool Pass, const FString& Message)
{
    if (!bFixtureRunning) return;
    bFixtureRunning = false;
    SetActorTickEnabled(false);
    if (GetWorld())
        if (auto* PC = GetWorld()->GetFirstPlayerController()) PC->SetPause(false);
    if (Pass)
    {
        UE_LOG(LogTemp, Display, TEXT("AEGIS_OPERATION_FUNCTIONAL_PASS | Assertions=%d | WorldType=%d | BegunPlay=%d | FixtureDamage=1 | AIStopped=1 | PlacedOccupants=1"),
               Assertions, static_cast<int32>(GetWorld()->WorldType), GetWorld()->HasBegunPlay() ? 1 : 0);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("AEGIS_OPERATION_FUNCTIONAL_FAIL | Stage=%d | %s"), Stage, *Message);
    }
    FinishTest(Pass ? EFunctionalTestResult::Succeeded : EFunctionalTestResult::Failed, Message);
}

void AAegisOperationFunctionalTest::EndPlay(const EEndPlayReason::Type Reason)
{
    bFixtureRunning = false;
    Super::EndPlay(Reason);
}
