#include "AegisLab.h"
#include "AegisUIPreferences.h"
#include "AegisPortfolioMusic.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "AegisPortfolio.h"
#include "AegisPortfolioPresentation.h"
#include "AegisPortfolioCapture.h"
#include "AegisAVProbe.h"
#include "AegisPortfolioProbe.h"
#include "AegisV2Probe.h"
#include "AegisV23Probe.h"
#include "AegisReleaseProbe.h"
#include "AegisDecisionLab.h"
#include "AegisDecisionLabProbe.h"
#include "AegisAIController.h"
#include "AegisInputProbe.h"
#include "AegisPlayProbe.h"
#include "AegisSquadPlanner.h"
#include "AegisCopilotProbe.h"
#include "AegisCopilotFaultProbe.h"
#include "BehaviorTree/BehaviorTree.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "EngineUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavigationPath.h"
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
#include "GameFramework/PlayerInput.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Guid.h"
#include "UnrealClient.h"
#include "Components/InputComponent.h"
#include "BrainComponent.h"
#include "InputCoreTypes.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Misc/Paths.h"

bool FAegisScenarioDefinition::IsValid() const
{
    return !Arena.IsEmpty() && PlayerConfig == TEXT("scripted") &&
           (CompanionPolicy == TEXT("utility") || CompanionPolicy == TEXT("priority")) &&
           EnemyPolicy == TEXT("behavior_tree") && EnemyCount >= 1 && EnemyCount <= 50 &&
           FMath::IsFinite(Duration) && Duration >= 1 && Duration <= 300;
}
namespace
{
bool HasEncounterNavigation(AActor* Context)
{
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Context->GetWorld());
    if (!Nav) return false;
    FNavLocation Allied, Enemy;
    const FVector Origin = Context->GetActorLocation();
    // Validate the playable spawn lanes and their connectivity. A helper actor's
    // floor-level origin is not a navigation contract for the actual encounter.
    if (!Nav->ProjectPointToNavigation(Origin + FVector(-1200, 0, 100), Allied) ||
        !Nav->ProjectPointToNavigation(Origin + FVector(1450, 800, 100), Enemy)) return false;
    auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(Context, Allied.Location, Enemy.Location);
    return Path && Path->IsValid() && !Path->IsPartial();
}
}
AAegisScenarioRunner::AAegisScenarioRunner()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("ArenaOrigin")));
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
}
void AAegisScenarioRunner::BeginPlay()
{
    Super::BeginPlay();
    BuildArenaPresentation();
    if (AegisPortfolioPresentation::Enabled()) AegisPortfolioPresentation::BuildArena(this);
    StartupDeadline = FPlatformTime::Seconds() + 30;
    // Delay until the map's dynamic navigation is ready, with a bounded failure path.
    GetWorldTimerManager().SetTimer(StartupTimer, this, &AAegisScenarioRunner::Startup, 0.5f, true);
}
void AAegisScenarioRunner::BuildArenaPresentation()
{
    if (!FApp::CanEverRender() || FParse::Param(FCommandLine::Get(), TEXT("NullRHI"))) return;
    auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Aegis/Materials/M_AegisActor.M_AegisActor"));
    if (!Cube || !Material) return;
    auto* Exposure = NewObject<UPostProcessComponent>(this, TEXT("ArenaExposure"));
    Exposure->bUnbound = true;
    Exposure->Settings.bOverride_AutoExposureBias = true;
    Exposure->Settings.AutoExposureBias = -1.2f;
    AddInstanceComponent(Exposure);
    Exposure->RegisterComponent();
    auto Layer = [&](FName Name, FLinearColor Tint) {
        auto* Mesh = NewObject<UInstancedStaticMeshComponent>(this, Name);
        Mesh->SetStaticMesh(Cube);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetCastShadow(false);
        AddInstanceComponent(Mesh);
        Mesh->RegisterComponent();
        auto* MID = UMaterialInstanceDynamic::Create(Material, this);
        MID->SetVectorParameterValue(TEXT("Tint"), Tint);
        Mesh->SetMaterial(0, MID);
        return Mesh;
    };
    auto* Grid = Layer(TEXT("ArenaGrid"), FLinearColor(0.075f, 0.12f, 0.16f));
    auto* Markings = Layer(TEXT("ArenaMarkings"), FLinearColor(0.11f, 0.55f, 0.63f));
    auto* Trim = Layer(TEXT("CoverTrim"), FLinearColor(0.55f, 0.39f, 0.15f));
    ObjectiveRing = Layer(TEXT("ObjectiveRing"), FLinearColor(0.12f, 0.9f, 0.65f));
    ObjectiveMaterial = Cast<UMaterialInstanceDynamic>(ObjectiveRing->GetMaterial(0));
    if (AegisPortfolioPresentation::Enabled())
        if (auto* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Aegis/Materials/M_PortfolioGlow.M_PortfolioGlow")))
        {
            ObjectiveMaterial = UMaterialInstanceDynamic::Create(Glow, this);
            ObjectiveMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.12f, 0.9f, 0.65f));
            ObjectiveRing->SetMaterial(0, ObjectiveMaterial);
        }
    for (int32 I = 0; I < 48; ++I)
    {
        const float Angle = I * 2.f * PI / 48;
        ObjectiveRing->AddInstance(FTransform(FRotator(0, FMath::RadiansToDegrees(Angle)+90, 0),
            FVector(FMath::Cos(Angle)*260, FMath::Sin(Angle)*260, 6), FVector(0.23, 0.055, 0.035)), false);
    }
    ObjectiveRing->SetVisibility(false);
    auto Add = [&](UInstancedStaticMeshComponent* Mesh, FVector Location, FVector Scale) {
        Mesh->AddInstance(FTransform(FRotator::ZeroRotator, GetActorLocation()+Location, Scale), true);
    };
    for (int32 X = -1800; X <= 1800; X += 300) Add(Grid, FVector(X, 0, 1), FVector(0.018, 29, 0.015));
    for (int32 Y = -1200; Y <= 1200; Y += 300) Add(Grid, FVector(0, Y, 1), FVector(39, 0.018, 0.015));
    for (float Side : {-1.f, 1.f})
    {
        Add(Markings, FVector(0, Side*1430, 2), FVector(38, 0.045, 0.02));
        Add(Markings, FVector(Side*1930, 0, 2), FVector(0.045, 28.6, 0.02));
        for (int32 Y = -3; Y <= 3; ++Y)
            Add(Markings, FVector(Side*1700, Y*100, 2), FVector(0.5, 0.035, 0.02));
    }
    const FVector Covers[] = {FVector(-300,400,301), FVector(300,-400,301), FVector(600,600,301), FVector(-700,-600,301)};
    for (int32 I=0; I<4; ++I)
    {
        const float Width = I<2 ? 3.6f : 2.8f;
        for (float Side : {-1.f, 1.f})
        {
            Add(Trim, Covers[I]+FVector(0,Side*(Width*50-12),0), FVector(Width-0.2,0.06,0.025));
            Add(Trim, Covers[I]+FVector(Side*(Width*50-12),0,0), FVector(0.06,Width-0.2,0.025));
        }
    }
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
    if (!HasEncounterNavigation(this))
    {
        if (FPlatformTime::Seconds() < StartupDeadline)
            return;
        GetWorldTimerManager().ClearTimer(StartupTimer);
        Status = TEXT("FAILED: navigation did not become ready within 30 seconds");
        UE_LOG(LogTemp, Error, TEXT("%s"), *Status);
        FNavLocation Probe;
        UE_LOG(LogTemp, Error, TEXT("AEGIS_NAV_PREFLIGHT origin=%s root=%s nav=%s playerPoint=%d"),
               *GetActorLocation().ToString(), *GetNameSafe(GetRootComponent()), *GetNameSafe(Nav),
               Nav && Nav->ProjectPointToNavigation(FVector(-1200, 0, 100), Probe) ? 1 : 0);
        if (FParse::Param(FCommandLine::Get(), TEXT("AegisQuit")))
            FPlatformMisc::RequestExitWithStatus(false, 2, TEXT("Aegis navigation preflight"));
        return;
    }
    GetWorldTimerManager().ClearTimer(StartupTimer);
    if (FParse::Param(FCommandLine::Get(), TEXT("AegisDecisionLab")))
    {
        auto* Lab = GetWorld()->SpawnActor<AAegisDecisionLab>();
        Lab->Initialize(this);
#if !UE_BUILD_SHIPPING
        if (FParse::Param(FCommandLine::Get(), TEXT("AegisLabInputProbe"))) {
            auto* Probe = GetWorld()->SpawnActor<AAegisDecisionLabProbe>();
            Probe->Initialize(Lab, Cast<AAegisPlayerController>(GetWorld()->GetFirstPlayerController()));
        }
#endif
        return;
    }
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
    if (AegisPortfolioPresentation::Enabled()) FParse::Value(FCommandLine::Get(), TEXT("AegisPortfolioSeed="), Definition.Seed);
    StartInteractive();
    if (AegisPortfolioPresentation::Enabled())
    {
        auto* Portfolio = GetWorld()->SpawnActor<AAegisPortfolio>();
        Portfolio->Initialize(this);
    }
    if (bObjectiveTrial && !AegisPortfolioPresentation::Enabled())
    {
        SquadPlanner = GetWorld()->SpawnActor<AAegisSquadPlanner>();
        if (SquadPlanner) SquadPlanner->Initialize(this);
    }
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("AegisReleaseProbe")))
    {
        auto* Probe = GetWorld()->SpawnActor<AAegisReleaseProbe>();
        Probe->Initialize(this);
    }
    else if (FParse::Param(FCommandLine::Get(), TEXT("AegisAVProbe")))
    {
        auto* Probe = GetWorld()->SpawnActor<AAegisAVProbe>();
        Probe->Initialize(this);
    }
    else if (FParse::Param(FCommandLine::Get(), TEXT("AegisV23Probe")))
    {
        auto* Probe = GetWorld()->SpawnActor<AAegisV23Probe>();
        Probe->Initialize(this);
    }
    else if (FParse::Param(FCommandLine::Get(), TEXT("AegisV2Probe")))
    {
        auto* Probe = GetWorld()->SpawnActor<AAegisV2Probe>();
        Probe->Initialize(this);
    }
    else if (FParse::Param(FCommandLine::Get(), TEXT("AegisPortfolioProbe")))
    {
        auto* Probe = GetWorld()->SpawnActor<AAegisPortfolioProbe>();
        Probe->Initialize(this);
    }
    else if (FParse::Param(FCommandLine::Get(), TEXT("AegisPortfolioCapture")))
    {
        auto* Capture = GetWorld()->SpawnActor<AAegisPortfolioCapture>();
        Capture->Initialize(this);
    }
    else if (FParse::Param(FCommandLine::Get(), TEXT("AegisCopilotFaultProbe")))
    {
        auto* Probe = GetWorld()->SpawnActor<AAegisCopilotFaultProbe>();
        if (Probe) Probe->Initialize(this, Cast<AAegisPlayerController>(GetWorld()->GetFirstPlayerController()));
    }
    else if (FParse::Param(FCommandLine::Get(), TEXT("AegisCopilotProbe")))
    {
        auto* Probe = GetWorld()->SpawnActor<AAegisCopilotProbe>();
        if (Probe) Probe->Initialize(this, Cast<AAegisPlayerController>(GetWorld()->GetFirstPlayerController()));
    }
    else if (FParse::Param(FCommandLine::Get(), TEXT("AegisPlayProbe")))
    {
        auto* Probe = GetWorld()->SpawnActor<AAegisPlayProbe>();
        if (Probe) Probe->Initialize(this, Cast<AAegisPlayerController>(GetWorld()->GetFirstPlayerController()));
    }
    else if (FParse::Param(FCommandLine::Get(), TEXT("AegisInputProbe")))
    {
        auto* Probe = GetWorld()->SpawnActor<AAegisInputProbe>();
        if (Probe) Probe->Initialize(this, Cast<AAegisPlayerController>(GetWorld()->GetFirstPlayerController()));
    }
