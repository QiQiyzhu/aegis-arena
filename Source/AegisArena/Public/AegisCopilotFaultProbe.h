#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AegisCopilotFaultProbe.generated.h"

// Development transport/lifecycle fixture. Requests use the public planner
// API; deploy, pause, manual override and restart use normal controller keys.
UCLASS()
class AEGISARENA_API AAegisCopilotFaultProbe : public AActor
{
    GENERATED_BODY()
  public:
    AAegisCopilotFaultProbe();
    void Initialize(class AAegisScenarioRunner* InRunner, class AAegisPlayerController* InController);
    virtual void Tick(float DeltaSeconds) override;
  protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
  private:
    UPROPERTY() TObjectPtr<class AAegisScenarioRunner> Runner;
    UPROPERTY() TObjectPtr<class AAegisPlayerController> PC;
    UPROPERTY() TObjectPtr<class AAegisSquadPlanner> Planner;
    TArray<TSharedPtr<class FJsonValue>> Assertions;
    TArray<FKey> Releases;
    FString Output, FailureReason;
    double StartedWall = 0, StageWall = 0, PausedGame = 0, TimeoutWall = 0, TimeoutElapsed = 0;
    int32 Stage = 0, InitialRequests = 0, InitialAccepted = 0, InitialRejected = 0, InitialFallbacks = 0;
    uint32 PreviousPawn = 0, PreviousCompanion = 0;
    bool bRunning = false, bAllPassed = true, bMayWrite = false;
    bool Check(const TCHAR* Name, bool Passed);
    bool IsGuard() const;
    bool SendRequest(const TCHAR* Assertion);
    void Tap(FKey Key);
    void Advance(int32 Next);
    void Finish(bool Passed, const FString& Reason);
};
