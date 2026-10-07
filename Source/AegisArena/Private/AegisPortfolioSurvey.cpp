#include "AegisPortfolio.h"
#include "AegisLab.h"
#include "AegisCharacter.h"
#include "AegisPortfolioPresentation.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"

namespace
{
void SurveyPosition(const TSharedRef<FJsonObject>& Data, const TCHAR* Field, const FVector& Position)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    Values.Add(MakeShared<FJsonValueNumber>(Position.X));
    Values.Add(MakeShared<FJsonValueNumber>(Position.Y));
    Values.Add(MakeShared<FJsonValueNumber>(Position.Z));
    Data->SetArrayField(Field, Values);
}
}

FVector AAegisPortfolio::GetSurveyLocation(int32 Node) const
{
    if (!Runner || Node < 0 || Node >= aegis::SurveyCache::nodeCount) return FVector::ZeroVector;
    const FVector Offset = Node == 0 ? FVector(-1050, -900, 8) : FVector(1050, 650, 8);
    return Runner->GetActorLocation() + Offset;
}

void AAegisPortfolio::GetSurveyRecipients(float& SelfHealing, float& AllyHealing) const
{
    // Repair already checks living allies, 5m proximity and the authored cover channel.
    GetRepairRecipients(SelfHealing, AllyHealing);
    SelfHealing = FMath::Min(SelfHealing, 20.f);
    AllyHealing = FMath::Min(AllyHealing, 15.f);
}

bool AAegisPortfolio::CanClaimSurveySupply() const
{
    if (!bV23 || !CanTransact()) return false;
    float Self = 0, Ally = 0;
    GetSurveyRecipients(Self, Ally);
    return Self + Ally > 0;
}

bool AAegisPortfolio::CanReserveSurveyKey() const
{
    if (!bV23 || !Runner || Survey.keyPending) return false;
    const auto& Trial = Runner->GetTrial();
    if (Trial.phase != aegis::TrialPhase::Active) return false;
    if (Trial.wave == 1) return true; // At least the two transfer relays remain.
    if (Trial.wave != 2 || Runner->GetOperation().complete) return false;
    // A key may wait behind the first transfer boost, but the last relay cannot
    // accept a second boost and there is no data relay after extraction.
    return Runner->GetOperation().relay == 0 || Survey.boostRelayKey != CurrentRelayKey();
}

void AAegisPortfolio::SurveyModeInput()
{
    if (!bV23 || !CanTransact()) return;
    if (Survey.node >= 0)
    {
        SetFeedback(TEXT("Survey: finish or cancel the current scan first"));
        return;
    }
    if (!Survey.selectSupply(!Survey.supplySelected)) return;
    auto Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("supply"), Survey.supplySelected);
    RecordEvent(TEXT("survey_mode_selected"), Data);
    SetFeedback(Survey.supplySelected ? TEXT("Survey reward: SUPPLY | heal 20 / 15")
                                      : TEXT("Survey reward: RELAY KEY | upload x1.25"));
}

void AAegisPortfolio::SurveyPressed()
{
    if (!bV23 || !CanTransact() || Survey.node >= 0) return;
    bSurveyHeld = true;
    int32 Nearest = -1;
    double Distance = aegis::SurveyCache::radius;
    for (int32 Node = 0; Node < aegis::SurveyCache::nodeCount; ++Node)
    {
        if (Survey.claimed(Node)) continue;
        const double Candidate = FVector::Dist2D(Player->GetActorLocation(), GetSurveyLocation(Node));
        if (Candidate <= Distance) { Distance = Candidate; Nearest = Node; }
    }
    if (Nearest < 0)
    {
        SetFeedback(TEXT("Survey: stand within 1.8m of an unclaimed cache"));
        return;
    }
    if (Survey.supplySelected && !CanClaimSurveySupply())
    {
        SetFeedback(TEXT("Survey supply: no healing needed"));
        return;
    }
    if (!Survey.supplySelected && !CanReserveSurveyKey())
    {
        SetFeedback(Survey.keyPending ? TEXT("Survey key: storage already full")
                                      : TEXT("Survey key: no data relays remain"));
        return;
    }
    if (!Survey.begin(Nearest)) return;
    SurveyStartedAt = SurveyUpdatedAt = GetWorld()->GetTimeSeconds();
    auto Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("node"), Nearest);
    Data->SetBoolField(TEXT("supply"), Survey.supplySelected);
    Data->SetNumberField(TEXT("distance"), Distance);
    SurveyPosition(Data, TEXT("playerPosition"), Player->GetActorLocation());
    SurveyPosition(Data, TEXT("nodePosition"), GetSurveyLocation(Nearest));
    RecordEvent(TEXT("survey_started"), Data);
    SetFeedback(TEXT("Survey: hold G for 2.5s | damage cancels"));
}

void AAegisPortfolio::SurveyReleased()
{
    bSurveyHeld = false;
    CancelSurvey(TEXT("released"));
}

