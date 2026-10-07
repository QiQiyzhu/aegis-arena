#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AegisV2Probe.generated.h"

// Disclosed input fixture: frozen AI, explicit wounds/placements and a removable occluder.
UCLASS()
class AEGISARENA_API AAegisV2Probe : public AActor
{
    GENERATED_BODY()
  public:
    AAegisV2Probe();
    void Initialize(class AAegisScenarioRunner* InRunner);
    virtual void Tick(float DeltaSeconds) override;
  protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
  private:
    UPROPERTY() TObjectPtr<class AAegisScenarioRunner> Runner;
    UPROPERTY() TObjectPtr<class AAegisPlayerController> PC;
    UPROPERTY() TObjectPtr<class AAegisPortfolio> Portfolio;
    UPROPERTY() TObjectPtr<class AStaticMeshActor> Cover;
    TWeakObjectPtr<class AAegisPlayerCharacter> PreviousPawn;
    TWeakObjectPtr<class AAegisAICharacter> FirstTarget, SecondTarget;
    TSet<TWeakObjectPtr<class AAegisAIController>> FrozenControllers;
    TSet<FKey> Held;
    TArray<FKey> Releases;
    TArray<TSharedPtr<class FJsonValue>> Assertions, Interventions;
    TArray<FString> Traces;
    FString Output;
    double StartedWall = 0, StageWall = 0, StageGame = 0, PausedGame = 0;
    float OverclockBefore = 0;
    double ChargeBefore = 0;
    int32 Stage = 0;
    bool bRunning = false, bAllPassed = true, bMayWrite = false;
    void FreezeAI();
    void Key(FKey Key, bool Down);
    void Tap(FKey Key);
    void Advance(int32 Next);
    bool Check(const TCHAR* Name, bool Passed);
    void Place(class AAegisCharacter* Character, const FVector& Location);
    bool Wound(class AAegisCharacter* Character, float Amount);
    class AAegisAICharacter* Hostile(int32 Index = 0) const;
    bool AimAt(class AAegisCharacter* Character);
    void Finish(bool Passed, const FString& Reason);
};