#endif
}
void AAegisScenarioRunner::EndPlay(EEndPlayReason::Type Reason)
{
    if (SquadPlanner) SquadPlanner->Destroy();
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
    if (!HasEncounterNavigation(this))
    {
        Status = TEXT("No connected navigation between encounter spawn lanes");
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
                                                            bool Companion, bool Elite, int32 Seed, int32 TacticalRole)
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
    Bot->bTacticalTrial = bInteractive && bObjectiveTrial;
    Bot->EnemyRole = static_cast<EAegisEnemyRole>(TacticalRole >= 0 ? TacticalRole % 3 : static_cast<uint32>(Seed) % 3);
    Bot->FinishSpawning(Transform);
    if (Bot->bTacticalTrial)
        if (auto* AI = Cast<AAegisAIController>(Bot->GetController()))
            AI->SetTacticalContext(Companion ? Cast<AAegisCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)) : nullptr,
                                   ObjectiveLocation, static_cast<uint32>(Seed) % 7);
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
    if (SquadPlanner) SquadPlanner->CancelPlan(TEXT("restart"));
    if (auto* AegisPC = Cast<AAegisPlayerController>(GetWorld()->GetFirstPlayerController())) AegisPC->ClosePlannerPanel();
    if (!Behavior || !CoverQuery || !AttackQuery || !RetreatQuery)
    {
        UE_LOG(LogTemp, Error, TEXT("Aegis interactive encounter has missing AI assets"));
        return;
    }
    CancelBatch();
    bInteractive = true;
    Trial = {};
    Operation = {};
    bNorthRouteFirst = false;
    bObjectiveContested = bUpgradePending = false;
    UpgradeMask = 0;
    LastUpgrade.Reset();
    if (ObjectiveRing) ObjectiveRing->SetVisibility(false);
    TrialDamageDealt = TrialDamageTaken = 0;
    EncounterRandom.Initialize(Definition.Seed);
    if (auto* PC = GetWorld()->GetFirstPlayerController())
    {
        PC->SetPause(false);
        if (auto* AegisPC = Cast<AAegisPlayerController>(PC))
        {
            AegisPC->bMenuOpen = false;
            AegisPC->CommandFeedback.Reset();
        }
        if (PC->PlayerInput) PC->PlayerInput->FlushPressedKeys();
        if (APawn* Previous = PC->GetPawn())
        {
            PC->UnPossess();
            Previous->Destroy();
        }
        GetWorld()->GetAuthGameMode()->RestartPlayer(PC);
        PC->bAutoManageActiveCameraTarget = true;
        PC->SetViewTarget(PC->GetPawn());
        if (auto* Player = Cast<AAegisPlayerCharacter>(PC->GetPawn()))
        {
            Player->bCombatEnabled = false;
            Player->bPulseEnabled = bObjectiveTrial;
            Player->Health->OnDamaged.AddDynamic(this, &AAegisScenarioRunner::RecordInteractiveDamage);
        }
        const FVector PlayerLocation =
            PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : FVector(-1200, 0, 100);
        if (auto* Companion = SpawnConfiguredBot(PlayerLocation + FVector(-160, 180, 0), EAegisTeam::Player,
                                                 true, false, Definition.Seed))
            OwnedBots.Add(Companion);
    }
    Status = TEXT("Briefing");
    GetWorldTimerManager().SetTimer(SampleTimer, this, &AAegisScenarioRunner::UpdateInteractive, 0.1f, true);
}
void AAegisScenarioRunner::DeployTrial(bool Pressure)
{
    if (!bInteractive || Trial.phase != aegis::TrialPhase::Briefing)
        return;
    bPressureSelected = Pressure;
    Trial.start(GetWorld()->GetTimeSeconds(), Pressure);
    if (auto* Player = Cast<AAegisPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0)))
        Player->bCombatEnabled = true;
    if (!SpawnTrialWave())
    {
        Trial.phase = aegis::TrialPhase::Lost;
        Status = TEXT("Deployment failed - press R to retry");
        if (auto* Player = Cast<AAegisPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0))) Player->bCombatEnabled = false;
        for (AAegisAICharacter* Bot : OwnedBots)
            if (IsValid(Bot))
                if (auto* AI = Cast<AAegisAIController>(Bot->GetController())) AI->ShutdownAI();
    }
}
int32 AAegisScenarioRunner::LivingEnemies() const
{
    int32 Count = 0;
    for (const AAegisAICharacter* Bot : OwnedBots)
        if (IsValid(Bot) && Bot->Team == EAegisTeam::Enemy && Bot->Health->IsAlive())
            ++Count;
    return Count;
}
AAegisAICharacter* AAegisScenarioRunner::GetCompanion() const
{
    for (AAegisAICharacter* Bot : OwnedBots)
        if (IsValid(Bot) && Bot->bCompanion)
            return Bot;
    return nullptr;
}
bool AAegisScenarioRunner::SpawnTrialWave()
{
    if (auto* Player = Cast<AAegisPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0))) Player->CancelCharge();
    // Retire the previous wave's bodies and controllers before the next wave.
    for (int32 I = OwnedBots.Num() - 1; I >= 0; --I)
        if (!IsValid(OwnedBots[I]) || !OwnedBots[I]->bCompanion)
        {
            if (IsValid(OwnedBots[I]))
            {
                auto* Controller = OwnedBots[I]->GetController();
                OwnedBots[I]->Destroy();
                if (IsValid(Controller)) Controller->Destroy();
            }
            OwnedBots.RemoveAt(I);
        }
    const int32 Count = Trial.enemyCount();
    const bool bV2Operation = FParse::Param(FCommandLine::Get(), TEXT("AegisV2"));
    Operation.reset(Trial.wave, bV2Operation ? (Trial.wave == 1 ? 8.0 : 10.0) : 4.0, bV2Operation ? 4.0 : 3.0);
    LastObjectiveAt = GetWorld()->GetTimeSeconds();
    SetObjectiveLocation();
    auto* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    // Alternate entry sides, choosing the side farther from the player.
    const float Side = Player && Player->GetActorLocation().X > 0 ? -1.f : 1.f;
    const int32 TacticalCount = Count + (bObjectiveTrial ? 1 : 0);
    for (int32 I = 0; I < TacticalCount; ++I)
    {
        const FVector Position = GetActorLocation() + FVector(Side * 1450, -800 + I * 1600.f / FMath::Max(1, TacticalCount - 1), 100);
        auto* Enemy = SpawnConfiguredBot(Position, EAegisTeam::Enemy, false,
                                         Trial.wave == 3 && I == TacticalCount - 1,
                                         Definition.Seed ^ (Trial.wave * 7919 + I * 104729), I % 3);
        if (!Enemy) return false;
        Enemy->Health->OnDamaged.AddDynamic(this, &AAegisScenarioRunner::RecordInteractiveDamage);
        OwnedBots.Add(Enemy);
    }
    Status = FString::Printf(TEXT("Wave %d / 3"), Trial.wave);
    return true;
}
void AAegisScenarioRunner::RecordInteractiveDamage(float Applied, AActor* Source, AActor* Victim)
{
    if (!bInteractive || Trial.phase != aegis::TrialPhase::Active) return;
    const auto* Attacker = Cast<AAegisCharacter>(Source);
    if (Attacker && Attacker->Team == EAegisTeam::Player) TrialDamageDealt += Applied;
    if (HasUpgrade(3) && !FParse::Param(FCommandLine::Get(), TEXT("AegisV2")) && Attacker && Attacker->Team == EAegisTeam::Player)
    {
        if (auto* Player = Cast<AAegisCharacter>(UGameplayStatics::GetPlayerPawn(this, 0))) Player->Health->Heal(Applied * 0.1f);
        if (auto* Companion = GetCompanion()) Companion->Health->Heal(Applied * 0.1f);
    }
    if (Victim == UGameplayStatics::GetPlayerPawn(this, 0)) TrialDamageTaken += Applied;
}
void AAegisScenarioRunner::UpdateInteractive()
{
    if (!bInteractive || Trial.phase == aegis::TrialPhase::Briefing || Trial.finished()) return;
    auto* Player = Cast<AAegisPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
    const auto Before = Trial.phase;
    const int32 WaveBefore = Trial.wave;
    const double Now = GetWorld()->GetTimeSeconds();
    if (Trial.phase == aegis::TrialPhase::Active && bObjectiveTrial)
        UpdateObjective(FMath::Clamp(Now - LastObjectiveAt, 0.0, 0.25));
    LastObjectiveAt = Now;
    Trial.observe(Now, Player && Player->Health->IsAlive(),
                  bObjectiveTrial && !Operation.complete ? FMath::Max(1, LivingEnemies()) : LivingEnemies());
    if (auto* Portfolio = AAegisPortfolio::Find(GetWorld())) Portfolio->ObserveProgress();
    if (Trial.phase == aegis::TrialPhase::Intermission && Before == aegis::TrialPhase::Active)
    {
        if (Player) Player->CancelCharge();
        Status = TEXT("Sector clear - recover and reposition");
        if (Player) Player->Health->Heal(Trial.pressure ? 15 : 30);
        if (auto* Companion = GetCompanion()) Companion->Health->Heal(Trial.pressure ? 20 : 40);
        if (ObjectiveRing) ObjectiveRing->SetVisibility(false);
        if (bObjectiveTrial)
        {
            bUpgradePending = true;
            if (auto* PC = GetWorld()->GetFirstPlayerController()) PC->SetPause(true);
        }
    }
    if (Trial.wave != WaveBefore && !SpawnTrialWave())
    {
        Trial.phase = aegis::TrialPhase::Lost;
        Status = TEXT("Deployment failed - press R to retry");
    }
    if (Trial.finished())
    {
        if (ObjectiveRing) ObjectiveRing->SetVisibility(false);
        if (Player)
        {
            Player->bCombatEnabled = false;
            Player->CancelCharge();
            Player->GetCharacterMovement()->StopMovementImmediately();
        }
        for (AAegisAICharacter* Bot : OwnedBots)
            if (IsValid(Bot))
                if (auto* AI = Cast<AAegisAIController>(Bot->GetController())) AI->ShutdownAI();
        Status = Trial.phase == aegis::TrialPhase::Won ? TEXT("All sectors secured") : TEXT("Signal lost");
    }
}
void AAegisScenarioRunner::SetObjectiveLocation()
{
    if (!bObjectiveTrial) return;
    FVector Offset = Trial.wave == 1 ? FVector(-500, -1000, 20) :
        Trial.wave == 2 ? ((Operation.relay == 0) != bNorthRouteFirst ? FVector(850, -850, 20) : FVector(0, 1000, 20)) : FVector(-1250, 0, 20);
    ObjectiveLocation = GetActorLocation() + Offset;
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    FNavLocation Projected;
    if (Nav && Nav->ProjectPointToNavigation(ObjectiveLocation, Projected, FVector(160, 160, 200)))
        ObjectiveLocation = Projected.Location;
    if (ObjectiveRing)
    {
        ObjectiveRing->SetWorldLocation(ObjectiveLocation);
        ObjectiveRing->SetVisibility(!Operation.complete);
    }
    int32 Index = 0;
    for (AAegisAICharacter* Bot : OwnedBots)
        if (IsValid(Bot) && !Bot->bCompanion && Bot->Health->IsAlive())
            if (auto* AI = Cast<AAegisAIController>(Bot->GetController()))
                AI->SetTacticalContext(nullptr, ObjectiveLocation, Index++);
}
void AAegisScenarioRunner::UpdateObjective(float DeltaSeconds)
{
    auto* Player = Cast<AAegisCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
    auto* Companion = GetCompanion();
    auto Inside = [&](const AAegisCharacter* C) {
        return IsValid(C) && C->Health->IsAlive() && FVector::DistSquared2D(C->GetActorLocation(), ObjectiveLocation) <= 260.f*260.f;
    };
    bObjectiveContested = false;
    for (const AAegisAICharacter* Bot : OwnedBots)
        if (IsValid(Bot) && Bot->Team == EAegisTeam::Enemy && Inside(Bot)) bObjectiveContested = true;
    const int32 PreviousRelay = Operation.relay;
    const double PreviousCharge = Operation.charge;
    const bool PlayerInside = Inside(Player);
    const bool CompanionInside = Inside(Companion);
    auto* Portfolio = AAegisPortfolio::Find(GetWorld());
    float Multiplier = Portfolio ? Portfolio->GetOverclockMultiplier() : 1.f;
    if (Portfolio && Trial.phase == aegis::TrialPhase::Active && Trial.wave < 3 &&
        !Operation.complete && !bObjectiveContested && (PlayerInside || CompanionInside) &&
        FMath::IsFinite(DeltaSeconds) && DeltaSeconds > 0 && DeltaSeconds * Multiplier * 1.25f <= 1.f)
        Multiplier *= Portfolio->ApplySurveyRelayBoost();
    // Aegis can operate data relays alone. Extraction additionally requires the player.
    Operation.observe(DeltaSeconds * Multiplier, PlayerInside, CompanionInside && (Trial.wave != 3 || PlayerInside), bObjectiveContested, LivingEnemies() == 0);
    if (Portfolio && (Operation.charge > PreviousCharge || Operation.relay != PreviousRelay))
    {
        const double AppliedProgress = Operation.relay != PreviousRelay ? Operation.requiredSeconds() - PreviousCharge : Operation.charge - PreviousCharge;
        const float ProductiveTime = FMath::Min(DeltaSeconds, static_cast<float>(AppliedProgress / (Multiplier * (PlayerInside && CompanionInside ? 1.5f : 1.f))));
        Portfolio->ApplySymbiosis(ProductiveTime, PlayerInside, CompanionInside);
    }
    if (Operation.relay != PreviousRelay) SetObjectiveLocation();
    if (ObjectiveMaterial)
        ObjectiveMaterial->SetVectorParameterValue(TEXT("Tint"), bObjectiveContested ? FLinearColor(1, 0.16f, 0.05f) :
            FLinearColor(0.05f, 0.7f + 0.25f*Operation.charge/Operation.requiredSeconds(), 0.7f));
    if (ObjectiveRing) ObjectiveRing->SetVisibility(!Operation.complete);
}
bool AAegisScenarioRunner::IsPlayerInObjective() const
{
    const auto* Player = Cast<AAegisCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
    return IsValid(Player) && Player->Health->IsAlive() && FVector::DistSquared2D(Player->GetActorLocation(), ObjectiveLocation) <= FMath::Square(GetObjectiveRadius());
}
bool AAegisScenarioRunner::IsCompanionInObjective() const
{
    const auto* Companion = GetCompanion();
    return IsValid(Companion) && Companion->Health->IsAlive() && FVector::DistSquared2D(Companion->GetActorLocation(), ObjectiveLocation) <= FMath::Square(GetObjectiveRadius());
}
FString AAegisScenarioRunner::ObjectiveText() const
{
    if (Trial.phase == aegis::TrialPhase::Briefing) return TEXT("SECURE  >  TRANSFER  >  EXTRACT");
    if (Trial.finished()) return Trial.phase == aegis::TrialPhase::Won ? TEXT("UPLINK COMPLETE") : TEXT("OPERATION FAILED");
    if (Trial.phase == aegis::TrialPhase::Intermission) return TEXT("CHOOSE A FIELD UPGRADE");
    if (Operation.complete) return TEXT("RELAY ONLINE - CLEAR REMAINING HOSTILES");
    if (Trial.wave == 3 && LivingEnemies() > 0) return TEXT("DEFEAT THE ELITE / CLEAR THE ARENA");
    if (bObjectiveContested) return TEXT("RELAY CONTESTED - PUSH HOSTILES OUT");
    return Trial.wave == 1 ? TEXT("SECURE THE RELAY - HOLD THE RING") : Trial.wave == 2 ?
        FString::Printf(TEXT("TRANSFER DATA - RELAY %d / 2"), Operation.relay+1) : TEXT("EXTRACT - BRING YOUR TEAM TO THE RING");
}
bool AAegisScenarioRunner::ToggleRelayRoute()
{
    if ((!FParse::Param(FCommandLine::Get(), TEXT("AegisV2")) &&
         !FParse::Param(FCommandLine::Get(), TEXT("AegisV23"))) ||
        !(Trial.phase == aegis::TrialPhase::Briefing || (Trial.wave == 1 && bUpgradePending))) return false;
    bNorthRouteFirst = !bNorthRouteFirst;
    return true;
}
bool AAegisScenarioRunner::ChooseUpgrade(int32 Index)
{
    if (!bUpgradePending || Trial.phase != aegis::TrialPhase::Intermission || Index < 1 || Index > 3 || HasUpgrade(Index)) return false;
    auto* Player = Cast<AAegisPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (!Player) return false;
    UpgradeMask |= 1 << (Index-1);
    if (Index == 1) { Player->Combat->bPiercingRounds = true; LastUpgrade = FParse::Param(FCommandLine::Get(), TEXT("AegisV2")) ? TEXT("PRISM SPLIT") : TEXT("PIERCING ROUNDS"); }
    if (Index == 2) { Player->bRestorativePulse = true; LastUpgrade = TEXT("RESCUE PULSE"); }
    if (Index == 3) LastUpgrade = FParse::Param(FCommandLine::Get(), TEXT("AegisV2")) ? TEXT("RELAY SYMBIOSIS") : TEXT("LIFE LINK");
    bUpgradePending = false;
    if (auto* PC = Cast<AAegisPlayerController>(GetWorld()->GetFirstPlayerController())) PC->SetPause(PC->bMenuOpen);
    return true;
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
    Current.CancelledQueries = 0;
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
            Current.CancelledQueries += AI->CancelledQueries;
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
AAegisDebugHUD::AAegisDebugHUD()
{
    static ConstructorHelpers::FObjectFinder<UTexture2D> Keyart(TEXT("/Game/Aegis/V23/UI/T_PrismArchive.T_PrismArchive"));
    BriefingIllustration = Keyart.Object;
    // Keep the baked atlas for Latin and use a packaged runtime fallback for
    // Chinese. Without the fallback, Canvas renders CJK strings as tofu boxes
    // in a cooked build even though the language toggle itself works.
    static ConstructorHelpers::FObjectFinder<UFont> Font(TEXT("/Engine/EngineFonts/RobotoDistanceField.RobotoDistanceField"));
    InterfaceFont = Font.Object;
    const FString CjkPath = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Fonts/DroidSansFallback.ttf"));
    if (FPaths::FileExists(CjkPath))
    {
        auto* RuntimeFont = NewObject<UFont>(this, TEXT("AegisRuntimeCjkFont"));
        RuntimeFont->FontCacheType = EFontCacheType::Runtime;
        RuntimeFont->RuntimeFontSource = ERuntimeFontSource::Asset;
        RuntimeFont->LegacyFontSize = 24;
        RuntimeFont->CompositeFont = FCompositeFont(FName(TEXT("Regular")), CjkPath,
            EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
        InterfaceFont = RuntimeFont;
    }
}
void AAegisDebugHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas) return;
    for (TActorIterator<AAegisDecisionLab> It(GetWorld()); It; ++It) { DrawDecisionLab(*It); return; }
    auto* PC = Cast<AAegisPlayerController>(GetOwningPlayerController());
    AAegisScenarioRunner* Runner = nullptr;
    for (TActorIterator<AAegisScenarioRunner> It(GetWorld()); It; ++It) { Runner = *It; break; }
    if (Runner && Runner->IsInteractive())
    {
        if (AegisPortfolioPresentation::Enabled()) { DrawPortfolio(Runner); return; }
        const float S = FMath::Min(Canvas->SizeX / 1280.f, Canvas->SizeY / 720.f);
        const float OX = (Canvas->SizeX - 1280 * S) / 2, OY = (Canvas->SizeY - 720 * S) / 2;
        const FLinearColor Ink(0.015f, 0.026f, 0.045f, 0.94f), Cyan(0.22f, 0.88f, 0.94f),
                           White(0.89f, 0.94f, 0.98f), Muted(0.51f, 0.64f, 0.72f), Gold(1.f, 0.7f, 0.27f);
        auto Box = [&](float X, float Y, float W, float H, FLinearColor C) { DrawRect(C, OX + X*S, OY + Y*S, W*S, H*S); };
        auto Text = [&](const FString& T, float X, float Y, FLinearColor C, float Size = 1.f) {
            UFont* Font = InterfaceFont ? InterfaceFont.Get() : GEngine->GetMediumFont();
            const float FontScale = 14.f / FMath::Max(1.f, Font->GetMaxCharHeight());
            DrawText(T, C, OX + X*S, OY + Y*S, Font, FontScale*FMath::Max(1.f, Size)*S, false);
        };
        auto Bar = [&](float X, float Y, float W, float Ratio, FLinearColor C) {
            Box(X, Y, W, 5, FLinearColor(0.11f, 0.18f, 0.22f));
            Box(X, Y, W * FMath::Clamp(Ratio, 0.f, 1.f), 5, C);
        };
        auto Button = [&](FName Name, const FString& Label, float X, float Y, float W, float H, FLinearColor Color) {
            float MX = -1, MY = -1;
            if (PC) PC->GetMousePosition(MX, MY);
            const bool Hover = MX >= OX+X*S && MX <= OX+(X+W)*S && MY >= OY+Y*S && MY <= OY+(Y+H)*S;
            Box(X, Y, W, H, Hover ? FLinearColor(0.10f, 0.28f, 0.32f, 1) : FLinearColor(0.04f, 0.09f, 0.13f, 1));
            Box(X, Y, 3, H, Color);
            Text(Label, X+14, Y+(H-18)/2, Color, 1.f);
            AddHitBox(FVector2D(OX+X*S, OY+Y*S), FVector2D(W*S, H*S), Name, true);
        };
        const auto& Trial = Runner->GetTrial();
        const auto& Operation = Runner->GetOperation();
        auto* Player = PC ? Cast<AAegisPlayerCharacter>(PC->GetPawn()) : nullptr;
        auto* Companion = Runner->GetCompanion();
        const bool Menu = PC && PC->bMenuOpen;
        const bool Briefing = Trial.phase == aegis::TrialPhase::Briefing;
        const bool Upgrade = Runner->IsUpgradePending();
        const bool Overlay = Menu || Briefing || Upgrade || Trial.finished() || (PC && PC->bPlannerOpen);
        Box(24, 22, 320, 80, Ink); Box(24, 22, 3, 80, Cyan);
        Text(TEXT("AEGIS / COPILOT"), 42, 33, White, 1.5f);
        Text(TEXT("ADAPT. COMMAND. EXTRACT."), 42, 74, Muted);
        Box(364, 22, 562, 80, Ink);
        Text(Runner->ObjectiveText(), 382, 35, Runner->IsObjectiveContested() ? Gold : Cyan);
        if (Trial.phase == aegis::TrialPhase::Active && !Operation.complete)
        {
            Bar(382, 70, 280, Operation.charge/Operation.requiredSeconds(), Cyan);
            Text(FString::Printf(TEXT("%.1f / %.0fs   SOLO x1 / DUO x1.5"), Operation.charge, Operation.requiredSeconds()), 680, 66, Muted);
        }
        else Text(TEXT("Q PULSE     Z GUARD / X FOCUS / C RALLY"), 382, 73, Muted);
        Box(950, 22, 306, 80, Ink);
        Text(FString::Printf(TEXT("SECTOR %02d / 03"), Trial.wave), 970, 34, Cyan);
        Text(FString::Printf(TEXT("%02d HOSTILES     %03ds LEFT"), Runner->LivingEnemies(), FMath::Max(0, FMath::CeilToInt(aegis::Trial::duration - Trial.elapsed))), 970, 72, White);
        Box(24, 572, 310, 124, Ink); Box(354, 572, 390, 124, Ink); Box(764, 572, 492, 124, Ink);
        if (Player)
        {
            Text(FString::Printf(TEXT("YOU                       %03.0f"), Player->Health->Current), 42, 584, Cyan);
            Bar(42, 613, 274, Player->Health->Current/Player->Health->Maximum, Cyan);
            Text(Player->GetDashCooldownRemaining() <= 0 ? TEXT("SPACE  DASH READY") : FString::Printf(TEXT("SPACE  DASH %.1fs"), Player->GetDashCooldownRemaining()), 42, 635, White);
            Text(Player->GetPulseCooldownRemaining() <= 0 ? TEXT("Q  PULSE READY / INTERRUPT + PUSH") : FString::Printf(TEXT("Q  PULSE RECHARGING %.1fs"), Player->GetPulseCooldownRemaining()), 42, 665, Gold);
        }
        Text(TEXT("AEGIS / COMPANION"), 372, 584, FLinearColor(0.36f, 0.92f, 0.65f));
        if (Companion)
        {
            Bar(372, 613, 354, Companion->Health->Current/Companion->Health->Maximum, FLinearColor(0.36f, 0.92f, 0.65f));
            FString State = TEXT("OFFLINE - finish the operation");
            if (Companion->Health->IsAlive())
            {
                State = TEXT("Ready for deployment");
                if (auto* AI = Cast<AAegisAIController>(Companion->GetController())) State = AI->DebugState;
                if (Briefing) State = TEXT("Ready - commands unlock on deploy");
                else if (Trial.finished()) State = TEXT("Standing down");
                else if (Menu) State = TEXT("Paused - awaiting your signal");
            }
            Text(State.Left(48), 372, 669, Muted);
        }
        if (!Overlay)
        {
            // Position-dependent commands are keyboard hints, not world clicks
            // through the HUD. The cursor stays on the intended enemy/floor.
            Text(TEXT("Z GUARD    X FOCUS    C RALLY"), 372, 638, Cyan);
            Button(TEXT("Menu"), TEXT("ESC  MENU / QUIT"), 782, 582, 456, 32, Gold);
        }
        else Text(TEXT("ESC  MENU / QUIT"), 784, 590, Gold, 1.15f);
        Text(TEXT("WASD MOVE    MOUSE AIM    HOLD LMB FIRE"), 784, 630, White);
        Text(TEXT("RMB MELEE    SPACE DASH    Q PULSE    TAB AI"), 784, 666, Muted);
        if (!Overlay && Runner->GetSquadPlanner())
        {
            const auto* Planner = Runner->GetSquadPlanner();
            Box(24, 110, 520, 48, Ink);
            Text(TEXT("TAB  SQUAD COPILOT  /  ") + Planner->StatusLabel.Left(43), 40, 116, Cyan);
            Text(Planner->StepsLabel.Left(65), 40, 137, Muted);
        }
        if (!Overlay && PC && GetWorld()->GetTimeSeconds() < PC->CommandFeedbackUntil)
        {
            Box(330, 528, 620, 34, Ink);
            Text(PC->CommandFeedback, 350, 536, Cyan);
        }
        if (!Overlay)
        {
            for (TActorIterator<AAegisCharacter> It(GetWorld()); It; ++It)
            {
                if (!It->Health->IsAlive() || *It == Player) continue;
                const FVector Screen = Project(It->GetActorLocation()+FVector(0,0,105));
                if (Screen.Z <= 0 || Screen.X < 0 || Screen.X > Canvas->SizeX || Screen.Y < 120*S || Screen.Y > 538*S) continue;
                const float W = 48*S;
                DrawRect(Ink, Screen.X-W/2-2*S, Screen.Y-2*S, W+4*S, 7*S);
                DrawRect(It->Team == EAegisTeam::Player ? FLinearColor(0.36f,0.92f,0.65f) : Gold, Screen.X-W/2, Screen.Y, W*FMath::Clamp(It->Health->Current/It->Health->Maximum,0.f,1.f),3*S);
            }
            if (Trial.phase == aegis::TrialPhase::Active && !Operation.complete)
            {
                const FVector Projected = Project(Runner->GetObjectiveLocation()+FVector(0,0,50));
                const float X = FMath::Clamp((Projected.X-OX)/S, 100.f, 1110.f), Y = FMath::Clamp((Projected.Y-OY)/S, 184.f, 500.f);
                Box(X-22, Y-22, 220, 31, Ink);
                Text(FString::Printf(TEXT("%s  %dm"), Trial.wave == 3 ? TEXT("EXTRACT") : TEXT("RELAY"), Player ? FMath::RoundToInt(FVector::Dist2D(Player->GetActorLocation(), Runner->GetObjectiveLocation())/100) : 0), X-10, Y-16, Cyan);
                DrawLine(OX+(X-8)*S, OY+(Y+16)*S, OX+X*S, OY+(Y+24)*S, Cyan, 2*S);
                DrawLine(OX+X*S, OY+(Y+24)*S, OX+(X+8)*S, OY+(Y+16)*S, Cyan, 2*S);
            }
            if (Trial.phase == aegis::TrialPhase::Active && PC)
            {
                float MX=0, MY=0;
                if (PC->GetMousePosition(MX,MY))
                {
                    const FLinearColor Aim = Player && Player->Combat->SecondsSinceLastHit()<0.15f ? Gold : Cyan;
                    DrawLine(MX-11*S,MY,MX-4*S,MY,Aim,1.5f*S); DrawLine(MX+4*S,MY,MX+11*S,MY,Aim,1.5f*S);
                    DrawLine(MX,MY-11*S,MX,MY-4*S,Aim,1.5f*S); DrawLine(MX,MY+4*S,MX,MY+11*S,Aim,1.5f*S);
                }
            }
        }
        if (Trial.phase == aegis::TrialPhase::Intermission && !Overlay)
        {
            Box(350, 125, 580, 68, Ink);
            Text(TEXT("FITTED: ")+Runner->LastUpgrade, 372, 136, Cyan);
            Text(FString::Printf(TEXT("Recover + reposition. Next sector in %.0fs"), FMath::Max(0.0, Trial.transitionAt-GetWorld()->GetTimeSeconds())),372,165,White);
        }
        if (Overlay && !(PC && PC->bPlannerOpen))
        {
            Box(0,110,1280,447,FLinearColor(0.006f,0.013f,0.024f,0.7f));
            Box(240,125,800,420,Ink); Box(240,125,3,420,Cyan);
            if (Menu)
            {
                Text(TEXT("PAUSED / GAME MENU"), 274, 155, White, 1.8f);
                Text(TEXT("The simulation is paused. Choose an action below."),274,205,Muted);
                Button(TEXT("Menu"),TEXT("[ ESC / P ]  RESUME"),274,258,732,57,Cyan);
                Button(TEXT("Restart"),TEXT("[ R ]  RESTART OPERATION"),274,330,732,57,White);
                Button(TEXT("Quit"),TEXT("[ X ]  QUIT TO DESKTOP"),274,402,732,68,Gold);
                Text(TEXT("Mouse buttons work here too."),274,503,Muted);
            }
            else if (Briefing)
            {
                Text(TEXT("THREE SECTORS. ONE SHARED SIGNAL."),274,150,White,1.65f);
                Text(TEXT("1 SECURE relay   >   2 TRANSFER across two relays   >   3 EXTRACT"),274,201,Cyan);
                Text(TEXT("Hold each ring with you or Aegis; clear its hostiles. Both inside = faster."),274,229,White);
                Text(TEXT("Q interrupts attacks. Z protects you, X marks a threat, C orders a position."),274,256,Muted);
                Button(TEXT("Guided"),Runner->bPressureSelected ? TEXT("[1] GUIDED / 3-4-5 foes") : TEXT("[1] GUIDED / SELECTED"),274,302,352,54,Cyan);
                Button(TEXT("Pressure"),Runner->bPressureSelected ? TEXT("[2] PRESSURE / SELECTED") : TEXT("[2] PRESSURE / 4-5-6 foes"),646,302,360,54,Gold);
                Button(TEXT("Deploy"),TEXT("[ ENTER ]  DEPLOY"),274,390,352,60,Cyan);
                Button(TEXT("Quit"),TEXT("QUIT TO DESKTOP"),646,390,360,60,Gold);
                Text(TEXT("WASD + mouse / hold LMB / Space dash / Esc menu and exit"),274,495,Muted);
            }
            else if (Upgrade)
            {
                Text(TEXT("SECTOR CLEAR / CHOOSE YOUR BUILD"),274,150,White,1.55f);
                Text(TEXT("Time is paused. Choose one upgrade; it lasts until the next attempt."),274,202,Muted);
                Button(TEXT("Upgrade1"),Runner->HasUpgrade(1)?TEXT("PIERCING ROUNDS / OWNED"):TEXT("[1] PIERCING ROUNDS"),274,245,732,47,Runner->HasUpgrade(1)?Muted:Cyan);
                Text(TEXT("Bullets pass through one hostile. Cover and allies still block the shot."),290,299,White);
                Button(TEXT("Upgrade2"),Runner->HasUpgrade(2)?TEXT("RESCUE PULSE / OWNED"):TEXT("[2] RESCUE PULSE"),274,336,732,47,Runner->HasUpgrade(2)?Muted:Cyan);
                Text(TEXT("Q also heals you and nearby Aegis. Stay together for emergency recovery."),290,390,White);
                Button(TEXT("Upgrade3"),Runner->HasUpgrade(3)?TEXT("LIFE LINK / OWNED"):TEXT("[3] LIFE LINK"),274,426,732,47,Runner->HasUpgrade(3)?Muted:Cyan);
                Text(TEXT("Actual allied damage repairs both teammates for 10% of damage dealt."),290,485,White);
            }
            else
            {
                Text(Trial.phase == aegis::TrialPhase::Won ? TEXT("UPLINK COMPLETE / EXTRACTION SECURED") : TEXT("SIGNAL LOST / TRY A NEW TACTIC"),274,151,White,1.6f);
                Text(FString::Printf(TEXT("TIME %.1fs     ALLIED DAMAGE %.0f     DAMAGE TAKEN %.0f"),Trial.elapsed,Runner->TrialDamageDealt,Runner->TrialDamageTaken),274,229,Cyan);
                Text(Companion && Companion->Health->IsAlive()?TEXT("Aegis survived the operation."):TEXT("Aegis is offline. Use Guard and Rescue Pulse to support your teammate."),274,280,Muted);
                Button(TEXT("Restart"),TEXT("[ R ]  NEW ATTEMPT"),274,365,352,62,Cyan);
                Button(TEXT("Quit"),TEXT("QUIT TO DESKTOP"),646,365,360,62,Gold);
                Text(TEXT("Different upgrades and squad commands create different approaches."),274,491,Muted);
            }
        }
    }
    else if (Runner)
    {
        DrawRect(FLinearColor(0.015f, 0.026f, 0.045f, 0.9f), 24, 24, 650, 65);
        DrawText(TEXT("AEGIS / ARENA"), FColor::Cyan, 42, 34, GEngine->GetMediumFont(), 1.2f);
        DrawText(Runner->Status == TEXT("Idle") ? TEXT("Preparing arena navigation...") : Runner->Status,
                 FColor::White, 42, 66, GEngine->GetSmallFont());
    }
#if !UE_BUILD_SHIPPING
    if (PC && !PC->bShowDiagnostics) return;
    float Y = 30;
    DrawText(TEXT("AEGIS / development diagnostics"), FColor::Cyan, 25, Y);
    Y += 24;
    for (TActorIterator<AAegisAIController> It(GetWorld()); It; ++It)
    {
        auto* Bot = Cast<AAegisCharacter>(It->GetPawn());
        if (!Bot)
            continue;
        if (It->IsTacticalTrialEnabled()) {
            DrawText(FString::Printf(TEXT("%s: TACTICAL execution / Utility scores not controlling / EQS not used"), *GetNameSafe(It->GetPawn())), FLinearColor::Yellow, 24, Y, GEngine->GetSmallFont());
            Y += 24; continue;
        }
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
    PlayerControllerClass = AAegisPlayerController::StaticClass();
}

namespace
{
AAegisDecisionLab* FindDecisionLab(UWorld* World)
{
    for (TActorIterator<AAegisDecisionLab> It(World); It; ++It) return *It;
    return nullptr;
}
AAegisScenarioRunner* FindInteractiveRunner(UWorld* World)
{
    for (TActorIterator<AAegisScenarioRunner> It(World); It; ++It)
        if (It->IsInteractive()) return *It;
    return nullptr;
}
}
void AAegisPlayerController::BeginPlay()
{
    Super::BeginPlay();
    const auto Preferences = FAegisUIPreferences::Load(FAegisUIPreferences::DefaultPath(), FCommandLine::Get());
    bEnglishUI = Preferences.bEnglish;
    bReducedEffects = Preferences.bReducedEffects;
    bPersistUILanguage = Preferences.bPersistent;
    bShowMouseCursor = true;
    bEnableClickEvents = true;
    bEnableMouseOverEvents = true;
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
    if (bPersistUILanguage && FSlateApplication::IsInitialized())
        ApplicationActivationHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(
            this, &AAegisPlayerController::OnApplicationActivationChanged);
}
void AAegisDebugHUD::NotifyHitBoxClick(FName BoxName)
{
    Super::NotifyHitBoxClick(BoxName);
    if (auto* PC = Cast<AAegisPlayerController>(GetOwningPlayerController())) PC->MenuAction(BoxName);
}
void AAegisPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &AAegisPlayerController::Deploy);
    InputComponent->BindKey(EKeys::One, IE_Pressed, this, &AAegisPlayerController::Guided).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &AAegisPlayerController::Pressure).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AAegisPlayerController::ThirdUpgrade).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::Z, IE_Pressed, this, &AAegisPlayerController::Guard);
    InputComponent->BindKey(EKeys::X, IE_Pressed, this, &AAegisPlayerController::FocusOrQuit).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::C, IE_Pressed, this, &AAegisPlayerController::Rally);
    InputComponent->BindKey(EKeys::Tab, IE_Pressed, this, &AAegisPlayerController::TogglePlannerPanel).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::L, IE_Pressed, this, &AAegisPlayerController::ToggleLanguage).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::F11, IE_Pressed, this, &AAegisPlayerController::ToggleWindowMode).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::K, IE_Pressed, this, &AAegisPlayerController::ToggleEffects).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::R, IE_Pressed, this, &AAegisPlayerController::Restart).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::P, IE_Pressed, this, &AAegisPlayerController::PauseTrial).bExecuteWhenPaused = true;
    InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AAegisPlayerController::PauseTrial).bExecuteWhenPaused = true;