void AAegisPortfolio::CancelSurvey(const TCHAR* Reason)
{
    if (!bV23 || Survey.node < 0) return;
    auto Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("node"), Survey.node);
    Data->SetStringField(TEXT("reason"), Reason);
    Data->SetNumberField(TEXT("progressSeconds"), Survey.progress);
    Survey.cancel();
    bSurveyHeld = false;
    SurveyStartedAt = SurveyUpdatedAt = 0;
    ++Statistics.SurveyCancelled;
    RecordEvent(TEXT("survey_cancelled"), Data);
    if (FCString::Strcmp(Reason, TEXT("damaged")) == 0)
        SetFeedback(TEXT("Survey: scan interrupted by damage"));
    else if (FCString::Strcmp(Reason, TEXT("left_range")) == 0)
        SetFeedback(TEXT("Survey: return to the cache to scan"));
    else if (FCString::Strcmp(Reason, TEXT("released")) == 0)
        SetFeedback(TEXT("Survey: release G and try again"));
}

void AAegisPortfolio::FinishSurveyBoost(const TCHAR* Reason)
{
    if (!bV23 || Survey.boostRelayKey < 0) return;
    auto Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("relayKey"), Survey.boostRelayKey);
    Data->SetStringField(TEXT("reason"), Reason);
    Survey.finishBoost();
    RecordEvent(TEXT("survey_boost_finished"), Data);
}

bool AAegisPortfolio::IsSurveyBoostActive() const
{
    return bV23 && Runner && Runner->GetTrial().phase == aegis::TrialPhase::Active &&
        Runner->GetTrial().wave < 3 && !Runner->GetOperation().complete &&
        Survey.boostRelayKey >= 0 && Survey.boostRelayKey == CurrentRelayKey();
}

float AAegisPortfolio::ApplySurveyRelayBoost()
{
    if (!bV23 || bFinished || !Runner || !GetWorld() || GetWorld()->IsPaused() ||
        !Player || !Player->Health->IsAlive() || Runner->GetTrial().phase != aegis::TrialPhase::Active ||
        Runner->GetTrial().wave >= 3 || Runner->GetOperation().complete ||
        Runner->IsObjectiveContested() || (!Runner->IsPlayerInObjective() && !Runner->IsCompanionInObjective()))
        return 1.f;
    const int32 Key = CurrentRelayKey();
    if (Survey.boostRelayKey >= 0 && Survey.boostRelayKey != Key) FinishSurveyBoost(TEXT("relay_changed"));
    if (Survey.bind(Key))
    {
        ++Statistics.SurveyBoosts;
        auto Data = MakeShared<FJsonObject>();
        Data->SetNumberField(TEXT("relayKey"), Key);
        Data->SetNumberField(TEXT("multiplier"), aegis::SurveyCache::relayMultiplier);
        RecordEvent(TEXT("survey_boost_bound"), Data);
        SetFeedback(TEXT("Survey key linked | this data relay x1.25"));
        AegisPortfolioPresentation::Sound(this, TEXT("S_Upgrade"), Runner->GetObjectiveLocation(), .45f);
    }
    return static_cast<float>(Survey.multiplier(Key));
}

void AAegisPortfolio::UpdateSurvey()
{
    UpdateSurveyIndicators();
    if (bFinished || !Runner || !GetWorld()) return;
    if (Survey.boostRelayKey >= 0)
    {
        if (Survey.boostRelayKey != CurrentRelayKey()) FinishSurveyBoost(TEXT("relay_changed"));
        else if (Runner->GetOperation().complete) FinishSurveyBoost(TEXT("relay_complete"));
    }
    if (Survey.node < 0) return;
    if (!CanTransact()) { CancelSurvey(TEXT("unavailable")); return; }
    if (!bSurveyHeld || !Controller->IsInputKeyDown(EKeys::G)) { CancelSurvey(TEXT("released")); return; }
    const double Distance = FVector::Dist2D(Player->GetActorLocation(), GetSurveyLocation(Survey.node));
    if (Distance > aegis::SurveyCache::radius) { CancelSurvey(TEXT("left_range")); return; }
    const double Now = GetWorld()->GetTimeSeconds();
    const double Delta = Now - SurveyUpdatedAt;
    SurveyUpdatedAt = Now;
    if (!Survey.advance(Delta)) return;
    float SelfQuote = 0, AllyQuote = 0;
    GetSurveyRecipients(SelfQuote, AllyQuote);
    const bool bSupply = Survey.supplySelected;
    const bool bEligible = bSupply ? SelfQuote + AllyQuote > 0 : CanReserveSurveyKey();
    const int32 Node = Survey.node;
    const double Elapsed = Now - SurveyStartedAt;
    if (!Survey.claim(bEligible))
    {
        CancelSurvey(TEXT("reward_unavailable"));
        SetFeedback(bSupply ? TEXT("Survey supply: no healing needed") :
            Survey.keyPending ? TEXT("Survey key: storage already full") : TEXT("Survey key: no data relays remain"));
        return;
    }
    auto* Ally = Runner->GetCompanion();
    const float SelfApplied = bSupply && SelfQuote > 0 ? Player->Health->Heal(20.f) : 0.f;
    const float AllyApplied = bSupply && AllyQuote > 0 && Ally ? Ally->Health->Heal(15.f) : 0.f;
    if (bSupply) ++Statistics.SurveySupplies; else ++Statistics.SurveyKeys;
    Statistics.SurveyPlayerHealing += SelfApplied;
    Statistics.SurveyCompanionHealing += AllyApplied;
    auto Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("node"), Node);
    Data->SetBoolField(TEXT("supply"), bSupply);
    Data->SetNumberField(TEXT("scanSeconds"), aegis::SurveyCache::scanSeconds);
    Data->SetNumberField(TEXT("elapsedGameSeconds"), Elapsed);
    Data->SetNumberField(TEXT("distance"), Distance);
    SurveyPosition(Data, TEXT("playerPosition"), Player->GetActorLocation());
    SurveyPosition(Data, TEXT("nodePosition"), GetSurveyLocation(Node));
    Data->SetNumberField(TEXT("playerActualHealing"), SelfApplied);
    Data->SetNumberField(TEXT("companionActualHealing"), AllyApplied);
    Data->SetNumberField(TEXT("claimedMask"), Survey.claimedMask);
    Data->SetBoolField(TEXT("keyPending"), Survey.keyPending);
    RecordEvent(TEXT("survey_claimed"), Data);
    SurveyStartedAt = SurveyUpdatedAt = 0;
    if (bSupply)
        SetFeedback(FString::Printf(TEXT("Survey supply | YOU +%.0f / ALLY +%.0f HP"), SelfApplied, AllyApplied));
    else SetFeedback(TEXT("Survey key stored | next data relay x1.25"));
    AegisPortfolioPresentation::Sound(this, bSupply ? TEXT("S_Repair") : TEXT("S_Upgrade"), GetSurveyLocation(Node), .55f);
}

