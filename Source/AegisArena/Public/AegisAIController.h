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

UCLASS(ClassGroup = (Aegis), meta = (BlueprintSpawnableComponent))
class AEGISARENA_API UAegisDecisionComponent : public UActorComponent
{
    GENERATED_BODY()
  public:
    UAegisDecisionComponent();
    aegis::Scores Scores{};
    aegis::Action Selected = aegis::Action::Follow;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy") bool bUtilityPolicy = true;
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
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 CoverUses = 0;
    double DecisionCpuSeconds = 0, QueryCpuSeconds = 0;
    int32 CompletedQueries = 0, FailedQueries = 0, ProfiledQueries = 0;
    TWeakObjectPtr<class AAegisCharacter> ObservedTarget;
    TWeakObjectPtr<class AAegisCharacter> ObservedAlly;
    bool bPaused = false, bHasTacticalPoint = false;
    void RefreshDecision();
    bool RequestTacticalPoint(class UEnvQuery* Query);
    void ShutdownAI();
    bool ExecuteAction(FName Action);
    virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;

  protected:
    virtual void OnPossess(APawn* InPawn) override;
    virtual void OnUnPossess() override;
    UFUNCTION() void PerceptionChanged(AActor* Actor, FAIStimulus Stimulus);

  private:
    double LastSensedAt = -1000, NextQueryAt = 0, SupportReadyAt = 0;
    int32 QueryId = INDEX_NONE;
    FRandomStream PatrolRandom;
    TSharedPtr<FEnvQueryInstance> ActiveQuery;
    bool bPendingCoverQuery = false;
    int32 DiagnosticQueries = 0;
    void QueryFinished(TSharedPtr<FEnvQueryResult> Result);
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
