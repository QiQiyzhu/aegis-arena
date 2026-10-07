#include "AegisPortfolio.h"
#include "AegisLab.h"
#include "AegisAIController.h"
#include "AegisCharacter.h"
#include "AegisPortfolioPresentation.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/InputComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

namespace
{
FString PortfolioJson(const TSharedRef<FJsonObject>& Object)
{
    FString Text;
    auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
    FJsonSerializer::Serialize(Object, Writer);
    return Text;
}
const TCHAR* PortfolioPhaseName(aegis::TrialPhase Phase)
{
    switch (Phase)
    {
    case aegis::TrialPhase::Briefing: return TEXT("briefing");
    case aegis::TrialPhase::Active: return TEXT("active");
    case aegis::TrialPhase::Intermission: return TEXT("intermission");
    case aegis::TrialPhase::Won: return TEXT("won");
    default: return TEXT("lost");
    }
}
}

AAegisPortfolio::AAegisPortfolio()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = false;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}
AAegisPortfolio* AAegisPortfolio::Find(UWorld* World)
{
    if (!World) return nullptr;
    for (TActorIterator<AAegisPortfolio> It(World); It; ++It)
        if (IsValid(*It) && It->bInitialized) return *It;
    return nullptr;
}
void AAegisPortfolio::Initialize(AAegisScenarioRunner* InRunner)
{
    if (bInitialized || !IsValid(InRunner) || !GetWorld() ||
        !(FParse::Param(FCommandLine::Get(), TEXT("AegisPortfolio")) || FParse::Param(FCommandLine::Get(), TEXT("AegisV2"))) ||
        !InRunner->IsInteractive() || !InRunner->bObjectiveTrial)
        return;
    Runner = InRunner;
    bV2 = FParse::Param(FCommandLine::Get(), TEXT("AegisV2"));
    bV23 = bV2 && FParse::Param(FCommandLine::Get(), TEXT("AegisV23"));
    // Observe invalidation while paused, without advancing game-time progress.
    PrimaryActorTick.bTickEvenWhenPaused = bV23;
    FParse::Value(FCommandLine::Get(), TEXT("AegisPortfolioOutput="), OutputDirectory);
    if (OutputDirectory.IsEmpty()) OutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AegisPortfolio"));
    OutputDirectory = FPaths::ConvertRelativePathToFull(OutputDirectory);
    bInitialized = true;
    SpawnHandle = GetWorld()->AddOnActorSpawnedHandler(
        FOnActorSpawned::FDelegate::CreateUObject(this, &AAegisPortfolio::BindCharacter));
    if (bV23) BuildSurveyIndicators();
    SynchronizePlayer();
}
void AAegisPortfolio::UnbindCharacters()
{
    for (const auto& Weak : BoundCharacters)
        if (auto* Character = Weak.Get())
            if (Character->Health) Character->Health->OnDamaged.RemoveDynamic(this, &AAegisPortfolio::OnDamage);
    BoundCharacters.Reset();
    CharacterIdentities.Reset();
    NextCharacterIdentity = 1;
}
void AAegisPortfolio::BindCharacter(AActor* Actor)
{
    auto* Character = Cast<AAegisCharacter>(Actor);
    const TWeakObjectPtr<AAegisCharacter> Weak(Character);
    if (!bInitialized || !IsValid(Character) || !Character->Health || BoundCharacters.Contains(Weak)) return;
    // This is a world accounting observer. It never forwards actor data to AI perception.
    Character->Health->OnDamaged.AddUniqueDynamic(this, &AAegisPortfolio::OnDamage);
    BoundCharacters.Add(Weak);
    // UObject GetUniqueID is an internal index that may be reused after GC.
    // Weak keys include the object serial; monotonically assigned run IDs do not repeat.
    CharacterIdentities.Add(Weak, NextCharacterIdentity++);
}
void AAegisPortfolio::SynchronizePlayer()
{
    if (!bInitialized || !IsValid(Runner) || !GetWorld()) return;
    auto* PC = Cast<AAegisPlayerController>(GetWorld()->GetFirstPlayerController());
    auto* Current = PC ? Cast<AAegisPlayerCharacter>(PC->GetPawn()) : nullptr;
    if (!IsValid(Current) || Current == Player) return;
    if (!RunId.IsEmpty() && !bFinished) FinishSession(TEXT("restarted"), false);
    if (Controller) DisableInput(Controller);
    UnbindCharacters();
    Controller = PC;
    Player = Current;
    Economy = {};
    Survey = {};
    bSurveyHeld = false;
    SurveyStartedAt = SurveyUpdatedAt = 0;
    Statistics = {};
    bSessionNorthRoute = Runner->IsNorthRouteFirst();
    UsedOverclocks.Reset();
    ActiveOverclockKey = -1;
    OverclockEndsAt = 0;
    CompanionLastState.Reset();
    CompanionPreviousState.Reset();
    CompanionStateChangedAt = -1;
    LastPhase = LastWave = -1;
    SeenUpgrades = EventCount = 0;
    bFinished = false;
    bLogHealthy = IFileManager::Get().MakeDirectory(*OutputDirectory, true);
    RunId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TracePath = FPaths::Combine(OutputDirectory, FString::Printf(TEXT("portfolio-%s.jsonl"), *RunId));
    ReportPath = FPaths::Combine(OutputDirectory, FString::Printf(TEXT("portfolio-%s.json"), *RunId));
    SessionStartedAt = GetWorld()->GetTimeSeconds();
    LastTrialElapsed = 0;
    // Set the dedicated playable preset only during normal pawn initialization.
    // Restarting creates new pawns; no heal or combat stat is injected mid-encounter.
    const bool bBriefing = Runner->GetTrial().phase == aegis::TrialPhase::Briefing;
    if (bBriefing) ConfigurePlayer();
    for (TActorIterator<AAegisCharacter> It(GetWorld()); It; ++It) BindCharacter(*It);
    EnableInput(Controller);
    if (InputComponent)
    {
        InputComponent->KeyBindings.Reset();
        auto& Binding = InputComponent->BindKey(EKeys::E, IE_Pressed, this, &AAegisPortfolio::RepairInput);
        Binding.bExecuteWhenPaused = false;
        Binding.bConsumeInput = true;
        if (bV2)
        {
            InputComponent->BindKey(EKeys::F, IE_Pressed, this, &AAegisPortfolio::OverclockInput);
            InputComponent->BindKey(EKeys::V, IE_Pressed, this, &AAegisPortfolio::RouteInput).bExecuteWhenPaused = true;
        }
        if (bV23)
        {
            InputComponent->BindKey(EKeys::G, IE_Pressed, this, &AAegisPortfolio::SurveyPressed);
            InputComponent->BindKey(EKeys::G, IE_Released, this, &AAegisPortfolio::SurveyReleased).bExecuteWhenPaused = true;
            InputComponent->BindKey(EKeys::H, IE_Pressed, this, &AAegisPortfolio::SurveyModeInput);
        }
    }
    SetFeedback(bV2 ? TEXT("PRISM FALL | RMB charge 12 / Q pulse 35 / F overclock 35") : TEXT("60 energy: Q pulse 35 | E repair 40"));
    auto Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("mode"), bV2 ? TEXT("aegis-prism-fall-v2.0") : TEXT("aegis-portfolio-v1.5"));
    if (bV2)
    {
        Data->SetStringField(TEXT("repairPricing"), TEXT("ceil(actualHealing*0.8), minimum8 maximum40; zero-heal rejected"));
        Data->SetNumberField(TEXT("chargedShotCost"), 12);
        Data->SetNumberField(TEXT("overclockCost"), 35);
        Data->SetNumberField(TEXT("overclockDuration"), 6);
        Data->SetStringField(TEXT("relayBaseSeconds"), TEXT("8 / 10+10 / 4"));
    }
    Data->SetBoolField(TEXT("presetAppliedDuringBriefing"), bBriefing);
    if (bV23)
    {
        Data->SetStringField(TEXT("gameplayVersion"), TEXT("2.3"));
        Data->SetNumberField(TEXT("surveySeconds"), aegis::SurveyCache::scanSeconds);
        Data->SetNumberField(TEXT("surveyRadius"), aegis::SurveyCache::radius);
        Data->SetNumberField(TEXT("surveyPlayerHeal"), 20);
        Data->SetNumberField(TEXT("surveyCompanionHeal"), 15);
        Data->SetNumberField(TEXT("surveyRelayMultiplier"), aegis::SurveyCache::relayMultiplier);
    }
    Data->SetNumberField(TEXT("initialEnergy"), aegis::PortfolioEconomy::initialEnergy);
    Data->SetNumberField(TEXT("maximumEnergy"), MaximumEnergy);
    Data->SetNumberField(TEXT("pulseCost"), PulseCost);
    Data->SetNumberField(TEXT("repairCost"), RepairCost);
    Data->SetNumberField(TEXT("killReward"), aegis::PortfolioEconomy::killReward);
    Data->SetNumberField(TEXT("stageReward"), aegis::PortfolioEconomy::stageReward);
    Data->SetNumberField(TEXT("repairCooldownSeconds"), aegis::PortfolioEconomy::repairCooldown);
    Data->SetNumberField(TEXT("playerMaximumHealth"), Player->Health->Maximum);
    Data->SetNumberField(TEXT("playerRangedDamage"), Player->Combat->RangedDamage);
    Data->SetNumberField(TEXT("playerRangedCooldown"), Player->Combat->RangedCooldown);
    RecordEvent(TEXT("session_started"), Data);
    UE_LOG(LogTemp, Display, TEXT("AEGIS_PORTFOLIO_BEGIN run=%s trace=%s"), *RunId, *TracePath);
}
void AAegisPortfolio::ConfigurePlayer()
{
    Player->Health->Maximum = Player->Health->Current = 140.f;
    Player->Combat->RangedDamage = 18.f;
    Player->Combat->RangedCooldown = 0.28f;
    if (auto* Companion = Runner->GetCompanion())
        Companion->Health->Maximum = Companion->Health->Current = 120.f;
    Player->SpringArm->TargetArmLength = 1750.f;
    Player->SpringArm->SetRelativeRotation(FRotator(-58.f, -35.f, 0));
    Player->SpringArm->bDoCollisionTest = false;
    Player->Camera->SetFieldOfView(58.f);
    UpdateCamera(0.f);
}
void AAegisPortfolio::UpdateCamera(float DeltaSeconds)
{
    if (!IsValid(Player) || !IsValid(Runner) || !Player->SpringArm) return;
    // A bounded center bias reveals routes while preserving follow and camera-relative WASD.
    FVector TowardCenter = Runner->GetActorLocation() - Player->GetActorLocation();
    TowardCenter.Z = 0;
    const FVector Desired = (TowardCenter * 0.18f).GetClampedToMaxSize(280.f);
    Player->SpringArm->TargetOffset = DeltaSeconds > 0 ?
        FMath::VInterpTo(Player->SpringArm->TargetOffset, Desired, DeltaSeconds, 4.f) : Desired;
}
bool AAegisPortfolio::CanTransact() const
{
    if (!bInitialized || bFinished || !IsValid(Runner) || !IsValid(Player) || !IsValid(Controller) ||
        !GetWorld() || GetWorld()->IsPaused() || Controller->GetPawn() != Player ||
        !Controller->IsLocalController() || Controller->IsMoveInputIgnored() ||
        Controller->bMenuOpen || Controller->bPlannerOpen || Runner->IsUpgradePending() ||
        !Player->bCombatEnabled || !Player->Health->IsAlive() ||
        Runner->GetTrial().phase != aegis::TrialPhase::Active)
        return false;
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("AegisInputProbe")) &&
        FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen"))) return true;
