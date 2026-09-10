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
#include "NavigationData.h"
#include "TimerManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformMemory.h"
#include "RenderTimer.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Guid.h"
#include "UnrealClient.h"

bool FAegisScenarioDefinition::IsValid() const
{
    return !Arena.IsEmpty() && PlayerConfig == TEXT("scripted") &&
           (CompanionPolicy == TEXT("utility") || CompanionPolicy == TEXT("priority")) &&
           EnemyPolicy == TEXT("behavior_tree") && EnemyCount >= 1 && EnemyCount <= 50 &&
           FMath::IsFinite(Duration) && Duration >= 1 && Duration <= 300;
}
AAegisScenarioRunner::AAegisScenarioRunner()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
}
void AAegisScenarioRunner::BeginPlay()
{
    Super::BeginPlay();
    StartupDeadline = FPlatformTime::Seconds() + 30;
    // Delay until the map's dynamic navigation is ready, with a bounded failure path.
    GetWorldTimerManager().SetTimer(StartupTimer, this, &AAegisScenarioRunner::Startup, 0.5f, true);
}
void AAegisScenarioRunner::Startup()
{
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    // The commandlet-authored arena deliberately stores no baked navigation tiles.
    // UE 5.8 auto-rebuilds newly spawned nav data, but a saved empty Recast actor
    // is not newly spawned. Request an asynchronous rebuild before timing starts;
    // do not block the game thread with the editor-oriented synchronous Build().
    if (Nav && !bNavigationRebuildRequested)
        if (auto* Data = Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
        {
            Data->RebuildAll();
            bNavigationRebuildRequested = true;
        }
    FNavLocation Point;
    if (!Nav || !Nav->ProjectPointToNavigation(GetActorLocation(), Point))
    {
        if (FPlatformTime::Seconds() < StartupDeadline)
            return;
        GetWorldTimerManager().ClearTimer(StartupTimer);
        Status = TEXT("FAILED: navigation did not become ready within 30 seconds");
        UE_LOG(LogTemp, Error, TEXT("%s"), *Status);
        if (FParse::Param(FCommandLine::Get(), TEXT("AegisQuit")))
            FPlatformMisc::RequestExitWithStatus(false, 2, TEXT("Aegis navigation preflight"));
        return;
    }
    GetWorldTimerManager().ClearTimer(StartupTimer);
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("AegisBatch")))
    {
        FParse::Value(FCommandLine::Get(), TEXT("AegisPolicy="), Definition.CompanionPolicy);
        FParse::Value(FCommandLine::Get(), TEXT("AegisEnemies="), Definition.EnemyCount);
        FParse::Value(FCommandLine::Get(), TEXT("AegisEpisodes="), EpisodeCount);
        FParse::Value(FCommandLine::Get(), TEXT("AegisSeed="), Definition.Seed);
        FParse::Value(FCommandLine::Get(), TEXT("AegisDuration="), Definition.Duration);
        Definition.PerformanceMode = FParse::Param(FCommandLine::Get(), TEXT("AegisPerformance"));
        Definition.DirectorEnabled = FParse::Param(FCommandLine::Get(), TEXT("AegisDirector"));
        bQuitWhenDone = FParse::Param(FCommandLine::Get(), TEXT("AegisQuit"));
        RunBatch();
        if (!Status.StartsWith(TEXT("Running")))
        {
            UE_LOG(LogTemp, Error, TEXT("Aegis batch preflight failed: %s"), *Status);
            if (bQuitWhenDone)
                FPlatformMisc::RequestExitWithStatus(false, 2, TEXT("Aegis batch preflight"));
        }
        return;
    }
#endif
    StartInteractive();
}
void AAegisScenarioRunner::EndPlay(EEndPlayReason::Type Reason)
{
    CancelBatch();
    GetWorldTimerManager().ClearTimer(StartupTimer);
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
        EpisodeCount < 1 || EpisodeCount > 100 || Definition.Seed > MAX_int32 - EpisodeCount)
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
    bInteractive = false;
    // The user's pawn must not contaminate scripted benchmark episodes.
    if (auto* PC = GetWorld()->GetFirstPlayerController())
    {
        PC->bAutoManageActiveCameraTarget = false;
        if (APawn* Pawn = PC->GetPawn())
        {
            PC->UnPossess();
            Pawn->Destroy();
        }
        for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
        {
            // PlayerCameraManager can spawn its own animation CameraActor.
            // Actor iteration order must not choose the evaluation viewpoint.
            if (!It->ActorHasTag(TEXT("AegisEvaluationCamera")))
                continue;
            PC->SetViewTarget(*It);
            UE_LOG(LogTemp, Display, TEXT("AEGIS_VIEW_TARGET %s actor=%s component=%s"),
                   *It->GetName(), *It->GetActorRotation().ToString(),
                   *It->GetCameraComponent()->GetComponentRotation().ToString());
            break;
        }
    }
    Results.Reset();
    Episode = 0;
    StartEpisode();