#if !UE_BUILD_SHIPPING
    InputComponent->BindKey(EKeys::F1, IE_Pressed, this, &AAegisPlayerController::Diagnostics).bExecuteWhenPaused = true;
#endif
}
void AAegisPlayerController::MenuAction(FName Action)
{
    if (Action == TEXT("WindowMode")) { ToggleWindowMode(); return; }
    if (Action == TEXT("Effects")) { ToggleEffects(); return; }
    if (Action == TEXT("Music")) { if (auto* Music = AAegisPortfolioMusic::Find(GetWorld())) Music->ToggleMute(); return; }
    if (Action == TEXT("Language"))
    {
        ToggleLanguage();
        return;
    }
    if (auto* Lab = FindDecisionLab(GetWorld()))
    {
        if (Action == TEXT("Menu")) PauseTrial();
        else if (Action == TEXT("Restart")) Restart();
        else if (Action == TEXT("Quit") && (bMenuOpen || Lab->Phase != TEXT("Active")))
            UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
        else if (!bMenuOpen)
        {
            if (Action == TEXT("Deploy")) Deploy();
            else if (Action == TEXT("Guided")) Guided();
            else if (Action == TEXT("Pressure")) Pressure();
            else if (Action == TEXT("Upgrade3")) ThirdUpgrade();
        }
        return;
    }
    auto* Runner = FindInteractiveRunner(GetWorld());
    if (!Runner) return;
    if (Action == TEXT("Menu")) PauseTrial();
    else if (Action == TEXT("Restart")) Restart();
    else if (Action == TEXT("Quit") && (bMenuOpen || Runner->GetTrial().phase == aegis::TrialPhase::Briefing || Runner->GetTrial().finished()))
        UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
    else if (!bMenuOpen)
    {
        if (Action == TEXT("Deploy")) Deploy();
        else if (Action == TEXT("Guided") || Action == TEXT("Upgrade1")) Guided();
        else if (Action == TEXT("Pressure") || Action == TEXT("Upgrade2")) Pressure();
        else if (Action == TEXT("Upgrade3")) ThirdUpgrade();
        else if (Action == TEXT("Guard")) Guard();
        else if (Action == TEXT("Focus")) FocusOrQuit();
        else if (Action == TEXT("Rally")) Rally();
    }
}
void AAegisPlayerController::Deploy()
{
    if (bMenuOpen) return;
    if (auto* Lab = FindDecisionLab(GetWorld())) { Lab->StartEncounter(); return; }
    if (auto* Runner = FindInteractiveRunner(GetWorld())) Runner->DeployTrial(Runner->bPressureSelected);
}
void AAegisPlayerController::Restart()
{
    if (auto* Lab = FindDecisionLab(GetWorld())) { Lab->ResetEncounter(); return; }
    if (auto* Runner = FindInteractiveRunner(GetWorld()))
    {
        const bool bHasProgress = Runner->GetTrial().phase != aegis::TrialPhase::Briefing && !Runner->GetTrial().finished();
        if (AegisPortfolioPresentation::Enabled() && bHasProgress && !bRestartConfirmation)
        {
            bRestartConfirmation = bMenuOpen = true;
            SetPause(true);
            if (auto* Controlled = Cast<AAegisPlayerCharacter>(GetPawn())) Controlled->CancelCharge();
            if (PlayerInput) PlayerInput->FlushPressedKeys();
            return;
        }
        bRestartConfirmation = bPausedForFocus = false;
        Runner->StartInteractive();
    }
}
void AAegisPlayerController::Guided()
{
    if (bMenuOpen) return;
    if (auto* Lab = FindDecisionLab(GetWorld())) { Lab->SetPolicy(TEXT("baseline")); return; }
    if (auto* Runner = FindInteractiveRunner(GetWorld()))
    {
        if (Runner->GetTrial().phase == aegis::TrialPhase::Briefing) Runner->bPressureSelected = false;
        else Runner->ChooseUpgrade(1);
    }
}
void AAegisPlayerController::Pressure()
{
    if (bMenuOpen) return;
    if (auto* Lab = FindDecisionLab(GetWorld())) { Lab->SetPolicy(TEXT("improved")); return; }
    if (auto* Runner = FindInteractiveRunner(GetWorld()))
    {
        if (Runner->GetTrial().phase == aegis::TrialPhase::Briefing) Runner->bPressureSelected = true;
        else Runner->ChooseUpgrade(2);
    }
}
void AAegisPlayerController::ThirdUpgrade()
{
    if (auto* Lab = FindDecisionLab(GetWorld())) { if (!bMenuOpen) Lab->SetPolicy(TEXT("priority")); return; }
    if (!bMenuOpen) if (auto* Runner = FindInteractiveRunner(GetWorld())) Runner->ChooseUpgrade(3);
}
void AAegisPlayerController::PauseTrial()
{
    if (bRestartConfirmation) { bRestartConfirmation = false; return; }
    bPausedForFocus = false;
    if (auto* Controlled = Cast<AAegisPlayerCharacter>(GetPawn())) Controlled->CancelCharge();
    if (FindDecisionLab(GetWorld())) {
        bMenuOpen = !bMenuOpen; SetPause(bMenuOpen);
        if (PlayerInput) PlayerInput->FlushPressedKeys();
        return;
    }
    if (bPlannerOpen) { ClosePlannerPanel(); return; }
    if (auto* Runner = FindInteractiveRunner(GetWorld()))
    {
        bMenuOpen = !bMenuOpen;
        SetPause(bMenuOpen || Runner->IsUpgradePending());
        if (PlayerInput) PlayerInput->FlushPressedKeys();
    }
}
void AAegisPlayerController::Guard() { IssueCompanionCommand(0); }
void AAegisPlayerController::FocusOrQuit()
{
    if (bMenuOpen) MenuAction(TEXT("Quit"));
    else if (auto* Runner = FindInteractiveRunner(GetWorld()); Runner && Runner->GetTrial().finished())
    {
#if !UE_BUILD_SHIPPING
        UE_LOG(LogTemp, Display, TEXT("AEGIS_RESULT_X_QUIT outcome=%s"),
               Runner->GetTrial().phase == aegis::TrialPhase::Won ? TEXT("won") : TEXT("lost"));
#endif
        MenuAction(TEXT("Quit"));
    }
    else IssueCompanionCommand(1);
}

