#include "AegisCombatFunctionalTest.h"
#include "AegisCharacter.h"
#include "AegisAIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BrainComponent.h"
#include "EnvironmentQuery/Contexts/EnvQueryContext_Querier.h"
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
                // No query assets in this fixture: tactical tasks must fail so
                // the authored BT selector can evaluate its lower-priority path.
                Pass &= AssertTrue(!AI->ExecuteAction(TEXT("AttackPosition")), TEXT("Missing attack query permits fallback"));
                Pass &= AssertTrue(!AI->ExecuteAction(TEXT("FindCover")), TEXT("Missing cover query permits fallback"));
                Pass &= AssertTrue(!AI->ExecuteAction(TEXT("Retreat")), TEXT("Missing retreat query permits fallback"));
                AI->ShutdownAI();
                AI->UnPossess();
                AI->Destroy();
            }
            Bot->Destroy();
        }
    }
    if (Pass)
        UE_LOG(LogTemp, Display, TEXT("AEGIS_FUNCTIONAL_PASS CombatAndTactical | WorldType=%d | BegunPlay=%d"),
               static_cast<int32>(GetWorld()->WorldType), GetWorld()->HasBegunPlay() ? 1 : 0);
    FinishTest(Pass ? EFunctionalTestResult::Succeeded : EFunctionalTestResult::Failed,
               Pass ? TEXT("Combat, moving EQS Querier, missing-query selector fallback verified in world")
                    : TEXT("Combat or tactical world invariant failed"));
    Cleanup();
}
void AAegisCombatFunctionalTest::Cleanup()
{
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
