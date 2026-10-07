#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "AegisPortfolioCapture.generated.h"
UCLASS()
class AEGISARENA_API AAegisPortfolioCapture : public AActor
{
    GENERATED_BODY()
  public:
    AAegisPortfolioCapture();
    void Initialize(class AAegisScenarioRunner* InRunner);
    void RecordSound(const TCHAR* Name, const FVector& Location, float Volume, float Pitch = 1.f,
                     const TCHAR* AssetPath = TEXT(""));
    void RecordMusic(const TCHAR* State, const TCHAR* Asset, float Volume, float FadeSeconds,
                     bool bLoop, const TCHAR* Reason, int32 Channel = -1);
    virtual void Tick(float DeltaSeconds) override;

  protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

  private:
    UPROPERTY() TObjectPtr<class AAegisScenarioRunner> Runner;
    UPROPERTY() TObjectPtr<class AAegisPlayerController> PC;
    TSet<FKey> Held;
    TArray<FKey> Releases;
    TArray<FVector> Path;
    TArray<TSharedPtr<class FJsonValue>> Samples, Frames, AudioEvents, MusicEvents;
    int32 PathIndex = 0, Frame = 0, Completed = 0, LastPhase = -1, Shots = 0, LastPlayerShots = 0,
          NextUpgrade = 2;
    int32 Inputs = 0;
    int32 ClearRouteChecks = 0, ClearRouteAccepted = 0;
    bool bClearRouteRecovery = false;
    FString LastPathStatus = TEXT("not_queried");
    double Clock = 0, NextPath = 0, NextSample = 0, PhaseAt = 0, FinishedAt = -1, NextCommand = 0,
           NextUtility = 0;
    double StartedWall = 0, CaptureEvery = 0.0333333333, NextCapture = 0;
    bool bRunning = false, bCapture = true, bPending = false, bDebugShown = false, bDebugClosed = false;
    double EvadeUntil = 0, NextDash = 0, DebugShownAt = 0;
    bool bV2 = false, bRouteSelected = false, bLanguageShown = false, bLanguageRestored = false;
    double NextCharge = 8, ChargeReleaseAt = 0;
    double NextSurveyAttempt = 0;
    FVector EvadeDirection = FVector::ZeroVector;
    FVector LastGoal = FVector::ZeroVector, LastDirection = FVector::ZeroVector;
    FString Output;
    FDelegateHandle ScreenshotHandle;
    void SetKey(FKey Key, bool Down);
    void Tap(FKey Key);
    void Release();
    void Drive(class AAegisPlayerCharacter* Player);
    void Sample();
    void Capture();
    void ScreenshotProcessed();
    void Finish(bool Valid);
};