#endif
}
void AAegisScenarioRunner::CancelBatch()
{
    if (GetWorld())
    {
        GetWorldTimerManager().ClearTimer(SampleTimer);
        GetWorldTimerManager().ClearTimer(StartupTimer);
    }
    SetActorTickEnabled(false);
    DestroyEpisode();
    Status = TEXT("Stopped");
}
void AAegisScenarioRunner::DestroyEpisode()
{
    if (IsValid(Director))
        Director->Destroy();
    Director = nullptr;
    for (AAegisAICharacter* Bot : OwnedBots)
        if (IsValid(Bot))
        {
            AController* Controller = Bot->GetController();
            Bot->Destroy();
            if (IsValid(Controller))
                Controller->Destroy();
        }
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
    EncounterRandom.Initialize(Current.Scenario.Seed);
    FRandomStream Random(Current.Scenario.Seed);
    const FVector Origin = GetActorLocation();
    for (int32 I = 0; I < Definition.EnemyCount + 2; ++I)
    {
        const FVector Offset =
            I == 0
                ? FVector(-1200, 0, 100)
                : (I == 1 ? FVector(-1400, 200, 100)
                          : FVector(1100 + Random.FRandRange(0, 500), Random.FRandRange(-1000, 1000), 100));
        auto* Bot = SpawnConfiguredBot(Origin + Offset, I < 2 ? EAegisTeam::Player : EAegisTeam::Enemy,
                                       I == 1, I == Definition.EnemyCount + 1 && Definition.EnemyCount >= 4,
                                       Current.Scenario.Seed ^ (I * 7919));
        if (!Bot)
        {
            Status = TEXT("Spawn failed; batch aborted");
            CancelBatch();
            return;
        }
        Bot->Health->OnDamaged.AddDynamic(this, &AAegisScenarioRunner::RecordDamage);
        OwnedBots.Add(Bot);
        LastPositions.Add(Bot->GetActorLocation());
        StuckSeconds.Add(0);
    }
    StartedAt = GetWorld()->GetTimeSeconds();
#if !UE_BUILD_SHIPPING
    CaptureDirectory.Reset();
    if (Episode == 0 && FParse::Param(FCommandLine::Get(), TEXT("AegisCapture")) &&
        !Definition.PerformanceMode && !FParse::Param(FCommandLine::Get(), TEXT("NullRHI")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("UseFixedTimeStep")))
    {
        CaptureDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AegisCaptures"),
                                           FGuid::NewGuid().ToString(EGuidFormats::Digits));
        IFileManager::Get().MakeDirectory(*CaptureDirectory, true);
        NextCaptureAt = 2;
        CaptureIndex = 0;
        UE_LOG(LogTemp, Display, TEXT("AEGIS_CAPTURE %s"), *CaptureDirectory);
    }
