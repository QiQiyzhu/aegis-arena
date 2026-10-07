#include "AegisAIController.h"
#include "AegisCharacter.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Damage.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "BrainComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "TimerManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#if USE_EQS_DEBUGGER && !UE_BUILD_SHIPPING
#include "DrawDebugHelpers.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
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
void AAegisAIController::RecordDecisionTelemetry(const aegis::Observation& O, aegis::Action Previous)
{
    auto& T = DecisionTelemetry;
    const double Now = GetWorld()->GetTimeSeconds();
    const int32 PreviousSequence = T.DecisionSequence;
    T.Observation = O;
    T.Scores = Decision->Scores;
    T.Previous = Previous;
    T.Selected = Decision->Selected;
    T.RawWinner = static_cast<aegis::Action>(std::distance(T.Scores.begin(),
        std::max_element(T.Scores.begin(), T.Scores.end())));
    T.DecisionSequence = Decision->DecisionCount;
    T.GameSeconds = Now;
    T.bUtilityPolicy = Decision->bUtilityPolicy;
    T.bImprovedPolicy = Decision->bImprovedPolicy;
    T.bTacticalTrial = bTacticalTrial;
    T.SelectionReason = !T.bUtilityPolicy ? TEXT("priority") :
        (T.Selected != T.RawWinner ? TEXT("hysteresis") : TEXT("raw_winner"));
    if (PreviousSequence > 0 && T.Selected != Previous)
    {
        ++T.SwitchCount;
        // A -> B -> A, with no intervening switch, within one game second.
        if (LastSwitchAt >= 0 && Now - LastSwitchAt <= 1.0 &&
            Previous == LastSwitchTo && T.Selected == LastSwitchFrom)
            ++T.ShortReversalCount;
        LastSwitchFrom = Previous;
        LastSwitchTo = T.Selected;
        LastSwitchAt = Now;
    }
}
void AAegisAIController::RecordBTExecution(FName Action, bool bSucceeded)
{
    auto& T = DecisionTelemetry;
    T.BTLeaf = Action;
    T.BTLeafSucceeded = bSucceeded;
    ++T.BTExecutionSequence;
    T.BTDecisionSequence = T.DecisionSequence;
    T.BTExecutedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : -1;
    if (bSucceeded)
    {
        T.AcceptedBTLeaf = Action;
        T.AcceptedBTLeafAt = T.BTExecutedAt;
    }
}
void AAegisAIController::RecordSupportHeal(float Applied)
{
    if (Applied <= 0)
        return;
    ++DecisionTelemetry.SupportHealCount;
    DecisionTelemetry.SupportHealAmount += Applied;
    DecisionTelemetry.LastSupportHealAt = GetWorld()->GetTimeSeconds();
}
AAegisCharacter* AAegisAIController::ResolveFollowPositionLink() const
{
    const auto* Self = Cast<AAegisAICharacter>(GetPawn());
    auto* Ally = FollowPositionLink.Get();
    return Self && Self->bCompanion && !bTacticalTrial && Self->Team != EAegisTeam::Neutral &&
        Ally && Ally != Self && Ally->Team == Self->Team ? Ally : nullptr;
}
void AAegisAIController::CancelFollowLinkMove()
{
    // Revoking a link must not cancel a later EQS/chase movement request.
    if (FollowLinkMoveRequestId.IsValid() &&
        FollowLinkMoveRequestId.GetID() == GetCurrentMoveRequestID().GetID())
        StopMovement();
    FollowLinkMoveRequestId = FAIRequestID::InvalidRequest;
}
void AAegisAIController::RefreshFollowLinkTelemetry()
{
    auto& T = DecisionTelemetry;
    AAegisCharacter* Ally = ResolveFollowPositionLink();
    T.FollowLinkAvailable = Ally != nullptr;
    if (!Ally || !Decision->bImprovedPolicy)
    {
        CancelFollowLinkMove();
        T.FollowLinkPosition = FVector::ZeroVector;
        T.FollowLinkDistance = T.FollowLinkSampleGameSeconds = -1;
        return;
    }
    // Only the opted-in companion samples explicit team position. In
    // particular, this must never read Ally->Health or alter Observation.
    T.FollowLinkPosition = Ally->GetActorLocation();
    T.FollowLinkDistance = FVector::Dist(GetPawn()->GetActorLocation(), T.FollowLinkPosition) / 100;
    T.FollowLinkSampleGameSeconds = GetWorld()->GetTimeSeconds();
}
bool AAegisAIController::SetFollowPositionLink(AAegisCharacter* Ally)
{
    CancelFollowLinkMove();
    FollowPositionLink = Ally;
    const bool bAccepted = ResolveFollowPositionLink() != nullptr;
    if (!bAccepted)
        FollowPositionLink.Reset();
    DecisionTelemetry.FollowTargetSource = TEXT("none");
    DecisionTelemetry.FollowTargetSampleGameSeconds = -1;
    RefreshFollowLinkTelemetry();
    return bAccepted;
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
    DecisionTelemetry = FAegisDecisionTelemetry{};
    LastSwitchAt = -1;
    FollowPositionLink.Reset();
    FollowLinkMoveRequestId = FAIRequestID::InvalidRequest;
    bStopped = false;
    bTacticalTrial = Bot->bTacticalTrial;
    bV2GuardSlots = bTacticalTrial && Bot->bCompanion && FParse::Param(FCommandLine::Get(), TEXT("AegisV2")) &&
        !FParse::Param(FCommandLine::Get(), TEXT("AegisGuardLegacy"));
    GuardSlotChecks = GuardSlotRejections = GuardAlternateSelections = 0;
    GuardSlotReason = bV2GuardSlots ? TEXT("awaiting_guard_observation") : TEXT("legacy_formation");
    if (bTacticalTrial)
        Bot->GetCharacterMovement()->SetAvoidanceEnabled(true);
    PatrolRandom.Initialize(Bot->DecisionSeed);
    Decision->bUtilityPolicy = Bot->bUtilityCompanionPolicy;
    if (Bot->bCompanion)
        Bot->Team = EAegisTeam::Player;
    SetGenericTeamId(FGenericTeamId(static_cast<uint8>(Bot->Team)));
    Senses->OnTargetPerceptionUpdated.AddUniqueDynamic(this, &AAegisAIController::PerceptionChanged);
    // The component registers before this controller possesses its pawn. At
    // that point our affiliation callback returns Neutral, so existing players
    // are omitted from Sight queries. SetGenericTeamId does not notify UE's
    // perception system; rebuild those queries now that pawn and team are set.
    Senses->RequestStimuliListenerUpdate();
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
void AAegisAIController::EndPlay(const EEndPlayReason::Type Reason)
{
    ShutdownAI();
    Super::EndPlay(Reason);
}
bool AAegisAIController::HasTargetMemory() const
{
    return !bStopped && bHasTargetMemory && GetWorld() &&
        GetWorld()->GetTimeSeconds() - LastSensedAt < FMath::Max(0.1f, TargetMemorySeconds);
}
void AAegisAIController::RememberTarget(const FVector& Location)
{
    LastKnown = Location;
    LastSensedAt = GetWorld()->GetTimeSeconds();
    bHasTargetMemory = true;
    GetWorldTimerManager().SetTimer(MemoryTimer, this, &AAegisAIController::ForgetTarget,
                                   FMath::Max(0.1f, TargetMemorySeconds), false);
}
void AAegisAIController::ClearVisibleTarget()
{
    CancelTacticalWindup();
    ObservedTarget.Reset();
    bTargetVisible = false;
    // Actor focus resolves a live transform every controller tick. It must not
    // survive sight loss while navigation uses only the last authorized point.
    ClearFocus(EAIFocusPriority::Gameplay);
    if (auto* BB = GetBlackboardComponent())
    {
        BB->ClearValue(TEXT("TargetActor"));
        BB->SetValueAsBool(TEXT("HasLOS"), false);
        BB->SetValueAsBool(TEXT("InRange"), false);
        BB->SetValueAsBool(TEXT("NeedsCover"), false);
    }
}
void AAegisAIController::CancelTacticalQuery()
{
    // AbortQuery calls its completion delegate synchronously. Invalidate the
    // identity before aborting so neither that nor a late result can be used.
    const int32 CancelledId = QueryId;
    const TSharedPtr<FEnvQueryInstance> CancelledQuery = ActiveQuery;
    QueryId = INDEX_NONE;
    ActiveQuery.Reset();
    bPendingCoverQuery = false;
    bHasTacticalPoint = false;
    SelectedPoint = FVector::ZeroVector;
    if (auto* BB = GetBlackboardComponent())
    {
        BB->SetValueAsBool(TEXT("QueryPending"), false);
        BB->ClearValue(TEXT("TacticalPoint"));
    }
    if (CancelledId != INDEX_NONE)
    {
        DecisionTelemetry.QueryStatus = TEXT("cancelled");
        DecisionTelemetry.QueryCompletedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : -1;
        DecisionTelemetry.bQueryMoveAccepted = false;
        if (auto* Manager = UEnvQueryManager::GetCurrent(GetWorld()))
            if (Manager->AbortQuery(CancelledId))
            {
                // Preserve legacy aggregate accounting (aborts were failed
                // completions) while retaining the cancellation distinction.
                ++CancelledQueries;
                ++CompletedQueries;
                ++FailedQueries;
                if (CancelledQuery)
                {
                    QueryCpuSeconds += CancelledQuery->TotalExecutionTime;
                    ++ProfiledQueries;
                }
            }
    }
}
void AAegisAIController::ForgetTarget()
{
    GetWorldTimerManager().ClearTimer(MemoryTimer);
    bHasTargetMemory = false;
    LastSensedAt = -1000;
    LastKnown = FVector::ZeroVector;
    ClearVisibleTarget();
    CancelTacticalQuery();
    if (bTacticalTrial)
        StopMovement();
    if (DebugState == TEXT("Chase") || DebugState == TEXT("Investigate") ||
        DebugState == TEXT("AttackPosition") || DebugState == TEXT("FindCover") ||
        DebugState == TEXT("Retreat"))
        StopMovement();
    if (auto* BB = GetBlackboardComponent())
    {
        BB->SetValueAsBool(TEXT("HasMemory"), false);
        BB->ClearValue(TEXT("LastKnownLocation"));
    }
}
void AAegisAIController::PerceptionChanged(AActor* Actor, FAIStimulus Stimulus)
{
    auto* Self = Cast<AAegisCharacter>(GetPawn());
    auto* Seen = Cast<AAegisCharacter>(Actor);
    if (bStopped || !Self || !Seen || !Self->IsHostile(Seen))
        return;
    // A damage event may reveal a historical location, never grant current visual tracking.
    if (Stimulus.WasSuccessfullySensed())
    {
        RememberTarget(Stimulus.StimulusLocation);
    }
    else if (Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>() && Actor == ObservedTarget.Get())
        ClearVisibleTarget();
}
void AAegisAIController::RefreshDecision()
{
    auto* Self = Cast<AAegisAICharacter>(GetPawn());
    auto* BB = GetBlackboardComponent();
    if (bPaused || bStopped || !Self || !Self->Health->IsAlive() || !BB)
        return;
    TRACE_CPUPROFILER_EVENT_SCOPE(Aegis_ObserveAndUtility);
    const double CpuStarted = FPlatformTime::Seconds();
    TArray<AActor*> Visible;
    Senses->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Visible);
    const TWeakObjectPtr<AAegisCharacter> PreviousTarget = ObservedTarget;
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
            double D = FVector::DistSquared(C->GetActorLocation(), Self->GetActorLocation());
            if (bTacticalTrial && Self->bCompanion)
            {
                if (CompanionCommand == EAegisCompanionCommand::Focus && C == CommandTarget.Get())
                    D = -1; // Identity priority only inside the genuinely visible set.
                else if (CompanionCommand == EAegisCompanionCommand::Guard && LinkedPlayer.IsValid())
                    D = FVector::DistSquared(C->GetActorLocation(), LinkedPlayer->GetActorLocation());
            }
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
    if (bTacticalTrial && Self->bCompanion && LinkedPlayer.IsValid() && LinkedPlayer->Health->IsAlive())
        ObservedAlly = LinkedPlayer; // Explicit allied radio link, independent of view direction.
    if (bTacticalTrial && PreviousTarget != ObservedTarget)
    {
        CancelTacticalWindup();
        TargetAcquiredAt = GetWorld()->GetTimeSeconds();
    }
    bTargetVisible = ObservedTarget.IsValid();
    if (bTargetVisible)
        RememberTarget(ObservedTarget->GetActorLocation());
    else
        ClearVisibleTarget();
    const bool Memory = HasTargetMemory();
    if (!Memory && bHasTargetMemory)
        ForgetTarget();
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
    const aegis::Action PreviousAction = Decision->Selected;
    Decision->Evaluate(O);
    BB->SetValueAsObject(TEXT("TargetActor"), ObservedTarget.Get());
    if (Memory)
        BB->SetValueAsVector(TEXT("LastKnownLocation"), LastKnown);
    else
        BB->ClearValue(TEXT("LastKnownLocation"));
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
    RecordDecisionTelemetry(O, PreviousAction);
    RefreshFollowLinkTelemetry();
}
bool AAegisAIController::RequestTacticalPoint(UEnvQuery* Query)
{
    if (bPaused || bStopped || !HasTargetMemory() || !Query || !GetPawn() ||
        QueryId != INDEX_NONE || GetWorld()->GetTimeSeconds() < NextQueryAt)
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
    if (QueryId != INDEX_NONE)
    {
        auto& T = DecisionTelemetry;
        T.QueryId = QueryId;
        T.QueryDecisionSequence = T.DecisionSequence;
        T.QueryName = GetNameSafe(Query);
        T.QuerySubmittedBTLeaf = FName(*DebugState);
        T.QueryStatus = TEXT("pending");
        T.QuerySubmittedAt = GetWorld()->GetTimeSeconds();
        T.QueryCompletedAt = T.QueryAcceptedAt = -1;
        T.QueryReturnedItems = 0;
        T.QueryGeneratedItems = T.QueryValidItems = -1;
        T.QueryAcceptedPoint = FVector::ZeroVector;
        T.bQueryMoveAccepted = false;
    }
    if (auto* BB = GetBlackboardComponent())
        BB->SetValueAsBool(TEXT("QueryPending"), QueryId != INDEX_NONE);
    return QueryId != INDEX_NONE;
}
void AAegisAIController::QueryFinished(TSharedPtr<FEnvQueryResult> Result)
{
    if (!Result.IsValid() || Result->QueryID != QueryId)
        return;
    auto& T = DecisionTelemetry;
    T.QueryCompletedAt = GetWorld()->GetTimeSeconds();
    T.QueryReturnedItems = Result->Items.Num();
    T.QueryStatus = Result->IsSuccessful() ? TEXT("completed") :
        (Result->IsAborted() ? TEXT("aborted") : TEXT("failed"));
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
            T.QueryGeneratedItems = Debug.DebugItems.Num();
            T.QueryValidItems = Valid;
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
    if (bPaused || bStopped || !HasTargetMemory() || !GetPawn() ||
        !Result->IsSuccessful() || Result->Items.IsEmpty())
    {
        ++FailedQueries;
        if (Result->IsSuccessful() && !Result->Items.IsEmpty())
            T.QueryStatus = TEXT("state_rejected");
        else if (Result->IsSuccessful())
            T.QueryStatus = TEXT("failed_empty");
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
    T.QueryAcceptedPoint = SelectedPoint;
    T.QueryAcceptedAt = GetWorld()->GetTimeSeconds();
    T.bQueryMoveAccepted = bHasTacticalPoint;
    T.QueryStatus = bHasTacticalPoint ? TEXT("accepted") : TEXT("move_rejected");
}
void AAegisAIController::ShutdownAI()
{
    bStopped = true;
    StopMovement();
    if (GetBrainComponent())
        GetBrainComponent()->StopLogic(TEXT("Aegis shutdown"));
    ForgetTarget();
    Senses->ForgetAll();
    ObservedAlly.Reset();
    FollowPositionLink.Reset();
    FollowLinkMoveRequestId = FAIRequestID::InvalidRequest;
    DecisionTelemetry.FollowLinkAvailable = false;
    DecisionTelemetry.FollowLinkPosition = FVector::ZeroVector;
    DecisionTelemetry.FollowLinkDistance = DecisionTelemetry.FollowLinkSampleGameSeconds = -1;
    DecisionTelemetry.FollowTargetSource = TEXT("none");
    DecisionTelemetry.FollowTargetSampleGameSeconds = -1;
    LinkedPlayer.Reset();
    CommandTarget.Reset();
    bTacticalContextReady = false;
    const auto* StoppedPawn = Cast<AAegisCharacter>(GetPawn());
    DebugState = StoppedPawn && StoppedPawn->Health && StoppedPawn->Health->IsAlive() ? TEXT("Stopped / session ended") : TEXT("Dead / stopped");
}
AAegisCharacter* AAegisAIController::GetLinkedPlayer() const
{
    return LinkedPlayer.Get();
}
void AAegisAIController::SetTacticalContext(AAegisCharacter* Player, const FVector& Anchor, int32 Slot)
{
    const auto* Self = Cast<AAegisAICharacter>(GetPawn());
    if (!Self || !Self->bTacticalTrial || Anchor.ContainsNaN())
        return;
    bTacticalTrial = true;
    bTacticalContextReady = true;
    EncounterAnchor = Anchor;
    FormationSlot = Slot;
    TacticalSide = Slot % 2 == 0 ? 1.f : -1.f;
    // A hostile controller can never obtain live player tracking via this API.
    LinkedPlayer = Self->bCompanion && Player && Player->Team == Self->Team ? Player : nullptr;
    NextTacticalMoveAt = NextTacticalActionAt = NextRepositionAt = 0;
    StopMovement();
}
void AAegisAIController::SetCompanionCommand(EAegisCompanionCommand Command,
                                              AAegisCharacter* DesignatedTarget, const FVector& Location)
{
    const auto* Self = Cast<AAegisAICharacter>(GetPawn());
    if (!bTacticalTrial || bStopped || !Self || !Self->bCompanion || Location.ContainsNaN())
        return;
    CompanionCommand = Command;
    CommandTarget = Command == EAegisCompanionCommand::Focus && DesignatedTarget &&
        Self->IsHostile(DesignatedTarget) ? DesignatedTarget : nullptr;
    // This point is a player's explicit command snapshot, not a target transform.
    CommandPoint = Location;
    NextTacticalMoveAt = NextTacticalActionAt = NextRepositionAt = 0;
    CancelTacticalWindup();
    StopMovement();
    DebugState = Command == EAegisCompanionCommand::Guard ? TEXT("Guard: take rear flank") :
        Command == EAegisCompanionCommand::Focus ? TEXT("Focus: designated threat") : TEXT("Rally: move to signal");
}
void AAegisAIController::CancelTacticalWindup()
{
    if (bWindingUpShot)
        if (auto* Self = Cast<AAegisCharacter>(GetPawn()))
            Self->ClearShotWindup();
    bWindingUpShot = false;
    WindupTarget.Reset();
    WindupEndsAt = 0;
}
bool AAegisAIController::InPlayerFireLane(const FVector& Point) const
{
    if (!LinkedPlayer.IsValid())
        return false;
    const FVector Offset = Point - LinkedPlayer->GetActorLocation();
    const FVector Forward = LinkedPlayer->GetActorForwardVector().GetSafeNormal2D();
    const double Along = FVector::DotProduct(Offset, Forward);
    const double Side = FMath::Abs(Offset.X * Forward.Y - Offset.Y * Forward.X);
    return Along > -45 && Along < 950 && Side < 85;
}
bool AAegisAIController::HasClearShot(const AAegisCharacter* Target, const FVector& AimPoint) const
{
    const auto* Self = Cast<AAegisCharacter>(GetPawn());
    if (!Self || !Target || !Senses->HasActiveStimulus(*Target, UAISense::GetSenseID<UAISense_Sight>()))
        return false;
    const FVector Start = Self->GetActorLocation() + FVector(0, 0, 30);
    if (FVector::DistSquared(Start, AimPoint) > FMath::Square(Self->Combat->RangedRange))
        return false;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(AegisTacticalFire), false, Self);
    // Use the same first-hit visibility channel as the actual weapon. A friend
    // or world obstacle blocks the shot; target identity alone grants nothing.
    return GetWorld()->LineTraceSingleByChannel(Hit, Start, AimPoint, ECC_Visibility, Params) &&
        Hit.GetActor() == Target;
}
bool AAegisAIController::IsCommittedFireLaneSafe(const FVector& AimPoint) const
{
    const auto* Self = Cast<AAegisCharacter>(GetPawn());
    if (!Self)
        return false;
    const FVector Start = Self->GetActorLocation() + FVector(0, 0, 30);
    const FVector End = Start + (AimPoint - Start).GetSafeNormal() * Self->Combat->RangedRange;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(AegisCommittedFire), false, Self);
    GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
    const auto* FirstCharacter = Cast<AAegisCharacter>(Hit.GetActor());
    // A missed telegraphed shot may hit scenery, but never knowingly fire into
    // an ally. The weapon trace itself stops at the first physical obstruction.
    return !FirstCharacter || Self->IsHostile(FirstCharacter);
}
bool AAegisAIController::GetGuardCandidateMuzzle(const FVector& NavigablePoint, FVector& OutMuzzle) const
{
    const auto* Self = Cast<AAegisAICharacter>(GetPawn());
    const auto* Movement = Self ? Self->GetCharacterMovement() : nullptr;
    if (!Self || !Self->bCompanion || !Movement || !Movement->IsMovingOnGround() ||
        !Movement->CurrentFloor.IsWalkableFloor()) return false;
    // Recast surface vertices can sit above collision geometry (10 cm in the
    // regression map). Adding a capsule height directly to the nav vertex
    // invents a higher muzzle and can incorrectly see over low cover.
    FHitResult Ground;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(AegisGuardStandingFloor), false, Self);
    const float HalfHeight = Self->GetSimpleCollisionHalfHeight();
    if (!GetWorld()->LineTraceSingleByChannel(Ground, NavigablePoint + FVector(0, 0, HalfHeight),
            NavigablePoint - FVector(0, 0, 250), ECC_GameTraceChannel1, Params) ||
        Ground.bStartPenetrating || !Movement->IsWalkable(Ground)) return false;
    // Match the current walking capsule's physical floor clearance and the
    // actual weapon's +30 cm origin; this query reads no target information.
    OutMuzzle = FVector(NavigablePoint.X, NavigablePoint.Y, Ground.ImpactPoint.Z + HalfHeight +
        FMath::Max(0.f, Movement->CurrentFloor.FloorDist) + 30.f);
    return true;
}
bool AAegisAIController::MoveTactically(const FVector& Desired, bool bRequireClearShot)
{
    auto* Self = Cast<AAegisAICharacter>(GetPawn());
    const double Now = GetWorld()->GetTimeSeconds();
    if (!Self || Now < NextTacticalMoveAt)
        return GetMoveStatus() == EPathFollowingStatus::Moving;
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!Nav)
        return false;
    FNavLocation Projected;
    if (!Nav->ProjectPointToNavigation(Desired, Projected, FVector(110, 110, 250)))
        return false;
    if (Self->bCompanion && InPlayerFireLane(Projected.Location))
        return false;
    if (bRequireClearShot)
    {
        if (!ObservedTarget.IsValid() ||
            !Senses->HasActiveStimulus(*ObservedTarget, UAISense::GetSenseID<UAISense_Sight>()))
            return false;
        FHitResult Hit;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(AegisTacticalCandidate), false, Self);
        FVector Eye = Projected.Location + FVector(0, 0, Self->GetSimpleCollisionHalfHeight() + 30);
        // Preserve legacy/enemy/Focus candidate behavior. Only the opt-in Guard
        // firing-slot qualification uses the corrected physical standing origin.
        if (bV2GuardSlots && CompanionCommand == EAegisCompanionCommand::Guard &&
            !GetGuardCandidateMuzzle(Projected.Location, Eye)) return false;
        if (!GetWorld()->LineTraceSingleByChannel(Hit, Eye,
                ObservedTarget->GetActorLocation() + FVector(0, 0, 30), ECC_Visibility, Params) ||
            Hit.GetActor() != ObservedTarget.Get())
            return false;
    }
    UNavigationPath* Path = Nav->FindPathToLocationSynchronously(
        GetWorld(), Self->GetActorLocation(), Projected.Location, Self);
    if (!Path || !Path->IsValid() || Path->IsPartial())
        return false;
    TacticalMoveGoal = Projected.Location;
    const auto Result = MoveToLocation(Projected.Location, 65, true, true, true, false, nullptr, false);
    if (Result == EPathFollowingRequestResult::Failed)
        return false;
    if (Result == EPathFollowingRequestResult::RequestSuccessful)
        ++TacticalMoveRequests;
    NextTacticalMoveAt = Now + 0.65;
    return true;
}
bool AAegisAIController::RepositionForTarget(const FVector& TargetLocation)
{
    auto* Self = Cast<AAegisAICharacter>(GetPawn());
    if (!Self)
        return false;
    if (Self->bCompanion && LinkedPlayer.IsValid() && CompanionCommand != EAegisCompanionCommand::Focus)
    {
        const FVector Forward = LinkedPlayer->GetActorForwardVector().GetSafeNormal2D();
        const FVector Right(-Forward.Y, Forward.X, 0);
        if (CompanionCommand == EAegisCompanionCommand::Rally)
        {
            // Stay inside the relay capture circle instead of repeatedly
            // leaving it for the wider Guard formation and returning.
            for (float Side : {TacticalSide, -TacticalSide})
                if (MoveTactically(CommandPoint + Right * Side * 100))
                    return true;
            return false;
        }
        const FVector Anchor = LinkedPlayer->GetActorLocation();
        // Guard's first navigable formation slot can still be behind a low
        // obstruction. In v2, an actively seen threat makes the existing two
        // slots firing candidates, so a blocked first slot cannot mask the
        // usable second one. Hidden targets never authorize these traces.
        const bool bCheckFiringSlot = bV2GuardSlots && ObservedTarget.IsValid() &&
            Senses->HasActiveStimulus(*ObservedTarget, UAISense::GetSenseID<UAISense_Sight>());
        if (bCheckFiringSlot && GetWorld()->GetTimeSeconds() < NextTacticalMoveAt)
            return GetMoveStatus() == EPathFollowingStatus::Moving;
        int32 CandidateIndex = 0;
        for (float Side : {TacticalSide, -TacticalSide})
        {
            if (bCheckFiringSlot) ++GuardSlotChecks;
            if (MoveTactically(Anchor - Forward * 150 + Right * Side * 240, bCheckFiringSlot))
            {
                if (bCheckFiringSlot)
                {
                    if (CandidateIndex == 1) ++GuardAlternateSelections;
                    GuardSlotReason = CandidateIndex == 1 ? TEXT("alternate_visible_firing_slot") : TEXT("preferred_visible_firing_slot");
                }
                else if (bV2GuardSlots) GuardSlotReason = TEXT("team_formation_without_visible_target");
                return true;
            }
            if (bCheckFiringSlot) ++GuardSlotRejections;
            ++CandidateIndex;
        }
        if (bCheckFiringSlot) GuardSlotReason = TEXT("no_reachable_visible_firing_slot");
        return false;
    }
    FVector Radial = (Self->GetActorLocation() - TargetLocation).GetSafeNormal2D();
    if (Radial.IsNearlyZero())
        Radial = -Self->GetActorForwardVector();
    const float Range = Self->bCompanion ? 600.f :
        Self->EnemyRole == EAegisEnemyRole::Suppressor ? 850.f :
        Self->EnemyRole == EAegisEnemyRole::Flanker ? 650.f : 500.f;
    const float Angle = Self->bCompanion ? 25.f :
        Self->EnemyRole == EAegisEnemyRole::Flanker ? 60.f : 22.f;
    // Finite, deterministic candidate set; every accepted point needs a full
    // NavMesh path. No teleport, direct-location move or enemy registry lookup.
    for (float Turn : {Angle * TacticalSide, -Angle * TacticalSide, 0.f})
    {
        const FVector Candidate = TargetLocation + Radial.RotateAngleAxis(Turn, FVector::UpVector) * Range;
        if (MoveTactically(Candidate, true))
            return true;
    }
    return MoveTactically(TargetLocation + Radial * FMath::Min(Range, 550.f));
}
bool AAegisAIController::ExecuteTacticalTrial()
{
    auto* Self = Cast<AAegisAICharacter>(GetPawn());
    if (!Self || !bTacticalContextReady)
        return false;
    const double Now = GetWorld()->GetTimeSeconds();
    // Authored BT selectors can try several tasks within one update. Execute
    // one tactical decision per observation interval, regardless of branch.
    if (Now < NextTacticalActionAt)
        return true;
    NextTacticalActionAt = Now + 0.15;
    if (Self->IsPulseStaggered())
    {
        CancelTacticalWindup();
        StopMovement();
        DebugState = TEXT("Staggered: regaining balance");
        return true;
    }
    if (Self->bCompanion && LinkedPlayer.IsValid())
    {
        const FVector PlayerPoint = LinkedPlayer->GetActorLocation();
        const FVector Forward = LinkedPlayer->GetActorForwardVector().GetSafeNormal2D();
        const FVector Right(-Forward.Y, Forward.X, 0);
        const FVector Formation = PlayerPoint - Forward * 150 + Right * TacticalSide * 240;
        const bool bMustRegroup = FVector::DistSquared2D(Self->GetActorLocation(), PlayerPoint) > 750 * 750;
        if (CompanionCommand == EAegisCompanionCommand::Rally &&
            FVector::DistSquared2D(Self->GetActorLocation(), CommandPoint) > 140 * 140)
        {
            CancelTacticalWindup();
            const FVector RallyPoint = InPlayerFireLane(CommandPoint) ? CommandPoint + Right * TacticalSide * 100 : CommandPoint;
            MoveTactically(RallyPoint);
            DebugState = TEXT("Rally: moving to your signal");
            return true;
        }
        if (CompanionCommand == EAegisCompanionCommand::Guard &&
            (bMustRegroup || InPlayerFireLane(Self->GetActorLocation())))
        {
            CancelTacticalWindup();
            MoveTactically(Formation);
            DebugState = TEXT("Guard: clear your firing lane");
            return true;
        }
        if (LinkedPlayer->Health->IsAlive() && LinkedPlayer->Health->Current < LinkedPlayer->Health->Maximum * 0.75f &&
            FVector::DistSquared2D(Self->GetActorLocation(), PlayerPoint) < 350 * 350 && Now >= SupportReadyAt)
        {
            FHitResult SupportHit;
            FCollisionQueryParams SupportParams(SCENE_QUERY_STAT(AegisTacticalSupport), false, Self);
            SupportParams.AddIgnoredActor(LinkedPlayer.Get());
            if (!GetWorld()->LineTraceSingleByChannel(SupportHit,
                    Self->GetActorLocation() + FVector(0, 0, 30), PlayerPoint + FVector(0, 0, 30),
                    ECC_GameTraceChannel1, SupportParams))
            {
                RecordSupportHeal(LinkedPlayer->Health->Heal(22));
                SupportReadyAt = Now + 5;
            }
        }
    }
    AAegisCharacter* Target = ObservedTarget.Get();
    if (!Target || !Senses->HasActiveStimulus(*Target, UAISense::GetSenseID<UAISense_Sight>()))
    {
        CancelTacticalWindup();
        if (Self->bCompanion && LinkedPlayer.IsValid())
        {
            const FRotator Facing = CompanionCommand == EAegisCompanionCommand::Focus ?
                (CommandPoint - Self->GetActorLocation()).Rotation() : LinkedPlayer->GetActorRotation();
            SetControlRotation(Facing);
            Self->SetActorRotation(Facing);
            if (CompanionCommand == EAegisCompanionCommand::Focus)
            {
                MoveTactically(CommandPoint);
                DebugState = TEXT("Focus: search your marked position");
            }
            else if (CompanionCommand == EAegisCompanionCommand::Rally)
            {
                // Already within the command's 140 cm arrival radius. Hold it;
                // no repeating offset/return movement during objective capture.
                StopMovement();
                DebugState = TEXT("Rally: holding your signal");
            }
            else
            {
                RepositionForTarget(LinkedPlayer->GetActorLocation());
                DebugState = TEXT("Guard: follow on your flank");
            }
        }
        else
        {
            MoveTactically(HasTargetMemory() ? LastKnown : EncounterAnchor);
            DebugState = HasTargetMemory() ? TEXT("Search: last observed position") : TEXT("Advance: relay objective");
            const FRotator Facing = GetMoveStatus() == EPathFollowingStatus::Moving ?
                (TacticalMoveGoal - Self->GetActorLocation()).Rotation() :
                FRotator(0, FMath::Fmod(Now * 45 + FormationSlot * 75, 360.0), 0);
            SetControlRotation(Facing);
            Self->SetActorRotation(Facing);
        }
        return true;
    }
    const FVector TargetPoint = Target->GetActorLocation() + FVector(0, 0, 30);
    if (!Target->Health->IsAlive())
    {
        ClearVisibleTarget();
        return true;
    }
    // Sight uses Pawn::GetViewRotation -> the controller's ControlRotation.
    // This controller deliberately does not tick, so visible aiming and fixed
    // search directions must update perception orientation explicitly.
    const FRotator AuthorizedFacing = ((bWindingUpShot ? WindupAimPoint : TargetPoint) - Self->GetActorLocation()).Rotation();
    SetControlRotation(AuthorizedFacing);
    Self->SetActorRotation(AuthorizedFacing);
    if (bWindingUpShot)
    {
        if (WindupTarget.Get() != Target || !HasClearShot(Target, TargetPoint))
        {
            CancelTacticalWindup();
            NextRepositionAt = 0;
            return true;
        }
        Self->SetActorRotation((WindupAimPoint - Self->GetActorLocation()).Rotation());
        if (Now >= WindupEndsAt)
        {
            const FVector CommittedAim = WindupAimPoint;
            CancelTacticalWindup();
            // Commit to the telegraphed snapshot. A player who dodges the line
            // does not get tracked by the windup and may make this shot miss.
            if (IsCommittedFireLaneSafe(CommittedAim) && Self->Combat->FireAt(CommittedAim))
                ++TacticalShots;
            NextRepositionAt = Now + 0.1;
            TacticalSide *= -1;
            DebugState = TEXT("Fire: relocating after shot");
        }
        return true;
    }
    if (Now >= NextRepositionAt)
    {
        const bool Moved = RepositionForTarget(Target->GetActorLocation());
        NextRepositionAt = Now + (Self->EnemyRole == EAegisEnemyRole::Suppressor ? 2.2 : 1.4);
        MoveBeforeAttackUntil = Moved ? Now + (Self->EnemyRole == EAegisEnemyRole::Flanker ? 1.0 : 0.55) : Now;
    }
    if (GetMoveStatus() == EPathFollowingStatus::Moving && Now < MoveBeforeAttackUntil)
    {
        DebugState = Self->bCompanion ? TEXT("Guard: moving for a clear shot") :
            Self->EnemyRole == EAegisEnemyRole::Flanker ? TEXT("Flanker: taking a side angle") :
            Self->EnemyRole == EAegisEnemyRole::Suppressor ? TEXT("Suppressor: holding range") : TEXT("Striker: closing the gap");
        return true;
    }
    if (!HasClearShot(Target, TargetPoint))
    {
        RepositionForTarget(Target->GetActorLocation());
        DebugState = TEXT("Reposition: shot obstructed");
        return true;
    }
    if (Self->Combat->CooldownRemaining() > 0 || Now - TargetAcquiredAt < (Self->bCompanion ? 0.2 : 0.5))
        return true;
    StopMovement();
    ClearFocus(EAIFocusPriority::Gameplay);
    WindupTarget = Target;
    WindupAimPoint = TargetPoint;
    const float Windup = Self->bCompanion ? 0.25f :
        Self->EnemyRole == EAegisEnemyRole::Suppressor ? 0.9f :
        Self->EnemyRole == EAegisEnemyRole::Flanker ? 0.7f : 0.6f;
    WindupEndsAt = Now + Windup;
    bWindingUpShot = true;
    Self->SetActorRotation((WindupAimPoint - Self->GetActorLocation()).Rotation());
    Self->ShowShotWindup(WindupAimPoint, Windup);
    DebugState = Self->bCompanion ? TEXT("Covering: aiming at visible threat") : TEXT("Aim: shot warning");
    return true;
}
bool AAegisAIController::ExecuteAction(FName Action)
{
    auto* Self = Cast<AAegisAICharacter>(GetPawn());
    if (bPaused || bStopped || !Self || !Self->Health->IsAlive())
        return false;
    if (bTacticalTrial)
        return ExecuteTacticalTrial();
    DebugState = Action.ToString();
    if (Action == TEXT("Attack"))
    {
        if (!ObservedTarget.IsValid() ||
            !Senses->HasActiveStimulus(*ObservedTarget, UAISense::GetSenseID<UAISense_Sight>()))
        {
            ClearVisibleTarget();
            return false;
        }
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
        if (!HasTargetMemory())
        {
            ForgetTarget();
            return false;
        }
        MoveToLocation(LastKnown, 100);
        return true;
    }
    if (Action == TEXT("FindCover") || Action == TEXT("Retreat") || Action == TEXT("AttackPosition"))
    {
        if (!HasTargetMemory())
        {
            ForgetTarget();
            return false;
        }
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
        AAegisCharacter* Ally = ObservedAlly.Get();
        bool bUsePositionLink = false;
        if (Action == TEXT("Follow"))
        {
            RefreshFollowLinkTelemetry();
            if (!Ally && Decision->bImprovedPolicy)
            {
                Ally = ResolveFollowPositionLink();
                bUsePositionLink = Ally != nullptr;
            }
            DecisionTelemetry.FollowTargetSource = !Ally ? TEXT("none") :
                (bUsePositionLink ? TEXT("team_position_link") : TEXT("sight"));
            DecisionTelemetry.FollowTargetSampleGameSeconds = GetWorld()->GetTimeSeconds();
        }
        if (!Ally)
            return false;
        if (FVector::Dist(Self->GetActorLocation(), Ally->GetActorLocation()) > 250)
        {
            MoveToActor(Ally, 200);
            if (bUsePositionLink)
                FollowLinkMoveRequestId = GetCurrentMoveRequestID();
        }
        else if (Action == TEXT("Support") && GetWorld()->GetTimeSeconds() >= SupportReadyAt)
        {
            RecordSupportHeal(Ally->Health->Heal(22));
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
    if (!AI)
        return EBTNodeResult::Failed;
    const bool bSucceeded = AI->ExecuteAction(Action);
    AI->RecordBTExecution(Action, bSucceeded);
    return bSucceeded ? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
}
void UEnvQueryContext_AegisThreat::ProvideContext(FEnvQueryInstance& Instance,
                                                  FEnvQueryContextData& Data) const
{
    const auto* AI = Cast<AAegisAIController>(Instance.Owner.Get());
    if (AI && AI->HasTargetMemory())
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
