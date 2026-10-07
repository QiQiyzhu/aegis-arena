#include "AegisWeaponsFunctionalTest.h"
#include "AegisCharacter.h"
#include "AegisAIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

namespace
{
const FVector WeaponOrigin(0, 0, 150);
bool HealthIs(const AAegisCharacter* Character, float Expected)
{
    return IsValid(Character) && FMath::IsNearlyEqual(Character->Health->Current, Expected, 0.01f);
}
}
void AAegisWeaponsFunctionalTest::StartTest()
{
    Super::StartTest();
    CleanupSubjects();
    GetWorldTimerManager().ClearTimer(Timer);
    Stage = Assertions = 0;
    bPassed = true;
    if (!Verify(GetWorld()->IsGameWorld() && GetWorld()->HasBegunPlay(), TEXT("Weapons fixture runs in begun play game world")))
    {
        Complete();
        return;
    }
    int32 Fixtures = 0;
    for (TActorIterator<AAegisWeaponsFunctionalTest> It(GetWorld()); It; ++It) ++Fixtures;
    if (!Verify(Fixtures == 1 && FParse::Param(FCommandLine::Get(), TEXT("AegisInputProbe")) &&
                FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")),
                TEXT("Weapons fixture has isolated offscreen input context")))
    {
        Complete();
        return;
    }
    PC = GetWorld()->GetFirstPlayerController();
    if (!Verify(PC && PC->IsLocalController(), TEXT("Weapons fixture owns a local player controller")) || !FreshPlayer())
    {
        Complete();
        return;
    }
    Verify(!Player->bPulseEnabled && !Player->bRestorativePulse && !Player->Combat->bPiercingRounds &&
               FMath::IsNearlyEqual(Player->Combat->RangedDamage, 14.f) &&
               FMath::IsNearlyEqual(Player->Combat->RangedCooldown, 0.7f) &&
               FMath::IsNearlyEqual(Player->Combat->RangedRange, 1100.f) &&
               FMath::IsNearlyEqual(Player->Combat->MeleeDamage, 22.f),
           TEXT("Fresh player preserves baseline combat and disabled upgrades"));
    Verify(!Player->TryPulse() && Player->PulseActivations == 0, TEXT("Pulse requires explicit trial opt in"));
    Player->bPulseEnabled = true;
    VisibleEnemy = SpawnBot(WeaponOrigin + FVector(220, 0, 0), false);
    ShieldedEnemy = SpawnTarget(WeaponOrigin + FVector(0, 330, 0));
    FarEnemy = SpawnTarget(WeaponOrigin + FVector(700, 0, 0));
    Ally = SpawnBot(WeaponOrigin + FVector(0, -180, 0), true);
    CoveredAlly = SpawnBot(WeaponOrigin + FVector(0, -360, 0), true);
    DeadAlly = SpawnBot(WeaponOrigin + FVector(-220, 0, 0), true);
    SpawnWall(WeaponOrigin + FVector(0, 170, 0), FVector(2, 0.25, 4));
    SpawnWall(WeaponOrigin + FVector(0, -280, 0), FVector(2, 0.25, 4));
    if (!VisibleEnemy || !ShieldedEnemy || !FarEnemy || !Ally || !CoveredAlly || !DeadAlly)
    {
        bPassed = false;
        Complete();
        return;
    }
    Player->Health->ApplyDamage(40, VisibleEnemy);
    Ally->Health->ApplyDamage(50, VisibleEnemy);
    CoveredAlly->Health->ApplyDamage(50, VisibleEnemy);
    DeadAlly->Health->ApplyDamage(1000, VisibleEnemy);
    // Explicitly seeded controller/visual state isolates pulse-to-cancel integration.
    // The separate tactical fixture tests windup creation from real perception.
    if (auto* AI = Cast<AAegisAIController>(VisibleEnemy->GetController())) AI->bWindingUpShot = true;
    VisibleEnemy->ShowShotWindup(WeaponOrigin + FVector(0, 0, 30), 1.f);
    UE_LOG(LogTemp, Display, TEXT("AEGIS_WEAPONS_FIXTURE | WorldType=%d | BegunPlay=1 | FixtureDamage=1 | FrozenMovement=1 | SeededWindup=1"),
           static_cast<int32>(GetWorld()->WorldType));
    Deadline = GetWorld()->GetTimeSeconds() + 12;
    CheckAt = GetWorld()->GetTimeSeconds() + 0.15;
    GetWorldTimerManager().SetTimer(Timer, this, &AAegisWeaponsFunctionalTest::Step, 0.05f, true);
}
bool AAegisWeaponsFunctionalTest::Verify(bool Condition, const TCHAR* Name)
{
    ++Assertions;
    bPassed &= AssertTrue(Condition, Name);
    if (Condition) UE_LOG(LogTemp, Display, TEXT("AEGIS_WEAPONS_ASSERT_PASS %s"), Name);
    return Condition;
}
bool AAegisWeaponsFunctionalTest::FreshPlayer(bool Pulse, bool Restore, bool Pierce)
{
    if (!PC) { bPassed = false; return false; }
    if (APawn* Previous = PC->GetPawn())
    {
        PC->UnPossess();
        Previous->Destroy();
    }
    FActorSpawnParameters Parameters;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Player = GetWorld()->SpawnActor<AAegisPlayerCharacter>(WeaponOrigin, FRotator::ZeroRotator, Parameters);
    if (!Player) { bPassed = false; return false; }
    Subjects.Add(Player);
    PC->Possess(Player);
    Player->SetActorTickEnabled(false);
    Player->GetCharacterMovement()->SetComponentTickEnabled(false);
    Player->bCombatEnabled = true;
    Player->bPulseEnabled = Pulse;
    Player->bRestorativePulse = Restore;
    Player->Combat->bPiercingRounds = Pierce;
    return true;
}
AAegisCharacter* AAegisWeaponsFunctionalTest::SpawnTarget(const FVector& Location, bool Friendly)
{
    FActorSpawnParameters Parameters;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Target = GetWorld()->SpawnActor<AAegisCharacter>(Location, FRotator::ZeroRotator, Parameters);
    if (Target)
    {
        Subjects.Add(Target);
        Target->Team = Friendly ? EAegisTeam::Player : EAegisTeam::Enemy;
        Target->GetCharacterMovement()->SetComponentTickEnabled(false);
    }
    return Target;
}
AAegisAICharacter* AAegisWeaponsFunctionalTest::SpawnBot(const FVector& Location, bool Companion)
{
    const FTransform Transform(FRotator(0, Companion ? 0 : 180, 0), Location);
    auto* Bot = GetWorld()->SpawnActorDeferred<AAegisAICharacter>(AAegisAICharacter::StaticClass(), Transform,
        nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Bot) return nullptr;
    Bot->bCompanion = Companion;
    Bot->bTacticalTrial = true;
    Bot->Behavior = LoadObject<UBehaviorTree>(nullptr, TEXT("/Game/Aegis/AI/BT_Aegis.BT_Aegis"));
    Bot->FinishSpawning(Transform);
    Subjects.Add(Bot);
    Bot->GetCharacterMovement()->SetComponentTickEnabled(false);
    if (auto* AI = Cast<AAegisAIController>(Bot->GetController())) AI->ShutdownAI();
    return Bot;
}
AStaticMeshActor* AAegisWeaponsFunctionalTest::SpawnWall(const FVector& Location, const FVector& Scale)
{
    auto* Barrier = GetWorld()->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
    if (!Barrier)
    {
        bPassed = false;
        return nullptr;
    }
    Barrier->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
    Barrier->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Barrier->SetActorScale3D(Scale);
    Barrier->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Barrier->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Block);
    Barrier->GetStaticMeshComponent()->SetCanEverAffectNavigation(false);
    Subjects.Add(Barrier);
    return Barrier;
}
void AAegisWeaponsFunctionalTest::ResetLineHealth()
{
    for (AAegisCharacter* Target : {First.Get(), Second.Get(), Third.Get(), BlockingAlly.Get()})
        if (IsValid(Target)) Target->Health->Heal(1000);
}
bool AAegisWeaponsFunctionalTest::Shoot()
{
    return Player && Player->Combat->FireAt(WeaponOrigin + FVector(1100, 0, 30));
}
void AAegisWeaponsFunctionalTest::Next(int32 NewStage, float Delay)
{
    Stage = NewStage;
    CheckAt = GetWorld()->GetTimeSeconds() + Delay;
}
void AAegisWeaponsFunctionalTest::Step()
{
    if (!bPassed || !IsValid(Player) || GetWorld()->GetTimeSeconds() > Deadline)
    {
        bPassed = false;
        Complete();
        return;
    }
    if (GetWorld()->GetTimeSeconds() < CheckAt) return;
    switch (Stage)
    {
    case 0:
        if (!Verify(VisibleEnemy->GetShotWindupRemaining() > 0 &&
                    Cast<AAegisAIController>(VisibleEnemy->GetController()) &&
                    Cast<AAegisAIController>(VisibleEnemy->GetController())->bWindingUpShot,
                    TEXT("Pulse fixture contains a pending seeded shot warning")))
        {
            Complete();
            return;
        }
        Verify(Player->TryPulse() && Player->PulseActivations == 1, TEXT("Enabled pulse activation uses real gameplay entry"));
        Verify(HealthIs(VisibleEnemy, 68) && Player->LastPulseTargetsHit == 1, TEXT("Pulse applies exactly thirty two damage to visible hostile"));
        Verify(HealthIs(ShieldedEnemy, 100), TEXT("World cover blocks pulse damage"));
        Verify(HealthIs(FarEnemy, 100), TEXT("Pulse cannot hit beyond radius"));
        Verify(HealthIs(Ally, 50) && HealthIs(CoveredAlly, 50) && HealthIs(Player, 60), TEXT("Normal pulse neither damages allies nor restores health"));
        Verify(HealthIs(DeadAlly, 0), TEXT("Normal pulse cannot revive dead companion"));
        Verify(VisibleEnemy->IsPulseStaggered() && VisibleEnemy->GetShotWindupRemaining() == 0 &&
                   !Cast<AAegisAIController>(VisibleEnemy->GetController())->bWindingUpShot,
               TEXT("Pulse staggers hostile and immediately cancels controller warning"));
        Verify(!VisibleEnemy->Combat->FireAt(WeaponOrigin + FVector(0, 0, 30)) &&
                   !VisibleEnemy->Combat->Melee() && VisibleEnemy->Combat->RangedShotsFired == 0,
               TEXT("Stagger blocks both ranged and melee combat"));
        Verify(!Player->TryPulse() && Player->PulseActivations == 1 && HealthIs(VisibleEnemy, 68) &&
                   Player->GetPulseCooldownRemaining() > 5.5f,
               TEXT("Pulse cooldown rejects duplicate without additional damage"));
        Next(1, 0.6f);
        break;
    case 1:
        Verify(!VisibleEnemy->IsPulseStaggered() && VisibleEnemy->Combat->FireAt(WeaponOrigin + FVector(0, 0, 30)),
               TEXT("Stagger expires and permits combat again"));
        if (!FreshPlayer(true, true)) { bPassed = false; Complete(); return; }
        Player->Health->ApplyDamage(40, VisibleEnemy);
        Next(2);
        break;
    case 2:
        Verify(Player->TryPulse(), TEXT("Restorative upgrade uses the same pulse activation"));
        Verify(HealthIs(Player, 72), TEXT("Restorative pulse heals self by twelve"));
        Verify(HealthIs(Ally, 70), TEXT("Restorative pulse heals visible companion by twenty"));
        Verify(HealthIs(CoveredAlly, 50), TEXT("World cover blocks companion restoration"));
        Verify(HealthIs(DeadAlly, 0) && !DeadAlly->Health->IsAlive(), TEXT("Restorative pulse cannot revive a dead companion"));
        CleanupSubjects();
        if (!FreshPlayer()) { bPassed = false; Complete(); return; }
        First = SpawnTarget(WeaponOrigin + FVector(220, 0, 0));
        Second = SpawnTarget(WeaponOrigin + FVector(450, 0, 0));
        Third = SpawnTarget(WeaponOrigin + FVector(750, 0, 0));
        if (!First || !Second || !Third) { bPassed = false; Complete(); return; }
        Next(3);
        break;
    case 3:
        Verify(Shoot() && HealthIs(First, 86) && HealthIs(Second, 100) && HealthIs(Third, 100),
               TEXT("Baseline shot stops at first hostile for fourteen damage"));
        if (!FreshPlayer(false, false, true)) { bPassed = false; Complete(); return; }
        ResetLineHealth();
        Next(4);
        break;
    case 4:
        Verify(Shoot() && HealthIs(First, 86) && HealthIs(Second, 86), TEXT("Piercing round damages two distinct hostiles once each"));
        Verify(HealthIs(Third, 100) && Player->Combat->RangedShotsFired == 1, TEXT("Piercing round stops after two hostiles from one attack"));
        if (!FreshPlayer(false, false, true)) { bPassed = false; Complete(); return; }
        ResetLineHealth();
        Wall = SpawnWall(WeaponOrigin + FVector(330, 0, 0), FVector(0.25, 2, 4));
        Next(5);
        break;
    case 5:
        Verify(Shoot() && HealthIs(First, 86) && HealthIs(Second, 100) && HealthIs(Third, 100),
               TEXT("Cover after first hostile blocks piercing continuation"));
        if (Wall) Wall->Destroy();
        Wall = nullptr;
        if (!FreshPlayer(false, false, true)) { bPassed = false; Complete(); return; }
        ResetLineHealth();
        BlockingAlly = SpawnTarget(WeaponOrigin + FVector(330, 0, 0), true);
        if (!BlockingAlly) { bPassed = false; Complete(); return; }
        Next(6);
        break;
    case 6:
        Verify(Shoot() && HealthIs(First, 86) && HealthIs(BlockingAlly, 100) && HealthIs(Second, 100),
               TEXT("Ally after first hostile blocks piercing and receives no damage"));
        if (!FreshPlayer(false, false, true)) { bPassed = false; Complete(); return; }
        ResetLineHealth();
        BlockingAlly->SetActorLocation(WeaponOrigin + FVector(100, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
        Next(7);
        break;
    case 7:
        Verify(Shoot() && HealthIs(BlockingAlly, 100) && HealthIs(First, 100) && HealthIs(Second, 100),
               TEXT("Ally at muzzle blocks entire piercing shot"));
        BlockingAlly->Destroy();
        BlockingAlly = nullptr;
        if (!FreshPlayer(false, false, true)) { bPassed = false; Complete(); return; }
        ResetLineHealth();
        Wall = SpawnWall(WeaponOrigin + FVector(100, 0, 0), FVector(0.25, 2, 4));
        Next(8);
        break;
    case 8:
        Verify(Shoot() && HealthIs(First, 100) && HealthIs(Second, 100) && HealthIs(Third, 100),
               TEXT("Cover at muzzle blocks entire piercing shot"));
        if (Wall) Wall->Destroy();
        Wall = nullptr;
        if (!FreshPlayer(false, false, true)) { bPassed = false; Complete(); return; }
        ResetLineHealth();
        Second->SetActorLocation(WeaponOrigin + FVector(1200, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
        Third->SetActorLocation(WeaponOrigin + FVector(1400, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
        Next(9);
        break;
    case 9:
        Verify(Shoot() && HealthIs(First, 86) && HealthIs(Second, 100) && HealthIs(Third, 100),
               TEXT("Piercing continuation retains original total range"));
        Player->bPulseEnabled = Player->bRestorativePulse = true;
        Player->PulseCooldownSeconds = 4;
        Verify(Player->TryPulse() && Player->GetPulseCooldownDuration() == 4 && Player->GetPulseCooldownRemaining() > 3.8f,
               TEXT("Pulse cooldown tuning controls actual ability and HUD duration"));
        if (!FreshPlayer()) { bPassed = false; Complete(); return; }
        Verify(!Player->bPulseEnabled && !Player->bRestorativePulse && !Player->Combat->bPiercingRounds &&
                   Player->PulseActivations == 0 && Player->Combat->RangedShotsFired == 0 &&
                   Player->GetPulseCooldownRemaining() == 0 && Player->Combat->CooldownRemaining() == 0 &&
                   Player->GetPulseCooldownDuration() == 6,
               TEXT("Fresh pawn clears upgrades activation counters and cooldown tuning"));
        Complete();
        break;
    }
}
void AAegisWeaponsFunctionalTest::CleanupSubjects()
{
    for (AActor* Subject : Subjects)
        if (IsValid(Subject))
        {
            AController* Controller = nullptr;
            if (auto* Pawn = Cast<APawn>(Subject))
            {
                Controller = Pawn->GetController();
                if (Controller) Controller->UnPossess();
            }
            Subject->Destroy();
            if (IsValid(Controller) && Controller != PC) Controller->Destroy();
        }
    Subjects.Reset();
    Player = nullptr;
}
void AAegisWeaponsFunctionalTest::Complete()
{
    GetWorldTimerManager().ClearTimer(Timer);
    if (bPassed)
    {
        UE_LOG(LogTemp, Display, TEXT("AEGIS_WEAPONS_FUNCTIONAL_PASS | Assertions=%d | WorldType=%d | BegunPlay=1 | FixtureDamage=1"),
               Assertions, static_cast<int32>(GetWorld()->WorldType));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("AEGIS_WEAPONS_FUNCTIONAL_FAIL | Stage=%d | Assertions=%d"), Stage, Assertions);
    }
    CleanupSubjects();
    FinishTest(bPassed ? EFunctionalTestResult::Succeeded : EFunctionalTestResult::Failed,
               bPassed ? TEXT("Weapons rules verified with isolated fixed geometry") : TEXT("Weapons rule fixture failed"));
}
void AAegisWeaponsFunctionalTest::EndPlay(const EEndPlayReason::Type Reason)
{
    GetWorldTimerManager().ClearTimer(Timer);
    CleanupSubjects();
    Super::EndPlay(Reason);
}
