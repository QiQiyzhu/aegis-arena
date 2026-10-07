#include "AegisGuardSlotFunctionalTest.h"
#include "AegisAIController.h"
#include "AegisCharacter.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BrainComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TimerManager.h"

namespace
{
const FVector Preferred(-150, 240, 0), Alternate(-150, -240, 0);
}

void AAegisGuardSlotFunctionalTest::StartTest()
{
    Super::StartTest();
    Cleanup();
    Assertions.Reset();
    PreferredPathDetails.Reset(); AlternatePathDetails.Reset();
    bImproved = FParse::Param(FCommandLine::Get(), TEXT("AegisV2"));
    bAllPassed = bRunning = true;
    bMayWrite = bObservedWindup = bHiddenChecksUnchanged = bHiddenShotsUnchanged = false;
    Stage = ShotsAtEngagement = HiddenShots = HiddenChecks = 0;
    MovementCm = 0;
    StartedWall = FPlatformTime::Seconds();
    StartedGame = StageAt = GetWorld()->GetTimeSeconds();
    Deadline = StartedGame + 18;
    if (!FParse::Value(FCommandLine::Get(), TEXT("AegisGuardSlotOutput="), Output))
    { Complete(false, TEXT("Explicit output directory required")); return; }
    Output = FPaths::ConvertRelativePathToFull(Output);
    if (IFileManager::Get().FileExists(*FPaths::Combine(Output, TEXT("guard-slot.json"))) ||
        !IFileManager::Get().MakeDirectory(*Output, true))
    { Complete(false, TEXT("Output exists or is not writable")); return; }
    bMayWrite = true;
    if (!Check(TEXT("game_world_begun"), GetWorld()->WorldType == EWorldType::PIE && GetWorld()->HasBegunPlay())) return;
    GetWorldTimerManager().SetTimer(Timer, this, &AAegisGuardSlotFunctionalTest::Step, 0.05f, true);
}
AStaticMeshActor* AAegisGuardSlotFunctionalTest::SpawnCover(const FVector& Location, const FVector& Scale)
{
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Cover = GetWorld()->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator, Params);
    if (!Cover) return nullptr;
    auto* Mesh = Cover->GetStaticMeshComponent();
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Mesh->SetCollisionProfileName(TEXT("BlockAll"));
    Mesh->SetCanEverAffectNavigation(false); // Both formation paths are on the other side of this fixture obstacle.
    Cover->SetActorScale3D(Scale);
    return Cover;
}
bool AAegisGuardSlotFunctionalTest::Setup()
{
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Leader = GetWorld()->SpawnActor<AAegisCharacter>(FVector(0, 0, 100), FRotator::ZeroRotator, Params);
    Target = GetWorld()->SpawnActor<AAegisCharacter>(FVector(650, 240, 100), FRotator::ZeroRotator, Params);
    LowCover = SpawnCover(FVector(200, 240, 62.5), FVector(0.6, 2.0, 1.25));
    if (!Leader || !Target || !LowCover) return false;
    Leader->Team = EAegisTeam::Player;
    Target->Team = EAegisTeam::Enemy;
    Leader->GetCharacterMovement()->DisableMovement();
    Target->GetCharacterMovement()->DisableMovement();
    // Keep a nonzero route to the preferred slot while remaining within the
    // unchanged 65 cm arrival radius. A query from the exact same ground point
    // can contain one path vertex and fail UNavigationPath::IsValid().
    // Both policies use this identical, disclosed 30 cm setup offset.
    const FTransform Transform(FRotator::ZeroRotator, FVector(-180, 240, 100));
    Bot = GetWorld()->SpawnActorDeferred<AAegisAICharacter>(AAegisAICharacter::StaticClass(), Transform,
        nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Bot) return false;
    Bot->bCompanion = Bot->bTacticalTrial = true;
    Bot->DecisionSeed = 22001;
    Bot->Combat->RangedDamage = 0; // Explicit fixture: observe real shots without changing actor lifetimes.
    Bot->Behavior = LoadObject<UBehaviorTree>(nullptr, TEXT("/Game/Aegis/AI/BT_Aegis.BT_Aegis"));
    Bot->FinishSpawning(Transform);
    AI = Cast<AAegisAIController>(Bot->GetController());
    if (!AI || !AI->GetBrainComponent()) return false;
    AI->GetBrainComponent()->StopLogic(TEXT("Guard slot fixture geometry setup"));
    AI->SetTacticalContext(Leader, FVector::ZeroVector, 0);
    return true;
}
bool AAegisGuardSlotFunctionalTest::HasSight() const
{
    return AI && Target && AI->Senses->HasActiveStimulus(*Target, UAISense::GetSenseID<UAISense_Sight>());
}
bool AAegisGuardSlotFunctionalTest::SlotTrace(const FVector& Point) const
{
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    FNavLocation Projected;
    FVector Start;
    if (!Nav || !Nav->ProjectPointToNavigation(Point, Projected, FVector(110, 110, 250)) ||
        !AI->GetGuardCandidateMuzzle(Projected.Location, Start)) return false;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(AegisGuardFixture), false, Bot);
    return GetWorld()->LineTraceSingleByChannel(Hit, Start, Target->GetActorLocation() + FVector(0, 0, 30),
        ECC_Visibility, Params) && Hit.GetActor() == Target;
}
bool AAegisGuardSlotFunctionalTest::SlotPath(const FVector& Point, TSharedPtr<FJsonObject>& Details) const
{
    Details = MakeShared<FJsonObject>();
    Details->SetStringField(TEXT("start"), Bot->GetActorLocation().ToString());
    Details->SetStringField(TEXT("requested"), Point.ToString());
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    FNavLocation Projected;
    const bool bProjected = Nav && Nav->ProjectPointToNavigation(Point, Projected, FVector(110, 110, 250));
    Details->SetBoolField(TEXT("projected"), bProjected);
    if (!bProjected) return false;
    Details->SetStringField(TEXT("projectedLocation"), Projected.Location.ToString());
    Details->SetStringField(TEXT("rawGroundMuzzle"), (Point + FVector(0, 0, Bot->GetSimpleCollisionHalfHeight() + 30)).ToString());
    Details->SetStringField(TEXT("oldNavMuzzle"), (Projected.Location + FVector(0, 0, Bot->GetSimpleCollisionHalfHeight() + 30)).ToString());
    Details->SetStringField(TEXT("actualCurrentMuzzle"), (Bot->GetActorLocation() + FVector(0, 0, 30)).ToString());
    FVector PhysicalMuzzle;
    const bool bPhysicalMuzzle = AI->GetGuardCandidateMuzzle(Projected.Location, PhysicalMuzzle);
    Details->SetBoolField(TEXT("physicalMuzzleValid"), bPhysicalMuzzle);
    if (bPhysicalMuzzle) Details->SetStringField(TEXT("physicalCandidateMuzzle"), PhysicalMuzzle.ToString());
    const auto* Path = Nav->FindPathToLocationSynchronously(GetWorld(), Bot->GetActorLocation(), Projected.Location, Bot);
    Details->SetBoolField(TEXT("pathExists"), Path != nullptr);
    Details->SetBoolField(TEXT("pathValid"), Path && Path->IsValid());
    Details->SetBoolField(TEXT("pathPartial"), Path && Path->IsPartial());
    Details->SetNumberField(TEXT("pointCount"), Path ? Path->PathPoints.Num() : 0);
    return bPhysicalMuzzle && Path && Path->IsValid() && !Path->IsPartial();
}
void AAegisGuardSlotFunctionalTest::Step()
{
    if (!bRunning) return;
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now > Deadline || FPlatformTime::Seconds() - StartedWall > 30)
    { Complete(false, FString::Printf(TEXT("Fixture timeout at stage %d"), Stage)); return; }
    if (Stage == 0)
    {
        auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
        FNavLocation Projected;
        if (!Nav || !Nav->ProjectPointToNavigation(Preferred, Projected)) return;
        if (!Setup()) { Complete(false, TEXT("Fixture actor setup failed")); return; }
        if (!Check(TEXT("v2_enable_matches_launch"), AI->IsV2GuardSlotEnabled() == bImproved)) return;
        Stage = 1; StageAt = Now;
    }
    else if (Stage == 1 && Now - StageAt >= 0.5 && HasSight())
    {
        // Do not turn asynchronous NavMesh readiness into a policy failure.
        // Both paths still have to be nonpartial, valid real navigation paths.
        const bool bPreferredReady = SlotPath(Preferred, PreferredPathDetails);
        const bool bAlternateReady = SlotPath(Alternate, AlternatePathDetails);
        if ((!bPreferredReady || !bAlternateReady) && Now - StageAt < 3.0) return;
        if (!Check(TEXT("own_sight_authorizes_target"), HasSight())) return;
        if (!Check(TEXT("preferred_slot_weapon_trace_blocked"), !SlotTrace(Preferred))) return;
        if (!Check(TEXT("alternate_slot_weapon_trace_clear"), SlotTrace(Alternate))) return;
        if (!Check(TEXT("both_formation_slots_have_complete_paths"), bPreferredReady && bAlternateReady)) return;
        StartedPosition = Bot->GetActorLocation();
        AI->GetBrainComponent()->RestartLogic();
        Stage = 2; StageAt = Now;
    }
    else if (Stage == 2)
    {
        bObservedWindup |= AI->bWindingUpShot && HasSight() && AI->ObservedTarget.Get() == Target;
        if (bImproved ? AI->TacticalShots <= 0 : Now - StageAt < 3.0) return;
        MovementCm = FVector::Dist2D(StartedPosition, Bot->GetActorLocation());
        ShotsAtEngagement = AI->TacticalShots;
        if (bImproved)
        {
            if (!Check(TEXT("v2_rejects_primary_and_selects_alternate"), AI->GuardSlotChecks >= 2 &&
                AI->GuardSlotRejections > 0 && AI->GuardAlternateSelections > 0)) return;
            if (!Check(TEXT("v2_physically_moves_toward_other_flank"), MovementCm >= 120 &&
                Bot->GetActorLocation().Y < StartedPosition.Y - 120)) return;
            if (!Check(TEXT("v2_winds_up_and_fires_normally"), bObservedWindup && ShotsAtEngagement > 0 &&
                Bot->Combat->RangedShotsFired == ShotsAtEngagement && HasSight() && Target->Health->Current == 100)) return;
        }
        else
        {
            if (!Check(TEXT("baseline_prefers_blocked_slot"), MovementCm <= 50 &&
                FVector::Dist2D(AI->TacticalMoveGoal, Preferred) < 30)) return;
            if (!Check(TEXT("baseline_cannot_fire_through_obstacle"), ShotsAtEngagement == 0 && !bObservedWindup && HasSight())) return;
            if (!Check(TEXT("baseline_v2_counters_unused"), AI->GuardSlotChecks == 0 && AI->GuardAlternateSelections == 0)) return;
        }
        OpaqueCover = SpawnCover(FVector(200, 0, 300), FVector(0.7, 30, 6));
        if (!OpaqueCover) { Complete(false, TEXT("Opaque cover spawn failed")); return; }
        Stage = 3; StageAt = Now;
    }
    else if (Stage == 3 && Now - StageAt >= 0.3 && !HasSight() && !AI->ObservedTarget.IsValid())
    {
        if (!Check(TEXT("opaque_cover_removes_sight"), !HasSight() && !AI->bTargetVisible)) return;
        HiddenShots = AI->TacticalShots;
        HiddenChecks = AI->GuardSlotChecks;
        HiddenMemory = AI->LastKnown;
        // Disclosed intervention behind opaque cover, never supplied to the controller.
        Target->SetActorLocation(FVector(900, 500, 100));
        Stage = 4; StageAt = Now;
    }
    else if (Stage == 4 && Now - StageAt >= 1.0)
    {
        bHiddenShotsUnchanged = !HasSight() && !AI->bWindingUpShot && AI->TacticalShots == HiddenShots;
        bHiddenChecksUnchanged = !AI->ObservedTarget.IsValid() && AI->GuardSlotChecks == HiddenChecks &&
            AI->LastKnown.Equals(HiddenMemory, 0.01);
        if (!Check(TEXT("hidden_target_does_not_fire"), bHiddenShotsUnchanged)) return;
        if (!Check(TEXT("hidden_target_does_not_run_slot_checks"), bHiddenChecksUnchanged)) return;
        Complete(true, bImproved ? TEXT("Authorized firing-slot selection produces real movement and fire") :
            TEXT("Baseline defect reproduced: reachable first slot blocks the usable alternate"));
    }
}
bool AAegisGuardSlotFunctionalTest::Check(const TCHAR* Name, bool Passed)
{
    auto Row = MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("name"), Name);
    Row->SetBoolField(TEXT("passed"), Passed);
    Row->SetNumberField(TEXT("gameSeconds"), GetWorld()->GetTimeSeconds() - StartedGame);
    Assertions.Add(MakeShared<FJsonValueObject>(Row));
    bAllPassed &= AssertTrue(Passed, Name);
    UE_LOG(LogTemp, Display, TEXT("AEGIS_GUARD_SLOT_ASSERT_%s %s"), Passed ? TEXT("PASS") : TEXT("FAIL"), Name);
    if (!Passed) Complete(false, Name);
    return Passed;
}
void AAegisGuardSlotFunctionalTest::Complete(bool Passed, const FString& Reason)
{
    if (!bRunning) return;
    bRunning = false;
    GetWorldTimerManager().ClearTimer(Timer);
    // Timeout is also a measurement: do not leave zero defaults which could be
    // mistaken for observed stillness or zero shots before the stage gate.
    if (Stage >= 2 && IsValid(Bot)) MovementCm = FVector::Dist2D(StartedPosition, Bot->GetActorLocation());
    if (AI) ShotsAtEngagement = AI->TacticalShots;
    Passed &= bAllPassed && Assertions.Num() == 12;
    auto Report = MakeShared<FJsonObject>();
    Report->SetNumberField(TEXT("schemaVersion"), 1);
    Report->SetStringField(TEXT("mode"), bImproved ? TEXT("improved") : TEXT("baseline"));
    Report->SetBoolField(TEXT("passed"), Passed);
    Report->SetNumberField(TEXT("worldType"), static_cast<int32>(GetWorld()->WorldType));
    Report->SetBoolField(TEXT("begunPlay"), GetWorld()->HasBegunPlay());
    Report->SetBoolField(TEXT("damageDisabled"), true);
    Report->SetBoolField(TEXT("stationaryLeaderAndTarget"), true);
    Report->SetBoolField(TEXT("setupBrainPaused"), true);
    Report->SetBoolField(TEXT("sensesToggled"), false);
    Report->SetBoolField(TEXT("baselineDefectObserved"), Passed && !bImproved);
    Report->SetBoolField(TEXT("observedWindup"), bObservedWindup);
    Report->SetNumberField(TEXT("shots"), ShotsAtEngagement);
    Report->SetNumberField(TEXT("movementCm"), MovementCm);
    Report->SetBoolField(TEXT("movementMeasured"), Stage >= 2 && IsValid(Bot));
    Report->SetStringField(TEXT("engagementStartPosition"), StartedPosition.ToString());
    if (IsValid(Bot)) Report->SetStringField(TEXT("finalPosition"), Bot->GetActorLocation().ToString());
    Report->SetNumberField(TEXT("slotChecks"), AI ? AI->GuardSlotChecks : 0);
    Report->SetNumberField(TEXT("slotRejections"), AI ? AI->GuardSlotRejections : 0);
    if (AI) Report->SetStringField(TEXT("slotReason"), AI->GuardSlotReason);
    Report->SetNumberField(TEXT("alternateSelections"), AI ? AI->GuardAlternateSelections : 0);
    Report->SetBoolField(TEXT("hiddenChecksUnchanged"), bHiddenChecksUnchanged);
    Report->SetBoolField(TEXT("hiddenShotsUnchanged"), bHiddenShotsUnchanged);
    Report->SetNumberField(TEXT("wallSeconds"), FPlatformTime::Seconds() - StartedWall);
    Report->SetNumberField(TEXT("gameSeconds"), GetWorld()->GetTimeSeconds() - StartedGame);
    Report->SetStringField(TEXT("reason"), Reason);
    Report->SetNumberField(TEXT("initialPreferredSlotOffsetCm"), 30);
    if (PreferredPathDetails.IsValid()) Report->SetObjectField(TEXT("preferredNavigation"), PreferredPathDetails);
    if (AlternatePathDetails.IsValid()) Report->SetObjectField(TEXT("alternateNavigation"), AlternatePathDetails);
    Report->SetArrayField(TEXT("assertions"), Assertions);
    FString Json;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    Passed &= bMayWrite && FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Output, TEXT("guard-slot.json")));
    if (Passed)
        UE_LOG(LogTemp, Display, TEXT("AEGIS_GUARD_SLOT_FUNCTIONAL_PASS mode=%s assertions=12 world=3 damageDisabled=1"),
            bImproved ? TEXT("improved") : TEXT("baseline"));
    FinishTest(Passed ? EFunctionalTestResult::Succeeded : EFunctionalTestResult::Failed, Reason);
    Cleanup();
}
void AAegisGuardSlotFunctionalTest::Cleanup()
{
    GetWorldTimerManager().ClearTimer(Timer);
    if (IsValid(Bot))
    {
        if (auto* Controller = Bot->GetController()) { Controller->UnPossess(); Controller->Destroy(); }
        Bot->Destroy();
    }
    if (IsValid(Leader)) Leader->Destroy();
    if (IsValid(Target)) Target->Destroy();
    if (IsValid(LowCover)) LowCover->Destroy();
    if (IsValid(OpaqueCover)) OpaqueCover->Destroy();
    Bot = nullptr; AI = nullptr; Leader = nullptr; Target = nullptr; LowCover = nullptr; OpaqueCover = nullptr;
}
void AAegisGuardSlotFunctionalTest::EndPlay(const EEndPlayReason::Type Reason)
{
    if (bRunning) Complete(false, TEXT("World ended before fixture completion"));
    Cleanup();
    Super::EndPlay(Reason);
}
