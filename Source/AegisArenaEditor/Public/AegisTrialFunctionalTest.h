#pragma once
#include "CoreMinimal.h"
#include "FunctionalTest.h"
#include "AegisTrialFunctionalTest.generated.h"

// Fixture-authored damage isolates encounter transitions from stochastic AI combat.
// Place in an isolated playable test map with one configured scenario runner.
UCLASS()
class AEGISARENAEDITOR_API AAegisTrialFunctionalTest : public AFunctionalTest
{
    GENERATED_BODY()
  public:
    virtual void StartTest() override;
    virtual bool IsEditorOnlyLoadedInPIE() const override { return true; }

  protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

  private:
    UPROPERTY() TObjectPtr<class AAegisScenarioRunner> Runner;
    UPROPERTY() TObjectPtr<class AAegisPlayerCharacter> Player;
    FTimerHandle Timer;
    double Deadline = 0, RecoveryAt = 0, CheckAt = 0;
    int32 Stage = 0, Retry = 0, Assertions = 0;
    bool bAllPassed = true;
    void Step();
    bool Verify(bool Condition, const TCHAR* Name);
    bool VerifyPopulation(int32 ExpectedBots);
    bool VerifyBriefing();
    void StopAI();
    class AAegisAICharacter* FirstEnemy() const;
    void ClearEnemies();
    void Complete(bool Pass, const FString& Message);
};