#endif
    Current.EnemiesSpawned = Definition.EnemyCount;
    Current.RenderingEnabled = !FParse::Param(FCommandLine::Get(), TEXT("NullRHI"));
    FrameMilliseconds.Reset();
    LastFrameAt = 0;
    GameThreadTotalMilliseconds = 0;
    SetActorTickEnabled(Definition.PerformanceMode);
    if (Definition.DirectorEnabled)
    {
        const FTransform Transform;
        Director = GetWorld()->SpawnActorDeferred<AAegisEncounterDirector>(
            AAegisEncounterDirector::StaticClass(), Transform);
        Director->Player = OwnedBots[0];
        Director->FinishSpawning(Transform);
        NextDirectorSpawnAt = StartedAt + 5;
    }
    Status = FString::Printf(TEXT("Running %d / %d"), Episode + 1, EpisodeCount);
    GetWorldTimerManager().SetTimer(SampleTimer, this, &AAegisScenarioRunner::Sample, 0.1f, true);
}
AAegisAICharacter* AAegisScenarioRunner::SpawnConfiguredBot(const FVector& Location, EAegisTeam Team,
                                                            bool Companion, bool Elite, int32 Seed)
{
    const FTransform Transform(FRotator(0, Team == EAegisTeam::Enemy ? 180 : 0, 0), Location);
    auto* Bot = GetWorld()->SpawnActorDeferred<AAegisAICharacter>(
        AAegisAICharacter::StaticClass(), Transform, this, nullptr,
        ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
    if (!Bot)
        return nullptr;
    Bot->Team = Team;
    Bot->bCompanion = Companion;
    Bot->bElite = Elite;
    Bot->bUtilityCompanionPolicy = Definition.CompanionPolicy == TEXT("utility");
    Bot->DecisionSeed = Seed;
    Bot->Behavior = Behavior;
    Bot->CoverQuery = CoverQuery;
    Bot->AttackQuery = AttackQuery;
    Bot->RetreatQuery = RetreatQuery;
    Bot->FinishSpawning(Transform);
    if (Definition.PerformanceMode && !bInteractive)
    {
        // Sustained workload only: damage disabled, never reported as policy evaluation.
        Bot->Combat->RangedDamage = 0;
        Bot->Combat->MeleeDamage = 0;
    }
    return Bot;
}
void AAegisScenarioRunner::StartInteractive()
{
    if (!Behavior || !CoverQuery || !AttackQuery || !RetreatQuery)
    {
        UE_LOG(LogTemp, Error, TEXT("Aegis interactive encounter has missing AI assets"));
        return;
    }
    CancelBatch();
    bInteractive = true;
    EncounterRandom.Initialize(Definition.Seed);
    if (auto* PC = GetWorld()->GetFirstPlayerController())
    {
        if (!PC->GetPawn())
            GetWorld()->GetAuthGameMode()->RestartPlayer(PC);
        const FVector PlayerLocation =
            PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : FVector(-1200, 0, 100);
        if (auto* Companion = SpawnConfiguredBot(PlayerLocation + FVector(-160, 180, 100), EAegisTeam::Player,
                                                 true, false, Definition.Seed))
            OwnedBots.Add(Companion);
        Director = GetWorld()->SpawnActorDeferred<AAegisEncounterDirector>(
            AAegisEncounterDirector::StaticClass(), FTransform());
        Director->Player = Cast<AAegisCharacter>(PC->GetPawn());
        Director->FinishSpawning(FTransform());
    }
    for (int32 I = 0; I < 4; ++I)
        if (auto* Enemy = SpawnConfiguredBot(FVector(1100, -750 + I * 500, 100), EAegisTeam::Enemy, false,
                                             I == 3, Definition.Seed ^ (I * 7919)))
            OwnedBots.Add(Enemy);
    NextDirectorSpawnAt = GetWorld()->GetTimeSeconds() + 5;
    Status = TEXT("Interactive: WASD / mouse turn / left fire / right melee");
    GetWorldTimerManager().SetTimer(SampleTimer, this, &AAegisScenarioRunner::ApplyDirector, 0.25f, true);
}
void AAegisScenarioRunner::ApplyDirector()
{
    if (!IsValid(Director) || !IsValid(Director->Player) || !Director->Player->Health->IsAlive() ||
        Director->bRecovery || GetWorld()->GetTimeSeconds() < NextDirectorSpawnAt)
        return;
    NextDirectorSpawnAt = GetWorld()->GetTimeSeconds() + 5;
    int32 Alive = 0;
    for (const AAegisAICharacter* Bot : OwnedBots)
        if (IsValid(Bot) && Bot->Team == EAegisTeam::Enemy && Bot->Health->IsAlive())
            ++Alive;
    const int32 PopulationCapacity = FMath::Max(0, 12 - Alive);
    const int32 EncounterCapacity = bInteractive ? FMath::Max(0, 50 - OwnedBots.Num()) : PopulationCapacity;
    const int32 Count =
        FMath::Clamp(Director->SpawnBudget, 0, FMath::Min(PopulationCapacity, EncounterCapacity));
    // This is a small finite demo encounter; do not accumulate unbounded corpses/controllers.
    if (bInteractive && OwnedBots.Num() >= 50)
        return;
    for (int32 I = 0; I < Count; ++I)
    {
        const FVector Point(1600, EncounterRandom.FRandRange(-1100, 1100), 100);
        const bool Elite = EncounterRandom.FRand() < Director->EliteProbability;
        if (auto* Bot = SpawnConfiguredBot(Point, EAegisTeam::Enemy, false, Elite,
                                           EncounterRandom.RandRange(0, MAX_int32 - 1)))
        {
            OwnedBots.Add(Bot);
            if (!bInteractive)
            {
                LastPositions.Add(Bot->GetActorLocation());
                StuckSeconds.Add(0);
                Bot->Health->OnDamaged.AddDynamic(this, &AAegisScenarioRunner::RecordDamage);
                ++Current.EnemiesSpawned;
            }
        }
    }
}
void AAegisScenarioRunner::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const double Now = FPlatformTime::Seconds();
    if (LastFrameAt > 0 && GetWorld()->GetTimeSeconds() - StartedAt >= 1 && FrameMilliseconds.Num() < 100000)
    {
        FrameMilliseconds.Add((Now - LastFrameAt) * 1000);
        GameThreadTotalMilliseconds += FPlatformTime::ToMilliseconds(GGameThreadTime);
        Current.PeakResidentMiB =
            FMath::Max(Current.PeakResidentMiB, FPlatformMemory::GetStats().UsedPhysical / 1048576.0);
    }
    LastFrameAt = Now;
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
    if (Definition.DirectorEnabled)
        ApplyDirector();
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
#if !UE_BUILD_SHIPPING
    // Deliberately separate media capture from timing measurements. Requests alone are not
    // evidence: the delivery workflow also checks actual PNG files and visually inspects them.
    if (!CaptureDirectory.IsEmpty() && CaptureIndex < 20 && Current.SurvivalSeconds >= NextCaptureAt)
    {
        if (CaptureIndex == 0)
            if (auto* PC = GetWorld()->GetFirstPlayerController())
            {
                FVector ViewLocation;
                FRotator ViewRotation;
                PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
                UE_LOG(LogTemp, Display, TEXT("AEGIS_CAPTURE_VIEW %s location=%s rotation=%s"),
                       *GetNameSafe(PC->GetViewTarget()), *ViewLocation.ToString(), *ViewRotation.ToString());
            }
        const FString Name = FString::Printf(TEXT("frame-%03d.png"), CaptureIndex++);
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(CaptureDirectory, Name), true, false);
        const FString Row = FString::Printf(TEXT("%s,%.3f\n"), *Name, Current.SurvivalSeconds);
        FFileHelper::SaveStringToFile(Row, *FPaths::Combine(CaptureDirectory, TEXT("frames.csv")),
                                    FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
                                    &IFileManager::Get(), FILEWRITE_Append);
        NextCaptureAt += 0.5;
    }
