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

  protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

  private:
    UPROPERTY() TObjectPtr<class AAegisCharacter> Shooter;
    UPROPERTY() TObjectPtr<class AAegisCharacter> Target;
    FTimerHandle Timer;
    void Verify();
    void Cleanup();
};
