#include "AegisCombatFunctionalTest.h"
#include "AegisCharacter.h"
#include "AegisAIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BrainComponent.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/Contexts/EnvQueryContext_Querier.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AISense_Sight.h"
#include "Engine/World.h"
#include "TimerManager.h"
void AAegisCombatFunctionalTest::StartTest()
{
    Super::StartTest();
    Cleanup();
    if (!GetWorld()->HasBegunPlay())
    {
        FinishTest(EFunctionalTestResult::Failed, TEXT("Fixture requires a begun-play world"));
        return;
    }
    FActorSpawnParameters Parameters;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Shooter = GetWorld()->SpawnActor<AAegisCharacter>(GetActorLocation() + FVector(0, 0, 150),
                                                      FRotator::ZeroRotator, Parameters);
    Target = GetWorld()->SpawnActor<AAegisCharacter>(GetActorLocation() + FVector(500, 0, 150),
                                                     FRotator::ZeroRotator, Parameters);
    if (!Shooter || !Target)
    {
        FinishTest(EFunctionalTestResult::Failed, TEXT("Character spawn failed"));
        Cleanup();
        return;
    }
    Shooter->Team = EAegisTeam::Player;
    Target->Team = EAegisTeam::Enemy;
    GetWorldTimerManager().SetTimer(Timer, this, &AAegisCombatFunctionalTest::Verify, 0.1f, false);
}
void AAegisCombatFunctionalTest::Verify()
{
    bool Pass = IsValid(Shooter) && IsValid(Target);
    if (Pass)
    {
        const float Before = Target->Health->Current;
        Pass &= AssertTrue(Shooter->Combat->FireAt(Target->GetActorLocation() + FVector(0, 0, 30)), TEXT("Ranged fire accepted"));
        Pass &= AssertTrue(FMath::IsNearlyEqual(Target->Health->Current, Before - 14), TEXT("Real trace applies 14 damage"));
        Pass &= AssertTrue(!Shooter->Combat->FireAt(Target->GetActorLocation()), TEXT("Cooldown rejects duplicate fire"));
        Target->Team = EAegisTeam::Player;
        Pass &= AssertTrue(Target->Health->ApplyDamage(20, Shooter) == 0, TEXT("Team filter rejects friendly damage"));
        Target->Team = EAegisTeam::Enemy;
        Target->Health->ApplyDamage(1000, Shooter);
        Pass &= AssertTrue(!Target->Health->IsAlive() && Target->Health->Heal(10) == 0, TEXT("Dead pawn cannot heal"));

        // Regression: EQS Querier must follow a moving pawn, not the controller
        // spawn point. Use the real world, controller possession and UE context.
        const FVector Spawn = GetActorLocation() + FVector(0, 1000, 150);
        FTransform Transform(FRotator::ZeroRotator, Spawn);
        auto* Bot = GetWorld()->SpawnActorDeferred<AAegisAICharacter>(
            AAegisAICharacter::StaticClass(), Transform, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Bot)
            Pass = false;
        else
        {
            Bot->Behavior = LoadObject<UBehaviorTree>(nullptr, TEXT("/Game/Aegis/AI/BT_Aegis.BT_Aegis"));
            Bot->FinishSpawning(Transform);
            auto* AI = Cast<AAegisAIController>(Bot->GetController());
            Pass &= AssertTrue(AI != nullptr, TEXT("Real default controller possesses pawn"));
            if (AI)
            {
                AI->GetBrainComponent()->StopLogic(TEXT("Functional regression fixture"));
                Bot->SetActorLocation(Spawn + FVector(500, 300, 0), false, nullptr, ETeleportType::TeleportPhysics);
                Pass &= AssertTrue(FVector::Dist(Bot->GetActorLocation(), Spawn) > 500, TEXT("Pawn moved away from spawn"));
                FEnvQueryInstance Instance;
                Instance.Owner = AI;
                Instance.World = GetWorld();
                TArray<FVector> Locations;
                Pass &= AssertTrue(Instance.PrepareContext(UEnvQueryContext_Querier::StaticClass(), Locations), TEXT("EQS Querier context resolves"));
                Pass &= AssertTrue(Locations.Num() == 1 && Locations[0].Equals(Bot->GetActorLocation(), 0.1), TEXT("EQS Querier follows moved pawn"));
                AI->ShutdownAI();
                AI->UnPossess();
                AI->Destroy();
            }
            Bot->Destroy();
        }
    }
    bAssertionsPassed = Pass;
    if (Pass)
        StartMemoryVerification();
    else
        CompleteVerification();
}
void AAegisCombatFunctionalTest::StartMemoryVerification()
{
    // Isolate from arena combat and geometry. Disabled character movement keeps
    // both pawns stationary in open air while the real sight system ticks.
    MemoryOrigin = GetActorLocation() + FVector(0, 0, 5000);
    const FTransform Transform(FRotator::ZeroRotator, MemoryOrigin);
    MemoryBot = GetWorld()->SpawnActorDeferred<AAegisAICharacter>(
        AAegisAICharacter::StaticClass(), Transform, nullptr, nullptr,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (MemoryBot)
    {
        MemoryBot->Behavior = LoadObject<UBehaviorTree>(nullptr, TEXT("/Game/Aegis/AI/BT_Aegis.BT_Aegis"));
        MemoryBot->DecisionSeed = 1001;
        MemoryBot->FinishSpawning(Transform);
        MemoryBot->GetCharacterMovement()->DisableMovement();
        MemoryAI = Cast<AAegisAIController>(MemoryBot->GetController());
    }
    FActorSpawnParameters Parameters;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    MemoryTarget = GetWorld()->SpawnActor<AAegisCharacter>(
        MemoryOrigin + FVector(500, 0, 0), FRotator::ZeroRotator, Parameters);
    MemoryQuery = LoadObject<UEnvQuery>(nullptr, TEXT("/Game/Aegis/AI/EQS_Attack.EQS_Attack"));
    if (!MemoryAI || !MemoryAI->GetBlackboardComponent() || !MemoryTarget || !MemoryQuery)
    {
        bAssertionsPassed = false;
        CompleteVerification();
        return;
    }
    MemoryAI->GetBrainComponent()->StopLogic(TEXT("Perception lifetime fixture"));
    MemoryAI->StopMovement();
    MemoryAI->SetControlRotation(FRotator::ZeroRotator);
    MemoryTarget->Team = EAegisTeam::Player;
    MemoryTarget->GetCharacterMovement()->DisableMovement();
    if (auto* Perception = UAIPerceptionSystem::GetCurrent(GetWorld()))
        Perception->RegisterSourceForSenseClass(UAISense_Sight::StaticClass(), *MemoryTarget);
    UE_LOG(LogTemp, Display, TEXT("AEGIS_MEMORY_FIXTURE | Seconds=%.3f | Seed=1001 | WorldType=%d | BegunPlay=%d"),
           MemoryAI->TargetMemorySeconds, static_cast<int32>(GetWorld()->WorldType),
           GetWorld()->HasBegunPlay() ? 1 : 0);
    MemoryPhase = 0;
    PhaseDeadline = GetWorld()->GetTimeSeconds() + 5;
    GetWorldTimerManager().SetTimer(Timer, this, &AAegisCombatFunctionalTest::VerifyMemory, 0.05f, true);
}
void AAegisCombatFunctionalTest::VerifyMemory()
{
    if (!IsValid(MemoryAI) || !IsValid(MemoryBot) || !IsValid(MemoryTarget))
    {
        bAssertionsPassed = false;
        CompleteVerification();
        return;
    }
    const double Now = GetWorld()->GetTimeSeconds();
    auto* BB = MemoryAI->GetBlackboardComponent();
    TArray<AActor*> Visible;
    MemoryAI->Senses->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Visible);
    const bool bSensed = Visible.Contains(MemoryTarget.Get());
    if (MemoryPhase == 0 || MemoryPhase == 3)
    {
        if (!bSensed)
        {
            if (Now >= PhaseDeadline)
            {
                AssertTrue(false, TEXT("Real sight acquisition completed before timeout"));
                bAssertionsPassed = false;
                CompleteVerification();
            }
            return;
        }
        MemoryAI->RefreshDecision();
        if (MemoryPhase == 0)
        {
            bAssertionsPassed &= AssertTrue(MemoryAI->ObservedTarget.Get() == MemoryTarget.Get() && MemoryAI->bTargetVisible &&
                BB->GetValueAsBool(TEXT("HasLOS")), TEXT("Memory fixture acquires target through real sight"));
            bAssertionsPassed &= AssertTrue(FMath::IsNearlyEqual(MemoryAI->TargetMemorySeconds, 2.5f),
                TEXT("Configured target memory lasts 2.5 seconds"));
            // A real observation authorizes tactical actions here. Without query
            // assets, each task must still fail so the selector can fall back.
            bAssertionsPassed &= AssertTrue(!MemoryAI->ExecuteAction(TEXT("AttackPosition")), TEXT("Missing attack query permits fallback"));
            bAssertionsPassed &= AssertTrue(!MemoryAI->ExecuteAction(TEXT("FindCover")), TEXT("Missing cover query permits fallback"));
            bAssertionsPassed &= AssertTrue(!MemoryAI->ExecuteAction(TEXT("Retreat")), TEXT("Missing retreat query permits fallback"));
            RememberedLocation = MemoryAI->LastKnown;
            LastObservationTime = Now;
            MemoryAI->SetFocus(MemoryTarget);
            MemoryTarget->SetActorLocation(MemoryOrigin + FVector(5000, 0, 0));
            MemoryPhase = 1;
            PhaseDeadline = Now + 5;
        }
        else
        {
            bAssertionsPassed &= AssertTrue(MemoryAI->ObservedTarget.Get() == MemoryTarget.Get() && MemoryAI->bTargetVisible &&
                MemoryAI->HasTargetMemory() && BB->GetValueAsBool(TEXT("HasLOS")) &&
                BB->GetValueAsBool(TEXT("HasMemory")), TEXT("Sight reacquisition restores target and memory"));
            bAssertionsPassed &= AssertTrue(MemoryAI->LastKnown.Equals(MemoryOrigin + FVector(700, 200, 0), 0.1),
                TEXT("Reacquisition records newly perceived location"));
            bAssertionsPassed &= AssertTrue(MemoryAI->RequestTacticalPoint(MemoryQuery) &&
                BB->GetValueAsBool(TEXT("QueryPending")), TEXT("Real EQS request becomes pending"));
            MemoryAI->ShutdownAI();
            QueriesBeforeCancellation = MemoryAI->CompletedQueries;
            bAssertionsPassed &= AssertTrue(!BB->GetValueAsBool(TEXT("QueryPending")) &&
                !BB->GetValueAsBool(TEXT("HasMemory")) && !MemoryAI->bHasTacticalPoint &&
                !BB->IsVectorValueSet(TEXT("TacticalPoint")) && MemoryAI->CancelledQueries == 1,
                TEXT("Shutdown cancels pending EQS and clears blackboard"));
            MemoryPhase = 4;
            PhaseDeadline = Now + 0.3;
        }
    }
    else if (MemoryPhase == 1)
    {
        if (bSensed)
        {
            if (Now >= PhaseDeadline)
            {
                AssertTrue(false, TEXT("Real sight loss completed before timeout"));
                bAssertionsPassed = false;
                CompleteVerification();
            }
            return;
        }
        // Check the perception callback before manually refreshing the service.
        bAssertionsPassed &= AssertTrue(!MemoryAI->ObservedTarget.IsValid() && !MemoryAI->bTargetVisible &&
            !BB->GetValueAsObject(TEXT("TargetActor")) && !BB->GetValueAsBool(TEXT("HasLOS")),
            TEXT("Sight loss clears live target and blackboard"));
        bAssertionsPassed &= AssertTrue(!MemoryAI->GetFocusActor(), TEXT("Sight loss clears actor focus"));
        MemoryAI->RefreshDecision();
        MemoryTarget->SetActorLocation(MemoryOrigin + FVector(6000, 1200, 0));
        MemoryAI->RefreshDecision();
        bAssertionsPassed &= AssertTrue(MemoryAI->LastKnown.Equals(RememberedLocation, 0.1) &&
            BB->GetValueAsVector(TEXT("LastKnownLocation")).Equals(RememberedLocation, 0.1),
            TEXT("Unseen motion cannot update remembered location"));
        bAssertionsPassed &= AssertTrue(!MemoryAI->ExecuteAction(TEXT("Attack")), TEXT("Hidden target cannot be attacked"));
        MemoryPhase = 2;
        PhaseDeadline = LastObservationTime + MemoryAI->TargetMemorySeconds - 0.25;
    }
    else if (MemoryPhase == 2 && Now >= PhaseDeadline)
    {
        bAssertionsPassed &= AssertTrue(MemoryAI->HasTargetMemory() && BB->GetValueAsBool(TEXT("HasMemory")) &&
            MemoryAI->LastKnown.Equals(RememberedLocation, 0.1),
            TEXT("Lost target remains remembered before deadline"));
        MemoryPhase = 5;
        PhaseDeadline = LastObservationTime + MemoryAI->TargetMemorySeconds + 0.15;
    }
    else if (MemoryPhase == 5 && Now >= PhaseDeadline)
    {
        // No service refresh while waiting: expiry must release references and
        // navigation even when the behavior tree is temporarily not observing.
        bAssertionsPassed &= AssertTrue(!MemoryAI->HasTargetMemory() && !BB->GetValueAsBool(TEXT("HasMemory")) &&
            !BB->IsVectorValueSet(TEXT("LastKnownLocation")) && MemoryAI->LastKnown.IsZero() &&
            !MemoryAI->ObservedTarget.IsValid(), TEXT("Memory expiry clears target and blackboard location"));
        bAssertionsPassed &= AssertTrue(!MemoryAI->ExecuteAction(TEXT("Investigate")) &&
            !MemoryAI->ExecuteAction(TEXT("Chase")), TEXT("Expired memory rejects investigation"));
        bAssertionsPassed &= AssertTrue(!MemoryAI->RequestTacticalPoint(MemoryQuery), TEXT("Expired memory rejects tactical query"));
        MemoryTarget->SetActorLocation(MemoryOrigin + FVector(700, 200, 0));
        MemoryAI->SetControlRotation(FRotator::ZeroRotator);
        MemoryPhase = 3;
        PhaseDeadline = Now + 5;
    }
    else if (MemoryPhase == 4 && Now >= PhaseDeadline)
    {
        MemoryAI->RefreshDecision();
        bAssertionsPassed &= AssertTrue(!MemoryAI->HasTargetMemory() && !MemoryAI->ObservedTarget.IsValid() &&
            !BB->GetValueAsBool(TEXT("HasLOS")), TEXT("Shutdown cannot be repopulated by perception"));
        bAssertionsPassed &= AssertTrue(!MemoryAI->bHasTacticalPoint && !BB->GetValueAsBool(TEXT("QueryPending")) &&
            MemoryAI->CompletedQueries == QueriesBeforeCancellation,
            TEXT("Cancelled EQS cannot publish a late tactical point"));
        CompleteVerification();
    }
}
void AAegisCombatFunctionalTest::CompleteVerification()
{
    GetWorldTimerManager().ClearTimer(Timer);
    if (bAssertionsPassed)
        UE_LOG(LogTemp, Display, TEXT("AEGIS_FUNCTIONAL_PASS CombatAndTactical | WorldType=%d | BegunPlay=%d"),
               static_cast<int32>(GetWorld()->WorldType), GetWorld()->HasBegunPlay() ? 1 : 0);
    FinishTest(bAssertionsPassed ? EFunctionalTestResult::Succeeded : EFunctionalTestResult::Failed,
               bAssertionsPassed ? TEXT("Combat, tactical fallback, real perception memory and EQS cancellation verified in world")
                    : TEXT("Combat, tactical or perception world invariant failed"));
    Cleanup();
}
void AAegisCombatFunctionalTest::Cleanup()
{
    GetWorldTimerManager().ClearTimer(Timer);
    if (IsValid(MemoryAI))
    {
        MemoryAI->UnPossess();
        MemoryAI->Destroy();
    }
    if (IsValid(MemoryBot))
        MemoryBot->Destroy();
    if (IsValid(MemoryTarget))
        MemoryTarget->Destroy();
    MemoryAI = nullptr;
    MemoryBot = nullptr;
    MemoryTarget = nullptr;
    MemoryQuery = nullptr;
    if (IsValid(Shooter))
        Shooter->Destroy();
    if (IsValid(Target))
        Target->Destroy();
    Shooter = nullptr;
    Target = nullptr;
}
void AAegisCombatFunctionalTest::EndPlay(EEndPlayReason::Type Reason)
{
    GetWorldTimerManager().ClearTimer(Timer);
    Cleanup();
    Super::EndPlay(Reason);
}
