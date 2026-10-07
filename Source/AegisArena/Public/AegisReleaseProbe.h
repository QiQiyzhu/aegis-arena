#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AegisReleaseProbe.generated.h"

UCLASS()
class AEGISARENA_API AAegisReleaseProbe : public AActor
{
    GENERATED_BODY()
public:
    AAegisReleaseProbe();
    void Initialize(class AAegisScenarioRunner* InRunner);
    virtual void Tick(float DeltaSeconds) override;
private:
    UPROPERTY() TObjectPtr<class AAegisScenarioRunner> Runner;
    UPROPERTY() TObjectPtr<class AAegisPlayerController> PC;
#if !UE_BUILD_SHIPPING
    int32 Stage = -1, Checks = 0;
    double Started = 0, Next = 0, PausedClock = 0;
    bool bRunning = false;
    bool bArt = false;
    int32 EffectsBefore = 0, FullEmission = 0, SnapshotEnergy = 0;
    float SnapshotHealth = 0;
    FString Output;
    bool Check(const TCHAR* Name, bool bPassed);
    void Finish(bool bPassed);
#endif
};