#endif
    auto* Viewport = GetWorld()->GetGameViewport();
    return !Viewport || !Viewport->Viewport || Viewport->Viewport->HasFocus();
}
void AAegisPortfolio::SetFeedback(const FString& Text)
{
    Feedback = Text;
    FeedbackAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0;
}
float AAegisPortfolio::GetFeedbackAge() const
{
    return GetWorld() && FeedbackAt >= 0 ? FMath::Max(0.f, static_cast<float>(GetWorld()->GetTimeSeconds() - FeedbackAt)) : 0.f;
}
float AAegisPortfolio::GetRepairCooldown() const
{
    return GetWorld() ? static_cast<float>(Economy.repairRemaining(GetWorld()->GetTimeSeconds())) : 0.f;
}
void AAegisPortfolio::UpdateStatistics()
{
    Statistics.EnergyEarned = Economy.earned;
    Statistics.EnergySpent = Economy.spent;
    Statistics.EnergyOverflow = Economy.overflow;
    Statistics.PulsesUsed = Economy.pulses;
    Statistics.RepairsUsed = Economy.repairs;
    Statistics.EnemiesRewarded = Economy.kills;
    Statistics.StagesRewarded = Economy.stages;
}
void AAegisPortfolio::RecordTransaction(const FString& Reason, int32 Before, int32 Offered)
{
    UpdateStatistics();
    Statistics.LastTransaction = FString::Printf(TEXT("%s: %+d energy"), *Reason, Economy.energy - Before);
    auto Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("reason"), Reason);
    Data->SetNumberField(TEXT("before"), Before);
    Data->SetNumberField(TEXT("offeredDelta"), Offered);
    Data->SetNumberField(TEXT("actualDelta"), Economy.energy - Before);
    Data->SetNumberField(TEXT("after"), Economy.energy);
    Data->SetNumberField(TEXT("overflowTotal"), Economy.overflow);
    RecordEvent(TEXT("energy_transaction"), Data);
}
bool AAegisPortfolio::TrySpendPulse()
{
    SynchronizePlayer();
    if (!CanTransact()) return false;
    const int32 Before = Economy.energy;
    if (!Economy.tryPulse(true))
    {
        SetFeedback(TEXT("Pulse needs 35 energy"));
        auto Data = MakeShared<FJsonObject>(); Data->SetStringField(TEXT("reason"), TEXT("insufficient_energy"));
        RecordEvent(TEXT("pulse_rejected"), Data);
        return false;
    }
    RecordTransaction(TEXT("pulse"), Before, -PulseCost);
    SetFeedback(TEXT("Pulse -35 energy | disrupt and push enemies"));
    return true;
}
void AAegisPortfolio::RepairInput() { TryRepair(); }
void AAegisPortfolio::GetRepairRecipients(float& SelfHealing, float& AllyHealing) const
{
    SelfHealing = AllyHealing = 0;
    if (!IsValid(Player) || !IsValid(Runner) || !GetWorld() || !Player->Health->IsAlive()) return;
    SelfHealing = FMath::Clamp(Player->Health->Maximum - Player->Health->Current, 0.f, 30.f);
    auto* Ally = Runner->GetCompanion();
    if (!IsValid(Ally) || Ally->Team != Player->Team || !Ally->Health->IsAlive() ||
        FVector::DistSquared(Player->GetActorLocation(), Ally->GetActorLocation()) > FMath::Square(500.f)) return;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(AegisPortfolioRepair), false, Player);
    if (!GetWorld()->LineTraceTestByChannel(Player->GetActorLocation() + FVector(0, 0, 20),
        Ally->GetActorLocation() + FVector(0, 0, 20), ECC_GameTraceChannel1, Params))
        AllyHealing = FMath::Clamp(Ally->Health->Maximum - Ally->Health->Current, 0.f, 20.f);
}
int32 AAegisPortfolio::GetRepairQuote() const
{
    if (!bV2) return RepairCost;
    float Self = 0, Ally = 0;
    GetRepairRecipients(Self, Ally);
    return Self + Ally > 0 ? FMath::Clamp(FMath::CeilToInt((static_cast<double>(Self) + Ally) * .8), 8, 40) : 0;
}
bool AAegisPortfolio::TryRepair()
{
    SynchronizePlayer();
    if (!CanTransact()) return false;
    FString Rejection;
    if (GetRepairCooldown() > 0) Rejection = TEXT("Repair is cooling down");
    auto* Companion = Runner->GetCompanion();
    float SelfQuote = 0, AllyQuote = 0;
    GetRepairRecipients(SelfQuote, AllyQuote);
    const bool bSelfEligible = SelfQuote > 0, bCompanionEligible = AllyQuote > 0;
    const int32 Cost = bV2 ? (SelfQuote + AllyQuote > 0 ? FMath::Clamp(FMath::CeilToInt((static_cast<double>(SelfQuote) + AllyQuote) * .8), 8, 40) : 0) : RepairCost;
    if (Rejection.IsEmpty() && !bSelfEligible && !bCompanionEligible)
        Rejection = TEXT("No repair needed: ally must be within 5m and clear of cover");
    if (Rejection.IsEmpty() && Economy.energy < Cost) Rejection = FString::Printf(TEXT("Repair needs %d energy"), Cost);
    if (!Rejection.IsEmpty())
    {
        SetFeedback(Rejection);
        auto Data = MakeShared<FJsonObject>(); Data->SetStringField(TEXT("reason"), Rejection);
        RecordEvent(TEXT("repair_rejected"), Data);
        return false;
    }
    const int32 Before = Economy.energy;
    if (!Economy.tryRepair(GetWorld()->GetTimeSeconds(), bSelfEligible || bCompanionEligible, true, Cost)) return false;
    // Eligibility and commit run on the game thread without an asynchronous gap.
    const float PlayerApplied = bSelfEligible ? Player->Health->Heal(30.f) : 0.f;
    const float CompanionApplied = bCompanionEligible ? Companion->Health->Heal(20.f) : 0.f;
    Statistics.PlayerHealing += PlayerApplied;
    Statistics.CompanionHealing += CompanionApplied;
    RecordTransaction(TEXT("repair"), Before, -Cost);
    auto Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("playerActualHealing"), PlayerApplied);
    Data->SetNumberField(TEXT("companionActualHealing"), CompanionApplied);
    Data->SetNumberField(TEXT("quotedHealing"), SelfQuote + AllyQuote);
    Data->SetNumberField(TEXT("cost"), Cost);
    Data->SetBoolField(TEXT("quoteReconciled"), FMath::IsNearlyEqual(SelfQuote + AllyQuote, PlayerApplied + CompanionApplied));
    Data->SetNumberField(TEXT("cooldownReadyGameSeconds"), Economy.repairReadyAt);
    RecordEvent(TEXT("repair_applied"), Data);
    AegisPortfolioPresentation::Sound(this, TEXT("S_Repair"), Player->GetActorLocation(), 0.6f);
    SetFeedback(FString::Printf(TEXT("Repair -%d energy | YOU +%.0f / ALLY +%.0f HP"), Cost, PlayerApplied, CompanionApplied));
    return true;
}
bool AAegisPortfolio::TrySpendChargedShot()
{
    SynchronizePlayer();
    if (!bV2 || !CanTransact()) return false;
    const int32 Before = Economy.energy;
    if (!Economy.trySpend(12, true)) { SetFeedback(TEXT("Charged shot needs 12 energy")); return false; }
    ++Statistics.ChargedShots;
    RecordTransaction(TEXT("charged_shot"), Before, -12);
    SetFeedback(TEXT("Prism shot -12 | cover and allies still block"));
    return true;
}
int32 AAegisPortfolio::CurrentRelayKey() const
{
    return Runner ? Runner->GetTrial().wave * 10 + Runner->GetOperation().relay : -1;
}
float AAegisPortfolio::GetOverclockRemaining() const
{
    return bV2 && Runner && GetWorld() && !Runner->GetOperation().complete &&
        ActiveOverclockKey == CurrentRelayKey() ? FMath::Max(0.f, static_cast<float>(OverclockEndsAt - GetWorld()->GetTimeSeconds())) : 0.f;
}
bool AAegisPortfolio::IsOverclockUsed() const { return UsedOverclocks.Contains(CurrentRelayKey()); }
void AAegisPortfolio::OverclockInput() { TryOverclock(); }
bool AAegisPortfolio::TryOverclock()
{
    SynchronizePlayer();
    if (!bV2 || !CanTransact()) return false;
    if (Runner->GetTrial().wave >= 3 || Runner->GetOperation().complete || !Runner->IsPlayerInObjective())
    { SetFeedback(TEXT("Overclock: stand inside an active data relay")); return false; }
    if (IsOverclockUsed()) { SetFeedback(TEXT("This relay has already been overclocked")); return false; }
    const int32 Before = Economy.energy;
    if (!Economy.trySpend(35, true)) { SetFeedback(TEXT("Overclock needs 35 energy")); return false; }
    ActiveOverclockKey = CurrentRelayKey();
    UsedOverclocks.Add(ActiveOverclockKey);
    OverclockEndsAt = GetWorld()->GetTimeSeconds() + 6;
    ++Statistics.Overclocks;
    RecordTransaction(TEXT("overclock"), Before, -35);
    auto Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("relayKey"), ActiveOverclockKey);
    Data->SetNumberField(TEXT("endsAtGameSeconds"), OverclockEndsAt);
    Data->SetBoolField(TEXT("contestedAtPurchase"), Runner->IsObjectiveContested());
    RecordEvent(TEXT("overclock_activated"), Data);
    AegisPortfolioPresentation::Sound(this, TEXT("S_Overclock"), Runner->GetObjectiveLocation(), .65f);
    SetFeedback(TEXT("OVERCLOCK x2 / 6s | contest stops progress, timer keeps running"));
    return true;
}
void AAegisPortfolio::RouteInput()
{
    if (!bV2 || !Runner || !Controller || Controller->bMenuOpen || !Runner->ToggleRelayRoute()) return;
    bSessionNorthRoute = Runner->IsNorthRouteFirst();
    auto Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("northFirst"), Runner->IsNorthRouteFirst());
    RecordEvent(TEXT("route_selected"), Data);
    SetFeedback(Runner->IsNorthRouteFirst() ? TEXT("Transfer route: NORTH > EAST") : TEXT("Transfer route: EAST > NORTH"));
}
void AAegisPortfolio::ApplySymbiosis(float ProgressSeconds, bool bPlayerInside, bool bCompanionInside)
{
    // Called only by the objective observer on frames that produced real progress.
    // Scale by wall game time, never the overclock multiplier.
    if (!bV2 || !Runner || !Runner->HasUpgrade(3) || !Player || !GetWorld() || GetWorld()->IsPaused() ||
        Runner->GetTrial().phase != aegis::TrialPhase::Active || !FMath::IsFinite(ProgressSeconds) ||
        ProgressSeconds <= 0 || ProgressSeconds > .25f) return;
    const float Self = bPlayerInside ? Player->Health->Heal(2.f * ProgressSeconds) : 0;
    auto* Ally = Runner->GetCompanion();
    const float Other = bCompanionInside && Ally ? Ally->Health->Heal(2.f * ProgressSeconds) : 0;
    Statistics.SymbiosisPlayerHealing += Self;
    Statistics.SymbiosisCompanionHealing += Other;
    if (Self + Other > 0)
    {
        auto Data = MakeShared<FJsonObject>();
        Data->SetNumberField(TEXT("playerActualHealing"), Self);
        Data->SetNumberField(TEXT("companionActualHealing"), Other);
        Data->SetNumberField(TEXT("progressGameSeconds"), ProgressSeconds);
        RecordEvent(TEXT("symbiosis_applied"), Data);
    }
}
void AAegisPortfolio::OnDamage(float Applied, AActor* Source, AActor* Victim)
{
    if (!bInitialized || bFinished || !IsValid(Runner) || !IsValid(Player) ||
        !IsValid(Controller) || Controller->GetPawn() != Player || !GetWorld() || GetWorld()->IsPaused() ||
        Runner->GetTrial().phase != aegis::TrialPhase::Active || Applied <= 0 || !FMath::IsFinite(Applied)) return;
    auto* Attacker = Cast<AAegisCharacter>(Source);
    auto* Target = Cast<AAegisCharacter>(Victim);
    auto* Companion = Runner->GetCompanion();
    const bool bAlliedSource = Attacker && (Attacker == Player || Attacker == Companion);
    const bool bEnemyVictim = Target && Target->Team == EAegisTeam::Enemy;
    const bool bPlayerVictim = Target == Player;
    const bool bCompanionVictim = Target && Target == Companion;
    if (!(bAlliedSource && bEnemyVictim) && !bPlayerVictim && !bCompanionVictim) return;
    if (bAlliedSource && bEnemyVictim) Statistics.AlliedDamageDealt += Applied;
    if (bPlayerVictim) Statistics.PlayerDamageTaken += Applied;
    if (bCompanionVictim) Statistics.CompanionDamageTaken += Applied;
    if (bV2 && bCompanionVictim) Statistics.CompanionSurvived = Target->Health->IsAlive();
    auto Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("source"), GetNameSafe(Source));
    Data->SetStringField(TEXT("victim"), GetNameSafe(Victim));
    Data->SetNumberField(TEXT("actualDamage"), Applied);
    Data->SetNumberField(TEXT("victimHealthAfter"), Target ? Target->Health->Current : 0);
    Data->SetBoolField(TEXT("enemyVictim"), bEnemyVictim);
    const uint64* Identity = Target ? CharacterIdentities.Find(TWeakObjectPtr<AAegisCharacter>(Target)) : nullptr;
    if (Identity) Data->SetNumberField(TEXT("victimRunIdentity"), static_cast<double>(*Identity));
    RecordEvent(TEXT("damage"), Data);
    if (bV23 && bPlayerVictim) CancelSurvey(TEXT("damaged"));
    if (bAlliedSource && bEnemyVictim && !Target->Health->IsAlive())
    {
        const int32 Before = Economy.energy;
        if (Identity && Economy.rewardEnemy(*Identity, Player->Health->IsAlive()))
        {
            RecordTransaction(TEXT("enemy_defeated"), Before, aegis::PortfolioEconomy::killReward);
            SetFeedback(FString::Printf(TEXT("Enemy defeated +%d energy"), Economy.energy - Before));
        }
    }
}
void AAegisPortfolio::ObserveProgress()
{
    if (!bInitialized || !IsValid(Runner) || !GetWorld() || GetWorld()->IsPaused()) return;
    SynchronizePlayer();
    if (!IsValid(Player) || bFinished) return;
    const auto& Trial = Runner->GetTrial();
    if (bV2 && Trial.phase == aegis::TrialPhase::Active)
    {
        auto* Ally = Runner->GetCompanion();
        auto* AI = Ally ? Cast<AAegisAIController>(Ally->GetController()) : nullptr;
        Statistics.CompanionSurvived = Ally && Ally->Health->IsAlive();
        if (AI)
        {
            Statistics.CompanionShots = AI->TacticalShots;
            Statistics.CompanionMoves = AI->TacticalMoveRequests;
            Statistics.GuardChecks = AI->GuardSlotChecks;
            Statistics.GuardRejects = AI->GuardSlotRejections;
            Statistics.GuardAlternates = AI->GuardAlternateSelections;
        }
        if (AI && AI->DebugState != CompanionLastState)
        {
            const double Now = GetWorld()->GetTimeSeconds();
            if (!CompanionLastState.IsEmpty()) ++Statistics.CompanionStateChanges;
            if (AI->DebugState == CompanionPreviousState && Now - CompanionStateChangedAt < 1.0)
                ++Statistics.CompanionShortReversals;
            auto Data = MakeShared<FJsonObject>();
            Data->SetStringField(TEXT("from"), CompanionLastState);
            Data->SetStringField(TEXT("to"), AI->DebugState);
            Data->SetBoolField(TEXT("ownSight"), AI->bTargetVisible);
            Data->SetStringField(TEXT("guardSlotReason"), AI->GuardSlotReason);
            RecordEvent(TEXT("companion_state_changed"), Data);
            CompanionPreviousState = CompanionLastState;
            CompanionLastState = AI->DebugState;
            CompanionStateChangedAt = Now;
        }
    }
    LastTrialElapsed = Trial.elapsed;
    if (LastPhase != static_cast<int32>(Trial.phase) || LastWave != Trial.wave)
    {
        auto Data = MakeShared<FJsonObject>();
        Data->SetStringField(TEXT("phase"), PortfolioPhaseName(Trial.phase));
        Data->SetNumberField(TEXT("wave"), Trial.wave);
        Data->SetBoolField(TEXT("pressure"), Trial.pressure);
        RecordEvent(TEXT("phase_changed"), Data);
        LastPhase = static_cast<int32>(Trial.phase);
        LastWave = Trial.wave;
    }
    // Infer already completed stages as well, so the pause between clearing and
    // selecting an upgrade cannot lose a reward. Ledger identities prevent repeats.
    const int32 CompletedStages = Trial.phase == aegis::TrialPhase::Won ? 3 :
        (Trial.phase == aegis::TrialPhase::Intermission ? Trial.wave : FMath::Max(0, Trial.wave - 1));
    for (int32 Stage = 1; Stage <= CompletedStages; ++Stage)
    {
        const int32 Before = Economy.energy;
        if (Economy.rewardStage(Stage, Player->Health->IsAlive() && Trial.phase != aegis::TrialPhase::Lost))
        {
            RecordTransaction(FString::Printf(TEXT("stage_%d_complete"), Stage), Before, aegis::PortfolioEconomy::stageReward);
            SetFeedback(FString::Printf(TEXT("Sector clear +%d energy | choose an upgrade"), Economy.energy - Before));
        }
    }
    for (int32 Index = 1; Index <= 3; ++Index)
        if (Runner->HasUpgrade(Index) && !(SeenUpgrades & (1 << (Index - 1))))
        {
            SeenUpgrades |= 1 << (Index - 1);
            auto Data = MakeShared<FJsonObject>();
            Data->SetNumberField(TEXT("upgradeIndex"), Index);
            Data->SetStringField(TEXT("upgrade"), Runner->LastUpgrade);
            RecordEvent(TEXT("upgrade_selected"), Data);
            AegisPortfolioPresentation::Sound(this, TEXT("S_Upgrade"), Player->GetActorLocation(), 0.6f);
        }
    if (Trial.finished()) FinishSession(Trial.phase == aegis::TrialPhase::Won ? TEXT("won") : TEXT("lost"), true);
}
void AAegisPortfolio::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    ObserveProgress();
    if (bV23) UpdateSurvey();
    if (GetWorld() && !GetWorld()->IsPaused()) UpdateCamera(DeltaSeconds);
}
void AAegisPortfolio::RecordEvent(const FString& Type, const TSharedRef<FJsonObject>& Data)
{
    if (!bLogHealthy || TracePath.IsEmpty()) return;
    if (EventCount >= 8192)
    {
        bLogHealthy = false;
        UE_LOG(LogTemp, Error, TEXT("AEGIS_PORTFOLIO_LOG_LIMIT run=%s"), *RunId);
        return;
    }
    auto Event = MakeShared<FJsonObject>();
    Event->SetStringField(TEXT("runId"), RunId);
    Event->SetNumberField(TEXT("sequence"), ++EventCount);
    Event->SetStringField(TEXT("event"), Type);
    Event->SetNumberField(TEXT("gameSeconds"), GetWorld() ? GetWorld()->GetTimeSeconds() : 0);
    Event->SetNumberField(TEXT("energy"), Economy.energy);
    Event->SetObjectField(TEXT("data"), Data);
    bLogHealthy = FFileHelper::SaveStringToFile(PortfolioJson(Event) + TEXT("\n"), *TracePath,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
    if (!bLogHealthy) UE_LOG(LogTemp, Error, TEXT("AEGIS_PORTFOLIO_LOG_WRITE_FAILED run=%s"), *RunId);
}
void AAegisPortfolio::FinishSession(const FString& Outcome, bool bCompleted)
{
    if (bFinished || RunId.IsEmpty()) return;
    if (bV23)
    {
        CancelSurvey(TEXT("session_ended"));
        FinishSurveyBoost(TEXT("session_ended"));
    }
    bFinished = true;
    UpdateStatistics();
    auto Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("mode"), bV2 ? TEXT("aegis-prism-fall-v2.0") : TEXT("aegis-portfolio-v1.5"));
    if (bV2)
    {
        Report->SetNumberField(TEXT("chargedShots"), Statistics.ChargedShots);
        Report->SetNumberField(TEXT("overclocks"), Statistics.Overclocks);
        Report->SetNumberField(TEXT("symbiosisPlayerActualHealing"), Statistics.SymbiosisPlayerHealing);
        Report->SetNumberField(TEXT("symbiosisCompanionActualHealing"), Statistics.SymbiosisCompanionHealing);
        Report->SetBoolField(TEXT("northRouteFirst"), bSessionNorthRoute);
        Report->SetBoolField(TEXT("companionSurvived"), Statistics.CompanionSurvived);
        Report->SetNumberField(TEXT("companionStateChanges"), Statistics.CompanionStateChanges);
        Report->SetNumberField(TEXT("companionShortReversalsUnder1s"), Statistics.CompanionShortReversals);
        Report->SetStringField(TEXT("stabilityMetricDefinition"), TEXT("Observed tactical DebugState transitions; includes normal aim/fire/reposition cycles, not Utility switching"));
        Report->SetNumberField(TEXT("companionShots"), Statistics.CompanionShots);
        Report->SetNumberField(TEXT("companionMoveRequests"), Statistics.CompanionMoves);
        Report->SetNumberField(TEXT("guardSlotChecks"), Statistics.GuardChecks);
        Report->SetNumberField(TEXT("guardSlotRejections"), Statistics.GuardRejects);
        Report->SetNumberField(TEXT("guardAlternateSelections"), Statistics.GuardAlternates);
    }
    Report->SetStringField(TEXT("runId"), RunId);
    if (bV23)
    {
        Report->SetStringField(TEXT("gameplayVersion"), TEXT("2.3"));
        Report->SetNumberField(TEXT("surveyKeys"), Statistics.SurveyKeys);
        Report->SetNumberField(TEXT("surveySupplies"), Statistics.SurveySupplies);
        Report->SetNumberField(TEXT("surveyBoosts"), Statistics.SurveyBoosts);
        Report->SetNumberField(TEXT("surveyCancelled"), Statistics.SurveyCancelled);
        Report->SetNumberField(TEXT("surveyPlayerActualHealing"), Statistics.SurveyPlayerHealing);
        Report->SetNumberField(TEXT("surveyCompanionActualHealing"), Statistics.SurveyCompanionHealing);
        Report->SetNumberField(TEXT("surveyClaimedMask"), Survey.claimedMask);
        Report->SetBoolField(TEXT("surveyKeyPending"), Survey.keyPending);
        Report->SetNumberField(TEXT("surveyBoostRelayKey"), Survey.boostRelayKey);
    }
    Report->SetStringField(TEXT("outcome"), Outcome);
    Report->SetBoolField(TEXT("completed"), bCompleted);
    Report->SetNumberField(TEXT("sessionGameSeconds"), GetWorld() ? GetWorld()->GetTimeSeconds() - SessionStartedAt : 0);
    // On R, the runner already reset Trial before the new pawn is observed here.
    Report->SetNumberField(TEXT("trialElapsedSeconds"), LastTrialElapsed);
    Report->SetNumberField(TEXT("energyInitial"), aegis::PortfolioEconomy::initialEnergy);
    Report->SetNumberField(TEXT("energyFinal"), Economy.energy);
    Report->SetNumberField(TEXT("energyEarned"), Statistics.EnergyEarned);
    Report->SetNumberField(TEXT("energySpent"), Statistics.EnergySpent);
    Report->SetNumberField(TEXT("energyOverflow"), Statistics.EnergyOverflow);
    Report->SetNumberField(TEXT("pulsesUsed"), Statistics.PulsesUsed);
    Report->SetNumberField(TEXT("repairsUsed"), Statistics.RepairsUsed);
    Report->SetNumberField(TEXT("enemiesRewarded"), Statistics.EnemiesRewarded);
    Report->SetNumberField(TEXT("stagesRewarded"), Statistics.StagesRewarded);
    Report->SetNumberField(TEXT("repairPlayerActualHealing"), Statistics.PlayerHealing);
    Report->SetNumberField(TEXT("repairCompanionActualHealing"), Statistics.CompanionHealing);
    Report->SetNumberField(TEXT("alliedActualDamageDealt"), Statistics.AlliedDamageDealt);
    Report->SetNumberField(TEXT("playerActualDamageTaken"), Statistics.PlayerDamageTaken);
    Report->SetNumberField(TEXT("companionActualDamageTaken"), Statistics.CompanionDamageTaken);
    Report->SetNumberField(TEXT("upgradeMask"), SeenUpgrades);
    Report->SetBoolField(TEXT("ledgerBalanced"), Economy.energy == aegis::PortfolioEconomy::initialEnergy + Economy.earned - Economy.spent);
    RecordEvent(TEXT("session_finished"), Report);
    Report->SetNumberField(TEXT("eventCount"), EventCount);
    Report->SetStringField(TEXT("tracePath"), TracePath);
    Report->SetBoolField(TEXT("traceComplete"), bLogHealthy);
    const bool bWritten = FFileHelper::SaveStringToFile(PortfolioJson(Report) + TEXT("\n"), *ReportPath,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    if (bCompleted && bWritten && bLogHealthy)
    {
        UE_LOG(LogTemp, Display, TEXT("AEGIS_PORTFOLIO_COMPLETE run=%s outcome=%s report=%s"), *RunId, *Outcome, *ReportPath);
    }
    else if (!bWritten || !bLogHealthy)
    {
        UE_LOG(LogTemp, Error, TEXT("AEGIS_PORTFOLIO_REPORT_FAILED run=%s"), *RunId);
    }
}
void AAegisPortfolio::EndPlay(const EEndPlayReason::Type Reason)
{
    if (!bFinished) FinishSession(TEXT("aborted"), false);
    if (GetWorld() && SpawnHandle.IsValid()) GetWorld()->RemoveOnActorSpawnedHandler(SpawnHandle);
    if (IsValid(Controller)) DisableInput(Controller);
    UnbindCharacters();
    Super::EndPlay(Reason);
}
