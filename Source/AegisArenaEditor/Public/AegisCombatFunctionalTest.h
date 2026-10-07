#pragma once
#include "CoreMinimal.h"
#include "FunctionalTest.h"
#include "AegisCombatFunctionalTest.generated.h"
UCLASS()
class AEGISARENAEDITOR_API AAegisCombatFunctionalTest : public AFunctionalTest
{
    GENERATED_BODY()
  public:
    virtual void StartTest() override;
    // This class lives in an Editor module to exclude fixtures from cooking,
    // but combat/navigation assertions require a ticking PIE game world.
    virtual bool IsEditorOnlyLoadedInPIE() const override { return true; }

  protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

  private:
    UPROPERTY() TObjectPtr<class AAegisCharacter> Shooter;
    UPROPERTY() TObjectPtr<class AAegisCharacter> Target;
    UPROPERTY() TObjectPtr<class AAegisAICharacter> MemoryBot;
    UPROPERTY() TObjectPtr<class AAegisCharacter> MemoryTarget;
    UPROPERTY() TObjectPtr<class AAegisAIController> MemoryAI;
    UPROPERTY() TObjectPtr<class UEnvQuery> MemoryQuery;
    FTimerHandle Timer;
    FVector MemoryOrigin = FVector::ZeroVector, RememberedLocation = FVector::ZeroVector;
    double PhaseDeadline = 0, LastObservationTime = 0;
    int32 MemoryPhase = 0, QueriesBeforeCancellation = 0;
    bool bAssertionsPassed = false;
    void Verify();
    void StartMemoryVerification();
    void VerifyMemory();
    void CompleteVerification();
    void Cleanup();
};
