#include "AegisCombatFunctionalTest.h"
#include "AegisCharacter.h"
#include "Engine/World.h"
#include "TimerManager.h"
void AAegisCombatFunctionalTest::StartTest()
{
    Super::StartTest();
    Cleanup();
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
        Pass &= Shooter->Combat->FireAt(Target->GetActorLocation() + FVector(0, 0, 30));
        Pass &= FMath::IsNearlyEqual(Target->Health->Current, Before - 14);
        Pass &= !Shooter->Combat->FireAt(Target->GetActorLocation());
        Target->Team = EAegisTeam::Player;
        Pass &= Target->Health->ApplyDamage(20, Shooter) == 0;
        Target->Team = EAegisTeam::Enemy;
        Target->Health->ApplyDamage(1000, Shooter);
        Pass &= !Target->Health->IsAlive() && Target->Health->Heal(10) == 0;
    }
    FinishTest(Pass ? EFunctionalTestResult::Succeeded : EFunctionalTestResult::Failed,
               Pass ? TEXT("Trace, cooldown, team filtering, death verified in world")
                    : TEXT("Combat world invariant failed"));
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
