#include "AegisLab.h"
#include "AegisAIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "EngineUtils.h"
#include "Engine/Canvas.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "GameFramework/CharacterMovementComponent.h"

bool FAegisScenarioDefinition::IsValid() const
{
    return !Arena.IsEmpty() && PlayerConfig == TEXT("scripted") &&
           (CompanionPolicy == TEXT("utility") || CompanionPolicy == TEXT("priority")) &&
           EnemyPolicy == TEXT("behavior_tree") && EnemyCount >= 1 && EnemyCount <= 50 &&
           FMath::IsFinite(Duration) && Duration >= 1 && Duration <= 300;
}
AAegisScenarioRunner::AAegisScenarioRunner()
{
    PrimaryActorTick.bCanEverTick = false;
}
void AAegisScenarioRunner::BeginPlay()
{
    Super::BeginPlay();
}
void AAegisScenarioRunner::EndPlay(EEndPlayReason::Type Reason)
{
    CancelBatch();
    Super::EndPlay(Reason);
}
void AAegisScenarioRunner::RunBatch()
{
#if !UE_BUILD_SHIPPING
    if (!GetWorld() || !GetWorld()->IsGameWorld())
    {
        Status = TEXT("Run requires PIE or standalone Development world");
        return;
    }
    if (!Definition.IsValid() || !Behavior || !CoverQuery || !AttackQuery || !RetreatQuery ||
        EpisodeCount < 1 || EpisodeCount > 100)
    {
        Status = TEXT("Invalid configuration or missing BT / EQS assets");
        return;
    }
    if (!GetWorld()->GetMapName().Contains(Definition.Arena))
    {
        Status = TEXT("Current map does not match scenario Arena");
        return;
    }
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    FNavLocation Point;
    if (!Nav || !Nav->ProjectPointToNavigation(GetActorLocation(), Point))
    {
        Status = TEXT("No built navigation at scenario origin");
        return;
    }
    CancelBatch();
    Results.Reset();
    Episode = 0;
    StartEpisode();
#endif
}
void AAegisScenarioRunner::CancelBatch()
{
    if (GetWorld())
        GetWorldTimerManager().ClearTimer(SampleTimer);
    DestroyEpisode();
    Status = TEXT("Stopped");
}
void AAegisScenarioRunner::DestroyEpisode()
{
    for (AAegisAICharacter* Bot : OwnedBots)
        if (IsValid(Bot))
            Bot->Destroy();
    OwnedBots.Reset();
    LastPositions.Reset();
    StuckSeconds.Reset();
}
void AAegisScenarioRunner::StartEpisode()
{
    DestroyEpisode();
    Current = {};
    Current.Scenario = Definition;
    Current.Scenario.Seed = Definition.Seed + Episode;
    FRandomStream Random(Current.Scenario.Seed);
    const FVector Origin = GetActorLocation();
    for (int32 I = 0; I < Definition.EnemyCount + 2; ++I)
    {
        const FVector Offset =
            I == 0
                ? FVector(-1200, 0, 100)
                : (I == 1 ? FVector(-1400, 200, 100)
                          : FVector(1100 + Random.FRandRange(0, 500), Random.FRandRange(-1000, 1000), 100));
        const FTransform Transform(FRotator::ZeroRotator, Origin + Offset);
        auto* Bot = GetWorld()->SpawnActorDeferred<AAegisAICharacter>(
            AAegisAICharacter::StaticClass(), Transform, this, nullptr,
            ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
        if (!Bot)
        {
            Status = TEXT("Spawn failed; batch aborted");
            CancelBatch();
            return;
        }
        Bot->Team = I < 2 ? EAegisTeam::Player : EAegisTeam::Enemy;
        Bot->bCompanion = I == 1;
        Bot->bElite = I == Definition.EnemyCount + 1 && Definition.EnemyCount >= 4;
        Bot->Behavior = Behavior;
        Bot->CoverQuery = CoverQuery;
        Bot->AttackQuery = AttackQuery;
        Bot->RetreatQuery = RetreatQuery;
        Bot->FinishSpawning(Transform);
        if (auto* AI = Cast<AAegisAIController>(Bot->GetController()))
            AI->Decision->bUtilityPolicy = Definition.CompanionPolicy == TEXT("utility");
        Bot->Health->OnDamaged.AddDynamic(this, &AAegisScenarioRunner::RecordDamage);
        OwnedBots.Add(Bot);
        LastPositions.Add(Bot->GetActorLocation());
        StuckSeconds.Add(0);
    }
    StartedAt = GetWorld()->GetTimeSeconds();
    Status = FString::Printf(TEXT("Running %d / %d"), Episode + 1, EpisodeCount);
    GetWorldTimerManager().SetTimer(SampleTimer, this, &AAegisScenarioRunner::Sample, 0.1f, true);
}
void AAegisScenarioRunner::RecordDamage(float Applied, AActor* Source, AActor* Victim)
{
    if (OwnedBots.IsEmpty())
        return;
    const auto* C = Cast<AAegisCharacter>(Source);
    if (C && C->Team == EAegisTeam::Player)
        Current.DamageDealt += Applied;
    if (Victim == OwnedBots[0])
        Current.DamageTaken += Applied;
    if (Current.TimeToEngage < 0)
        Current.TimeToEngage = GetWorld()->GetTimeSeconds() - StartedAt;
}
void AAegisScenarioRunner::Sample()
{
    if (OwnedBots.Num() < 2)
    {
        CancelBatch();
        return;
    }
    for (const AAegisAICharacter* Bot : OwnedBots)
        if (!IsValid(Bot))
        {
            CancelBatch();
            Status = TEXT("Episode actor destroyed outside runner; no result recorded");
            return;
        }
    Current.SurvivalSeconds =
        FMath::Min(Definition.Duration, static_cast<float>(GetWorld()->GetTimeSeconds() - StartedAt));
    Current.EnemyDeaths = 0;
    Current.CompanionDeaths = OwnedBots[1]->Health->IsAlive() ? 0 : 1;
    Current.DecisionCounts = 0;
    Current.CoverUsage = 0;
    for (int32 I = 0; I < OwnedBots.Num(); ++I)
    {
        auto* Bot = OwnedBots[I];
        if (!IsValid(Bot))
        {
            Status = TEXT("An episode actor was destroyed outside runner");
            CancelBatch();
            return;
        }
        const double Distance = FVector::Dist(Bot->GetActorLocation(), LastPositions[I]);
        Current.DistanceTravelled += Distance;
        LastPositions[I] = Bot->GetActorLocation();
        if (I >= 2 && !Bot->Health->IsAlive())
            ++Current.EnemyDeaths;
        if (auto* AI = Cast<AAegisAIController>(Bot->GetController()))
        {
            Current.DecisionCounts += AI->Decision->DecisionCount;
            Current.CoverUsage += AI->CoverUses;
            if (Bot->Health->IsAlive() && AI->GetMoveStatus() == EPathFollowingStatus::Moving && Distance < 1)
            {
                StuckSeconds[I] += 0.1f;
                if (StuckSeconds[I] >= 2)
                {
                    ++Current.StuckEvents;
                    StuckSeconds[I] = 0;
                }
            }
            else
                StuckSeconds[I] = 0;
        }
    }
    if (!OwnedBots[0]->Health->IsAlive() || Current.EnemyDeaths == Definition.EnemyCount ||
        Current.SurvivalSeconds >= Definition.Duration)
    {
        Current.Win = OwnedBots[0]->Health->IsAlive() && Current.EnemyDeaths == Definition.EnemyCount;
        FinishEpisode();
    }
}
void AAegisScenarioRunner::FinishEpisode()
{
    GetWorldTimerManager().ClearTimer(SampleTimer);
    Results.Add(Current);
    ++Episode;
    if (Episode < EpisodeCount)
        StartEpisode();
    else
    {
        WriteReport();
        DestroyEpisode();
    }
}
void AAegisScenarioRunner::WriteReport()
{
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AegisReports"),
                                              FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")));
    IFileManager::Get().MakeDirectory(*Directory, true);
    FString Csv = TEXT(
        "engine,seed,policy,enemy_count,win,survival_seconds,damage_dealt,damage_taken,companion_deaths,"
        "enemy_deaths,time_to_engage,stuck_events,distance_cm,cover_queries_succeeded,decision_counts\n");
    bool Success = true;
    for (int32 I = 0; I < Results.Num(); ++I)
    {
        const auto& R = Results[I];
        FString Json;
        Success &= FJsonObjectConverter::UStructToJsonObjectString(R, Json);
        Success &= FFileHelper::SaveStringToFile(
            Json, *FPaths::Combine(Directory, FString::Printf(TEXT("episode-%03d.json"), I)));
        Csv += FString::Printf(TEXT("unreal-runtime,%d,%s,%d,%d,%.3f,%.3f,%.3f,%d,%d,%.3f,%d,%.3f,%d,%d\n"),
                               R.Scenario.Seed, *R.Scenario.CompanionPolicy, R.Scenario.EnemyCount,
                               R.Win ? 1 : 0, R.SurvivalSeconds, R.DamageDealt, R.DamageTaken,
                               R.CompanionDeaths, R.EnemyDeaths, R.TimeToEngage, R.StuckEvents,
                               R.DistanceTravelled, R.CoverUsage, R.DecisionCounts);
    }
    Success &= FFileHelper::SaveStringToFile(Csv, *FPaths::Combine(Directory, TEXT("episodes.csv")));
    Status = Success ? TEXT("Complete: ") + Directory : TEXT("Report write failed; see Saved logs");
}
AAegisEncounterDirector::AAegisEncounterDirector()
{
    PrimaryActorTick.bCanEverTick = false;
}
void AAegisEncounterDirector::BeginPlay()
{
    Super::BeginPlay();
    StartedAt = GetWorld()->GetTimeSeconds();
    if (Player)
        Player->Health->OnDamaged.AddDynamic(this, &AAegisEncounterDirector::PlayerDamaged);
    GetWorldTimerManager().SetTimer(Timer, this, &AAegisEncounterDirector::Update, 0.25f, true);
}
void AAegisEncounterDirector::EndPlay(EEndPlayReason::Type Reason)
{
    GetWorldTimerManager().ClearTimer(Timer);
    if (IsValid(Player))
        Player->Health->OnDamaged.RemoveDynamic(this, &AAegisEncounterDirector::PlayerDamaged);
    Super::EndPlay(Reason);
}
void AAegisEncounterDirector::PlayerDamaged(float Amount, AActor* Source, AActor* Victim)
{
    (void)Source;
    (void)Victim;
    RecentDamage += Amount;
}
void AAegisEncounterDirector::Update()
{
    if (!IsValid(Player))
        return;
    int32 Alive = 0, Dead = 0;
    for (TActorIterator<AAegisAICharacter> It(GetWorld()); It; ++It)
        if (It->Team == EAegisTeam::Enemy)
        {
            if (It->Health->IsAlive())
                ++Alive;
            else
                ++Dead;
        }
    const double Elapsed = GetWorld()->GetTimeSeconds() - StartedAt;
    aegis::DirectorInput Input{Player->Health->Current / Player->Health->Maximum, RecentDamage, Elapsed,
                               Dead / FMath::Max(1.0, Elapsed / 10), Alive};
    const auto Out = Model.update(Input, Elapsed, 0.25);
    SpawnBudget = Out.spawnBudget;
    EliteProbability = Out.eliteProbability;
    bRecovery = Out.recovery;
    Pressure = Out.pressure;
    RecoverySeconds = Out.recoverySeconds;
    RecentDamage *= FMath::Exp(-0.25f / 5);
    // These are bounded recommendations. Spawn execution belongs to the encounter owner, not this observer.
}
void AAegisDebugHUD::DrawHUD()
{
    Super::DrawHUD();
#if !UE_BUILD_SHIPPING
    float Y = 30;
    DrawText(TEXT("AEGIS / development diagnostics"), FColor::Cyan, 25, Y);
    Y += 24;
    for (TActorIterator<AAegisAIController> It(GetWorld()); It; ++It)
    {
        auto* Bot = Cast<AAegisCharacter>(It->GetPawn());
        if (!Bot)
            continue;
        const auto& S = It->Decision->Scores;
        DrawText(FString::Printf(TEXT("%s | %s | target %s | LOS %d | HP %.0f | CD %.2f"), *Bot->GetName(),
                                 *It->DebugState, *GetNameSafe(It->ObservedTarget.Get()),
                                 It->bTargetVisible ? 1 : 0, Bot->Health->Current,
                                 Bot->Combat->CooldownRemaining()),
                 FColor::White, 25, Y);
        Y += 18;
        DrawText(FString::Printf(TEXT("  utility F %.2f A %.2f S %.2f R %.2f | EQS %s | nav %d"), S[0], S[1],
                                 S[2], S[3], *It->SelectedPoint.ToCompactString(),
                                 static_cast<int32>(It->GetMoveStatus())),
                 FColor::Silver, 25, Y);
        Y += 22;
        if (Y > Canvas->SizeY - 100)
            break;
    }
    for (TActorIterator<AAegisEncounterDirector> It(GetWorld()); It; ++It)
    {
        DrawText(FString::Printf(TEXT("Director: budget %d | elite %.2f | pressure %.2f | recovery %d"),
                                 It->SpawnBudget, It->EliteProbability, It->Pressure, It->bRecovery),
                 FColor::Yellow, 25, Y);
        Y += 22;
    }
#endif
}
AAegisGameMode::AAegisGameMode()
{
    DefaultPawnClass = AAegisPlayerCharacter::StaticClass();
    HUDClass = AAegisDebugHUD::StaticClass();
}