void AAegisPortfolio::BuildSurveyIndicators()
{
    if (!FApp::CanEverRender() || !SurveyIndicators.IsEmpty()) return;
    auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Aegis/V2/Materials/M_V2Energy.M_V2Energy"));
    if (!Cube || !Material) return;
    for (int32 Node = 0; Node < aegis::SurveyCache::nodeCount; ++Node)
    {
        auto* Ring = NewObject<UInstancedStaticMeshComponent>(this);
        Ring->SetStaticMesh(Cube);
        Ring->SetMobility(EComponentMobility::Movable);
        Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Ring->SetCanEverAffectNavigation(false);
        Ring->SetCastShadow(false);
        AddInstanceComponent(Ring);
        Ring->RegisterComponent();
        auto* Tint = UMaterialInstanceDynamic::Create(Material, Ring);
        Tint->SetVectorParameterValue(TEXT("Tint"), FLinearColor(.55f, .28f, 1.f));
        Ring->SetMaterial(0, Tint);
        for (int32 Segment = 0; Segment < 24; ++Segment)
        {
            const float Angle = Segment * 2.f * PI / 24;
            Ring->AddInstance(FTransform(FRotator(0, FMath::RadiansToDegrees(Angle) + 90, 0),
                GetSurveyLocation(Node) + FVector(FMath::Cos(Angle) * 180, FMath::Sin(Angle) * 180, 1),
                FVector(.30f, .035f, .025f)), true);
        }
        SurveyIndicators.Add(Ring);
    }
}

void AAegisPortfolio::UpdateSurveyIndicators()
{
    const int32 Segments = FMath::CeilToInt(GetSurveyProgress() * 24);
    if (SurveyVisualMask == GetSurveyClaimedMask() && SurveyVisualNode == Survey.node &&
        SurveyVisualSegments == Segments) return;
    SurveyVisualMask = GetSurveyClaimedMask();
    SurveyVisualNode = Survey.node;
    SurveyVisualSegments = Segments;
    for (int32 Node = 0; Node < SurveyIndicators.Num(); ++Node)
    {
        auto* Ring = SurveyIndicators[Node].Get();
        auto* Tint = Ring ? Cast<UMaterialInstanceDynamic>(Ring->GetMaterial(0)) : nullptr;
        if (!Tint) continue;
        const bool bClaimed = Survey.claimed(Node), bScanning = Survey.node == Node;
        Tint->SetVectorParameterValue(TEXT("Tint"), bClaimed ? FLinearColor(.08f, .22f, .20f) :
            bScanning ? FLinearColor(1.f, .58f, .15f) : FLinearColor(.55f, .28f, 1.f));
        // The ring closes clockwise as input accumulates. Its state is directly
        // read from the same ledger that grants the reward, not a demo animation.
        for (int32 Segment = 0; Segment < 24; ++Segment)
        {
            const float Angle = Segment * 2.f * PI / 24;
            const bool bLit = !bScanning || Segment < Segments;
            Ring->UpdateInstanceTransform(Segment, FTransform(FRotator(0, FMath::RadiansToDegrees(Angle) + 90, 0),
                GetSurveyLocation(Node) + FVector(FMath::Cos(Angle) * 180, FMath::Sin(Angle) * 180, 1),
                FVector(.30f, bLit ? .035f : .009f, .025f)), true, Segment == 23, true);
        }
    }
}
