#include "AegisTrialFunctionalTest.h"
#include "AegisLab.h"
#include "AegisCharacter.h"
#include "AegisAIController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

void AAegisTrialFunctionalTest::StartTest()
{
    Super::StartTest();
    GetWorldTimerManager().ClearTimer(Timer);
    Stage = Retry = Assertions = 0;
    bAllPassed = true;
    Runner = nullptr;
    Player = nullptr;
    if (!GetWorld()->IsGameWorld() || !GetWorld()->HasBegunPlay())
    {
        Complete(false, TEXT("Trial fixture requires a begun-play game world"));
        return;
    }
    int32 Runners = 0;
    for (TActorIterator<AAegisScenarioRunner> It(GetWorld()); It; ++It)
    {
        Runner = *It;
        ++Runners;
    }
    if (!Verify(Runners == 1, TEXT("Exactly one configured trial runner")))
    {
        Complete(false, TEXT("Trial test map must contain exactly one runner"));
        return;
    }
    Deadline = GetWorld()->GetTimeSeconds() + 20;
    // Preserve the v1.1 five-second wave lifecycle contract. The separate v1.2
    // operation/tactical fixtures exercise objectives and upgrade selection.
    Runner->bObjectiveTrial = false;
    GetWorldTimerManager().SetTimer(Timer, this, &AAegisTrialFunctionalTest::Step, 0.05f, true);
}

bool AAegisTrialFunctionalTest::Verify(bool Condition, const TCHAR* Name)
{
    ++Assertions;
    bAllPassed &= AssertTrue(Condition, Name);
    if (Condition)
        UE_LOG(LogTemp, Display, TEXT("AEGIS_TRIAL_ASSERT_PASS %s"), Name);
    return Condition;
}

bool AAegisTrialFunctionalTest::VerifyPopulation(int32 ExpectedBots)
{
    int32 Bots = 0, Controllers = 0, Players = 0;
    for (TActorIterator<AAegisAICharacter> It(GetWorld()); It; ++It)
        if (IsValid(*It)) ++Bots;
    for (TActorIterator<AAegisAIController> It(GetWorld()); It; ++It)
        if (IsValid(*It)) ++Controllers;
    for (TActorIterator<AAegisPlayerCharacter> It(GetWorld()); It; ++It)
        if (IsValid(*It)) ++Players;
    return Verify(Bots == ExpectedBots && Controllers == ExpectedBots && Players == 1,
                  TEXT("Exact bot controller and possessed-player population"));
}

bool AAegisTrialFunctionalTest::VerifyBriefing()
{
    Player = Cast<AAegisPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (!Verify(IsValid(Player) && IsValid(Runner->GetCompanion()), TEXT("Player and companion exist")))
        return false;
    bool Pass = Verify(Runner->GetTrial().phase == aegis::TrialPhase::Briefing &&
                           Runner->GetTrial().wave == 0 && Runner->LivingEnemies() == 0,
                       TEXT("Briefing contains no enemies or active wave"));
    Pass &= Verify(!Player->bCombatEnabled, TEXT("Briefing blocks player combat input"));
    Pass &= Verify(Player->Health->IsAlive() &&
                       FMath::IsNearlyEqual(Player->Health->Current, Player->Health->Maximum) &&
                       Player->GetCharacterMovement()->MovementMode != MOVE_None &&
                       Player->GetActorEnableCollision() && Player->Combat->CooldownRemaining() == 0 &&
                       Player->GetDashCooldownRemaining() == 0,
                   TEXT("Fresh player health movement collision and cooldowns restored"));
    Pass &= Verify(Runner->TrialDamageDealt == 0 && Runner->TrialDamageTaken == 0,
                   TEXT("Trial damage counters reset"));
    Pass &= VerifyPopulation(1);
    return Pass;
}

void AAegisTrialFunctionalTest::StopAI()
{
    for (TActorIterator<AAegisAIController> It(GetWorld()); It; ++It)
        It->ShutdownAI();
}

