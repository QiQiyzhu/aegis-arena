#include "AegisAIController.h"
#include "AegisCharacter.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Damage.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Point.h"
#include "NavigationSystem.h"
#include "BrainComponent.h"

UAegisDecisionComponent::UAegisDecisionComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}
void UAegisDecisionComponent::Evaluate(const aegis::Observation& O)
{
    Scores = aegis::utility(O);
    Selected = bUtilityPolicy ? aegis::choose(Scores, Selected)
                              : (O.targetVisible ? aegis::Action::Attack : aegis::Action::Follow);
    ++DecisionCount;
}
AAegisAIController::AAegisAIController()
{
    PrimaryActorTick.bCanEverTick = false;
    Senses = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Perception"));
    SetPerceptionComponent(*Senses);
    Decision = CreateDefaultSubobject<UAegisDecisionComponent>(TEXT("Decision"));
    auto* Sight = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("Sight"));
    Sight->SightRadius = 1500;
    Sight->LoseSightRadius = 1700;
    Sight->PeripheralVisionAngleDegrees = 100;
    Sight->SetMaxAge(2.5f);
    Sight->DetectionByAffiliation.bDetectEnemies = true;
    Sight->DetectionByAffiliation.bDetectFriendlies = true;
    Sight->DetectionByAffiliation.bDetectNeutrals = false;
    Senses->ConfigureSense(*Sight);
    Senses->SetDominantSense(UAISense_Sight::StaticClass());
    auto* Damage = CreateDefaultSubobject<UAISenseConfig_Damage>(TEXT("Damage"));
    Damage->SetMaxAge(2.5f);
    Senses->ConfigureSense(*Damage);
}
ETeamAttitude::Type AAegisAIController::GetTeamAttitudeTowards(const AActor& Other) const
{
    const auto* Self = Cast<AAegisCharacter>(GetPawn());
    const auto* Target = Cast<AAegisCharacter>(&Other);
    if (!Self || !Target || Target->Team == EAegisTeam::Neutral)
        return ETeamAttitude::Neutral;
    return Self->IsHostile(Target) ? ETeamAttitude::Hostile : ETeamAttitude::Friendly;
}
void AAegisAIController::OnPossess(APawn* Pawn)
{
    Super::OnPossess(Pawn);
    auto* Bot = Cast<AAegisAICharacter>(Pawn);
    if (!Bot)
        return;
    if (Bot->bCompanion)
        Bot->Team = EAegisTeam::Player;
    SetGenericTeamId(FGenericTeamId(static_cast<uint8>(Bot->Team)));
    Senses->OnTargetPerceptionUpdated.AddUniqueDynamic(this, &AAegisAIController::PerceptionChanged);
    if (Bot->Behavior)
    {
        RunBehaviorTree(Bot->Behavior);
        DebugState = TEXT("Patrol");
    }
    else
    {
        DebugState = TEXT("Missing BT asset");
        UE_LOG(LogTemp, Error, TEXT("Aegis %s has no Behavior asset; see docs/unreal-setup.md"),
               *GetNameSafe(Bot));
    }
}
void AAegisAIController::OnUnPossess()
{
    ShutdownAI();
    Senses->OnTargetPerceptionUpdated.RemoveDynamic(this, &AAegisAIController::PerceptionChanged);
    Super::OnUnPossess();
}
void AAegisAIController::PerceptionChanged(AActor* Actor, FAIStimulus Stimulus)
{
    auto* Self = Cast<AAegisCharacter>(GetPawn());
    auto* Seen = Cast<AAegisCharacter>(Actor);
    if (!Self || !Seen || !Self->IsHostile(Seen))
        return;
    // A damage event may reveal a historical location, never grant current visual tracking.
    if (Stimulus.WasSuccessfullySensed())
    {
        LastKnown = Stimulus.StimulusLocation;
        LastSensedAt = GetWorld()->GetTimeSeconds();
    }
}
void AAegisAIController::RefreshDecision()
{
    auto* Self = Cast<AAegisAICharacter>(GetPawn());
    auto* BB = GetBlackboardComponent();
    if (bPaused || !Self || !Self->Health->IsAlive() || !BB)
        return;
    TArray<AActor*> Visible;
    Senses->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Visible);
    ObservedTarget.Reset();
    ObservedAlly.Reset();
    double Best = DBL_MAX;
    for (AActor* Actor : Visible)
    {
        auto* C = Cast<AAegisCharacter>(Actor);
        if (!C || !C->Health->IsAlive() || C == Self)
            continue;
        if (Self->IsHostile(C))
        {
            const double D = FVector::DistSquared(C->GetActorLocation(), Self->GetActorLocation());
            if (D < Best)
            {
                Best = D;
                ObservedTarget = C;
            }
        }
        else if (C->Team == EAegisTeam::Player)
        {
            const auto* AlliedBot = Cast<AAegisAICharacter>(C);
            if (!AlliedBot || !AlliedBot->bCompanion)
                ObservedAlly = C;
        }
    }
    bTargetVisible = ObservedTarget.IsValid();
    if (bTargetVisible)
    {
        LastKnown = ObservedTarget->GetActorLocation();
        LastSensedAt = GetWorld()->GetTimeSeconds();
    }
    const bool Memory = GetWorld()->GetTimeSeconds() - LastSensedAt < 2.5;
    aegis::Observation O;
    O.health = Self->Health->Current / Self->Health->Maximum;
    O.targetVisible = bTargetVisible;
    O.targetRemembered = Memory;
    O.targetDistance = bTargetVisible ? FVector::Dist(Self->GetActorLocation(), LastKnown) / 100 : 1000;
    O.allyKnown = ObservedAlly.IsValid();
    if (O.allyKnown)
    {
        O.allyHealth = ObservedAlly->Health->Current / ObservedAlly->Health->Maximum;
        O.allyDistance = FVector::Dist(Self->GetActorLocation(), ObservedAlly->GetActorLocation()) / 100;
    }
    O.supportReady = GetWorld()->GetTimeSeconds() >= SupportReadyAt;
    Decision->Evaluate(O);
    BB->SetValueAsObject(TEXT("TargetActor"), ObservedTarget.Get());
    BB->SetValueAsVector(TEXT("LastKnownLocation"), LastKnown);
    BB->SetValueAsBool(TEXT("HasLOS"), bTargetVisible);
    BB->SetValueAsBool(TEXT("HasMemory"), Memory);
    BB->SetValueAsBool(TEXT("CriticalHealth"), O.health < 0.28);
    BB->SetValueAsBool(TEXT("NeedsRecovery"), O.health < 0.65 && !Memory);
    BB->SetValueAsBool(TEXT("NeedsCover"),
                       O.health < 0.65 && bTargetVisible && Self->Combat->CooldownRemaining() > 0);
    BB->SetValueAsBool(TEXT("InRange"), bTargetVisible && O.targetDistance < 9);
    BB->SetValueAsBool(TEXT("IsCompanion"), Self->bCompanion);
    BB->SetValueAsInt(TEXT("UtilityAction"), static_cast<int32>(Decision->Selected));
    BB->SetValueAsBool(TEXT("QueryPending"), QueryId != INDEX_NONE);
}
bool AAegisAIController::RequestTacticalPoint(UEnvQuery* Query)
{
    if (!Query || !GetPawn() || QueryId != INDEX_NONE || GetWorld()->GetTimeSeconds() < NextQueryAt)
        return false;
    NextQueryAt = GetWorld()->GetTimeSeconds() + 1;
    bHasTacticalPoint = false;
    FEnvQueryRequest Request(Query, this);
    QueryId = Request.Execute(EEnvQueryRunMode::SingleResult, this, &AAegisAIController::QueryFinished);
    return QueryId != INDEX_NONE;
}
void AAegisAIController::QueryFinished(TSharedPtr<FEnvQueryResult> Result)
{
    if (!Result.IsValid() || Result->QueryID != QueryId)
        return;
    QueryId = INDEX_NONE;
    if (bPaused || !GetPawn() || !Result->IsSuccessful() || Result->Items.IsEmpty())
        return;
    SelectedPoint = Result->GetItemAsLocation(0);
    bHasTacticalPoint = true;
    ++CoverUses;
    if (auto* BB = GetBlackboardComponent())
    {
        BB->SetValueAsVector(TEXT("TacticalPoint"), SelectedPoint);
        BB->SetValueAsBool(TEXT("QueryPending"), false);
    }
    MoveToLocation(SelectedPoint, 50);
}
void AAegisAIController::ShutdownAI()
{
    StopMovement();
    if (GetBrainComponent())
        GetBrainComponent()->StopLogic(TEXT("Aegis shutdown"));
    if (QueryId != INDEX_NONE)
        if (auto* Manager = UEnvQueryManager::GetCurrent(GetWorld()))
            Manager->AbortQuery(QueryId);
    QueryId = INDEX_NONE;
    ObservedTarget.Reset();
    ObservedAlly.Reset();
    bTargetVisible = false;
    DebugState = TEXT("Dead / stopped");
}
bool AAegisAIController::ExecuteAction(FName Action)
{
    auto* Self = Cast<AAegisAICharacter>(GetPawn());
    if (bPaused || !Self || !Self->Health->IsAlive())
        return false;
    DebugState = Action.ToString();
    if (Action == TEXT("Attack"))
    {
        if (!ObservedTarget.IsValid())
            return false;
        SetFocus(ObservedTarget.Get());
        if (FVector::Dist(Self->GetActorLocation(), ObservedTarget->GetActorLocation()) < 180)
            return Self->Combat->Melee();
        return Self->Combat->FireAt(ObservedTarget->GetActorLocation() + FVector(0, 0, 30));
    }
    if (Action == TEXT("Chase") || Action == TEXT("Investigate"))
    {
        MoveToLocation(LastKnown, 100);
        return true;
    }
    if (Action == TEXT("FindCover"))
        return RequestTacticalPoint(Self->CoverQuery);
    if (Action == TEXT("Retreat"))
        return RequestTacticalPoint(Self->RetreatQuery);
    if (Action == TEXT("AttackPosition"))
        return RequestTacticalPoint(Self->AttackQuery);
    if (Action == TEXT("Recover"))
    {
        Self->Health->Heal(0.7f);
        return true;
    }
    if (Action == TEXT("Follow") || Action == TEXT("Support"))
    {
        if (!ObservedAlly.IsValid())
            return false;
        if (FVector::Dist(Self->GetActorLocation(), ObservedAlly->GetActorLocation()) > 250)
            MoveToActor(ObservedAlly.Get(), 200);
        else if (Action == TEXT("Support") && GetWorld()->GetTimeSeconds() >= SupportReadyAt)
        {
            ObservedAlly->Health->Heal(22);
            SupportReadyAt = GetWorld()->GetTimeSeconds() + 5;
        }
        return true;
    }
    if (Action == TEXT("Patrol"))
    {
        ClearFocus(EAIFocusPriority::Gameplay);
        if (GetMoveStatus() == EPathFollowingStatus::Moving)
            return true;
        auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
        FNavLocation Location;
        if (Nav && Nav->GetRandomReachablePointInRadius(Self->GetActorLocation(), 700, Location))
        {
            MoveToLocation(Location.Location, 80);
            return true;
        }
    }
    return false;
}
UBTService_AegisObserve::UBTService_AegisObserve()
{
    NodeName = TEXT("Aegis: authorized observation");
    Interval = 0.2f;
    RandomDeviation = 0.02f;
}
void UBTService_AegisObserve::TickNode(UBehaviorTreeComponent& Owner, uint8* Memory, float Delta)
{
    Super::TickNode(Owner, Memory, Delta);
    if (auto* AI = Cast<AAegisAIController>(Owner.GetAIOwner()))
        AI->RefreshDecision();
}
UBTTask_AegisAction::UBTTask_AegisAction()
{
    NodeName = TEXT("Aegis: execute action");
}
EBTNodeResult::Type UBTTask_AegisAction::ExecuteTask(UBehaviorTreeComponent& Owner, uint8* Memory)
{
    (void)Memory;
    auto* AI = Cast<AAegisAIController>(Owner.GetAIOwner());
    return AI && AI->ExecuteAction(Action) ? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
}
void UEnvQueryContext_AegisThreat::ProvideContext(FEnvQueryInstance& Instance,
                                                  FEnvQueryContextData& Data) const
{
    const auto* AI = Cast<AAegisAIController>(Instance.Owner.Get());
    if (AI)
        UEnvQueryItemType_Point::SetContextHelper(Data, AI->LastKnown);
}
