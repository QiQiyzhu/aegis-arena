#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AegisCopilotProbe.generated.h"

UCLASS()
class AEGISARENA_API AAegisCopilotProbe : public AActor
{
    GENERATED_BODY()
public:
    AAegisCopilotProbe();
    void Initialize(class AAegisScenarioRunner*, class AAegisPlayerController*);
    virtual void Tick(float DeltaSeconds) override;
private:
    UPROPERTY() TObjectPtr<class AAegisScenarioRunner> Runner;
    UPROPERTY() TObjectPtr<class AAegisPlayerController> PC;
    UPROPERTY() TObjectPtr<class AAegisSquadPlanner> Planner;
    FString Output, Provider, Plan;
    TArray<TSharedPtr<class FJsonValue>> Assertions;
    TArray<FString> Screenshots;
    TArray<FKey> Releases;
    FVector CompanionStart;
    double StartedWall = 0, StageWall = 0, PausedGame = 0, MovingGame = 0;
    int32 Stage = 0, RequestsBefore = 0, AcceptedBefore = 0;
    uint32 PreviousPawnId = 0;
    bool bRunning = false, bAllPassed = true, bFixtureService = false;
    bool Check(const TCHAR* Name, bool Passed);
    void Capture(const TCHAR* Name);
    void Tap(FKey Key);
    void SlateKey(FKey Key);
    void Advance(int32 Next);
    void Finish(bool Passed);
};