AAegisAICharacter* AAegisTrialFunctionalTest::FirstEnemy() const
{
    for (TActorIterator<AAegisAICharacter> It(GetWorld()); It; ++It)
        if (It->Team == EAegisTeam::Enemy && It->Health->IsAlive()) return *It;
    return nullptr;
}

void AAegisTrialFunctionalTest::ClearEnemies()
{
    // Real health/death/delegate path, with intervention disclosed in the test result.
    // This tests wave lifecycle, not weapon accuracy or natural encounter balance.
    for (TActorIterator<AAegisAICharacter> It(GetWorld()); It; ++It)
        if (It->Team == EAegisTeam::Enemy && It->Health->IsAlive())
            It->Health->ApplyDamage(It->Health->Maximum + 1, Player);
}

void AAegisTrialFunctionalTest::Step()
{
    if (!bAllPassed || !IsValid(Runner))
    {
        Complete(false, TEXT("Trial invariant failed or runner was destroyed"));
        return;
    }
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now > Deadline)
    {
        Complete(false, FString::Printf(TEXT("Trial fixture timed out at stage %d"), Stage));
        return;
    }
    using aegis::TrialPhase;
    const auto& Trial = Runner->GetTrial();
    switch (Stage)
    {
    case 0:
        if (!Runner->IsInteractive()) return;
        if (!VerifyBriefing()) return;
        Runner->DeployTrial(false);
        StopAI();
        if (!Verify(Trial.phase == TrialPhase::Active && Trial.wave == 1 &&
                        Runner->LivingEnemies() == 2 && Player->bCombatEnabled,
                    TEXT("Guided deploy creates two enemies and enables combat"))) return;
        VerifyPopulation(3);
        Runner->DeployTrial(true);
        Verify(!Trial.pressure && Runner->LivingEnemies() == 2,
               TEXT("Duplicate deploy cannot change mode or add enemies"));
        if (auto* Enemy = FirstEnemy())
        {
            Player->Health->ApplyDamage(50, Enemy);
            Runner->GetCompanion()->Health->ApplyDamage(60, Enemy);
        }
        else
        {
            Verify(false, TEXT("Hostile actor exists for fixture damage"));
            return;
        }
        Verify(FMath::IsNearlyEqual(Player->Health->Current, 50.f) &&
                   FMath::IsNearlyEqual(Runner->GetCompanion()->Health->Current, 40.f),
               TEXT("Authored hostile damage reaches real allied health components"));
        ClearEnemies();
        Verify(FMath::IsNearlyEqual(Runner->TrialDamageDealt, 200.f) &&
                   FMath::IsNearlyEqual(Runner->TrialDamageTaken, 50.f),
               TEXT("Damage delegates record allied output and player intake once"));
        Stage = 1;
        break;
    case 1:
        if (Trial.phase != TrialPhase::Intermission) return;
        Verify(Trial.wave == 1 && Runner->LivingEnemies() == 0,
               TEXT("First elimination enters recovery without spawning early"));
        Verify(FMath::IsNearlyEqual(Player->Health->Current, 80.f) &&
                   FMath::IsNearlyEqual(Runner->GetCompanion()->Health->Current, 80.f),
               TEXT("Guided recovery heals player thirty and companion forty"));
        RecoveryAt = Trial.transitionAt;
        CheckAt = Now + 1;
        Stage = 2;
        break;
    case 2:
        if (Now < CheckAt) return;
        Verify(Trial.phase == TrialPhase::Intermission && Trial.wave == 1 &&
                   Trial.transitionAt == RecoveryAt && Runner->LivingEnemies() == 0,
               TEXT("Repeated recovery updates neither spawn nor postpone transition"));
        Verify(FMath::IsNearlyEqual(Player->Health->Current, 80.f) &&
                   FMath::IsNearlyEqual(Runner->GetCompanion()->Health->Current, 80.f),
               TEXT("Recovery healing happens exactly once"));
        Stage = 3;
        break;
    case 3:
        if (Trial.phase != TrialPhase::Active || Trial.wave != 2) return;
        StopAI();
        Verify(Now >= RecoveryAt && Runner->LivingEnemies() == 3,
               TEXT("Second wave starts after real five-second recovery with three enemies"));
        VerifyPopulation(4);
        ClearEnemies();
        Stage = 4;
        break;
    case 4:
        if (Trial.phase != TrialPhase::Intermission) return;
        Verify(Trial.wave == 2, TEXT("Second clear enters second recovery"));
        RecoveryAt = Trial.transitionAt;
        Stage = 5;
        break;
    case 5:
        if (Trial.phase != TrialPhase::Active || Trial.wave != 3) return;
        StopAI();
        Verify(Now >= RecoveryAt && Runner->LivingEnemies() == 4,
               TEXT("Final guided wave contains four enemies after recovery"));
        VerifyPopulation(5);
        {
            int32 Elites = 0;
            for (TActorIterator<AAegisAICharacter> It(GetWorld()); It; ++It)
                if (It->Team == EAegisTeam::Enemy && It->bElite && It->Health->IsAlive()) ++Elites;
            Verify(Elites == 1, TEXT("Final wave contains exactly one elite"));
        }
        ClearEnemies();
        Stage = 6;
        break;
    case 6:
        if (Trial.phase != TrialPhase::Won) return;
        Verify(Runner->LivingEnemies() == 0 && !Player->bCombatEnabled && Player->Health->IsAlive(),
               TEXT("Final clear wins and blocks player combat"));
        Runner->DeployTrial(true);
        Verify(Trial.phase == TrialPhase::Won && Runner->LivingEnemies() == 0,
               TEXT("Finished trial cannot redeploy without restart"));
        Runner->StartInteractive();
        if (!VerifyBriefing()) return;
        Runner->DeployTrial(true);
        StopAI();
        Verify(Trial.phase == TrialPhase::Active && Trial.pressure && Runner->LivingEnemies() == 3,
               TEXT("Pressure opening creates three enemies"));
        VerifyPopulation(4);
        if (auto* Enemy = FirstEnemy()) Player->Health->ApplyDamage(Player->Health->Maximum + 1, Enemy);
        Stage = 7;
        break;
    case 7:
        if (Trial.phase != TrialPhase::Lost) return;
        Verify(!Player->Health->IsAlive() && !Player->bCombatEnabled &&
                   Player->GetCharacterMovement()->MovementMode == MOVE_None && !Player->GetActorEnableCollision(),
               TEXT("Player death produces loss and stops combat movement collision"));
        Stage = 8;
        break;
    case 8:
        {
            AAegisPlayerCharacter* Previous = Player;
            Runner->StartInteractive();
            if (!VerifyBriefing()) return;
            Verify(Player != Previous && !IsValid(Previous), TEXT("Retry destroys prior pawn and possesses a new one"));
            StopAI();
            ++Retry;
            if (Retry == 3)
                Complete(bAllPassed, TEXT("Trial transitions and retries verified with fixture-authored damage"));
        }
        break;
    default:
        Complete(false, TEXT("Unknown fixture stage"));
        break;
    }
}

void AAegisTrialFunctionalTest::Complete(bool Pass, const FString& Message)
{
    GetWorldTimerManager().ClearTimer(Timer);
    if (Pass)
        UE_LOG(LogTemp, Display, TEXT("AEGIS_TRIAL_FUNCTIONAL_PASS | Assertions=%d | WorldType=%d | BegunPlay=%d | FixtureDamage=1"),
               Assertions, static_cast<int32>(GetWorld()->WorldType), GetWorld()->HasBegunPlay() ? 1 : 0);
    FinishTest(Pass ? EFunctionalTestResult::Succeeded : EFunctionalTestResult::Failed, Message);
}

void AAegisTrialFunctionalTest::EndPlay(const EEndPlayReason::Type Reason)
{
    GetWorldTimerManager().ClearTimer(Timer);
    Super::EndPlay(Reason);
}
