#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "AegisAVProbe.generated.h"

// Normal-keyboard audio lifecycle check. Master submix WAVs prove rendered samples,
// not a microphone measurement or human listening/playability evaluation.
UCLASS()
class AEGISARENA_API AAegisAVProbe : public AActor
{
    GENERATED_BODY()
  public:
    AAegisAVProbe();
    void Initialize(class AAegisScenarioRunner* InRunner);
    virtual void Tick(float DeltaSeconds) override;
  private:
    UPROPERTY() TObjectPtr<class AAegisScenarioRunner> Runner;
    UPROPERTY() TObjectPtr<class AAegisPlayerController> PC;
    TWeakObjectPtr<APawn> PreviousPawn;
    TArray<FKey> Releases;
    TArray<TSharedPtr<class FJsonValue>> Assertions;
    FString Output, RecordingName;
    double StartedAt = 0, StageAt = 0, PausedGame = 0;
    int32 Stage = 0;
    bool bRunning = false, bMayWrite = false;
    void Tap(FKey Key);
    void Advance(int32 Next);
    bool Check(const TCHAR* Name, bool Passed);
    void StartAudio(const TCHAR* Name);
    void StopAudio();
    void Finish(bool Passed, const TCHAR* Reason);
};
