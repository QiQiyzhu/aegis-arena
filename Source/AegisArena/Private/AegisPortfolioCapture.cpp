#include "AegisPortfolioCapture.h"
#include "AegisPortfolio.h"
#include "AegisPortfolioMusic.h"
#include "AegisCharacter.h"
#include "AegisAIController.h"
#include "AegisLab.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/FileManager.h"
#include "InputKeyEventArgs.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

AAegisPortfolioCapture::AAegisPortfolioCapture()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}
void AAegisPortfolioCapture::Initialize(AAegisScenarioRunner* InRunner)
{
#if !UE_BUILD_SHIPPING
    Runner = InRunner;
    bV2 = FParse::Param(FCommandLine::Get(), TEXT("AegisV2"));
    if (bV2) NextUpgrade = 3;
    PC = Cast<AAegisPlayerController>(GetWorld()->GetFirstPlayerController());
    if (!Runner || !PC || !FParse::Value(FCommandLine::Get(), TEXT("AegisPortfolioCaptureOutput="), Output))
        return;
    Output = FPaths::ConvertRelativePathToFull(Output);
    bCapture = !FParse::Param(FCommandLine::Get(), TEXT("AegisPortfolioNoFrames"));
    FParse::Value(FCommandLine::Get(), TEXT("AegisPortfolioFrameInterval="), CaptureEvery);
    CaptureEvery = FMath::Clamp(CaptureEvery, 0.016, 5.0);
    if (IFileManager::Get().FileExists(*FPaths::Combine(Output, TEXT("capture.json"))))
        return;
    IFileManager::Get().MakeDirectory(*FPaths::Combine(Output, TEXT("frames")), true);
    ScreenshotHandle = FScreenshotRequest::OnScreenshotRequestProcessed().AddUObject(
        this, &AAegisPortfolioCapture::ScreenshotProcessed);
    StartedWall = FPlatformTime::Seconds();
    bRunning = true;
    UE_LOG(LogTemp, Display,
           TEXT("AEGIS_PORTFOLIO_CAPTURE_BEGIN scripted_input=1 fixture_damage=0 frames=%d"), bCapture);
#endif
}
void AAegisPortfolioCapture::SetKey(FKey Key, bool Down)
{
    if (!PC || Held.Contains(Key) == Down)
        return;
    PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Down ? IE_Pressed : IE_Released, Down ? 1.f : 0.f));
    if (Down)
        Held.Add(Key);
    else
        Held.Remove(Key);
    ++Inputs;
}
void AAegisPortfolioCapture::Tap(FKey Key)
{
    SetKey(Key, true);
    Releases.AddUnique(Key);
}
void AAegisPortfolioCapture::Release()
{
    auto Keys = Held;
    for (const auto& Key : Keys)
        SetKey(Key, false);
    Releases.Reset();
}
void AAegisPortfolioCapture::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bRunning)
        return;
    Clock += FApp::GetDeltaTime();
    if (!Runner || !PC || Clock > 215 || FPlatformTime::Seconds() - StartedWall > 2500)
    {
        Finish(false);
        return;
    }
    auto Keys = Releases;
    Releases.Reset();
    for (const auto& Key : Keys)
        SetKey(Key, false);
    const auto& Trial = Runner->GetTrial();
    if (LastPhase != static_cast<int32>(Trial.phase))
    {
        LastPhase = static_cast<int32>(Trial.phase);
        PhaseAt = Clock;
        Sample();
    }
    if (Clock >= NextSample)
    {
        Sample();
        NextSample = Clock + 0.1;
    }
    if (bCapture && Clock >= NextCapture && !bPending &&
        !(Trial.finished() && FinishedAt >= 0 && Clock - FinishedAt >= 4))
    {
        Capture();
        NextCapture = Clock + CaptureEvery - 0.00001;
    }
    if (Trial.finished())
    {
        Release();
        if (FinishedAt < 0)
            FinishedAt = Clock;
        if (Clock - FinishedAt >= 4 && !bPending)
            Finish(true);
        return;
    }
    if (Trial.phase == aegis::TrialPhase::Briefing)
    {
        if (bV2 && !bRouteSelected && Clock >= 2) { Tap(EKeys::V); bRouteSelected = true; }
        // v2.3 portfolio takes a short, reversible language pass during the
        // briefing so the delivered video proves Chinese default + English
        // switch without changing gameplay or telemetry.
        if (FParse::Param(FCommandLine::Get(), TEXT("AegisV23")) && !bLanguageShown && Clock >= 3.5)
        {
            Tap(EKeys::L);
            bLanguageShown = true;
        }
        if (Clock >= (FParse::Param(FCommandLine::Get(), TEXT("AegisV23")) ? 7.0 : 4.0))
            Tap(EKeys::Enter);
        return;
    }
    if (Runner->IsUpgradePending())
    {
        Release();
        if (FParse::Param(FCommandLine::Get(), TEXT("AegisV23")) && bLanguageShown && !bLanguageRestored && Clock - PhaseAt >= 1.2)
        {
            Tap(EKeys::L);
            bLanguageRestored = true;
        }
        if (Clock - PhaseAt >= 3.5)
        {
            Tap(NextUpgrade == 1 ? EKeys::One : NextUpgrade == 2 ? EKeys::Two : EKeys::Three);
            NextUpgrade = bV2 ? 1 : 3;
        }
        return;
    }
    if (Trial.phase == aegis::TrialPhase::Active && !PC->IsPaused())
    {
        if (auto* Player = Cast<AAegisPlayerCharacter>(PC->GetPawn()))
            Drive(Player);
        if (!FParse::Param(FCommandLine::Get(), TEXT("AegisV23")) && !bDebugShown && Trial.wave == 2 && Trial.elapsed > 30)
        {
            Tap(EKeys::F1);
            bDebugShown = true;
            DebugShownAt = Clock;
        }
        if (bDebugShown && !bDebugClosed && Clock - DebugShownAt > 7)
        {
            Tap(EKeys::F1);
            bDebugClosed = true;
        }
    }
    else
        Release();
}
void AAegisPortfolioCapture::Drive(AAegisPlayerCharacter* Player)
{
    const FVector Position = Player->GetActorLocation();
    AAegisCharacter* Target = nullptr;
    FVector2D TargetScreen = FVector2D::ZeroVector;
    bool Danger = false;
    double Best = 2000;
    int32 W = 0, H = 0;
    PC->GetViewportSize(W, H);
    for (TActorIterator<AAegisCharacter> It(GetWorld()); It; ++It)
    {
        if (!It->Health->IsAlive() || !Player->IsHostile(*It) || !PC->LineOfSightTo(*It))
            continue;
        const double Dist = FVector::Dist2D(Position, It->GetActorLocation());
        FVector Ground = It->GetActorLocation();
        Ground.Z = Position.Z - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        FVector2D Screen;
        if (Dist >= Best || !PC->ProjectWorldLocationToScreen(Ground, Screen) || Screen.X < 30 ||
            Screen.Y < 100 || Screen.X > W - 30 || Screen.Y > H - 130)
            continue;
        if (PC->GetHUD() && PC->GetHUD()->GetHitBoxAtCoordinates(Screen))
            continue;
        if (It->GetShotWindupRemaining() > 0 && Dist < 1100)
            Danger = true;
        Target = *It;
        TargetScreen = Screen;
        Best = Dist;
    }
    FVector Goal = Runner->GetObjectiveLocation();
    if (Runner->GetOperation().complete || Runner->GetTrial().wave == 3)
    {
        if (Target && Best > 600)
            Goal = Target->GetActorLocation();
        else if (Target)
            Goal = Position;
        else if (Runner->LivingEnemies() > 0)
            Goal = FVector(FMath::Sin(Clock * .12) * 1200, FMath::Cos(Clock * .12) * 850, Position.Z);
    }
    auto* CurrentSystem = AAegisPortfolio::Find(GetWorld());
    int32 SurveyTarget = -1;
    if (CurrentSystem && CurrentSystem->IsV23())
    {
        const int32 Mask = CurrentSystem->GetSurveyClaimedMask();
        if (!(Mask & 1) && Runner->GetTrial().wave < 3)
            SurveyTarget = 0;
        else if (!(Mask & 2) && Runner->GetTrial().wave >= 2 && CurrentSystem->CanClaimSurveySupply())
            SurveyTarget = 1;
        if (SurveyTarget >= 0)
        {
            Goal = CurrentSystem->GetSurveyLocation(SurveyTarget);
            const bool bSupply = SurveyTarget == 1;
            if (CurrentSystem->IsSurveySupplySelected() != bSupply && CurrentSystem->GetSurveyNode() < 0)
                Tap(EKeys::H);
            const bool bCanHold = FVector::Dist2D(Position, Goal) < 150 && !Danger && (!Target || Best > 650);
            if (Held.Contains(EKeys::G) && CurrentSystem->GetSurveyNode() < 0)
            {
                SetKey(EKeys::G, false);
                NextSurveyAttempt = Clock + .35;
            }
            else
                SetKey(EKeys::G, bCanHold && Clock >= NextSurveyAttempt);
        }
        else SetKey(EKeys::G, false);
    }
    if (Clock >= NextPath)
    {
        Path.Reset();
        // Navigation queries use the feet and nearby walkable surface, not
        // the capsule centre. The driver still moves using normal WASD input.
        auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
        FNavLocation Start, End;
        LastPathStatus = TEXT("navigation_unavailable");
        if (Nav)
        {
            LastPathStatus = TEXT("start_projection_failed");
            if (Nav->ProjectPointToNavigation(Player->GetNavAgentLocation(), Start, FVector(180, 180, 250)))
            {
                LastPathStatus = TEXT("goal_projection_failed");
                if (Nav->ProjectPointToNavigation(Goal, End, FVector(180, 180, 250)))
                {
                    LastPathStatus = TEXT("no_complete_path");
                    if (auto* P = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), Start.Location,
                                                                                   End.Location, Player))
                        if (P->IsValid() && !P->IsPartial())
                        {
                            Path = P->PathPoints;
                            LastPathStatus = TEXT("complete_path");
                        }
                }
            }
        }
        PathIndex = 0;
        NextPath = Clock + 0.4;
    }
    const bool bSurveyEdition = CurrentSystem && CurrentSystem->IsV23();
    // Clear cover corners before accepting the next waypoint.
    while (Path.IsValidIndex(PathIndex) && FVector::Dist2D(Position, Path[PathIndex]) < (bSurveyEdition ? 20 : 90))
        ++PathIndex;
    FVector Direction = FVector::ZeroVector;
    bClearRouteRecovery = false;
    if (FVector::Dist2D(Position, Goal) > 120 && Path.IsValidIndex(PathIndex))
        Direction = (Path[PathIndex] - Position).GetSafeNormal2D();
    else if (bV2 && Runner->LivingEnemies() == 0 && !Target &&
             FVector::Dist2D(Position, Goal) > 120 && Path.Num() == 0)
    {
        // Recording-driver recovery only: a failed navigation query must not
        // strand a player in a visibly clear corridor after combat. Check the
        // entire physical capsule route, then emit ordinary WASD below.
        // Never bypass a blocker, teleport, or change game/AI rules.
        ++ClearRouteChecks;
        const auto* Capsule = Player->GetCapsuleComponent();
        const FVector End(Goal.X, Goal.Y, Position.Z);
        FCollisionQueryParams Query(SCENE_QUERY_STAT(PortfolioClearRoute), false, Player);
        // Character responses are customized at runtime, so the profile name
        // can be "Custom" without a registered preset. Use its actual channel
        // and response container instead of a profile-name lookup.
        if (!GetWorld()->SweepTestByChannel(Position, End, FQuat::Identity, Capsule->GetCollisionObjectType(),
                FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()),
                Query, FCollisionResponseParams(Capsule->GetCollisionResponseToChannels())))
        {
            Direction = (End - Position).GetSafeNormal2D();
            bClearRouteRecovery = true;
            ++ClearRouteAccepted;
        }
    }
    if (Danger && Target && Clock >= NextDash)
    {
        const FVector Away = (Position - Target->GetActorLocation()).GetSafeNormal2D();
        EvadeDirection = FVector(-Away.Y, Away.X, 0);
        if (FVector::DotProduct(EvadeDirection, -Position) < 0)
            EvadeDirection *= -1;
        EvadeUntil = Clock + 0.48;
        NextDash = Clock + 1.0;
    }
    if (Clock < EvadeUntil)
        Direction = EvadeDirection;
    LastGoal = Goal;
    LastDirection = Direction;
    const FRotationMatrix Basis(FRotator(0, Player->SpringArm->GetComponentRotation().Yaw, 0));
    if (bSurveyEdition && !Direction.IsNearlyZero())
    {
        // Quantization to digital WASD can turn a safe nav vector into a blocked
        // direction. Choose only physically clear normal keyboard directions.
        const auto* Capsule = Player->GetCapsuleComponent();
        FCollisionQueryParams Query(SCENE_QUERY_STAT(PortfolioInputSteering), false, Player);
        double Score = -2;
        FVector Safe = FVector::ZeroVector;
        for (int32 Forward = -1; Forward <= 1; ++Forward)
            for (int32 Right = -1; Right <= 1; ++Right)
            {
                if (Forward == 0 && Right == 0) continue;
                const FVector Candidate = (Basis.GetUnitAxis(EAxis::X) * Forward +
                    Basis.GetUnitAxis(EAxis::Y) * Right).GetSafeNormal2D();
                const double Alignment = FVector::DotProduct(Candidate, Direction);
                if (Alignment <= .05 || Alignment <= Score) continue;
                if (!GetWorld()->SweepTestByChannel(Position, Position + Candidate * 85.f, FQuat::Identity,
                    Capsule->GetCollisionObjectType(),
                    FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()),
                    Query, FCollisionResponseParams(Capsule->GetCollisionResponseToChannels())))
                {
                    Safe = Candidate;
                    Score = Alignment;
                }
            }
        Direction = Safe;
        LastDirection = Direction;
    }
    const double F = FVector::DotProduct(Direction, Basis.GetUnitAxis(EAxis::X)),
                 R = FVector::DotProduct(Direction, Basis.GetUnitAxis(EAxis::Y));
    SetKey(EKeys::W, F > .22);
    SetKey(EKeys::S, F < -.22);
    SetKey(EKeys::D, R > .22);
    SetKey(EKeys::A, R < -.22);
    if (Clock < EvadeUntil && Player->GetDashCooldownRemaining() <= 0)
        Tap(EKeys::SpaceBar);
    if (Target)
        PC->SetMouseLocation(FMath::RoundToInt(TargetScreen.X), FMath::RoundToInt(TargetScreen.Y));
    if (bV2)
    {
        if (Held.Contains(EKeys::RightMouseButton))
        {
            if (!Target || Clock >= ChargeReleaseAt)
            {
                SetKey(EKeys::RightMouseButton, false);
                NextCharge = Clock + 5;
            }
        }
        else if (Target && Best < 1400 && Clock >= NextCharge && CurrentSystem && CurrentSystem->GetEnergy() >= 47)
        {
            SetKey(EKeys::LeftMouseButton, false);
            SetKey(EKeys::RightMouseButton, true);
            ChargeReleaseAt = Clock + .85;
        }
    }
    SetKey(EKeys::LeftMouseButton, Target && Best < Player->Combat->RangedRange - 25 && !Held.Contains(EKeys::RightMouseButton));
    if (Clock >= NextUtility)
    {
        if (auto* System = AAegisPortfolio::Find(GetWorld()))
        {
            const int32 RepairCost = System->GetRepairQuote();
            if (bV2 && Runner->GetTrial().wave < 3 && Runner->IsPlayerInObjective() &&
                !Runner->IsObjectiveContested() && !Runner->GetOperation().complete &&
                !System->IsOverclockUsed() && System->GetEnergy() >= 35)
                Tap(EKeys::F);
            else if (!(SurveyTarget == 1 && Player->Health->Current >= 80) &&
                Player->Health->Current < Player->Health->Maximum - 10 && System->GetEnergy() >= RepairCost &&
                System->GetRepairCooldown() <= 0 && System->GetStatistics().RepairsUsed == 0)
                Tap(EKeys::E);
            else if (Target && Best < Player->PulseRadius * .92 && System->GetEnergy() >= 35 &&
                     Player->GetPulseCooldownRemaining() <= 0 && Player->Health->Current > 60)
                Tap(EKeys::Q);
            else if (!(SurveyTarget == 1 && Player->Health->Current >= 80) &&
                     Player->Health->Current < 105 && System->GetEnergy() >= RepairCost &&
                     System->GetRepairCooldown() <= 0)
                Tap(EKeys::E);
            else if (Target && Best < Player->PulseRadius * .92 && System->GetEnergy() >= 35 &&
                     Player->GetPulseCooldownRemaining() <= 0)
                Tap(EKeys::Q);
        }
        NextUtility = Clock + 0.4;
    }
    if (Clock >= NextCommand)
    {
        Tap(EKeys::Z);
        NextCommand = Clock + 12;
    }
}
void AAegisPortfolioCapture::Sample()
{
    auto Row = MakeShared<FJsonObject>();
    Row->SetNumberField(TEXT("videoSeconds"), Clock);
    Row->SetNumberField(TEXT("worldSeconds"), GetWorld()->GetTimeSeconds());
    Row->SetNumberField(TEXT("frame"), Frame);
    Row->SetNumberField(TEXT("phase"), static_cast<int>(Runner->GetTrial().phase));
    Row->SetNumberField(TEXT("wave"), Runner->GetTrial().wave);
    Row->SetNumberField(TEXT("charge"), Runner->GetOperation().charge);
    Row->SetNumberField(TEXT("relay"), Runner->GetOperation().relay);
    Row->SetNumberField(TEXT("enemies"), Runner->LivingEnemies());
    Row->SetBoolField(TEXT("contested"), Runner->IsObjectiveContested());
    Row->SetBoolField(TEXT("diagnostics"), PC->bShowDiagnostics);
    Row->SetNumberField(TEXT("pathPoints"), Path.Num());
    Row->SetNumberField(TEXT("pathIndex"), PathIndex);
    Row->SetStringField(TEXT("pathQueryStatus"), LastPathStatus);
    Row->SetBoolField(TEXT("clearRouteRecovery"), bClearRouteRecovery);
    Row->SetNumberField(TEXT("clearRouteChecks"), ClearRouteChecks);
    Row->SetNumberField(TEXT("clearRouteAccepted"), ClearRouteAccepted);
    Row->SetArrayField(TEXT("driverGoal"),
                       {MakeShared<FJsonValueNumber>(LastGoal.X), MakeShared<FJsonValueNumber>(LastGoal.Y),
                        MakeShared<FJsonValueNumber>(LastGoal.Z)});
    Row->SetArrayField(TEXT("driverDirection"), {MakeShared<FJsonValueNumber>(LastDirection.X),
                                                 MakeShared<FJsonValueNumber>(LastDirection.Y)});
    Row->SetArrayField(TEXT("heldWasd"), {MakeShared<FJsonValueBoolean>(Held.Contains(EKeys::W)),
                                          MakeShared<FJsonValueBoolean>(Held.Contains(EKeys::A)),
                                          MakeShared<FJsonValueBoolean>(Held.Contains(EKeys::S)),
                                          MakeShared<FJsonValueBoolean>(Held.Contains(EKeys::D))});
    Row->SetArrayField(TEXT("actualWasd"), {MakeShared<FJsonValueBoolean>(PC->IsInputKeyDown(EKeys::W)),
                                            MakeShared<FJsonValueBoolean>(PC->IsInputKeyDown(EKeys::A)),
                                            MakeShared<FJsonValueBoolean>(PC->IsInputKeyDown(EKeys::S)),
                                            MakeShared<FJsonValueBoolean>(PC->IsInputKeyDown(EKeys::D))});
    if (auto* P = Cast<AAegisPlayerCharacter>(PC->GetPawn()))
    {
        Row->SetNumberField(TEXT("health"), P->Health->Current);
        Row->SetNumberField(TEXT("positionZ"), P->GetActorLocation().Z);
        Row->SetNumberField(TEXT("speed"), P->GetVelocity().Size2D());
        Row->SetBoolField(TEXT("combatEnabled"), P->bCombatEnabled);
        Row->SetBoolField(TEXT("moveInputIgnored"), PC->IsMoveInputIgnored());
        Row->SetNumberField(TEXT("shots"), P->Combat->RangedShotsFired);
        const FAegisShotVFXStats& FX = P->GetShotVFXStats();
        auto FXRow = MakeShared<FJsonObject>();
        FXRow->SetNumberField(TEXT("shots"), FX.Shots);
        FXRow->SetNumberField(TEXT("chargedShots"), FX.ChargedShots);
        FXRow->SetNumberField(TEXT("renderedShots"), FX.RenderedShots);
        FXRow->SetNumberField(TEXT("traceImpacts"), FX.TraceImpacts);
        FXRow->SetNumberField(TEXT("damageImpacts"), FX.DamageImpacts);
        FXRow->SetNumberField(TEXT("worldImpacts"), FX.WorldImpacts);
        FXRow->SetNumberField(TEXT("blockedCharacterImpacts"), FX.BlockedCharacterImpacts);
        FXRow->SetNumberField(TEXT("fatalImpacts"), FX.FatalImpacts);
        FXRow->SetNumberField(TEXT("deathsObserved"), FX.DeathsObserved);
        FXRow->SetNumberField(TEXT("renderedDeathBursts"), FX.RenderedDeathBursts);
        FXRow->SetNumberField(TEXT("poolComponents"), FX.PoolComponents);
        FXRow->SetNumberField(TEXT("peakActiveComponents"), FX.PeakActiveComponents);
        FXRow->SetNumberField(TEXT("reusedActiveSlots"), FX.ReusedActiveSlots);
        Row->SetObjectField(TEXT("playerShotVfx"), FXRow);
        Row->SetNumberField(TEXT("pulses"), P->PulseActivations);
        if (bV2)
        {
            Row->SetBoolField(TEXT("charging"), P->IsCharging());
            Row->SetNumberField(TEXT("chargeFraction"), P->GetChargeFraction());
        }
        Row->SetArrayField(TEXT("position"), {MakeShared<FJsonValueNumber>(P->GetActorLocation().X),
                                              MakeShared<FJsonValueNumber>(P->GetActorLocation().Y)});
    }
    if (auto* System = AAegisPortfolio::Find(GetWorld()))
    {
        Row->SetNumberField(TEXT("energy"), System->GetEnergy());
        Row->SetNumberField(TEXT("repairs"), System->GetStatistics().RepairsUsed);
        if (System->IsV23())
        {
            Row->SetNumberField(TEXT("surveyClaimedMask"), System->GetSurveyClaimedMask());
            Row->SetNumberField(TEXT("surveyKeys"), System->GetStatistics().SurveyKeys);
            Row->SetNumberField(TEXT("surveySupplies"), System->GetStatistics().SurveySupplies);
            Row->SetNumberField(TEXT("surveyBoosts"), System->GetStatistics().SurveyBoosts);
            Row->SetNumberField(TEXT("surveyNode"), System->GetSurveyNode());
            Row->SetNumberField(TEXT("surveyProgress"), System->GetSurveyProgress());
            Row->SetBoolField(TEXT("surveySupplySelected"), System->IsSurveySupplySelected());
            Row->SetBoolField(TEXT("surveyHeld"), PC->IsInputKeyDown(EKeys::G));
        }
        if (bV2)
        {
            Row->SetNumberField(TEXT("repairQuote"), System->GetRepairQuote());
            Row->SetNumberField(TEXT("chargedShots"), System->GetStatistics().ChargedShots);
            Row->SetNumberField(TEXT("overclocks"), System->GetStatistics().Overclocks);
            Row->SetNumberField(TEXT("overclockRemaining"), System->GetOverclockRemaining());
            Row->SetNumberField(TEXT("symbiosisHealing"), System->GetStatistics().SymbiosisPlayerHealing + System->GetStatistics().SymbiosisCompanionHealing);
            Row->SetBoolField(TEXT("northRouteFirst"), Runner->IsNorthRouteFirst());
            Row->SetNumberField(TEXT("requiredObjectiveSeconds"), Runner->GetOperation().requiredSeconds());
            if (auto* Ally = Runner->GetCompanion())
            {
                Row->SetNumberField(TEXT("companionHealth"), Ally->Health->Current);
                if (auto* AI = Cast<AAegisAIController>(Ally->GetController()))
                {
                    Row->SetStringField(TEXT("companionState"), AI->DebugState);
                    Row->SetBoolField(TEXT("companionSight"), AI->bTargetVisible);
                    Row->SetStringField(TEXT("guardSlotReason"), AI->GuardSlotReason);
                }
            }
        }
    }
    if (bV2)
        for (TActorIterator<AAegisPortfolioMusic> It(GetWorld()); It; ++It)
        {
            Row->SetStringField(TEXT("musicState"), It->GetState());
            Row->SetBoolField(TEXT("musicMuted"), It->IsMuted());
            Row->SetNumberField(TEXT("musicActiveChannels"), It->GetActiveComponentCount());
            Row->SetNumberField(TEXT("musicPlayingChannels"), It->GetPlayingComponentCount());
            Row->SetNumberField(TEXT("musicLoadedTracks"), It->GetLoadedTrackCount());
            Row->SetArrayField(TEXT("musicChannelGains"),
                {MakeShared<FJsonValueNumber>(It->GetChannelGain(0)), MakeShared<FJsonValueNumber>(It->GetChannelGain(1))});
            break;
        }
    Samples.Add(MakeShared<FJsonValueObject>(Row));
}
void AAegisPortfolioCapture::RecordSound(const TCHAR* Name, const FVector& Location, float Volume,
                                        float Pitch, const TCHAR* AssetPath)
{
    if (!bRunning)
        return;
    auto Row = MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("asset"), Name);
    Row->SetNumberField(TEXT("videoSeconds"), Clock);
    Row->SetNumberField(TEXT("worldSeconds"), GetWorld()->GetTimeSeconds());
    Row->SetNumberField(TEXT("volume"), Volume);
    Row->SetNumberField(TEXT("pitch"), Pitch);
    Row->SetStringField(TEXT("assetPath"), AssetPath);
    Row->SetArrayField(TEXT("location"),
                       {MakeShared<FJsonValueNumber>(Location.X), MakeShared<FJsonValueNumber>(Location.Y),
                        MakeShared<FJsonValueNumber>(Location.Z)});
    AudioEvents.Add(MakeShared<FJsonValueObject>(Row));
}
void AAegisPortfolioCapture::RecordMusic(const TCHAR* State, const TCHAR* Asset, float Volume,
                                        float FadeSeconds, bool bLoop, const TCHAR* Reason, int32 Channel)
{
    if (!bRunning) return;
    auto Row = MakeShared<FJsonObject>();
    Row->SetNumberField(TEXT("sequence"), MusicEvents.Num() + 1);
    Row->SetStringField(TEXT("state"), State);
    Row->SetStringField(TEXT("asset"), Asset);
    Row->SetStringField(TEXT("assetPath"), FCString::Strlen(Asset) > 0 ?
        FString::Printf(TEXT("/Game/Aegis/V21/Audio/%s.%s"), Asset, Asset) : FString());
    Row->SetNumberField(TEXT("videoSeconds"), Clock);
    Row->SetNumberField(TEXT("worldSeconds"), GetWorld()->GetTimeSeconds());
    Row->SetNumberField(TEXT("volume"), Volume);
    Row->SetNumberField(TEXT("fadeSeconds"), FadeSeconds);
    Row->SetNumberField(TEXT("channel"), Channel);
    Row->SetBoolField(TEXT("loop"), bLoop);
    Row->SetStringField(TEXT("reason"), Reason);
    MusicEvents.Add(MakeShared<FJsonValueObject>(Row));
}
void AAegisPortfolioCapture::Capture()
{
    const FString File = FString::Printf(TEXT("frame-%06d.png"), Frame++);
    auto Row = MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("file"), File);
    Row->SetNumberField(TEXT("videoSeconds"), Clock);
    Row->SetNumberField(TEXT("worldSeconds"), GetWorld()->GetTimeSeconds());
    Frames.Add(MakeShared<FJsonValueObject>(Row));
    bPending = true;
    FScreenshotRequest::RequestScreenshot(FPaths::Combine(Output, TEXT("frames"), File), true, false);
}
void AAegisPortfolioCapture::ScreenshotProcessed()
{
    ++Completed;
    bPending = false;
}
void AAegisPortfolioCapture::Finish(bool Valid)
{
    if (!bRunning)
        return;
    bRunning = false;
    Release();
    // Exercise the same result-page X binding as manual play. A missing binding
    // must leave the process running and fail the external runner's deadline.
    const bool ResultQuit = Valid && Completed == Frame && Runner && Runner->GetTrial().finished() &&
                            PC && !PC->bMenuOpen && !PC->IsPaused() && FinishedAt >= 0 && Clock - FinishedAt >= 4;
    const TCHAR* Outcome = Runner && Runner->GetTrial().phase == aegis::TrialPhase::Won ? TEXT("won") :
                           Runner && Runner->GetTrial().phase == aegis::TrialPhase::Lost ? TEXT("lost") : TEXT("aborted");
    auto Report = MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("validRun"), ResultQuit);
    Report->SetBoolField(TEXT("scriptedPlayer"), true);
    Report->SetBoolField(TEXT("fixtureDamage"), false);
    Report->SetBoolField(TEXT("humanPlaytest"), false);
    if (bV2) Report->SetStringField(TEXT("mode"), TEXT("aegis-prism-fall-v2.0"));
    Report->SetStringField(TEXT("outcome"), Outcome);
    Report->SetStringField(TEXT("quitPath"), ResultQuit ? TEXT("normal PlayerController X from result page") : TEXT("capture failure exit"));
    Report->SetStringField(TEXT("quitKey"), ResultQuit ? TEXT("X") : TEXT("none"));
    Report->SetBoolField(TEXT("quitMenuOpen"), PC && PC->bMenuOpen);
    Report->SetBoolField(TEXT("quitPaused"), PC && PC->IsPaused());
    Report->SetNumberField(TEXT("resultHoldSeconds"), FinishedAt >= 0 ? Clock - FinishedAt : 0);
    Report->SetNumberField(TEXT("inputs"), Inputs);
    Report->SetNumberField(TEXT("frames"), Frame);
    Report->SetNumberField(TEXT("completedFrames"), Completed);
    Report->SetNumberField(TEXT("videoSeconds"), Clock);
    Report->SetArrayField(TEXT("samples"), Samples);
    Report->SetArrayField(TEXT("frameTimes"), Frames);
    Report->SetArrayField(TEXT("audioEvents"), AudioEvents);
    Report->SetArrayField(TEXT("musicEvents"), MusicEvents);
    Report->SetStringField(TEXT("presentationVersion"), bV2 ? TEXT("2.1") : TEXT("1.5"));
    Report->SetStringField(TEXT("visualDeliveryVersion"), FParse::Param(FCommandLine::Get(), TEXT("AegisV23")) ? TEXT("2.3") : (bV2 ? TEXT("2.1") : TEXT("1.5")));
    Report->SetBoolField(TEXT("languageDemo"), bLanguageShown && bLanguageRestored);
    if (FParse::Param(FCommandLine::Get(), TEXT("AegisV23")))
        Report->SetStringField(TEXT("gameplayVersion"), TEXT("2.3"));
    Report->SetStringField(TEXT("musicCapture"),
        TEXT("Actual engine channel/play/gain commands on capture clock; offline reconstruction, not loopback audio"));
    Report->SetStringField(TEXT("audioCapture"),
                           TEXT("Engine-triggered event log for offline mix; not loopback recording"));
    FString Json;
    auto Writer = TJsonWriterFactory<>::Create(&Json);
    FJsonSerializer::Serialize(Report, Writer);
    const bool Saved = FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Output, TEXT("capture.json")),
                                                     FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp, Display, TEXT("AEGIS_PORTFOLIO_CAPTURE_COMPLETE valid=%d frames=%d completed=%d"),
           ResultQuit && Saved, Frame, Completed);
    if (ResultQuit && Saved)
    {
        UE_LOG(LogTemp, Display, TEXT("AEGIS_PORTFOLIO_CAPTURE_RESULT_X outcome=%s"), Outcome);
        SetKey(EKeys::X, true);
    }
    else
        FPlatformMisc::RequestExitWithStatus(false, 2, TEXT("Portfolio capture failure"));
}
void AAegisPortfolioCapture::EndPlay(const EEndPlayReason::Type Reason)
{
    FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);
    Super::EndPlay(Reason);
}
