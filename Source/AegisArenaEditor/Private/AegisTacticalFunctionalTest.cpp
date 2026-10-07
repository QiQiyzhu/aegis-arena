#include "AegisTacticalFunctionalTest.h"
#include "AegisAIController.h"
#include "AegisCharacter.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BrainComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "TimerManager.h"

void AAegisTacticalFunctionalTest::StartTest()
{
    Super::StartTest();
    Cleanup();
    Stage = -3;
    EnemyRoleIndex = Assertions = 0;
    bAllPassed = true;
    if (!GetWorld()->IsGameWorld() || !GetWorld()->HasBegunPlay())
    {
        Complete(false, TEXT("Requires a begun-play game world"));
        return;
    }
    Deadline = GetWorld()->GetTimeSeconds() + 20;
    GetWorldTimerManager().SetTimer(Timer, this, &AAegisTacticalFunctionalTest::Step, 0.05f, true);
}
AAegisAICharacter* AAegisTacticalFunctionalTest::SpawnBot(const FVector& Location, bool Companion,
                                                          EAegisEnemyRole EnemyRole)
{
    const FTransform Transform(FRotator(0, 180, 0), Location);
    auto* Spawned = GetWorld()->SpawnActorDeferred<AAegisAICharacter>(
        AAegisAICharacter::StaticClass(), Transform, nullptr, nullptr,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Spawned)
        return nullptr;
    Spawned->bTacticalTrial = true;
    Spawned->bCompanion = Companion;
    Spawned->EnemyRole = EnemyRole;
    Spawned->DecisionSeed = 3101 + EnemyRoleIndex;
    Spawned->Health->Maximum = 10000;
    Spawned->Combat->RangedDamage = 0;
    Spawned->Behavior = LoadObject<UBehaviorTree>(nullptr, TEXT("/Game/Aegis/AI/BT_Aegis.BT_Aegis"));
    Spawned->FinishSpawning(Transform);
    return Spawned;
}
bool AAegisTacticalFunctionalTest::Check(bool Condition, const FString& Name)
{
    ++Assertions;
    bAllPassed &= AssertTrue(Condition, Name);
    if (Condition)
        UE_LOG(LogTemp, Display, TEXT("AEGIS_TACTICAL_ASSERT_PASS %s"), *Name);
    return Condition;
}
void AAegisTacticalFunctionalTest::StartEnemyRole()
{
    DestroyBot(Bot);
    Bot = nullptr;
    AI = nullptr;
    Leader->SetActorLocation(FVector(0, 0, 100));
    Bot = SpawnBot(FVector(1200, -250, 100), false, static_cast<EAegisEnemyRole>(EnemyRoleIndex));
    AI = Bot ? Cast<AAegisAIController>(Bot->GetController()) : nullptr;
    if (!AI)
    {
        Complete(false, TEXT("Enemy role spawn failed"));
        return;
    }
    // Even a caller passing the player here cannot give an enemy a live link.
    AI->SetTacticalContext(Leader, FVector(350, 350, 100), EnemyRoleIndex);
    AI->Senses->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
    Check(!AI->GetLinkedPlayer(), FString::Printf(TEXT("role_%d_rejects_live_player_link"), EnemyRoleIndex));
    StartedPosition = Bot->GetActorLocation();
    CheckAt = GetWorld()->GetTimeSeconds() + 1.0;
    Deadline = GetWorld()->GetTimeSeconds() + 9;
    Stage = 10;
}
void AAegisTacticalFunctionalTest::Step()
{
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now > Deadline)
    {
        Complete(false, FString::Printf(TEXT("Tactical fixture timed out at stage %d role %d"), Stage, EnemyRoleIndex));
        return;
    }
    if (Stage == -3)
    {
        auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
        UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(GetWorld(),
            FVector(900, 0, 100), FVector(0, 0, 100)) : nullptr;
        if (!Path || !Path->IsValid() || Path->IsPartial()) return;
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Leader = GetWorld()->SpawnActor<AAegisPlayerCharacter>(FVector(0, 0, 100), FRotator::ZeroRotator, Params);
        Nominee = SpawnBot(FVector(-400, 400, 100), true, EAegisEnemyRole::Striker);
        if (!Leader || !Nominee)
        {
            Complete(false, TEXT("Natural spawn-order actors failed"));
            return;
        }
        Leader->Health->Maximum = Leader->Health->Current = 10000;
        Leader->GetCharacterMovement()->DisableMovement();
        if (auto* CompanionAI = Cast<AAegisAIController>(Nominee->GetController()))
        {
            CompanionAI->GetBrainComponent()->StopLogic(TEXT("Stationary spawn-order companion"));
            CompanionAI->StopMovement();
        }
        Nominee->GetCharacterMovement()->DisableMovement();
        // Register the two targets before the enemy's controller is spawned,
        // matching the real arena. Do not toggle senses or repair its listener
        // from the fixture: possession must initialize existing-target queries.
        CheckAt = Now + 0.3;
        Deadline = Now + 10;
        Stage = -2;
    }
    else if (Stage == -2 && Now >= CheckAt)
    {
        Bot = SpawnBot(FVector(900, 0, 100), false, EAegisEnemyRole::Striker);
        AI = Bot ? Cast<AAegisAIController>(Bot->GetController()) : nullptr;
        if (!AI)
        {
            Complete(false, TEXT("Natural spawn-order enemy failed"));
            return;
        }
        AI->SetTacticalContext(nullptr, FVector(-200, 0, 100), 0);
        ShotsBefore = AI->TacticalShots;
        Stage = -1;
    }
    else if (Stage == -1 && AI->bWindingUpShot)
    {
        TArray<AActor*> Sight;
        AI->Senses->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Sight);
        Check(Sight.Contains(Leader.Get()) && Sight.Contains(Nominee.Get()),
              TEXT("natural_spawn_order_sees_existing_player_and_companion_without_sense_toggle"));
        Check(AI->ObservedTarget.Get() == Leader && AI->bTargetVisible && AI->TacticalShots == ShotsBefore,
              TEXT("natural_spawn_order_winds_up_at_player_without_sense_toggle"));
        Stage = -4;
    }
    else if (Stage == -4 && AI->TacticalShots > ShotsBefore)
    {
        Check(AI->ObservedTarget.Get() == Leader && AI->bTargetVisible,
              TEXT("natural_spawn_order_fires_at_player_without_sense_toggle"));
        DestroyBot(Bot);
        DestroyBot(Nominee);
        Leader->Destroy();
        Bot = nullptr;
        Nominee = nullptr;
        Leader = nullptr;
        AI = nullptr;
        Deadline = Now + 20;
        Stage = 0;
    }
    else if (Stage == 0)
    {
        auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
        UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(GetWorld(),
            FVector(-1500, 0, 100), FVector(500, 600, 100)) : nullptr;
        if (!Path || !Path->IsValid() || Path->IsPartial())
            return;
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Leader = GetWorld()->SpawnActor<AAegisCharacter>(FVector(-800, 0, 100), FRotator::ZeroRotator, Params);
        Bot = SpawnBot(FVector(-1500, 0, 100), true, EAegisEnemyRole::Striker);
        AI = Bot ? Cast<AAegisAIController>(Bot->GetController()) : nullptr;
        if (!Leader || !AI)
        {
            Complete(false, TEXT("Tactical actor spawn failed"));
            return;
        }
        Leader->Team = EAegisTeam::Player;
        Leader->GetCharacterMovement()->DisableMovement();
        AI->SetTacticalContext(Leader, FVector::ZeroVector, 0);
        AI->Senses->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
        Check(AI->IsTacticalTrialEnabled(), TEXT("tactical_companion_enabled"));
        StartedPosition = Bot->GetActorLocation();
        CheckAt = Now + 2.5;
        Deadline = Now + 15;
        Stage = 1;
    }
    else if (Stage == 1 && Now >= CheckAt)
    {
        TArray<AActor*> Sight;
        AI->Senses->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Sight);
        Check(AI->GetLinkedPlayer() == Leader && AI->ObservedAlly.Get() == Leader && Sight.IsEmpty(),
              TEXT("explicit_ally_link_survives_no_sight"));
        Check(FVector::Dist2D(Bot->GetActorLocation(), StartedPosition) > 200 && AI->TacticalMoveRequests > 0,
              TEXT("guard_moves_without_visual_player"));
        const FVector Relative = Bot->GetActorLocation() - Leader->GetActorLocation();
        Check(Relative.X < -45 && FMath::Abs(Relative.Y) > 125, TEXT("guard_avoids_player_fire_lane"));
        AI->SetCompanionCommand(EAegisCompanionCommand::Rally, nullptr, Signal);
        CheckAt = Now + 3;
        Stage = 2;
    }
    else if (Stage == 2 && Now >= CheckAt)
    {
        Check(AI->CompanionCommand == EAegisCompanionCommand::Rally &&
              FVector::Dist2D(Bot->GetActorLocation(), Signal) < 160, TEXT("rally_command_moves_to_signal"));
        AI->SetCompanionCommand(EAegisCompanionCommand::Rally, nullptr, FVector(10000, 10000, 100));
        StartedPosition = Bot->GetActorLocation();
        MovesBefore = AI->TacticalMoveRequests;
        CheckAt = Now + 0.8;
        Stage = 3;
    }
    else if (Stage == 3 && Now >= CheckAt)
    {
        Check(FVector::Dist2D(Bot->GetActorLocation(), StartedPosition) < 15 &&
              AI->TacticalMoveRequests == MovesBefore, TEXT("invalid_rally_does_not_teleport_or_accept_partial_path"));
        Nominee = SpawnBot(FVector(1600, -1200, 100), false, EAegisEnemyRole::Flanker);
        if (!Nominee)
        {
            Complete(false, TEXT("Nominee spawn failed"));
            return;
        }
        auto* NomineeAI = Cast<AAegisAIController>(Nominee->GetController());
        NomineeAI->GetBrainComponent()->StopLogic(TEXT("Tactical visibility intervention"));
        NomineeAI->StopMovement();
        Nominee->GetCharacterMovement()->DisableMovement();
        AI->Senses->SetSenseEnabled(UAISense_Sight::StaticClass(), true);
        Signal = FVector(200, 600, 100);
        AI->SetCompanionCommand(EAegisCompanionCommand::Focus, Nominee, Signal);
        ShotsBefore = AI->TacticalShots;
        CheckAt = Now + 0.9;
        Stage = 4;
    }
    else if (Stage == 4 && Now >= CheckAt)
    {
        Check(!AI->ObservedTarget.IsValid() && !AI->bWindingUpShot && AI->TacticalShots == ShotsBefore,
              TEXT("focus_nomination_does_not_grant_sight_or_fire"));
        Check(FVector::Dist2D(AI->TacticalMoveGoal, Signal) < 120,
              TEXT("hidden_focus_uses_command_snapshot"));
        Nominee->SetActorLocation(Bot->GetActorLocation() + FVector(500, 0, 0));
        Bot->SetActorRotation(FRotator::ZeroRotator);
        AI->SetControlRotation(FRotator::ZeroRotator);
        Deadline = Now + 6;
        Stage = 5;
    }
    else if (Stage == 5 && AI->bWindingUpShot)
    {
        Check(AI->ObservedTarget.Get() == Nominee && AI->bTargetVisible,
              TEXT("focus_selects_genuinely_seen_nominee"));
        Check(AI->TacticalShots == ShotsBefore, TEXT("companion_emits_windup_before_fire"));
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        const FVector Mid = (Bot->GetActorLocation() + Nominee->GetActorLocation()) * 0.5;
        Wall = GetWorld()->SpawnActor<AStaticMeshActor>(Mid, FRotator::ZeroRotator, Params);
        Wall->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
        Wall->SetActorScale3D(FVector(1.5, 8, 5));
        Wall->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
        CheckAt = Now + 0.9;
        Stage = 6;
    }
    else if (Stage == 6 && Now >= CheckAt)
    {
        Check(!AI->bWindingUpShot && AI->TacticalShots == ShotsBefore && !AI->bTargetVisible,
              TEXT("occlusion_cancels_windup_without_firing"));
        Wall->Destroy();
        Wall = nullptr;
        DestroyBot(Nominee);
        Nominee = nullptr;
        StartEnemyRole();
    }
    else if (Stage == 10 && Now >= CheckAt)
    {
        Check(!AI->ObservedTarget.IsValid() && FVector::Dist2D(Bot->GetActorLocation(), StartedPosition) > 120,
              FString::Printf(TEXT("role_%d_advances_to_objective_without_target"), EnemyRoleIndex));
        AI->Senses->SetSenseEnabled(UAISense_Sight::StaticClass(), true);
        Bot->SetActorRotation((Leader->GetActorLocation() - Bot->GetActorLocation()).Rotation());
        AI->SetControlRotation(Bot->GetActorRotation());
        Stage = 11;
    }
    else if (Stage == 11 && AI->bWindingUpShot)
    {
        Check(AI->TacticalMoveRequests > 0 && FVector::Dist2D(Bot->GetActorLocation(), StartedPosition) > 120,
              FString::Printf(TEXT("role_%d_moves_before_aiming"), EnemyRoleIndex));
        ShotsBefore = AI->TacticalShots;
        CheckAt = Now + 0.2;
        Stage = 12;
    }
    else if (Stage == 12 && Now >= CheckAt)
    {
        Check(AI->bWindingUpShot && AI->TacticalShots == ShotsBefore,
              FString::Printf(TEXT("role_%d_reaction_warning_precedes_shot"), EnemyRoleIndex));
        Stage = 13;
    }
    else if (Stage == 13 && AI->TacticalShots > ShotsBefore)
    {
        Check(AI->ObservedTarget.Get() == Leader && AI->bTargetVisible,
              FString::Printf(TEXT("role_%d_fires_with_real_los"), EnemyRoleIndex));
        ShotPosition = Bot->GetActorLocation();
        CheckAt = Now + 1.5;
        Stage = 14;
    }
    else if (Stage == 14 && Now >= CheckAt)
    {
        Check(FVector::Dist2D(Bot->GetActorLocation(), ShotPosition) > 60,
              FString::Printf(TEXT("role_%d_repositions_after_shot"), EnemyRoleIndex));
        if (++EnemyRoleIndex < 3)
            StartEnemyRole();
        else
            Complete(bAllPassed, TEXT("Tactical commands, authorized sight and three moving enemy roles verified"));
    }
}
void AAegisTacticalFunctionalTest::DestroyBot(AAegisAICharacter* Actor)
{
    if (!IsValid(Actor))
        return;
    if (auto* Controller = Cast<AAegisAIController>(Actor->GetController()))
    {
        Controller->UnPossess();
        Controller->Destroy();
    }
    Actor->Destroy();
}
void AAegisTacticalFunctionalTest::Cleanup()
{
    GetWorldTimerManager().ClearTimer(Timer);
    DestroyBot(Bot);
    DestroyBot(Nominee);
    if (IsValid(Leader)) Leader->Destroy();
    if (IsValid(Wall)) Wall->Destroy();
    Bot = nullptr;
    Nominee = nullptr;
    AI = nullptr;
    Leader = nullptr;
    Wall = nullptr;
}
void AAegisTacticalFunctionalTest::Complete(bool Pass, const FString& Reason)
{
    if (Pass && Assertions == 32)
        UE_LOG(LogTemp, Display, TEXT("AEGIS_TACTICAL_FUNCTIONAL_PASS | Assertions=32 | WorldType=%d | BegunPlay=%d | DamageDisabled=1"),
               static_cast<int32>(GetWorld()->WorldType), GetWorld()->HasBegunPlay() ? 1 : 0);
    FinishTest(Pass && Assertions == 32 ? EFunctionalTestResult::Succeeded : EFunctionalTestResult::Failed, Reason);
    Cleanup();
}
void AAegisTacticalFunctionalTest::EndPlay(const EEndPlayReason::Type Reason)
{
    Cleanup();
    Super::EndPlay(Reason);
}
