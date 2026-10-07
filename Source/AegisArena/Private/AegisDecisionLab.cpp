#include "AegisDecisionLab.h"
#include "AegisLab.h"
#include "AegisAIController.h"
#include "EngineUtils.h"
#include "Camera/CameraActor.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Navigation/PathFollowingComponent.h"

namespace
{
FString LabJson(const TSharedRef<FJsonObject>& J)
{
    FString S;
    auto W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&S);
    FJsonSerializer::Serialize(J, W);
    return S;
}
FString ActionName(aegis::Action A)
{
    const TCHAR* Names[] = {TEXT("follow"), TEXT("attack"), TEXT("support"), TEXT("retreat")};
    return Names[static_cast<int32>(A)];
}
TSharedRef<FJsonObject> NumberMap(const TMap<FString, double>& Values)
{
    auto J = MakeShared<FJsonObject>();
    for (const auto& V : Values)
        J->SetNumberField(V.Key, V.Value);
    return J;
}
} // namespace
AAegisDecisionLab::AAegisDecisionLab()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.1f;
}
void AAegisDecisionLab::Initialize(AAegisScenarioRunner* InAssets)
{
    Assets = InAssets;
    const TCHAR* Cmd = FCommandLine::Get();
    FParse::Value(Cmd, TEXT("AegisLabPolicy="), Policy);
    FParse::Value(Cmd, TEXT("AegisLabSeed="), Seed);
    FParse::Value(Cmd, TEXT("AegisLabEnemies="), EnemyCount);
    FParse::Value(Cmd, TEXT("AegisLabEpisodes="), Episodes);
    FParse::Value(Cmd, TEXT("AegisLabDuration="), Duration);
    FParse::Value(Cmd, TEXT("AegisLabLayout="), Layout);
    FParse::Value(Cmd, TEXT("AegisLabOutput="), Output);
    bAutomated = FParse::Param(Cmd, TEXT("AegisLabAutomated"));
    bCapture = FParse::Param(Cmd, TEXT("AegisLabCapture")) && !FParse::Param(Cmd, TEXT("NullRHI"));
    if (!Assets || !Assets->Behavior || !Assets->RetreatQuery || EnemyCount < 1 || EnemyCount > 8 ||
        Episodes < 1 || Episodes > 100 || Duration < 1 || Duration > 180 || Layout < 0 || Layout > 1 ||
        (Policy != TEXT("baseline") && Policy != TEXT("improved") && Policy != TEXT("priority")))
    {
        UE_LOG(LogTemp, Error, TEXT("AEGIS_DECISION_LAB_INVALID_CONFIG"));
        FPlatformMisc::RequestExitWithStatus(false, 2, TEXT("Decision lab configuration"));
        return;
    }
    FirstSeed = Seed;
    if (!Output.IsEmpty())
        IFileManager::Get().MakeDirectory(*Output, true);
    if (auto* PC = Cast<AAegisPlayerController>(GetWorld()->GetFirstPlayerController()))
    {
        PC->bShowDiagnostics = bCapture;
        if (auto* Pawn = Cast<AAegisPlayerCharacter>(PC->GetPawn()))
            Pawn->bCombatEnabled = false;
    }
    if (bAutomated)
        StartEncounter();
}
double AAegisDecisionLab::GetElapsed() const
{
    if (Phase == TEXT("Briefing"))
        return 0;
    return (Phase == TEXT("Active") ? GetWorld()->GetTimeSeconds() : FinishedAt) - StartedAt;
}
AAegisAIController* AAegisDecisionLab::GetCompanionController() const
{
    return Companion ? Cast<AAegisAIController>(Companion->GetController()) : nullptr;
}
void AAegisDecisionLab::SetPolicy(const FString& Value)
{
    if (Phase != TEXT("Active") &&
        (Value == TEXT("baseline") || Value == TEXT("improved") || Value == TEXT("priority")))
        Policy = Value;
}
void AAegisDecisionLab::ClearEncounter()
{
    for (AAegisCharacter* A : Actors)
        if (IsValid(A))
        {
            AController* C = A->GetController();
            A->Destroy();
            if (C && !C->IsPlayerController())
                C->Destroy();
        }
    Actors.Reset();
    Player = nullptr;
    Companion = nullptr;
}
void AAegisDecisionLab::ResetEncounter()
{
    if (bAutomated)
        return;
    if (auto* PC = Cast<AAegisPlayerController>(GetWorld()->GetFirstPlayerController()))
    {
        PC->SetPause(false);
        PC->bMenuOpen = false;
        if (PC->PlayerInput)
            PC->PlayerInput->FlushPressedKeys();
    }
    ClearEncounter();
    Phase = TEXT("Briefing");
    EnemyDeaths = 0;
}
AAegisAICharacter* AAegisDecisionLab::SpawnBot(FVector Position, EAegisTeam Team, bool IsCompanion,
                                               int32 Index)
{
    if (Layout == 1)
    {
        Position.X = -Position.X;
        Position.Y = -Position.Y;
    }
    const FTransform T(FRotator(0, (Team == EAegisTeam::Enemy ? 180.f : 0.f) + Layout * 180.f, 0),
                       Assets->GetActorLocation() + Position);
    auto* B = GetWorld()->SpawnActorDeferred<AAegisAICharacter>(
        AAegisAICharacter::StaticClass(), T, this, nullptr,
        ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
    if (!B)
        return nullptr;
    B->Team = Team;
    B->bCompanion = IsCompanion;
    B->bUtilityCompanionPolicy = Policy != TEXT("priority");
    B->bTacticalTrial = false;
    B->bElite = false;
    B->DecisionSeed = Seed ^ (Index * 7919);
    B->Behavior = Assets->Behavior;
    B->CoverQuery = Assets->CoverQuery;
    B->AttackQuery = Assets->AttackQuery;
    B->RetreatQuery = Assets->RetreatQuery;
    B->FinishSpawning(T);
    if (auto* AI = Cast<AAegisAIController>(B->GetController()))
        AI->Decision->bImprovedPolicy = IsCompanion && Policy == TEXT("improved");
    Actors.Add(B);
    B->Health->OnDamaged.AddDynamic(this, &AAegisDecisionLab::RecordDamage);
    return B;
}
void AAegisDecisionLab::StartEncounter()
{
    if (Phase == TEXT("Active"))
        return;
    ClearEncounter();
    auto* PC = Cast<AAegisPlayerController>(GetWorld()->GetFirstPlayerController());
    if (PC)
    {
        PC->bMenuOpen = false;
        PC->SetPause(false);
        if (PC->PlayerInput)
            PC->PlayerInput->FlushPressedKeys();
        if (APawn* Old = PC->GetPawn())
        {
            PC->UnPossess();
            Old->Destroy();
        }
    }
    PlayerDamageTaken = CompanionDamageTaken = PlayerDamageDealt = CompanionDamageDealt = 0;
    PlayerHealing = CompanionHealing = LastPlayerDamage = LastCompanionDamage = 0;
    LastPlayerHP = LastCompanionHP = 100;
    PlayerDeathAt = CompanionDeathAt = -1;
    CompanionAliveSeconds = CompanionStuckSeconds = 0;
    EnemyDeaths = 0;
    LastDecision = -1;
    ActualSeconds.Reset();
    SelectedSeconds.Reset();
    RecordedDeaths.Reset();
    Trace.Reset();
    LastQueryKey.Reset();
    CaptureIndex = 0;
    NextCapture = 0.3;
    StartedAt = GetWorld()->GetTimeSeconds();
    LastSampleAt = StartedAt;
    Phase = TEXT("Active");
    Event(TEXT("episode_start"), MakeShared<FJsonObject>());
    if (bAutomated)
    {
        if (PC)
            PC->bAutoManageActiveCameraTarget = false;
        Player = SpawnBot(FVector(-1200, 0, 100), EAegisTeam::Player, false, 0);
        if (PC)
            for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
                if (It->ActorHasTag(TEXT("AegisEvaluationCamera")))
                {
                    PC->SetViewTarget(*It);
                    break;
                }
    }
    else
    {
        const FVector P = Layout == 0 ? FVector(-1200, 0, 100) : FVector(1200, 0, 100);
        auto* Human = GetWorld()->SpawnActor<AAegisPlayerCharacter>(Assets->GetActorLocation() + P,
                                                                    FRotator::ZeroRotator);
        Player = Human;
        if (Human)
        {
            Human->SpringArm->TargetArmLength = 1850;
            Human->bCombatEnabled = true;
            Human->bPulseEnabled = false;
            Actors.Add(Human);
            Human->Health->OnDamaged.AddDynamic(this, &AAegisDecisionLab::RecordDamage);
            if (PC)
                PC->Possess(Human);
        }
    }
    Companion = SpawnBot(FVector(-1400, 200, 100), EAegisTeam::Player, true, 1);
    FRandomStream Random(Seed);
    for (int32 I = 0; I < EnemyCount; ++I)
        SpawnBot(FVector(1100 + Random.FRandRange(0, 500), Random.FRandRange(-1000, 1000), 100),
                 EAegisTeam::Enemy, false, I + 2);
    if (!Player || !Companion || Actors.Num() != EnemyCount + 2)
    {
        UE_LOG(LogTemp, Error, TEXT("AEGIS_DECISION_LAB_SPAWN_FAILED"));
        FPlatformMisc::RequestExitWithStatus(false, 3, TEXT("Decision lab spawn"));
        return;
    }
    if (auto* AI = GetCompanionController())
        AI->SetFollowPositionLink(Player);
    LastCompanionPosition = Companion->GetActorLocation();
    PreviousLeaf = TEXT("waiting");
    PreviousSelected = TEXT("none");
}
FString AAegisDecisionLab::Role(const AActor* A) const
{
    return A == Player ? TEXT("player") : A == Companion ? TEXT("companion") : TEXT("enemy");
}
void AAegisDecisionLab::Event(const FString& Kind, const TSharedRef<FJsonObject>& J)
{
    if (Output.IsEmpty())
        return;
    J->SetNumberField(TEXT("timeSeconds"), GetElapsed());
    J->SetNumberField(TEXT("worldGameSeconds"), GetWorld()->GetTimeSeconds());
    J->SetStringField(TEXT("event"), Kind);
    Trace += LabJson(J) + TEXT("\n");
}
void AAegisDecisionLab::RecordDamage(float Amount, AActor* Source, AActor* Victim)
{
    if (Phase != TEXT("Active"))
        return;
    if (Victim == Player)
        PlayerDamageTaken += Amount;
    if (Victim == Companion)
        CompanionDamageTaken += Amount;
    if (Source == Player)
        PlayerDamageDealt += Amount;
    if (Source == Companion)
        CompanionDamageDealt += Amount;
    auto J = MakeShared<FJsonObject>();
    J->SetStringField(TEXT("source"), Role(Source));
    J->SetStringField(TEXT("victim"), Role(Victim));
    J->SetNumberField(TEXT("amount"), Amount);
    Event(TEXT("damage"), J);
    if (auto* A = Cast<AAegisCharacter>(Victim))
        if (!A->Health->IsAlive() && !RecordedDeaths.Contains(A))
        {
            RecordedDeaths.Add(A);
            auto D = MakeShared<FJsonObject>();
            D->SetStringField(TEXT("actor"), Role(A));
            Event(TEXT("death"), D);
            if (A == Player)
                PlayerDeathAt = GetElapsed();
            if (A == Companion)
                CompanionDeathAt = GetElapsed();
        }
}
void AAegisDecisionLab::Sample()
{
    const double Now = GetWorld()->GetTimeSeconds(), Dt = Now - LastSampleAt;
    auto Healing = [&](AAegisCharacter* A, double Damage, double& LastHP, double& LastDamage, double& Total)
    {
        const double H =
            FMath::Max(0., static_cast<double>(A->Health->Current) - LastHP + Damage - LastDamage);
        if (H > 0.001)
        {
            Total += H;
            auto J = MakeShared<FJsonObject>();
            J->SetStringField(TEXT("source"), TEXT("health_accounting"));
            J->SetStringField(TEXT("victim"), Role(A));
            J->SetNumberField(TEXT("amount"), H);
            Event(TEXT("heal"), J);
        }
        LastHP = A->Health->Current;
        LastDamage = Damage;
    };
    Healing(Player, PlayerDamageTaken, LastPlayerHP, LastPlayerDamage, PlayerHealing);
    Healing(Companion, CompanionDamageTaken, LastCompanionHP, LastCompanionDamage, CompanionHealing);
    EnemyDeaths = 0;
    for (AAegisCharacter* A : Actors)
        if (A && !A->Health->IsAlive())
        {
            if (A->Team == EAegisTeam::Enemy)
                ++EnemyDeaths;
            if (!RecordedDeaths.Contains(A))
            {
                RecordedDeaths.Add(A);
                auto J = MakeShared<FJsonObject>();
                J->SetStringField(TEXT("actor"), Role(A));
                Event(TEXT("death"), J);
                if (A == Player)
                    PlayerDeathAt = GetElapsed();
                if (A == Companion)
                    CompanionDeathAt = GetElapsed();
            }
        }
    if (auto* AI = GetCompanionController())
    {
        const auto& T = AI->GetDecisionTelemetry();
        // Left-endpoint sampled dwell; death truncates exposure exactly. Historical
        // accepted leaves become waiting when stale or superseded by a failed attempt.
        const double AliveDt = FMath::Max(
            0., FMath::Min(Now, CompanionDeathAt < 0 ? Now : StartedAt + CompanionDeathAt) - LastSampleAt);
        CompanionAliveSeconds += AliveDt;
        ActualSeconds.FindOrAdd(PreviousLeaf) += AliveDt;
        SelectedSeconds.FindOrAdd(PreviousSelected) += AliveDt;
        if (AI->GetMoveStatus() == EPathFollowingStatus::Moving &&
            FVector::Dist2D(LastCompanionPosition, Companion->GetActorLocation()) < 2)
            CompanionStuckSeconds += AliveDt;
        PreviousLeaf = T.BTLeafSucceeded && T.AcceptedBTLeafAt >= 0 && Now - T.AcceptedBTLeafAt <= 0.35
                           ? T.AcceptedBTLeaf.ToString()
                           : TEXT("waiting");
        PreviousSelected = T.DecisionSequence > 0 ? ActionName(T.Selected) : TEXT("none");
        LastCompanionPosition = Companion->GetActorLocation();
        const FString QueryKey =
            FString::Printf(TEXT("%d/%s/%.4f"), T.QueryId, *T.QueryStatus, T.QueryCompletedAt);
        if (QueryKey != LastQueryKey)
        {
            LastQueryKey = QueryKey;
            auto Q = MakeShared<FJsonObject>();
            Q->SetNumberField(TEXT("id"), T.QueryId);
            Q->SetStringField(TEXT("status"), T.QueryStatus);
            Q->SetStringField(TEXT("name"), T.QueryName);
            Q->SetNumberField(TEXT("decisionSequence"), T.QueryDecisionSequence);
            Q->SetNumberField(TEXT("completedAt"), T.QueryCompletedAt);
            Q->SetBoolField(TEXT("moveAccepted"), T.bQueryMoveAccepted);
            Q->SetStringField(TEXT("point"), T.QueryAcceptedPoint.ToString());
            Event(TEXT("eqs"), Q);
        }
        if (LastDecision != T.DecisionSequence && T.DecisionSequence > 0)
        {
            LastDecision = T.DecisionSequence;
            auto J = MakeShared<FJsonObject>();
            auto O = MakeShared<FJsonObject>();
            O->SetNumberField(TEXT("health"), T.Observation.health);
            O->SetNumberField(TEXT("allyHealth"), T.Observation.allyHealth);
            O->SetNumberField(TEXT("allyDistance"), T.Observation.allyDistance);
            O->SetNumberField(TEXT("targetDistance"), T.Observation.targetDistance);
            O->SetBoolField(TEXT("targetVisible"), T.Observation.targetVisible);
            O->SetBoolField(TEXT("targetRemembered"), T.Observation.targetRemembered);
            O->SetBoolField(TEXT("allyKnown"), T.Observation.allyKnown);
            O->SetBoolField(TEXT("supportReady"), T.Observation.supportReady);
            auto Position = MakeShared<FJsonObject>();
            const FVector SelfPosition = Companion->GetActorLocation();
            Position->SetNumberField(TEXT("x"), SelfPosition.X);
            Position->SetNumberField(TEXT("y"), SelfPosition.Y);
            Position->SetNumberField(TEXT("z"), SelfPosition.Z);
            J->SetObjectField(TEXT("selfPosition"), Position);
            J->SetNumberField(TEXT("navigationStatus"), static_cast<int32>(AI->GetMoveStatus()));
            J->SetObjectField(TEXT("observation"), O);
            auto L = MakeShared<FJsonObject>();
            L->SetBoolField(TEXT("registered"), T.FollowLinkAvailable);
            L->SetStringField(TEXT("executionSource"), T.FollowTargetSource);
            L->SetNumberField(TEXT("executionSampleGameSeconds"), T.FollowTargetSampleGameSeconds);
            L->SetNumberField(TEXT("sampleGameSeconds"), T.FollowLinkSampleGameSeconds);
            if (T.FollowLinkSampleGameSeconds >= 0)
            {
                L->SetStringField(TEXT("position"), T.FollowLinkPosition.ToString());
                L->SetNumberField(TEXT("distance"), T.FollowLinkDistance);
            }
            J->SetObjectField(TEXT("allyPositionLink"), L);
            auto S = MakeShared<FJsonObject>();
            for (int32 I = 0; I < 4; ++I)
                S->SetNumberField(ActionName(static_cast<aegis::Action>(I)), T.Scores[I]);
            J->SetObjectField(TEXT("scores"), S);
            J->SetNumberField(TEXT("sequence"), T.DecisionSequence);
            J->SetNumberField(TEXT("observationGameSeconds"), T.GameSeconds);
            J->SetStringField(TEXT("selected"), ActionName(T.Selected));
            J->SetStringField(TEXT("reason"), T.SelectionReason);
            J->SetStringField(TEXT("executed"), T.AcceptedBTLeaf.ToString());
            J->SetNumberField(TEXT("executedGameSeconds"), T.AcceptedBTLeafAt);
            J->SetNumberField(TEXT("btExecutedAt"), T.BTExecutedAt);
            J->SetNumberField(TEXT("btExecutionSequence"), T.BTExecutionSequence);
            J->SetStringField(TEXT("attempted"), T.BTLeaf.ToString());
            J->SetBoolField(TEXT("attemptAccepted"), T.BTLeafSucceeded);
            J->SetNumberField(TEXT("btDecisionSequence"), T.BTDecisionSequence);
            J->SetNumberField(TEXT("switches"), T.SwitchCount);
            J->SetNumberField(TEXT("shortReversals"), T.ShortReversalCount);
            J->SetNumberField(TEXT("supportHealAmount"), T.SupportHealAmount);
            auto Q = MakeShared<FJsonObject>();
            Q->SetNumberField(TEXT("id"), T.QueryId);
            Q->SetStringField(TEXT("name"), T.QueryName);
            Q->SetStringField(TEXT("status"), T.QueryStatus);
            Q->SetNumberField(TEXT("decisionSequence"), T.QueryDecisionSequence);
            Q->SetNumberField(TEXT("returnedItems"), T.QueryReturnedItems);
            Q->SetNumberField(TEXT("generatedItems"), T.QueryGeneratedItems);
            Q->SetNumberField(TEXT("validItems"), T.QueryValidItems);
            Q->SetBoolField(TEXT("moveAccepted"), T.bQueryMoveAccepted);
            Q->SetNumberField(TEXT("completedAt"), T.QueryCompletedAt);
            Q->SetStringField(TEXT("point"), T.QueryAcceptedPoint.ToString());
            J->SetObjectField(TEXT("eqs"), Q);
            Event(TEXT("decision"), J);
        }
    }
    LastSampleAt = Now;
}
void AAegisDecisionLab::Tick(float Dt)
{
    Super::Tick(Dt);
    if (bAdvance)
    {
        bAdvance = false;
        ++Episode;
        Seed = FirstSeed + Episode;
        StartEncounter();
        return;
    }
    if (Phase != TEXT("Active"))
        return;
    Sample();
    if (bCapture && !Output.IsEmpty() && GetElapsed() >= NextCapture)
    {
        const FString Directory = FPaths::Combine(Output, TEXT("frames"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        FScreenshotRequest::RequestScreenshot(
            FPaths::Combine(Directory, FString::Printf(TEXT("frame-%04d.png"), CaptureIndex)), true, false);
        auto J = MakeShared<FJsonObject>();
        J->SetNumberField(TEXT("frame"), CaptureIndex++);
        J->SetNumberField(TEXT("decisionSequence"), LastDecision);
        Event(TEXT("capture_requested"), J);
        NextCapture = GetElapsed() + 0.25;
    }
    if (!Player->Health->IsAlive())
        Finish(TEXT("player_dead"));
    else if (EnemyDeaths == EnemyCount)
        Finish(TEXT("enemy_clear"));
    else if (GetElapsed() >= Duration)
        Finish(TEXT("timeout"));
}
void AAegisDecisionLab::Finish(const FString& Reason)
{
    FinishedAt = GetWorld()->GetTimeSeconds();
    const bool Win = Reason == TEXT("enemy_clear");
    Phase = Win ? TEXT("Won") : Reason == TEXT("timeout") ? TEXT("Timeout") : TEXT("Lost");
    auto End = MakeShared<FJsonObject>();
    End->SetStringField(TEXT("terminationReason"), Reason);
    End->SetBoolField(TEXT("win"), Win);
    Event(TEXT("episode_end"), End);
    auto S = MakeShared<FJsonObject>();
    S->SetStringField(TEXT("policy"), Policy);
    S->SetNumberField(TEXT("seed"), Seed);
    S->SetNumberField(TEXT("enemyCount"), EnemyCount);
    S->SetNumberField(TEXT("duration"), Duration);
    S->SetNumberField(TEXT("layout"), Layout);
    S->SetStringField(TEXT("scenarioId"), TEXT("decision_lab_v1"));
    auto M = MakeShared<FJsonObject>();
    M->SetNumberField(TEXT("playerDamageTaken"), PlayerDamageTaken);
    M->SetNumberField(TEXT("companionDamageTaken"), CompanionDamageTaken);
    M->SetNumberField(TEXT("playerDamageDealt"), PlayerDamageDealt);
    M->SetNumberField(TEXT("companionDamageDealt"), CompanionDamageDealt);
    M->SetNumberField(TEXT("playerHealingReceived"), PlayerHealing);
    M->SetNumberField(TEXT("companionHealingReceived"), CompanionHealing);
    M->SetNumberField(TEXT("playerDeathSeconds"), PlayerDeathAt);
    M->SetNumberField(TEXT("companionDeathSeconds"), CompanionDeathAt);
    M->SetNumberField(TEXT("companionAliveSeconds"), CompanionAliveSeconds);
    M->SetNumberField(TEXT("companionStuckSeconds"), CompanionStuckSeconds);
    M->SetNumberField(TEXT("companionShots"), Companion->Combat->RangedShotsFired);
    M->SetObjectField(TEXT("companionActionSeconds"), NumberMap(ActualSeconds));
    M->SetObjectField(TEXT("companionSelectedSeconds"), NumberMap(SelectedSeconds));
    if (auto* AI = GetCompanionController())
    {
        const auto& T = AI->GetDecisionTelemetry();
        M->SetNumberField(TEXT("companionDecisions"), T.DecisionSequence);
        M->SetNumberField(TEXT("companionActionSwitches"), T.SwitchCount);
        M->SetNumberField(TEXT("companionShortReversals"), T.ShortReversalCount);
        M->SetNumberField(TEXT("companionSupportHeals"), T.SupportHealAmount);
        M->SetNumberField(TEXT("companionSupportHealCount"), T.SupportHealCount);
    }
    auto J = MakeShared<FJsonObject>();
    J->SetNumberField(TEXT("schemaVersion"), 1);
    J->SetStringField(TEXT("engine"), TEXT("unreal-runtime"));
    J->SetBoolField(TEXT("automated"), bAutomated);
    J->SetObjectField(TEXT("scenario"), S);
    J->SetBoolField(TEXT("win"), Win);
    J->SetStringField(TEXT("terminationReason"), Reason);
    J->SetNumberField(TEXT("elapsedGameSeconds"), GetElapsed());
    J->SetBoolField(TEXT("playerAlive"), Player->Health->IsAlive());
    J->SetBoolField(TEXT("companionAlive"), Companion->Health->IsAlive());
    J->SetNumberField(TEXT("enemiesAlive"), EnemyCount - EnemyDeaths);
    J->SetObjectField(TEXT("metrics"), M);
    if (!Output.IsEmpty())
    {
        FFileHelper::SaveStringToFile(
            LabJson(J), *FPaths::Combine(Output, FString::Printf(TEXT("episode-%03d.json"), Episode)),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        FFileHelper::SaveStringToFile(
            Trace, *FPaths::Combine(Output, FString::Printf(TEXT("decision-%03d.jsonl"), Episode)),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
    for (AAegisCharacter* A : Actors)
        if (auto* AI = Cast<AAegisAIController>(A->GetController()))
            AI->ShutdownAI();
    if (auto* Human = Cast<AAegisPlayerCharacter>(Player))
        Human->bCombatEnabled = false;
    if (bAutomated && Episode + 1 < Episodes)
    {
        bAdvance = true;
        return;
    }
    if (bAutomated)
    {
        auto R = MakeShared<FJsonObject>();
        R->SetNumberField(TEXT("schemaVersion"), 1);
        R->SetStringField(TEXT("engine"), TEXT("unreal-runtime"));
        R->SetBoolField(TEXT("completed"), true);
        R->SetNumberField(TEXT("episodes"), Episodes);
        R->SetStringField(TEXT("policy"), Policy);
        R->SetNumberField(TEXT("firstSeed"), FirstSeed);
        R->SetNumberField(TEXT("enemyCount"), EnemyCount);
        R->SetNumberField(TEXT("duration"), Duration);
        R->SetNumberField(TEXT("layout"), Layout);
        R->SetStringField(TEXT("scenarioId"), TEXT("decision_lab_v1"));
        if (!Output.IsEmpty())
            FFileHelper::SaveStringToFile(LabJson(R), *FPaths::Combine(Output, TEXT("results.json")),
                                          FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTemp, Display, TEXT("AEGIS_DECISION_LAB_COMPLETE episodes=%d"), Episodes);
        FPlatformMisc::RequestExit(false);
    }
}
