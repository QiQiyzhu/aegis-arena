#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/HUD.h"
#include "GameFramework/GameModeBase.h"
#include "AegisCharacter.h"
#include "aegis/rules.hpp"
#include "AegisLab.generated.h"

USTRUCT(BlueprintType)
struct FAegisScenarioDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Arena = TEXT("AegisArena");
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString PlayerConfig = TEXT("scripted");
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString CompanionPolicy = TEXT("utility");
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString EnemyPolicy = TEXT("behavior_tree");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1", ClampMax = "50"))
    int32 EnemyCount = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Seed = 1001;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1", ClampMax = "300"))
    float Duration = 60;
    bool IsValid() const;
};

USTRUCT()
struct FAegisEpisodeResult
{
    GENERATED_BODY()
    UPROPERTY() FString Engine = TEXT("unreal-runtime");
    UPROPERTY() FAegisScenarioDefinition Scenario;
    UPROPERTY() bool Win = false;
    UPROPERTY() float SurvivalSeconds = 0;
    UPROPERTY() float DamageDealt = 0;
    UPROPERTY() float DamageTaken = 0;
    UPROPERTY() int32 CompanionDeaths = 0;
    UPROPERTY() int32 EnemyDeaths = 0;
    UPROPERTY() float TimeToEngage = -1;
    UPROPERTY() int32 StuckEvents = 0;
    UPROPERTY() double DistanceTravelled = 0;
    UPROPERTY() int32 CoverUsage = 0;
    UPROPERTY() int32 DecisionCounts = 0;
};

UCLASS()
class AEGISARENA_API AAegisScenarioRunner : public AActor
{
    GENERATED_BODY()
  public:
    AAegisScenarioRunner();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario") FAegisScenarioDefinition Definition;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario",
              meta = (ClampMin = "1", ClampMax = "100"))
    int32 EpisodeCount = 10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
    TObjectPtr<class UBehaviorTree> Behavior;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario") TObjectPtr<class UEnvQuery> CoverQuery;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
    TObjectPtr<class UEnvQuery> AttackQuery;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
    TObjectPtr<class UEnvQuery> RetreatQuery;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scenario") FString Status = TEXT("Idle");
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Scenario") void RunBatch();
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Scenario") void CancelBatch();

  protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

  private:
    UPROPERTY() TArray<TObjectPtr<AAegisAICharacter>> OwnedBots;
    TArray<FVector> LastPositions;
    TArray<float> StuckSeconds;
    TArray<FAegisEpisodeResult> Results;
    FTimerHandle SampleTimer;
    FAegisEpisodeResult Current;
    int32 Episode = 0;
    double StartedAt = 0;
    void StartEpisode();
    void Sample();
    void FinishEpisode();
    void WriteReport();
    void DestroyEpisode();
    UFUNCTION() void RecordDamage(float Applied, AActor* Source, AActor* Victim);
};

UCLASS()
class AEGISARENA_API AAegisEncounterDirector : public AActor
{
    GENERATED_BODY()
  public:
    AAegisEncounterDirector();
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Director") TObjectPtr<AAegisCharacter> Player;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Director") int32 SpawnBudget = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Director") float EliteProbability = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Director") bool bRecovery = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Director") float Pressure = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Director") float RecoverySeconds = 0;

  protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

  private:
    aegis::Director Model;
    FTimerHandle Timer;
    float RecentDamage = 0;
    double StartedAt = 0;
    void Update();
    UFUNCTION() void PlayerDamaged(float Amount, AActor* Source, AActor* Victim);
};

UCLASS()
class AEGISARENA_API AAegisDebugHUD : public AHUD
{
    GENERATED_BODY()
  public:
    virtual void DrawHUD() override;
};

UCLASS()
class AEGISARENA_API AAegisGameMode : public AGameModeBase
{
    GENERATED_BODY()
  public:
    AAegisGameMode();
};