#endif
    Current.EnemyDeaths = 0;
    Current.CompanionDeaths = OwnedBots[1]->Health->IsAlive() ? 0 : 1;
    Current.DecisionCounts = 0;
    Current.CoverUsage = 0;
    Current.DecisionCpuMilliseconds = 0;
    Current.EqsCpuMilliseconds = 0;
    Current.CompletedQueries = 0;
    Current.FailedQueries = 0;
    Current.ProfiledQueries = 0;
    for (int32 I = 0; I < OwnedBots.Num(); ++I)
    {
        auto* Bot = OwnedBots[I].Get();
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
            Current.DecisionCpuMilliseconds += AI->DecisionCpuSeconds * 1000;
            Current.EqsCpuMilliseconds += AI->QueryCpuSeconds * 1000;
            Current.CompletedQueries += AI->CompletedQueries;
            Current.FailedQueries += AI->FailedQueries;
            Current.ProfiledQueries += AI->ProfiledQueries;
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
    if (!OwnedBots[0]->Health->IsAlive() ||
        (!Definition.DirectorEnabled && Current.EnemyDeaths == Current.EnemiesSpawned) ||
        Current.SurvivalSeconds >= Definition.Duration)
    {
        Current.Win = !Definition.PerformanceMode && OwnedBots[0]->Health->IsAlive() &&
                      Current.EnemyDeaths == Current.EnemiesSpawned;
        FinishEpisode();
    }
}
void AAegisScenarioRunner::FinishEpisode()
{
    GetWorldTimerManager().ClearTimer(SampleTimer);
    SetActorTickEnabled(false);
    Current.FrameSamples = FrameMilliseconds.Num();
    if (!FrameMilliseconds.IsEmpty())
    {
        double Total = 0;
        for (double Milliseconds : FrameMilliseconds)
            Total += Milliseconds;
        FrameMilliseconds.Sort();
        Current.FrameMeanMilliseconds = Total / FrameMilliseconds.Num();
        Current.FrameP95Milliseconds = FrameMilliseconds[FMath::Clamp(
            FMath::CeilToInt(FrameMilliseconds.Num() * 0.95) - 1, 0, FrameMilliseconds.Num() - 1)];
        Current.GameThreadMeanMilliseconds = GameThreadTotalMilliseconds / FrameMilliseconds.Num();
    }
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
    const FString Directory =
        FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AegisReports"),
                        FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") +
                            FGuid::NewGuid().ToString(EGuidFormats::Digits));
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
    UE_LOG(LogTemp, Display, TEXT("AEGIS_REPORT %s"), *Status);
    if (bQuitWhenDone)
        FPlatformMisc::RequestExitWithStatus(false, Success ? 0 : 3, TEXT("Aegis batch complete"));
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
        if (!It->QueryDiagnostic.IsEmpty())
        {
            DrawText(It->QueryDiagnostic, FColor::Green, 25, Y);
            Y += 20;
        }
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
