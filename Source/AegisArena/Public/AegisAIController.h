#pragma once
#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "aegis/rules.hpp"
#include "AegisAIController.generated.h"

UENUM(BlueprintType)
enum class EAegisCompanionCommand : uint8
{
    Guard,
    Focus,
    Rally
};

// Passive, per-controller evidence. Values come from the same authorized
// observation used by the policy, never from an additional world scan.
struct FAegisDecisionTelemetry
{
    aegis::Observation Observation;
    aegis::Scores Scores{};
    aegis::Action Selected = aegis::Action::Follow;
    aegis::Action Previous = aegis::Action::Follow;
    aegis::Action RawWinner = aegis::Action::Follow;
    int32 DecisionSequence = 0, SwitchCount = 0, ShortReversalCount = 0;
    double GameSeconds = -1;
    FString SelectionReason = TEXT("not_evaluated");
    bool bUtilityPolicy = true, bImprovedPolicy = false, bTacticalTrial = false;

    // A successful BT leaf means ExecuteAction accepted/held the action; it
    // does not prove that a shot hit, navigation arrived, or healing occurred.
    FName BTLeaf = NAME_None, AcceptedBTLeaf = NAME_None;
    bool BTLeafSucceeded = false;
    int32 BTExecutionSequence = 0, BTDecisionSequence = 0;
    double BTExecutedAt = -1, AcceptedBTLeafAt = -1;

    int32 QueryId = INDEX_NONE, QueryReturnedItems = 0, QueryDecisionSequence = 0;
    // SingleResult normally discards candidates. -1 means not retained;
    // complete candidate counts are populated only by opt-in EQS diagnostics.
    int32 QueryGeneratedItems = -1, QueryValidItems = -1;
    FString QueryName, QueryStatus = TEXT("idle");
    FName QuerySubmittedBTLeaf = NAME_None;
    double QuerySubmittedAt = -1, QueryCompletedAt = -1, QueryAcceptedAt = -1;
    FVector QueryAcceptedPoint = FVector::ZeroVector;
    bool bQueryMoveAccepted = false;

    int32 SupportHealCount = 0;
    double SupportHealAmount = 0, LastSupportHealAt = -1;

    // The most recent Follow attempt records its source separately from Sight.
    FString FollowTargetSource = TEXT("none");
    double FollowTargetSampleGameSeconds = -1;
    bool FollowLinkAvailable = false;
    FVector FollowLinkPosition = FVector::ZeroVector;
    // Metres; -1/time -1 mean this policy has not sampled the position link.
    double FollowLinkDistance = -1, FollowLinkSampleGameSeconds = -1;
};

UCLASS(ClassGroup = (Aegis), meta = (BlueprintSpawnableComponent))
class AEGISARENA_API UAegisDecisionComponent : public UActorComponent
{
    GENERATED_BODY()
  public:
    UAegisDecisionComponent();
    aegis::Scores Scores{};
    aegis::Action Selected = aegis::Action::Follow;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy") bool bUtilityPolicy = true;
    // Enables only the explicit allied-position fallback in classic Follow.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy") bool bImprovedPolicy = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Policy") int32 DecisionCount = 0;
    void Evaluate(const aegis::Observation& Observation);
};

