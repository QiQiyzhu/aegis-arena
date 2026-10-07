#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AegisCharacter.h"
#include "AegisDecisionLab.generated.h"

// A compact interactive view and a native, instrumented evaluation of the SAME
// companion BT/Utility/EQS path. The automated player is explicitly a BT bot.
UCLASS()
class AEGISARENA_API AAegisDecisionLab : public AActor
{
    GENERATED_BODY()
  public:
    AAegisDecisionLab();
    void Initialize(class AAegisScenarioRunner* InAssets);
    virtual void Tick(float DeltaSeconds) override;
    void StartEncounter();
    void ResetEncounter();
    void SetPolicy(const FString& Value);
    double GetElapsed() const;
    class AAegisAIController* GetCompanionController() const;
    FString Phase = TEXT("Briefing"), Policy = TEXT("baseline");
    int32 Seed = 2001, EnemyCount = 2, EnemyDeaths = 0, Layout = 0;
    float Duration = 60;
    bool bAutomated = false;
    UPROPERTY() TObjectPtr<AAegisCharacter> Player;
    UPROPERTY() TObjectPtr<AAegisAICharacter> Companion;
    double PlayerDamageTaken = 0, CompanionDamageTaken = 0;
    double PlayerDamageDealt = 0, CompanionDamageDealt = 0;

  private:
    UPROPERTY() TObjectPtr<class AAegisScenarioRunner> Assets;
    UPROPERTY() TArray<TObjectPtr<AAegisCharacter>> Actors;
    FString Output, Trace, PreviousLeaf, PreviousSelected, LastQueryKey;
    int32 Episode = 0, Episodes = 1, FirstSeed = 2001, LastDecision = -1, CaptureIndex = 0;
    double StartedAt = 0, FinishedAt = 0, LastSampleAt = 0, NextCapture = 0;
    double PlayerHealing = 0, CompanionHealing = 0, PlayerDeathAt = -1, CompanionDeathAt = -1;
    double LastPlayerHP = 100, LastCompanionHP = 100, LastPlayerDamage = 0, LastCompanionDamage = 0;
    double CompanionAliveSeconds = 0, CompanionStuckSeconds = 0;
    FVector LastCompanionPosition = FVector::ZeroVector;
    TMap<FString, double> ActualSeconds, SelectedSeconds;
    TSet<TWeakObjectPtr<AActor>> RecordedDeaths;
    bool bCapture = false, bAdvance = false;
    void ClearEncounter();
    AAegisAICharacter* SpawnBot(FVector Position, EAegisTeam Team, bool bCompanion, int32 Index);
    void Sample();
    void Finish(const FString& Reason);
    FString Role(const AActor* Actor) const;
    void Event(const FString& Kind, const TSharedRef<class FJsonObject>& Data);
    UFUNCTION() void RecordDamage(float Applied, AActor* Source, AActor* Victim);
};
