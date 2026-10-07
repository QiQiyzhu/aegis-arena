#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/HUD.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "AegisCharacter.h"
#include "aegis/rules.hpp"
#include "aegis/trial.hpp"
#include "aegis/operation.hpp"
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool DirectorEnabled = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool PerformanceMode = false;
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
    UPROPERTY() int32 EnemiesSpawned = 0;
    UPROPERTY() int32 CompletedQueries = 0;
    UPROPERTY() int32 FailedQueries = 0;
    UPROPERTY() int32 CancelledQueries = 0;
    UPROPERTY() int32 ProfiledQueries = 0;
    UPROPERTY() double DecisionCpuMilliseconds = 0;
    UPROPERTY() double EqsCpuMilliseconds = 0;
    UPROPERTY() int32 FrameSamples = 0;
    UPROPERTY() double FrameMeanMilliseconds = 0;
    UPROPERTY() double FrameP95Milliseconds = 0;
    UPROPERTY() double GameThreadMeanMilliseconds = 0;
    UPROPERTY() double PeakResidentMiB = 0;
    UPROPERTY() bool RenderingEnabled = false;
};

UCLASS()
class AEGISARENA_API AAegisScenarioRunner : public AActor
{
    GENERATED_BODY()
  public:
    AAegisScenarioRunner();
    virtual void Tick(float DeltaSeconds) override;
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
    UFUNCTION(BlueprintCallable, Category = "Scenario") void StartInteractive();
    void DeployTrial(bool Pressure);
    const aegis::Trial& GetTrial() const { return Trial; }
    bool IsInteractive() const { return bInteractive; }
    int32 LivingEnemies() const;
    AAegisAICharacter* GetCompanion() const;
    float TrialDamageDealt = 0, TrialDamageTaken = 0;
    bool bPressureSelected = false;
    // The old wave-lifecycle fixture opts out explicitly; normal play uses objectives.
    UPROPERTY(EditAnywhere, Category = "Scenario") bool bObjectiveTrial = true;
    const aegis::Operation& GetOperation() const { return Operation; }
    FVector GetObjectiveLocation() const { return ObjectiveLocation; }
    bool IsObjectiveContested() const { return bObjectiveContested; }
    float GetObjectiveRadius() const { return 260.f; }
    bool IsPlayerInObjective() const;
    bool IsCompanionInObjective() const;
    bool IsNorthRouteFirst() const { return bNorthRouteFirst; }
    bool ToggleRelayRoute();
    bool IsUpgradePending() const { return bUpgradePending; }
    bool HasUpgrade(int32 Index) const { return Index >= 1 && Index <= 3 && (UpgradeMask & (1 << (Index-1))) != 0; }
    bool ChooseUpgrade(int32 Index);
    FString ObjectiveText() const;
    FString LastUpgrade;
    class AAegisSquadPlanner* GetSquadPlanner() const { return SquadPlanner; }

  protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

  private:
    UPROPERTY() TArray<TObjectPtr<AAegisAICharacter>> OwnedBots;
    UPROPERTY() TObjectPtr<class AAegisEncounterDirector> Director;
    UPROPERTY() TObjectPtr<class AAegisSquadPlanner> SquadPlanner;
    TArray<FVector> LastPositions;
    TArray<float> StuckSeconds;
    TArray<FAegisEpisodeResult> Results;
    FTimerHandle SampleTimer;
    FAegisEpisodeResult Current;
    int32 Episode = 0;
    double StartedAt = 0;
    double LastFrameAt = 0, GameThreadTotalMilliseconds = 0, NextDirectorSpawnAt = 0;
    TArray<double> FrameMilliseconds;
    FRandomStream EncounterRandom;
    bool bInteractive = false, bQuitWhenDone = false;
    FTimerHandle StartupTimer;
    double StartupDeadline = 0;
    bool bNavigationRebuildRequested = false;
    FString CaptureDirectory;
    double NextCaptureAt = 2;
    int32 CaptureIndex = 0;
    aegis::Trial Trial;
    aegis::Operation Operation;
    FVector ObjectiveLocation = FVector::ZeroVector;
    bool bObjectiveContested = false, bUpgradePending = false;
    bool bNorthRouteFirst = false;
    int32 UpgradeMask = 0;
    double LastObjectiveAt = 0;
    UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> ObjectiveRing;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> ObjectiveMaterial;
    void SetObjectiveLocation();
    void UpdateObjective(float DeltaSeconds);
    void UpdateInteractive();
    bool SpawnTrialWave();
    UFUNCTION() void RecordInteractiveDamage(float Applied, AActor* Source, AActor* Victim);
    void Startup();
    void BuildArenaPresentation();
    AAegisAICharacter* SpawnConfiguredBot(const FVector& Location, EAegisTeam Team, bool Companion,
                                          bool Elite, int32 Seed, int32 TacticalRole = -1);
    void ApplyDirector();
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
    AAegisDebugHUD();
    virtual void DrawHUD() override;
    virtual void NotifyHitBoxClick(FName BoxName) override;
  private:
    void DrawDecisionLab(class AAegisDecisionLab* Lab);
    void DrawPortfolio(class AAegisScenarioRunner* Runner);
    UPROPERTY() TObjectPtr<class UFont> InterfaceFont;
    UPROPERTY() TObjectPtr<class UTexture2D> BriefingIllustration;
};

UCLASS()
class AEGISARENA_API AAegisPlayerController : public APlayerController
{
    GENERATED_BODY()
  public:
    bool bShowDiagnostics = false;
    bool bMenuOpen = false;
    bool bRestartConfirmation = false;
    bool bPausedForFocus = false;
    bool bReducedEffects = false;
    bool bPlannerOpen = false;
    // Saved local UI preference. Missing/invalid settings default to Chinese.
    bool bEnglishUI = false;
    FString CommandFeedback;
    double CommandFeedbackUntil = 0;
    void MenuAction(FName Action);
    void TogglePlannerPanel();
    void ToggleLanguage();
    void ToggleWindowMode();
    void ToggleEffects();
    void OnApplicationActivationChanged(bool bActive);
    void ClosePlannerPanel();
    bool SubmitPlannerInstruction(const FString& Instruction);
    virtual void SetupInputComponent() override;
  protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
  private:
    bool bPersistUILanguage = true;
    FDelegateHandle ApplicationActivationHandle;
    TSharedPtr<class SWidget> PlannerPanel;
    void Deploy();
    void Restart();
    void Guided();
    void Pressure();
    void ThirdUpgrade();
    void Guard();
    void FocusOrQuit();
    void Rally();
    void IssueCompanionCommand(int32 Mode);
    void PauseTrial();
    void Diagnostics();
};

UCLASS()
class AEGISARENA_API AAegisGameMode : public AGameModeBase
{
    GENERATED_BODY()
  public:
    AAegisGameMode();
};