UCLASS()
class AEGISARENA_API AAegisAIController : public AAIController
{
    GENERATED_BODY()
  public:
    AAegisAIController();
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UAIPerceptionComponent> Senses;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAegisDecisionComponent> Decision;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString DebugState = TEXT("Uninitialized");
    FString QueryDiagnostic;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FVector LastKnown = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FVector SelectedPoint = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bTargetVisible = false;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Perception", meta = (ClampMin = "0.1"))
    float TargetMemorySeconds = 2.5f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 CoverUses = 0;
    double DecisionCpuSeconds = 0, QueryCpuSeconds = 0;
    int32 CompletedQueries = 0, FailedQueries = 0, ProfiledQueries = 0;
    int32 CancelledQueries = 0;
    TWeakObjectPtr<class AAegisCharacter> ObservedTarget;
    TWeakObjectPtr<class AAegisCharacter> ObservedAlly;
    bool bPaused = false, bHasTacticalPoint = false;
    void RefreshDecision();
    bool RequestTacticalPoint(class UEnvQuery* Query);
    bool HasTargetMemory() const;
    void ShutdownAI();
    bool ExecuteAction(FName Action);
    const FAegisDecisionTelemetry& GetDecisionTelemetry() const { return DecisionTelemetry; }
    void RecordBTExecution(FName Action, bool bSucceeded);
    // Scenario-owned team knowledge, never inferred by scanning actors. This
    // link grants only position for improved Follow, not health or enemy data.
    bool SetFollowPositionLink(class AAegisCharacter* Ally);
    // Interactive trial only. The allied link is explicit team knowledge;
    // enemies receive a fixed objective point, never a live player link.
    void SetTacticalContext(class AAegisCharacter* LinkedPlayer, const FVector& EncounterAnchor,
                            int32 FormationIndex = 0);
    void SetCompanionCommand(EAegisCompanionCommand Command,
                             class AAegisCharacter* DesignatedTarget = nullptr,
                             const FVector& CommandLocation = FVector::ZeroVector);
    void CancelTacticalWindup();
    class AAegisCharacter* GetLinkedPlayer() const;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) EAegisCompanionCommand CompanionCommand = EAegisCompanionCommand::Guard;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FVector TacticalMoveGoal = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bWindingUpShot = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 TacticalMoveRequests = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 TacticalShots = 0;
    // v2 Guard only: counts candidate checks/acceptance, not hits or arrival.
    int32 GuardSlotChecks = 0, GuardSlotRejections = 0, GuardAlternateSelections = 0;
    FString GuardSlotReason = TEXT("legacy_formation");
    bool IsV2GuardSlotEnabled() const { return bV2GuardSlots; }
    // Geometry-only projected standing muzzle. Shared with the bounded fixture
    // so its occlusion precondition uses the same physical height as Guard.
    bool GetGuardCandidateMuzzle(const FVector& NavigablePoint, FVector& OutMuzzle) const;
    bool IsTacticalTrialEnabled() const { return bTacticalTrial; }
    virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;

  protected:
    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UFUNCTION() void PerceptionChanged(AActor* Actor, FAIStimulus Stimulus);

  private:
    FAegisDecisionTelemetry DecisionTelemetry;
    aegis::Action LastSwitchFrom = aegis::Action::Follow, LastSwitchTo = aegis::Action::Follow;
    double LastSwitchAt = -1;
    void RecordDecisionTelemetry(const aegis::Observation& Observation, aegis::Action Previous);
    void RecordSupportHeal(float Applied);
    TWeakObjectPtr<class AAegisCharacter> FollowPositionLink;
    FAIRequestID FollowLinkMoveRequestId = FAIRequestID::InvalidRequest;
    class AAegisCharacter* ResolveFollowPositionLink() const;
    void RefreshFollowLinkTelemetry();
    void CancelFollowLinkMove();
    double LastSensedAt = -1000, NextQueryAt = 0, SupportReadyAt = 0;
    int32 QueryId = INDEX_NONE;
    FRandomStream PatrolRandom;
    TSharedPtr<FEnvQueryInstance> ActiveQuery;
    bool bPendingCoverQuery = false;
    bool bHasTargetMemory = false, bStopped = true;
    FTimerHandle MemoryTimer;
    int32 DiagnosticQueries = 0;
    void RememberTarget(const FVector& Location);
    void ForgetTarget();
    void ClearVisibleTarget();
    void CancelTacticalQuery();
    void QueryFinished(TSharedPtr<FEnvQueryResult> Result);
    bool bTacticalTrial = false, bTacticalContextReady = false, bV2GuardSlots = false;
    TWeakObjectPtr<class AAegisCharacter> LinkedPlayer, CommandTarget, WindupTarget;
    FVector EncounterAnchor = FVector::ZeroVector, CommandPoint = FVector::ZeroVector;
    FVector WindupAimPoint = FVector::ZeroVector;
    double WindupEndsAt = 0, NextTacticalMoveAt = 0, MoveBeforeAttackUntil = 0;
    double NextRepositionAt = 0, NextTacticalActionAt = 0, TargetAcquiredAt = 0;
    int32 FormationSlot = 0;
    float TacticalSide = 1;
    bool ExecuteTacticalTrial();
    bool MoveTactically(const FVector& Desired, bool bRequireClearShot = false);
    bool RepositionForTarget(const FVector& TargetLocation);
    bool HasClearShot(const class AAegisCharacter* Target, const FVector& AimPoint) const;
    bool IsCommittedFireLaneSafe(const FVector& AimPoint) const;
    bool InPlayerFireLane(const FVector& Point) const;
};

UCLASS()
class AEGISARENA_API UBTService_AegisObserve : public UBTService
{
    GENERATED_BODY()
  public:
    UBTService_AegisObserve();

  protected:
    virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
};

UCLASS()
class AEGISARENA_API UBTTask_AegisAction : public UBTTaskNode
{
    GENERATED_BODY()
  public:
    UBTTask_AegisAction();
    UPROPERTY(EditAnywhere, Category = "Aegis") FName Action = TEXT("Patrol");

  protected:
    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};

/** Immutable key comparison; the shared BT template never owns per-agent state. */
UCLASS()
class AEGISARENA_API UBTDecorator_AegisCondition : public UBTDecorator
{
    GENERATED_BODY()
  public:
    UBTDecorator_AegisCondition();
    UPROPERTY(EditAnywhere, Category = "Aegis") FName Key;
    UPROPERTY(EditAnywhere, Category = "Aegis") bool bInteger = false;
    UPROPERTY(EditAnywhere, Category = "Aegis") int32 Expected = 1;
    virtual FString GetStaticDescription() const override;

  protected:
    virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp,
                                            uint8* NodeMemory) const override;
};

UCLASS()
class AEGISARENA_API UEnvQueryContext_AegisThreat : public UEnvQueryContext
{
    GENERATED_BODY()
  protected:
    virtual void ProvideContext(FEnvQueryInstance& QueryInstance,
                                FEnvQueryContextData& ContextData) const override;
};
