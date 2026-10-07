#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "aegis/portfolio.hpp"
#include "aegis/survey.hpp"
#include "AegisPortfolio.generated.h"

class AAegisCharacter;
class AAegisPlayerCharacter;
class AAegisScenarioRunner;
class AAegisPlayerController;

struct FAegisPortfolioStatistics
{
    int32 EnergyEarned = 0, EnergySpent = 0, EnergyOverflow = 0;
    int32 PulsesUsed = 0, RepairsUsed = 0, EnemiesRewarded = 0, StagesRewarded = 0;
    // Actual E-repair only: excludes Support, restorative Q, life link and stage recovery.
    float PlayerHealing = 0, CompanionHealing = 0;
    float AlliedDamageDealt = 0, PlayerDamageTaken = 0, CompanionDamageTaken = 0;
    int32 ChargedShots = 0, Overclocks = 0;
    int32 SurveyKeys = 0, SurveySupplies = 0, SurveyBoosts = 0, SurveyCancelled = 0;
    float SurveyPlayerHealing = 0, SurveyCompanionHealing = 0;
    float SymbiosisPlayerHealing = 0, SymbiosisCompanionHealing = 0;
    int32 CompanionStateChanges = 0, CompanionShortReversals = 0;
    int32 CompanionShots = 0, CompanionMoves = 0, GuardChecks = 0, GuardRejects = 0, GuardAlternates = 0;
    bool CompanionSurvived = true;
    FString LastTransaction;
};

UCLASS()
class AEGISARENA_API AAegisPortfolio : public AActor
{
    GENERATED_BODY()
  public:
    AAegisPortfolio();
    static AAegisPortfolio* Find(UWorld* World);
    void Initialize(AAegisScenarioRunner* InRunner);
    virtual void Tick(float DeltaSeconds) override;
    // Optional precise transition hook: call after Trial.observe and before pausing for upgrades.
    void ObserveProgress();
    bool TrySpendPulse();
    bool TryRepair();
    bool IsV2() const { return bV2; }
    bool IsV23() const { return bV23; }
    FVector GetSurveyLocation(int32 Node) const;
    int32 GetSurveyClaimedMask() const { return static_cast<int32>(Survey.claimedMask); }
    float GetSurveyProgress() const { return static_cast<float>(Survey.progress / aegis::SurveyCache::scanSeconds); }
    int32 GetSurveyNode() const { return Survey.node; }
    bool IsSurveySupplySelected() const { return Survey.supplySelected; }
    bool HasSurveyKey() const { return Survey.keyPending; }
    int32 GetSurveyBoostRelayKey() const { return Survey.boostRelayKey; }
    bool IsSurveyBoostActive() const;
    bool CanClaimSurveySupply() const;
    // Call only on a frame that can actually advance an uncontested data relay.
    // This consumes a stored key once and binds the multiplier to that relay.
    float ApplySurveyRelayBoost();
    bool CanCharge() const { return bV2 && CanTransact(); }
    bool TrySpendChargedShot();
    bool TryOverclock();
    int32 GetRepairQuote() const;
    float GetOverclockRemaining() const;
    float GetOverclockMultiplier() const { return GetOverclockRemaining() > 0 ? 2.f : 1.f; }
    bool IsOverclockUsed() const;
    void ApplySymbiosis(float ProgressSeconds, bool bPlayerInside, bool bCompanionInside);
    int32 GetEnergy() const { return Economy.energy; }
    float GetRepairCooldown() const;
    const FString& GetFeedback() const { return Feedback; }
    float GetFeedbackAge() const;
    const FAegisPortfolioStatistics& GetStatistics() const { return Statistics; }
    const FString& GetReportPath() const { return ReportPath; }
    const FString& GetTracePath() const { return TracePath; }
    static constexpr int32 MaximumEnergy = aegis::PortfolioEconomy::maximumEnergy;
    static constexpr int32 PulseCost = aegis::PortfolioEconomy::pulseCost;
    static constexpr int32 RepairCost = aegis::PortfolioEconomy::repairCost;

  protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

  private:
    UPROPERTY() TObjectPtr<AAegisScenarioRunner> Runner;
    UPROPERTY() TObjectPtr<AAegisPlayerCharacter> Player;
    UPROPERTY() TObjectPtr<AAegisPlayerController> Controller;
    TSet<TWeakObjectPtr<AAegisCharacter>> BoundCharacters;
    TMap<TWeakObjectPtr<AAegisCharacter>, uint64> CharacterIdentities;
    uint64 NextCharacterIdentity = 1;
    aegis::PortfolioEconomy Economy;
    aegis::SurveyCache Survey;
    UPROPERTY() TArray<TObjectPtr<class UInstancedStaticMeshComponent>> SurveyIndicators;
    double SurveyStartedAt = 0, SurveyUpdatedAt = 0;
    bool bSurveyHeld = false;
    int32 SurveyVisualMask = -1, SurveyVisualNode = -2, SurveyVisualSegments = -1;
    FAegisPortfolioStatistics Statistics;
    FDelegateHandle SpawnHandle;
    FString OutputDirectory, RunId, TracePath, ReportPath, Feedback;
    double FeedbackAt = -1, SessionStartedAt = 0, LastTrialElapsed = 0;
    int32 LastPhase = -1, LastWave = -1, SeenUpgrades = 0, EventCount = 0;
    bool bInitialized = false, bFinished = false, bLogHealthy = true;
    bool bV2 = false, bV23 = false;
    bool bSessionNorthRoute = false;
    TSet<int32> UsedOverclocks;
    int32 ActiveOverclockKey = -1;
    double OverclockEndsAt = 0;
    FString CompanionLastState, CompanionPreviousState;
    double CompanionStateChangedAt = -1;
    void SynchronizePlayer();
    void ConfigurePlayer();
    void UpdateCamera(float DeltaSeconds);
    void BindCharacter(AActor* Actor);
    void UnbindCharacters();
    bool CanTransact() const;
    void RepairInput();
    void OverclockInput();
    void RouteInput();
    void SurveyPressed();
    void SurveyReleased();
    void SurveyModeInput();
    void UpdateSurvey();
    void CancelSurvey(const TCHAR* Reason);
    void FinishSurveyBoost(const TCHAR* Reason);
    void BuildSurveyIndicators();
    void UpdateSurveyIndicators();
    bool CanReserveSurveyKey() const;
    void GetSurveyRecipients(float& SelfHealing, float& AllyHealing) const;
    int32 CurrentRelayKey() const;
    void GetRepairRecipients(float& SelfHealing, float& AllyHealing) const;
    void UpdateStatistics();
    void SetFeedback(const FString& Text);
    void RecordEvent(const FString& Type, const TSharedRef<class FJsonObject>& Data);
    void RecordTransaction(const FString& Reason, int32 Before, int32 Offered);
    void FinishSession(const FString& Outcome, bool bCompleted);
    UFUNCTION() void OnDamage(float Applied, AActor* Source, AActor* Victim);
};
