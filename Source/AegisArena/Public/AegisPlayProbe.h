#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AegisPlayProbe.generated.h"

// Observation-only telemetry plus normal local input, compiled active in Development only.
UCLASS()
class AEGISARENA_API AAegisPlayProbe : public AActor
{
    GENERATED_BODY()
  public:
    AAegisPlayProbe();
    void Initialize(class AAegisScenarioRunner* InRunner, class AAegisPlayerController* InController);
    virtual void Tick(float DeltaSeconds) override;
  protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
  private:
    UPROPERTY() TObjectPtr<class AAegisScenarioRunner> Runner;
    UPROPERTY() TObjectPtr<class AAegisPlayerController> PC;
    struct FObservation
    {
        FVector Position = FVector::ZeroVector;
        int32 Shots = 0, Pulses = 0;
        bool Winding = false, Dead = false;
        FString Role;
    };
    TMap<uint32, FObservation> Observed;
    TMap<FString, double> Totals;
    TArray<TSharedPtr<class FJsonValue>> Samples, Events;
    TArray<FString> Screenshots;
    TArray<FKey> Held, Releases;
    TArray<FVector> PathPoints;
    FDelegateHandle ScreenshotDelegate;
    FString OutputDirectory, Outcome;
    double StartedWall = 0, NextSample = 0, NextPath = 0, NextCommand = 3, LastUpgradeInput = -10;
    int32 ScreenshotProcessed = 0, PathIndex = 0, CommandIndex = 0;
    uint32 LastAimId = 0;
    int32 LastPhase = -1, LastWave = -1, LastRelay = -1;
    bool bRunning = false, bMayWrite = false, bStarted = false, bFinalCaptured = false;
    void SetKey(const FKey& Key, bool Down);
    void Tap(const FKey& Key);
    void ReleaseControls();
    void Observe();
    void Drive(class AAegisPlayerCharacter* Player, double WallAge);
    void Sample();
    TSharedPtr<class FJsonObject> TotalsJson() const;
    TSharedPtr<class FJsonObject> Event(const TCHAR* Kind);
    void Capture(const TCHAR* Name);
    void ScreenshotCompleted();
    void Finish(bool ValidRun);
    UFUNCTION() void Damaged(float Amount, AActor* Source, AActor* Victim);
};
