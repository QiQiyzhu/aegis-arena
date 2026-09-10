#include "AegisAIController.h"
#include "AegisCharacter.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Damage.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "NavigationSystem.h"
#include "BrainComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#if USE_EQS_DEBUGGER && !UE_BUILD_SHIPPING
#include "DrawDebugHelpers.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif

UAegisDecisionComponent::UAegisDecisionComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}
void UAegisDecisionComponent::Evaluate(const aegis::Observation& O)
{
    Scores = aegis::utility(O);
    Selected = bUtilityPolicy ? aegis::choose(Scores, Selected)
                              : (O.targetVisible ? aegis::Action::Attack : aegis::Action::Follow);
    ++DecisionCount;
}
AAegisAIController::AAegisAIController()
{
    // EQS Querier reads the owner's actor transform. Our owner is this
    // controller, so it must follow the possessed pawn rather than its spawn.
    bAttachToPawn = true;
    PrimaryActorTick.bCanEverTick = false;
    Senses = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Perception"));
    SetPerceptionComponent(*Senses);
    Decision = CreateDefaultSubobject<UAegisDecisionComponent>(TEXT("Decision"));
    auto* Sight = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("Sight"));
    Sight->SightRadius = 1500;
    Sight->LoseSightRadius = 1700;
    Sight->PeripheralVisionAngleDegrees = 100;
    Sight->SetMaxAge(2.5f);
    Sight->DetectionByAffiliation.bDetectEnemies = true;
    Sight->DetectionByAffiliation.bDetectFriendlies = true;
    Sight->DetectionByAffiliation.bDetectNeutrals = false;
    Senses->ConfigureSense(*Sight);
    Senses->SetDominantSense(UAISense_Sight::StaticClass());
    auto* Damage = CreateDefaultSubobject<UAISenseConfig_Damage>(TEXT("Damage"));
    Damage->SetMaxAge(2.5f);
    Senses->ConfigureSense(*Damage);
}
ETeamAttitude::Type AAegisAIController::GetTeamAttitudeTowards(const AActor& Other) const
{
    const auto* Self = Cast<AAegisCharacter>(GetPawn());
    const auto* Target = Cast<AAegisCharacter>(&Other);
    if (!Self || !Target || Target->Team == EAegisTeam::Neutral)
        return ETeamAttitude::Neutral;
    return Self->IsHostile(Target) ? ETeamAttitude::Hostile : ETeamAttitude::Friendly;
}
void AAegisAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);
    auto* Bot = Cast<AAegisAICharacter>(InPawn);
    if (!Bot)
        return;
    PatrolRandom.Initialize(Bot->DecisionSeed);
    Decision->bUtilityPolicy = Bot->bUtilityCompanionPolicy;
    if (Bot->bCompanion)
        Bot->Team = EAegisTeam::Player;
    SetGenericTeamId(FGenericTeamId(static_cast<uint8>(Bot->Team)));
    Senses->OnTargetPerceptionUpdated.AddUniqueDynamic(this, &AAegisAIController::PerceptionChanged);
    if (Bot->Behavior)
    {
        UBlackboardComponent* InitialBlackboard = nullptr;
        if (Bot->Behavior->BlackboardAsset &&
            UseBlackboard(Bot->Behavior->BlackboardAsset, InitialBlackboard))
            InitialBlackboard->SetValueAsBool(TEXT("IsCompanion"), Bot->bCompanion);
        RunBehaviorTree(Bot->Behavior);
        DebugState = TEXT("Patrol");
    }
    else
    {
        DebugState = TEXT("Missing BT asset");
        UE_LOG(LogTemp, Error, TEXT("Aegis %s has no Behavior asset; see docs/unreal-setup.md"),
               *GetNameSafe(Bot));
    }
}
void AAegisAIController::OnUnPossess()
{
    ShutdownAI();
    Senses->OnTargetPerceptionUpdated.RemoveDynamic(this, &AAegisAIController::PerceptionChanged);
    Super::OnUnPossess();
}
void AAegisAIController::PerceptionChanged(AActor* Actor, FAIStimulus Stimulus)
{
    auto* Self = Cast<AAegisCharacter>(GetPawn());
    auto* Seen = Cast<AAegisCharacter>(Actor);
    if (!Self || !Seen || !Self->IsHostile(Seen))
        return;
    // A damage event may reveal a historical location, never grant current visual tracking.
    if (Stimulus.WasSuccessfullySensed())
    {
        LastKnown = Stimulus.StimulusLocation;
        LastSensedAt = GetWorld()->GetTimeSeconds();
    }
}
void AAegisAIController::RefreshDecision()
{
    auto* Self = Cast<AAegisAICharacter>(GetPawn());
    auto* BB = GetBlackboardComponent();
    if (bPaused || !Self || !Self->Health->IsAlive() || !BB)
        return;
    TRACE_CPUPROFILER_EVENT_SCOPE(Aegis_ObserveAndUtility);
    const double CpuStarted = FPlatformTime::Seconds();
    TArray<AActor*> Visible;
    Senses->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Visible);
    ObservedTarget.Reset();
    ObservedAlly.Reset();
    double Best = DBL_MAX;
    for (AActor* Actor : Visible)
    {
        auto* C = Cast<AAegisCharacter>(Actor);
        if (!C || !C->Health->IsAlive() || C == Self)
            continue;
        if (Self->IsHostile(C))
        {
            const double D = FVector::DistSquared(C->GetActorLocation(), Self->GetActorLocation());
            if (D < Best)
            {
                Best = D;
                ObservedTarget = C;
            }
        }
        else if (C->Team == EAegisTeam::Player)
        {
            const auto* AlliedBot = Cast<AAegisAICharacter>(C);
            if (!AlliedBot || !AlliedBot->bCompanion)
                ObservedAlly = C;
        }
    }
    bTargetVisible = ObservedTarget.IsValid();
    if (bTargetVisible)
    {
        LastKnown = ObservedTarget->GetActorLocation();
        LastSensedAt = GetWorld()->GetTimeSeconds();
    }
    const bool Memory = GetWorld()->GetTimeSeconds() - LastSensedAt < 2.5;
    aegis::Observation O;
    O.health = Self->Health->Current / Self->Health->Maximum;
    O.targetVisible = bTargetVisible;
    O.targetRemembered = Memory;
    O.targetDistance = bTargetVisible ? FVector::Dist(Self->GetActorLocation(), LastKnown) / 100 : 1000;
    O.allyKnown = ObservedAlly.IsValid();
    if (O.allyKnown)
    {
        O.allyHealth = ObservedAlly->Health->Current / ObservedAlly->Health->Maximum;
        O.allyDistance = FVector::Dist(Self->GetActorLocation(), ObservedAlly->GetActorLocation()) / 100;
    }
    O.supportReady = GetWorld()->GetTimeSeconds() >= SupportReadyAt;
    Decision->Evaluate(O);
    BB->SetValueAsObject(TEXT("TargetActor"), ObservedTarget.Get());
    BB->SetValueAsVector(TEXT("LastKnownLocation"), LastKnown);
    BB->SetValueAsBool(TEXT("HasLOS"), bTargetVisible);
    BB->SetValueAsBool(TEXT("HasMemory"), Memory);
    BB->SetValueAsBool(TEXT("CriticalHealth"), O.health < 0.28);
    BB->SetValueAsBool(TEXT("NeedsRecovery"), O.health < 0.65 && !Memory);
    BB->SetValueAsBool(TEXT("NeedsCover"),
                       O.health < 0.65 && bTargetVisible && Self->Combat->CooldownRemaining() > 0);
    BB->SetValueAsBool(TEXT("InRange"), bTargetVisible && O.targetDistance < 9);
    BB->SetValueAsBool(TEXT("IsCompanion"), Self->bCompanion);
    BB->SetValueAsInt(TEXT("UtilityAction"), static_cast<int32>(Decision->Selected));
    BB->SetValueAsBool(TEXT("QueryPending"), QueryId != INDEX_NONE);
    DecisionCpuSeconds += FPlatformTime::Seconds() - CpuStarted;
}
bool AAegisAIController::RequestTacticalPoint(UEnvQuery* Query)
{
    if (!Query || !GetPawn() || QueryId != INDEX_NONE || GetWorld()->GetTimeSeconds() < NextQueryAt)
        return false;
    NextQueryAt = GetWorld()->GetTimeSeconds() + 1;
    bHasTacticalPoint = false;
    FEnvQueryRequest Request(Query, this);
    auto* Manager = UEnvQueryManager::GetCurrent(GetWorld());
    if (!Manager)
        return false;
    ActiveQuery = Manager->PrepareQueryInstance(Request, EEnvQueryRunMode::SingleResult);
    if (!ActiveQuery)
        return false;
#if USE_EQS_DEBUGGER && !UE_BUILD_SHIPPING
    // Explicit opt-in: retaining candidate data changes query memory/cost, so it
    // is never enabled for policy or performance measurements by default.
    ActiveQuery->bStoreDebugInfo = DiagnosticQueries < 50 &&
        FParse::Param(FCommandLine::Get(), TEXT("AegisQueryDiagnostics"));
#endif
    const auto* Bot = Cast<AAegisAICharacter>(GetPawn());
    bPendingCoverQuery = Bot && Query == Bot->CoverQuery;
    QueryId = Manager->RunQuery(
        ActiveQuery, FQueryFinishedSignature::CreateUObject(this, &AAegisAIController::QueryFinished));
    if (auto* BB = GetBlackboardComponent())
        BB->SetValueAsBool(TEXT("QueryPending"), QueryId != INDEX_NONE);
    return QueryId != INDEX_NONE;
}
void AAegisAIController::QueryFinished(TSharedPtr<FEnvQueryResult> Result)
{
    if (!Result.IsValid() || Result->QueryID != QueryId)
        return;
    ++CompletedQueries;
    if (ActiveQuery && ActiveQuery->QueryID == QueryId)
    {
        QueryCpuSeconds += ActiveQuery->TotalExecutionTime;
        ++ProfiledQueries;
#if USE_EQS_DEBUGGER && !UE_BUILD_SHIPPING
        if (ActiveQuery->bStoreDebugInfo)
        {
            ++DiagnosticQueries;
            const auto& Debug = ActiveQuery->DebugData;
            auto Record = MakeShared<FJsonObject>();
            Record->SetStringField(TEXT("controller"), GetName());
            Record->SetStringField(TEXT("query"), ActiveQuery->QueryName);
            Record->SetStringField(TEXT("btTask"), DebugState);
            Record->SetStringField(TEXT("controllerLocation"), static_cast<const AActor*>(this)->GetActorLocation().ToCompactString());
            Record->SetStringField(TEXT("pawnLocation"), GetPawn() ? GetPawn()->GetActorLocation().ToCompactString() : TEXT("none"));
            Record->SetStringField(TEXT("threatLocation"), LastKnown.ToCompactString());
            Record->SetNumberField(TEXT("queryId"), QueryId);
            Record->SetNumberField(TEXT("gameSeconds"), GetWorld()->GetTimeSeconds());
            Record->SetBoolField(TEXT("successful"), Result->IsSuccessful());
            Record->SetBoolField(TEXT("aborted"), Result->IsAborted());
            Record->SetNumberField(TEXT("returnedItems"), Result->Items.Num());
            TArray<TSharedPtr<FJsonValue>> Tests, Items;
            for (const auto& Name : Debug.PerformedTestNames)
                Tests.Add(MakeShared<FJsonValueString>(Name));
            int32 Valid = 0;
            for (int32 I = 0; I < Debug.DebugItems.Num(); ++I)
            {
                const auto& Item = Debug.DebugItems[I];
                auto Row = MakeShared<FJsonObject>();
                Row->SetNumberField(TEXT("index"), I);
                Row->SetBoolField(TEXT("valid"), Item.IsValid());
                Row->SetNumberField(TEXT("score"), Item.Score);
                if (Debug.DebugItemDetails.IsValidIndex(I))
                {
                    const auto& Detail = Debug.DebugItemDetails[I];
                    Row->SetNumberField(TEXT("failedTestIndex"), Detail.FailedTestIndex);
                    Row->SetStringField(TEXT("failure"), Detail.FailedDescription);
                    TArray<TSharedPtr<FJsonValue>> Raw;
                    for (float Value : Detail.TestResults)
                        Raw.Add(MakeShared<FJsonValueNumber>(Value));
                    Row->SetArrayField(TEXT("rawTestResults"), Raw);
                }
                if (Item.IsValid() && ActiveQuery->ItemTypeVectorCDO &&
                    Item.DataOffset + ActiveQuery->ValueSize <= Debug.RawData.Num())
                {
                    ++Valid;
                    const FVector Point = ActiveQuery->ItemTypeVectorCDO->GetItemLocation(
                        Debug.RawData.GetData() + Item.DataOffset);
                    Row->SetNumberField(TEXT("x"), Point.X);
                    Row->SetNumberField(TEXT("y"), Point.Y);
                    Row->SetNumberField(TEXT("z"), Point.Z);
                    // These are actual engine candidate scores, not an Editor
                    // debugger mock-up. Keep a small, readable live sample.
                    if (Result->IsSuccessful() && Valid <= 12)
                    {
                        DrawDebugSphere(GetWorld(), Point + FVector(0, 0, 18), 18, 8,
                                        FColor::Green, false, 1.2f);
                        DrawDebugString(GetWorld(), Point + FVector(0, 0, 60),
                            FString::Printf(TEXT("Q%d %.2f"), QueryId, Item.Score),
                            nullptr, FColor::Yellow, 1.2f, false, 1.1f);
                    }
                }
                Items.Add(MakeShared<FJsonValueObject>(Row));
            }
            Record->SetNumberField(TEXT("validDebugItems"), Valid);
            Record->SetArrayField(TEXT("performedTests"), Tests);
            Record->SetArrayField(TEXT("items"), Items);
            FString Json;
            TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
                TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
            FJsonSerializer::Serialize(Record, Writer);
            UE_LOG(LogTemp, Display, TEXT("AEGIS_EQS %s"), *Json);
            QueryDiagnostic = FString::Printf(TEXT("Q%d %s %s | valid %d / %d"), QueryId,
                *ActiveQuery->QueryName, Result->IsSuccessful() ? TEXT("SUCCESS") : TEXT("FAILED"),
                Valid, Debug.DebugItems.Num());
        }
#endif
    }
    ActiveQuery.Reset();
    QueryId = INDEX_NONE;
    if (auto* BB = GetBlackboardComponent())
        BB->SetValueAsBool(TEXT("QueryPending"), false);
    if (bPaused || !GetPawn() || !Result->IsSuccessful() || Result->Items.IsEmpty())
    {
        ++FailedQueries;
        return;
    }
    SelectedPoint = Result->GetItemAsLocation(0);
    bHasTacticalPoint = true;
    if (bPendingCoverQuery)
        ++CoverUses;
    if (auto* BB = GetBlackboardComponent())
    {
        BB->SetValueAsVector(TEXT("TacticalPoint"), SelectedPoint);
        BB->SetValueAsBool(TEXT("QueryPending"), false);
    }
    bHasTacticalPoint = MoveToLocation(SelectedPoint, 50) != EPathFollowingRequestResult::Failed;
}
void AAegisAIController::ShutdownAI()
{
    StopMovement();
    if (GetBrainComponent())
        GetBrainComponent()->StopLogic(TEXT("Aegis shutdown"));
    if (QueryId != INDEX_NONE)
        if (auto* Manager = UEnvQueryManager::GetCurrent(GetWorld()))
            Manager->AbortQuery(QueryId);
    QueryId = INDEX_NONE;
    ActiveQuery.Reset();
    bHasTacticalPoint = false;
    ObservedTarget.Reset();
    ObservedAlly.Reset();
    bTargetVisible = false;
    DebugState = TEXT("Dead / stopped");
}
bool AAegisAIController::ExecuteAction(FName Action)
{
    auto* Self = Cast<AAegisAICharacter>(GetPawn());
    if (bPaused || !Self || !Self->Health->IsAlive())
        return false;
    DebugState = Action.ToString();
    if (Action == TEXT("Attack"))
    {
        if (!ObservedTarget.IsValid())
            return false;
        SetFocus(ObservedTarget.Get());
        Self->SetActorRotation((ObservedTarget->GetActorLocation() - Self->GetActorLocation()).Rotation());
        StopMovement();
        if (FVector::Dist(Self->GetActorLocation(), ObservedTarget->GetActorLocation()) < 180)
            Self->Combat->Melee();
        else
            Self->Combat->FireAt(ObservedTarget->GetActorLocation() + FVector(0, 0, 30));
        // Holding an attack during cooldown succeeds; falling through to Patrol would cancel combat.
        return true;
    }
    if (Action == TEXT("Chase") || Action == TEXT("Investigate"))
    {
        MoveToLocation(LastKnown, 100);
        return true;
    }
    if (Action == TEXT("FindCover") || Action == TEXT("Retreat") || Action == TEXT("AttackPosition"))
    {
        const bool Started = RequestTacticalPoint(Action == TEXT("FindCover")
                                 ? Self->CoverQuery
                                 : (Action == TEXT("Retreat") ? Self->RetreatQuery : Self->AttackQuery));
        // Keep an asynchronous query, accepted move or reached point alive. The
        // accepted point expires when the next budgeted query starts; arriving
        // early must not make Retreat fall through to Follow before that retry.
        // A failed query,
        // absent asset, or rejected move must let the existing selector try
        // Attack / Investigate(authorized LastKnown) / Patrol instead of stall.
        return Started || QueryId != INDEX_NONE ||
            (bHasTacticalPoint && (GetMoveStatus() == EPathFollowingStatus::Moving ||
                FVector::DistSquared2D(Self->GetActorLocation(), SelectedPoint) <= 10000));
    }
    if (Action == TEXT("Recover"))
    {
        Self->Health->Heal(0.7f);
        return true;
    }
    if (Action == TEXT("Follow") || Action == TEXT("Support"))
    {
        if (!ObservedAlly.IsValid())
            return false;
        if (FVector::Dist(Self->GetActorLocation(), ObservedAlly->GetActorLocation()) > 250)
            MoveToActor(ObservedAlly.Get(), 200);
        else if (Action == TEXT("Support") && GetWorld()->GetTimeSeconds() >= SupportReadyAt)
        {
            ObservedAlly->Health->Heal(22);
            SupportReadyAt = GetWorld()->GetTimeSeconds() + 5;
        }
        return true;
    }
    if (Action == TEXT("Patrol"))
    {
        ClearFocus(EAIFocusPriority::Gameplay);
        if (GetMoveStatus() == EPathFollowingStatus::Moving)
            return true;
        auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
        FNavLocation Location;
        for (int32 Attempt = 0; Nav && Attempt < 8; ++Attempt)
        {
            const FVector Candidate =
                Self->GetActorLocation() +
                FVector(PatrolRandom.FRandRange(-700, 700), PatrolRandom.FRandRange(-700, 700), 0);
            if (Nav->ProjectPointToNavigation(Candidate, Location))
            {
                MoveToLocation(Location.Location, 80);
                return true;
            }
        }
    }
    return false;
}
UBTService_AegisObserve::UBTService_AegisObserve()
{
    NodeName = TEXT("Aegis: authorized observation");
    Interval = 0.2f;
    RandomDeviation = 0;
}
void UBTService_AegisObserve::TickNode(UBehaviorTreeComponent& Owner, uint8* Memory, float Delta)
{
    Super::TickNode(Owner, Memory, Delta);
    if (auto* AI = Cast<AAegisAIController>(Owner.GetAIOwner()))
        AI->RefreshDecision();
}
UBTTask_AegisAction::UBTTask_AegisAction()
{
    NodeName = TEXT("Aegis: execute action");
}
EBTNodeResult::Type UBTTask_AegisAction::ExecuteTask(UBehaviorTreeComponent& Owner, uint8* Memory)
{
    (void)Memory;
    auto* AI = Cast<AAegisAIController>(Owner.GetAIOwner());
    return AI && AI->ExecuteAction(Action) ? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
}
void UEnvQueryContext_AegisThreat::ProvideContext(FEnvQueryInstance& Instance,
                                                  FEnvQueryContextData& Data) const
{
    const auto* AI = Cast<AAegisAIController>(Instance.Owner.Get());
    if (AI)
        UEnvQueryItemType_Point::SetContextHelper(Data, AI->LastKnown);
}

UBTDecorator_AegisCondition::UBTDecorator_AegisCondition()
{
    NodeName = TEXT("Aegis Blackboard condition");
}
bool UBTDecorator_AegisCondition::CalculateRawConditionValue(UBehaviorTreeComponent& Owner,
                                                             uint8* Memory) const
{
    (void)Memory;
    const auto* BB = Owner.GetBlackboardComponent();
    return BB && (bInteger ? BB->GetValueAsInt(Key) == Expected : BB->GetValueAsBool(Key) == (Expected != 0));
}
FString UBTDecorator_AegisCondition::GetStaticDescription() const
{
    return FString::Printf(TEXT("%s == %d"), *Key.ToString(), Expected);
}
