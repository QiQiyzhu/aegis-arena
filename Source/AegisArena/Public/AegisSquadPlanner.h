#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HttpFwd.h"
#include "AegisSquadPlanner.generated.h"

// Slow, optional local planning selects bounded intentions. The existing
// tactical controller retains navigation, perception and weapon authority.
UCLASS()
class AEGISARENA_API AAegisSquadPlanner : public AActor
{
    GENERATED_BODY()
  public:
    AAegisSquadPlanner();
    void Initialize(class AAegisScenarioRunner* InRunner);
    bool RequestPlan(const FString& Instruction);
    void CancelPlan(const FString& Reason, bool bReturnToGuard = true);
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString StatusLabel = TEXT("Classic squad control ready");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString DecisionLabel;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString ProviderLabel = TEXT("Classic BT");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString StepsLabel;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString LastOutcome;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString TracePath;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float LastLatencyMs = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bRequestPending = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bPlanActive = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 RequestCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 AcceptedPlans = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 RejectedPlans = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 Fallbacks = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 CompletedSteps = 0;

  protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

  private:
    friend struct FAegisPlannerProtocolAccess;
    enum class ESkill : uint8 { Guard, CaptureRelay, FocusVisible, Regroup };
    struct FStep
    {
        ESkill Skill = ESkill::Guard;
        FString Token = TEXT("none");
        TWeakObjectPtr<class AAegisCharacter> Target;
    };
    UPROPERTY() TObjectPtr<class AAegisScenarioRunner> Runner;
    TWeakObjectPtr<class AAegisCharacter> BoundPlayer;
    TWeakObjectPtr<class AAegisAICharacter> BoundCompanion;
    TWeakObjectPtr<class AAegisAIController> BoundController;
    TWeakObjectPtr<class AAegisCharacter> DamageListenerTarget;
    TMap<FString, TWeakObjectPtr<class AAegisCharacter>> RequestTargets;
    TArray<FStep> Steps;
    FHttpRequestPtr ActiveRequest;
    FString RequestedModel, RequestSecret;
    uint64 Generation = 0;
    int32 BoundWave = 0, BoundRelay = 0, StepIndex = 0, TraceEvents = 0;
    int64 TraceBytes = 0;
    double RequestWall = 0, RequestBudgetSeconds = 10, StepStartedGame = 0, PlanStartedGame = -1, ProgressGame = 0;
    double BestDistance = 0, StepCharge = 0, NextRegroupUpdateGame = 0;
    float StepDamage = 0;
    FVector RegroupPoint = FVector::ZeroVector;
    bool bStepStarted = false, bTraceCapped = false;

    bool ResolveActors(class AAegisCharacter*& Player, class AAegisAICharacter*& Companion,
                       class AAegisAIController*& Controller) const;
    bool ContextMatches(FString& Reason) const;
    bool IsVisibleTarget(const class AAegisCharacter* Target) const;
    bool ResolveNavigableGoal(const FVector& Desired, FVector& Goal) const;
    bool DecodePlan(const FString& Content, TArray<FStep>& Result, FString& Label, FString& Reason) const;
    TSharedPtr<class FJsonObject> BuildObservation();
    TSharedPtr<class FJsonObject> BuildSchema() const;
    void ResponseReceived(uint64 RequestGeneration, FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess);
    bool BeginStep();
    void UpdateStep();
    void CompleteStep(const FString& Outcome);
    void RejectAndFallback(const FString& Reason);
    void ReturnToGuard();
    void RemoveDamageListener();
    void RecordEvent(const TCHAR* Kind, const FString& Detail, TSharedPtr<class FJsonObject> Data = nullptr);
    static FString SkillName(ESkill Skill);
    UFUNCTION() void ObserveDamage(float Amount, AActor* Source, AActor* Victim);
};
