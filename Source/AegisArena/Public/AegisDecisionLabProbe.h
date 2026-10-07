#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AegisDecisionLabProbe.generated.h"

// Synthetic PlayerController input, not human usability or policy-quality evidence.
// All execution is opt-in and excluded from Shipping.
UCLASS()
class AEGISARENA_API AAegisDecisionLabProbe : public AActor
{
    GENERATED_BODY()
public:
    AAegisDecisionLabProbe();
    void Initialize(class AAegisDecisionLab* InLab, class AAegisPlayerController* InController);
    virtual void Tick(float DeltaSeconds) override;
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    enum class EStage : uint8
    {
        Warmup, PolicyOne, PolicyTwo, PolicyThree, PolicyReset, Deploy, Walk, Dash, DashRepeat,
        Settle, Aim, Fire, FireReleased, Debug, Pause, Frozen, PausedToggle, Resume,
        RestartHeld, Restarted, SecondDeploy, Screenshots
    };
    UPROPERTY() TObjectPtr<class AAegisDecisionLab> Lab;
    UPROPERTY() TObjectPtr<class AAegisPlayerController> PC;
    TWeakObjectPtr<class AAegisAICharacter> PreviousCompanion;
    EStage Stage = EStage::Warmup;
    FString OutputDirectory;
    TArray<FString> ScreenshotPaths;
    TArray<TSharedPtr<class FJsonValue>> Assertions;
    TArray<FKey> PendingReleases;
    FDelegateHandle ScreenshotDelegate;
    FVector StartPosition = FVector::ZeroVector, DashPosition = FVector::ZeroVector;
    FVector ExpectedAim = FVector::ZeroVector;
    double StartedWall = 0, StageWall = 0, StageGame = 0, PausedGame = 0, DashReadyAt = 0;
    double WalkDistance = 0, DashDistance = 0;
    int32 InitialSeed = 0, ShotsBefore = 0, ShotsAfter = 0, HeldShots = 0;
    int32 ScreenshotRequests = 0, ScreenshotProcessed = 0;
    uint32 PreviousPlayerId = 0;
    bool bRunning = false, bMayWriteReport = false, bCapturedActive = false;
    void Key(const FKey& Input, EInputEvent Event);
    void Tap(const FKey& Input);
    void Advance(EStage Next);
    bool Check(const TCHAR* Name, bool Passed, const FString& Detail);
    void Capture(const TCHAR* Name);
    void ScreenshotCompleted();
    void Finish(bool Passed, const FString& Reason);
};
