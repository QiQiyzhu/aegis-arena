#pragma once
#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "GameFramework/Actor.h"
#include "AegisV23Probe.generated.h"

// Disclosed survey input fixture. AI is frozen; placements and wounds are logged.
UCLASS()
class AEGISARENA_API AAegisV23Probe : public AActor
{
    GENERATED_BODY()
  public:
    AAegisV23Probe();
    void Initialize(class AAegisScenarioRunner* InRunner);
    virtual void Tick(float DeltaSeconds) override;
  protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
  private:
    UPROPERTY() TObjectPtr<class AAegisScenarioRunner> Runner;
    UPROPERTY() TObjectPtr<class AAegisPlayerController> PC;
    UPROPERTY() TObjectPtr<class AAegisPortfolio> Portfolio;
    TSet<TWeakObjectPtr<class AAegisAIController>> FrozenControllers;
    TSet<FKey> Held;
    TArray<FKey> Releases;
    TArray<TSharedPtr<class FJsonValue>> Assertions, Interventions;
    FString Output, Trace;
    double StartedWall = 0, StageWall = 0, StageGame = 0, PausedGame = 0, ChargeBefore = 0;
    int32 Stage = 0;
    bool bRunning = false, bAllPassed = true, bMayWrite = false;
    void FreezeAI();
    void Key(FKey Key, bool Down);
    void Tap(FKey Key);
    void Advance(int32 Next);
    bool Check(const TCHAR* Name, bool Passed);
    void Place(class AAegisCharacter* Character, const FVector& Location);
    bool Wound(class AAegisCharacter* Character, float Amount);
    class AAegisAICharacter* Hostile() const;
    void Finish(bool Passed, const FString& Reason);
};
