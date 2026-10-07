#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AegisInputProbe.generated.h"

// A development-only, bounded input fixture. No test behavior is compiled into Shipping.
UCLASS()
class AEGISARENA_API AAegisInputProbe : public AActor
{
    GENERATED_BODY()
  public:
    AAegisInputProbe();
    void Initialize(class AAegisScenarioRunner* InRunner, class AAegisPlayerController* InController);
    virtual void Tick(float DeltaSeconds) override;
  protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
  private:
    enum class EStage : uint8
    {
        Warmup, Briefing, Deploy, Walk, DashPressed, DashBlocked, Settle, Aim, FireHeld,
        FireReleased, PauseRequested, Paused, ResumeRequested, RestartHeld, Restarted, Screenshots, ExitMenu
    };
    UPROPERTY() TObjectPtr<class AAegisScenarioRunner> Runner;
    UPROPERTY() TObjectPtr<class AAegisPlayerController> PC;
    EStage Stage = EStage::Warmup;
    FString OutputDirectory;
    TArray<FString> ScreenshotPaths;
    TArray<TSharedPtr<class FJsonValue>> Assertions;
    TArray<FKey> PendingReleases;
    FDelegateHandle ScreenshotDelegate;
    FVector StartPosition = FVector::ZeroVector, DashPosition = FVector::ZeroVector;
    FVector ExpectedAim = FVector::ZeroVector, PausePosition = FVector::ZeroVector;
    double StartedWall = 0, StageWall = 0, StageGame = 0, DashReadyTime = 0, PausedGame = 0;
    int32 ShotsBeforeHold = 0, ShotsAfterRelease = 0, ScreenshotRequests = 0, ScreenshotProcessed = 0;
    uint32 PreviousPawnId = 0;
    bool bRunning = false, bActiveCaptured = false, bMayWriteReport = false;
    bool bExitThroughMenu = false;
    void Key(const FKey& Input, EInputEvent Event);
    void Tap(const FKey& Input);
    void Advance(EStage Next);
    bool Check(const TCHAR* Name, bool Passed, const FString& Detail);
    void Capture(const TCHAR* Name);
    void ScreenshotCompleted();
    void Finish(bool Passed, const FString& Reason);
};
