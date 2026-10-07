#pragma once
#include "CoreMinimal.h"
#include "FunctionalTest.h"
#include "AegisGuardSlotFunctionalTest.generated.h"

// A controlled geometry regression, not a win-rate or natural-play test.
UCLASS()
class AEGISARENAEDITOR_API AAegisGuardSlotFunctionalTest : public AFunctionalTest
{
    GENERATED_BODY()
  public:
    virtual void StartTest() override;
    virtual bool IsEditorOnlyLoadedInPIE() const override { return true; }
  protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
  private:
    UPROPERTY() TObjectPtr<class AAegisCharacter> Leader;
    UPROPERTY() TObjectPtr<class AAegisCharacter> Target;
    UPROPERTY() TObjectPtr<class AAegisAICharacter> Bot;
    UPROPERTY() TObjectPtr<class AAegisAIController> AI;
    UPROPERTY() TObjectPtr<class AStaticMeshActor> LowCover;
    UPROPERTY() TObjectPtr<class AStaticMeshActor> OpaqueCover;
    FTimerHandle Timer;
    TArray<TSharedPtr<class FJsonValue>> Assertions;
    TSharedPtr<class FJsonObject> PreferredPathDetails, AlternatePathDetails;
    FString Output;
    FVector StartedPosition = FVector::ZeroVector, HiddenMemory = FVector::ZeroVector;
    double StartedWall = 0, StartedGame = 0, Deadline = 0, StageAt = 0, MovementCm = 0;
    int32 Stage = 0, ShotsAtEngagement = 0, HiddenShots = 0, HiddenChecks = 0;
    bool bImproved = false, bAllPassed = true, bRunning = false, bMayWrite = false;
    bool bObservedWindup = false, bHiddenChecksUnchanged = false, bHiddenShotsUnchanged = false;
    void Step();
    bool Setup();
    bool Check(const TCHAR* Name, bool Passed);
    bool HasSight() const;
    bool SlotTrace(const FVector& GroundPoint) const;
    bool SlotPath(const FVector& GroundPoint, TSharedPtr<class FJsonObject>& Details) const;
    class AStaticMeshActor* SpawnCover(const FVector& Location, const FVector& Scale);
    void Complete(bool Passed, const FString& Reason);
    void Cleanup();
};
