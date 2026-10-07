#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AegisTacticalFX.generated.h"

enum class EAegisTacticalCue : uint8 { Dash, ChargeRelease, Repair, Overclock, ArchiveClaim, RelayComplete };

// Presentation only. No damage, collision, navigation, RNG or objective writes.
UCLASS()
class AEGISARENA_API AAegisTacticalFX : public AActor
{
    GENERATED_BODY()
public:
    AAegisTacticalFX();
    static AAegisTacticalFX* Find(UWorld* World);
    static void Ensure(AActor* Arena);
    static void Emit(UObject* Context, EAegisTacticalCue Cue, const FVector& Position, const FVector& Direction=FVector::ForwardVector);
    virtual void Tick(float DeltaSeconds) override;
    static constexpr int32 ChannelCount=4;
    static constexpr int32 SlotsPerChannel=48;
    static constexpr int32 TransientBudget=ChannelCount*SlotsPerChannel;
    int32 GetActiveCount() const { return ActiveCount; }
    int32 GetPeakCount() const { return PeakCount; }
    int32 GetDroppedCount() const { return DroppedCount; }
    int32 GetEmittedCount() const { return EmittedCount; }
    int32 GetComponentCount() const { return FXLayers.Num()+Semantic.Num(); }
    bool HasSemanticCharge() const { return bSemanticCharge; }
    void ResetTransient();
private:
    struct FMote { float Age=0, Life=0; FVector Position, Velocity, Scale; FRotator Rotation; };
    FMote Motes[TransientBudget];
    UPROPERTY() TArray<TObjectPtr<class UInstancedStaticMeshComponent>> FXLayers;
    UPROPERTY() TArray<TObjectPtr<class UInstancedStaticMeshComponent>> Semantic;
    UPROPERTY() TObjectPtr<class AAegisScenarioRunner> Runner;
    TWeakObjectPtr<class APawn> LastPawn;
    int32 ActiveCount=0, PeakCount=0, DroppedCount=0, EmittedCount=0, LastArchiveMask=0;
    bool bComplete=false, bSemanticCharge=false;
    void Build();
    void Burst(EAegisTacticalCue Cue, const FVector& Position, const FVector& Direction);
    bool Reduced() const;
    void Add(int32 Channel, const FVector& Position, const FVector& Velocity, const FVector& Scale, const FRotator& Rotation, float Life);
};
