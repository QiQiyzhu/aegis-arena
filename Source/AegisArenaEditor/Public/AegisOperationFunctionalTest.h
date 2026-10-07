#pragma once
#include "CoreMinimal.h"
#include "FunctionalTest.h"
#include "AegisOperationFunctionalTest.generated.h"

// Objective integration only: AI is stopped, occupants are placed by the fixture,
// and health damage is authored. This is not combat balance or usability evidence.
UCLASS()
class AEGISARENAEDITOR_API AAegisOperationFunctionalTest : public AFunctionalTest
{
    GENERATED_BODY()
  public:
    AAegisOperationFunctionalTest();
    virtual void StartTest() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual bool IsEditorOnlyLoadedInPIE() const override { return true; }
  protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
  private:
    UPROPERTY() TObjectPtr<class AAegisScenarioRunner> Runner;
    UPROPERTY() TObjectPtr<class AAegisPlayerCharacter> Player;
    UPROPERTY() TObjectPtr<class AAegisAICharacter> Companion;
    UPROPERTY() TObjectPtr<class AAegisAICharacter> DamageSource;
    bool bFixtureRunning = false;
    int32 Stage = 0, Assertions = 0;
    double StartedWall = 0, DeadlineWall = 0, CheckAt = 0;
    double SampleAt = 0, SampleCharge = 0, PauseGameAt = 0;
    FVector FirstRelay = FVector::ZeroVector;
    void Step();
    bool Verify(bool Condition, const TCHAR* Name);
    void StopAI();
    void Place(class AAegisCharacter* Character, const FVector& GroundLocation);
    class AAegisAICharacter* FirstEnemy() const;
    void ClearEnemies();
    void Complete(bool Pass, const FString& Message);
};