void AAegisPlayerController::ToggleLanguage()
{
    bEnglishUI = !bEnglishUI;
    auto Preferences = FAegisUIPreferences::Load(FAegisUIPreferences::DefaultPath(), FCommandLine::Get());
    Preferences.bEnglish = bEnglishUI;
    Preferences.bPersistent = bPersistUILanguage;
    const bool bSaved = Preferences.Save(FAegisUIPreferences::DefaultPath());
    if (!bSaved)
        CommandFeedback = bEnglishUI ? TEXT("English selected; preference could not be saved") : TEXT("已切换中文；语言偏好未能保存");
    else if (bPersistUILanguage)
        CommandFeedback = bEnglishUI ? TEXT("Language saved: English") : TEXT("界面语言已保存：中文");
    else
        CommandFeedback = bEnglishUI ? TEXT("Language: English") : TEXT("界面语言：中文");
    CommandFeedbackUntil = GetWorld() ? GetWorld()->GetTimeSeconds() + 2.0 : 0.0;
}
void AAegisPlayerController::ToggleWindowMode()
{
    if (!bPersistUILanguage || !GEngine) return;
    if (auto* Settings = GEngine->GetGameUserSettings())
    {
        Settings->SetFullscreenMode(Settings->GetFullscreenMode() == EWindowMode::Windowed
            ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
        Settings->ApplySettings(false);
        Settings->ConfirmVideoMode();
        Settings->SaveSettings();
    }
}
void AAegisPlayerController::ToggleEffects()
{
    bReducedEffects = !bReducedEffects;
    auto Preferences = FAegisUIPreferences::Load(FAegisUIPreferences::DefaultPath(), FCommandLine::Get());
    Preferences.bEnglish = bEnglishUI;
    Preferences.bReducedEffects = bReducedEffects;
    const bool Saved = Preferences.Save(FAegisUIPreferences::DefaultPath());
    CommandFeedback = bEnglishUI ? TEXT("Effects updated; gameplay cues remain visible") : TEXT("特效已更新；必要的战斗提示始终保留");
    if (!Saved) CommandFeedback = bEnglishUI ? TEXT("Effects changed; setting could not be saved") : TEXT("特效已切换；偏好未能保存");
    CommandFeedbackUntil = GetWorld() ? GetWorld()->GetTimeSeconds()+2 : 0;
}
void AAegisPlayerController::OnApplicationActivationChanged(bool bActive)
{
    // Returning focus never resumes combat: the player explicitly presses Esc.
    if (bActive || !GetWorld() || bMenuOpen || bPlannerOpen) return;
    auto* Runner = FindInteractiveRunner(GetWorld());
    if (!Runner || !AegisPortfolioPresentation::Enabled() || Runner->IsUpgradePending() ||
        Runner->GetTrial().phase == aegis::TrialPhase::Briefing || Runner->GetTrial().finished()) return;
    if (auto* Controlled = Cast<AAegisPlayerCharacter>(GetPawn())) Controlled->CancelCharge();
    if (PlayerInput) PlayerInput->FlushPressedKeys();
    bMenuOpen = bPausedForFocus = true;
    SetPause(true);
}
void AAegisPlayerController::Rally() { IssueCompanionCommand(2); }
void AAegisPlayerController::IssueCompanionCommand(int32 Mode)
{
    auto* Runner = FindInteractiveRunner(GetWorld());
    auto* ControlledCharacter = Cast<AAegisPlayerCharacter>(GetPawn());
    if (!Runner || !ControlledCharacter || bMenuOpen || IsPaused() || Runner->GetTrial().phase != aegis::TrialPhase::Active) return;
    if (auto* Planner = Runner->GetSquadPlanner()) Planner->CancelPlan(TEXT("manual override"));
    auto* Companion = Runner->GetCompanion();
    auto Feedback = [&](const TCHAR* Chinese, const TCHAR* English) {
        CommandFeedback = bEnglishUI ? English : Chinese;
        CommandFeedbackUntil = GetWorld()->GetTimeSeconds()+3;
    };
    if (!Companion || !Companion->Health->IsAlive()) { Feedback(TEXT("艾吉斯离线 · 指令不可用"), TEXT("AEGIS OFFLINE - command unavailable")); return; }
    auto* AI = Cast<AAegisAIController>(Companion->GetController());
    if (!AI) return;
    if (Mode == 0)
    {
        AI->SetCompanionCommand(EAegisCompanionCommand::Guard);
        Feedback(TEXT("防守已确认 · 艾吉斯将重新集结并掩护你"), TEXT("GUARD CONFIRMED - Aegis will regroup and cover you"));
        return;
    }
    FVector Point = ControlledCharacter->GetAimPoint();
    if (Mode == 1)
    {
        AAegisAICharacter* Target = nullptr;
        double Best = 400.f*400.f;
        for (TActorIterator<AAegisAICharacter> It(GetWorld()); It; ++It)
        {
            const double Distance = FVector::DistSquared2D(Point, It->GetActorLocation());
            if (It->Team != EAegisTeam::Enemy || !It->Health->IsAlive() || Distance >= Best || !LineOfSightTo(*It)) continue;
            Best = Distance; Target = *It;
        }
        if (!Target) { Feedback(TEXT("集火：瞄准可见敌人后按 X"), TEXT("FOCUS: aim near a visible hostile, then press X")); return; }
        AI->SetCompanionCommand(EAegisCompanionCommand::Focus, Target, Target->GetActorLocation());
        Feedback(TEXT("集火已确认 · 已标记威胁，需要保持视线"), TEXT("FOCUS CONFIRMED - marked threat, line of sight required"));
        return;
    }
    const FVector Origin = Runner->GetActorLocation();
    Point.X = FMath::Clamp(Point.X, Origin.X-1750, Origin.X+1750);
    Point.Y = FMath::Clamp(Point.Y, Origin.Y-1200, Origin.Y+1200);
    Point.Z = Origin.Z+50;
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    FNavLocation Projected;
    if (!Nav || !Nav->ProjectPointToNavigation(Point, Projected, FVector(180,180,250))) { Feedback(TEXT("集结：选择竞技场内的开阔地面"), TEXT("RALLY: choose open floor inside the arena")); return; }
    auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(this, Companion->GetActorLocation(), Projected.Location);
    if (!Path || !Path->IsValid() || Path->IsPartial()) { Feedback(TEXT("集结：艾吉斯无法到达标记位置"), TEXT("RALLY: Aegis cannot reach that position")); return; }
    AI->SetCompanionCommand(EAegisCompanionCommand::Rally, nullptr, Projected.Location);
    Feedback(TEXT("集结已确认 · 艾吉斯将坚守标记位置"), TEXT("RALLY CONFIRMED - Aegis will hold the marked position"));
}
void AAegisPlayerController::Diagnostics()
{
#if !UE_BUILD_SHIPPING
    bShowDiagnostics = !bShowDiagnostics;
#endif
}
