#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AegisPortfolioProbe.generated.h"

// Disclosed Development fixture: normal PC keys, frozen AI, one explicit hostile damage call.
// This is transaction/input integration evidence, never natural combat or human usability evidence.
UCLASS()
class AEGISARENA_API AAegisPortfolioProbe : public AActor
{
    GENERATED_BODY()
  public:
    AAegisPortfolioProbe();
    void Initialize(class AAegisScenarioRunner* InRunner);
    virtual void Tick(float DeltaSeconds) override;
  protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
  private:
    UPROPERTY() TObjectPtr<class AAegisScenarioRunner> Runner;
    UPROPERTY() TObjectPtr<class AAegisPlayerController> PC;
    UPROPERTY() TObjectPtr<class AAegisPortfolio> Portfolio;
    TWeakObjectPtr<class AAegisPlayerCharacter> PreviousPawn;
    TSet<TWeakObjectPtr<class AAegisAIController>> FrozenControllers;
    TArray<TSharedPtr<class FJsonValue>> Assertions;
    TArray<FKey> Releases;
    TArray<FString> PortfolioTraces;
    FString Output;
    double StartedWall = 0, StageWall = 0, PausedGame = 0;
    float FixtureDamageApplied = 0;
    int32 Stage = 0;
    bool bRunning = false, bAllPassed = true, bMayWrite = false, bHeldFire = false;
    void FreezeAI();
    void Tap(FKey Key);
    void Advance(int32 Next);
    bool Check(const TCHAR* Name, bool Passed);
    void Finish(bool Passed, const FString& Reason);
};
