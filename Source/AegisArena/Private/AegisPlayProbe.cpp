#include "AegisPlayProbe.h"

#if UE_BUILD_DEVELOPMENT
#include "AegisAIController.h"
#include "AegisCharacter.h"
#include "AegisLab.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/FileManager.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

namespace
{
FString PhaseName(aegis::TrialPhase Phase)
{
    switch (Phase)
    {
    case aegis::TrialPhase::Briefing: return TEXT("briefing");
    case aegis::TrialPhase::Active: return TEXT("active");
    case aegis::TrialPhase::Intermission: return TEXT("intermission");
    case aegis::TrialPhase::Won: return TEXT("won");
    case aegis::TrialPhase::Lost: return TEXT("lost");
    }
    return TEXT("unknown");
}
TArray<TSharedPtr<FJsonValue>> VectorJson(const FVector& Vector)
{
    return {MakeShared<FJsonValueNumber>(Vector.X), MakeShared<FJsonValueNumber>(Vector.Y),
            MakeShared<FJsonValueNumber>(Vector.Z)};
}
FString RoleOf(const AAegisCharacter* Character)
{
    if (!Character) return TEXT("unknown");
    if (Cast<AAegisPlayerCharacter>(Character)) return TEXT("player");
    return Character->Team == EAegisTeam::Enemy ? TEXT("enemy") : TEXT("companion");
}
}
#endif

AAegisPlayProbe::AAegisPlayProbe()
{
#if UE_BUILD_DEVELOPMENT
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
#endif
}
void AAegisPlayProbe::Initialize(AAegisScenarioRunner* InRunner, AAegisPlayerController* InController)
{
#if UE_BUILD_DEVELOPMENT
    Runner = InRunner;
    PC = InController;
    StartedWall = FPlatformTime::Seconds();
    bRunning = true;
    if (!IsValid(Runner) || !IsValid(PC) || !PC->IsLocalController() || !GetWorld()->IsGameWorld() ||
        !GetWorld()->HasBegunPlay() || !FParse::Param(FCommandLine::Get(), TEXT("AegisPlayProbe")) ||
        !FParse::Param(FCommandLine::Get(), TEXT("AegisInputProbe")) ||
        !FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")) ||
        FParse::Param(FCommandLine::Get(), TEXT("NullRHI")) ||
        !FParse::Value(FCommandLine::Get(), TEXT("AegisPlayOutput="), OutputDirectory))
    {
        Finish(false);
        return;
    }
    OutputDirectory = FPaths::ConvertRelativePathToFull(OutputDirectory);
    if (IFileManager::Get().FileExists(*FPaths::Combine(OutputDirectory, TEXT("play-probe.json"))))
    {
        Finish(false);
        return;
    }
    bMayWrite = IFileManager::Get().MakeDirectory(*OutputDirectory, true);
    for (const TCHAR* Name : {TEXT("playerShots"), TEXT("companionShots"), TEXT("enemyShots"),
             TEXT("playerDistanceCm"), TEXT("companionDistanceCm"), TEXT("enemyDistanceCm"),
             TEXT("playerDamageDealt"), TEXT("enemyDamageDealt"), TEXT("playerDamageTaken"), TEXT("companionDamageTaken"),
             TEXT("enemyDeaths"), TEXT("companionDeaths"), TEXT("playerDeaths"), TEXT("enemyWindups"),
             TEXT("companionWindups"), TEXT("pulseActivations")}) Totals.Add(Name, 0);
    ScreenshotDelegate = FScreenshotRequest::OnScreenshotRequestProcessed().AddUObject(this, &AAegisPlayProbe::ScreenshotCompleted);
    UE_LOG(LogTemp, Display, TEXT("AEGIS_PLAY_PROBE_BEGIN synthetic=1 offscreen=1 fixture_damage=0 frozen_ai=0"));
    Observe();
#else
    (void)InRunner;
    (void)InController;
#endif
}
void AAegisPlayProbe::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
#if UE_BUILD_DEVELOPMENT
    if (!bRunning) return;
    const double Age = FPlatformTime::Seconds() - StartedWall;
    if (Age > 64.5 || !IsValid(PC) || !IsValid(Runner)) { Finish(false); return; }
    const auto Pending = Releases;
    Releases.Reset();
    for (const FKey& Key : Pending) SetKey(Key, false);
    Observe();
    if (Age >= NextSample) { Sample(); NextSample = Age + 1; }
    if (Screenshots.IsEmpty() && Age >= 0.25) Capture(TEXT("play-briefing.png"));
    const TCHAR* TimedScreens[] = {TEXT("play-02.png"), TEXT("play-04.png"), TEXT("play-06.png"), TEXT("play-08.png")};
    if (Screenshots.Num() >= 1 && Screenshots.Num() <= 4 && ScreenshotProcessed == Screenshots.Num() &&
        Age >= 2.0 * Screenshots.Num()) Capture(TimedScreens[Screenshots.Num()-1]);
    if (!bRunning) return;
    auto* Player = Cast<AAegisPlayerCharacter>(PC->GetPawn());
    // Choosing an upgrade is a normal paused gameplay state; the number-key
    // bindings execute when paused. The probe never changes pause state itself.
    if (!Player || (PC->IsPaused() && !Runner->IsUpgradePending())) { Finish(false); return; }
    const auto& Trial = Runner->GetTrial();
    if (!bStarted && Age >= 0.6 && Trial.phase == aegis::TrialPhase::Briefing)
    {
        Tap(EKeys::Enter);
        bStarted = true;
    }
    if (Trial.finished()) Outcome = PhaseName(Trial.phase);
    if (Outcome.IsEmpty() && Age >= 60) Outcome = TEXT("budget_exhausted");
    if (!Outcome.IsEmpty())
    {
        ReleaseControls();
        if (Screenshots.Num() == 5 && ScreenshotProcessed == 5 && !bFinalCaptured)
        {
            Capture(TEXT("play-final.png"));
            bFinalCaptured = true;
        }
        if (ScreenshotProcessed == 6)
        {
            Sample();
            Finish(true);
        }
        return;
    }
    if (Trial.phase == aegis::TrialPhase::Active) Drive(Player, Age);
    else
    {
        ReleaseControls();
        if (Runner->IsUpgradePending() && Age - LastUpgradeInput > 0.5)
        {
            for (int32 I = 1; I <= 3; ++I)
                if (!Runner->HasUpgrade(I))
                {
                    Tap(I == 1 ? EKeys::One : I == 2 ? EKeys::Two : EKeys::Three);
                    LastUpgradeInput = Age;
                    break;
                }
        }
    }
#endif
}
void AAegisPlayProbe::EndPlay(const EEndPlayReason::Type Reason)
{
#if UE_BUILD_DEVELOPMENT
    FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotDelegate);
    for (TActorIterator<AAegisCharacter> It(GetWorld()); It; ++It)
        It->Health->OnDamaged.RemoveDynamic(this, &AAegisPlayProbe::Damaged);
#endif
    Super::EndPlay(Reason);
}

#if UE_BUILD_DEVELOPMENT
void AAegisPlayProbe::SetKey(const FKey& Key, bool Down)
{
    if (!IsValid(PC) || Held.Contains(Key) == Down) return;
    PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Down ? IE_Pressed : IE_Released, Down ? 1.f : 0.f));
    if (Down) Held.Add(Key); else Held.Remove(Key);
    auto Entry = Event(TEXT("input"));
    Entry->SetStringField(TEXT("key"), Key.ToString());
    Entry->SetStringField(TEXT("state"), Down ? TEXT("pressed") : TEXT("released"));
}
void AAegisPlayProbe::Tap(const FKey& Key)
{
    SetKey(Key, true);
    Releases.AddUnique(Key);
}
void AAegisPlayProbe::ReleaseControls()
{
    const auto Keys = Held;
    for (const FKey& Key : Keys) SetKey(Key, false);
    Releases.Reset();
}
TSharedPtr<FJsonObject> AAegisPlayProbe::Event(const TCHAR* Kind)
{
    auto Entry = MakeShared<FJsonObject>();
    Entry->SetStringField(TEXT("kind"), Kind);
    Entry->SetNumberField(TEXT("wallSeconds"), FPlatformTime::Seconds() - StartedWall);
    Entry->SetNumberField(TEXT("gameSeconds"), GetWorld()->GetTimeSeconds());
    Events.Add(MakeShared<FJsonValueObject>(Entry));
    return Entry;
}
void AAegisPlayProbe::Observe()
{
    for (TActorIterator<AAegisCharacter> It(GetWorld()); It; ++It)
    {
        AAegisCharacter* Character = *It;
        if (!IsValid(Character)) continue;
        const uint32 Id = Character->GetUniqueID();
        if (!Observed.Contains(Id))
        {
            FObservation Initial;
            Initial.Position = Character->GetActorLocation();
            Initial.Role = RoleOf(Character);
            Observed.Add(Id, Initial);
            Character->Health->OnDamaged.AddUniqueDynamic(this, &AAegisPlayProbe::Damaged);
            auto Entry = Event(TEXT("spawn_observed"));
            Entry->SetStringField(TEXT("actor"), Character->GetName());
            Entry->SetStringField(TEXT("role"), Initial.Role);
        }
        auto& Previous = Observed[Id];
        Totals.FindOrAdd(Previous.Role + TEXT("DistanceCm")) += FVector::Dist2D(Previous.Position, Character->GetActorLocation());
        Previous.Position = Character->GetActorLocation();
        Totals.FindOrAdd(Previous.Role + TEXT("Shots")) += FMath::Max(0, Character->Combat->RangedShotsFired - Previous.Shots);
        Previous.Shots = Character->Combat->RangedShotsFired;
        if (auto* Player = Cast<AAegisPlayerCharacter>(Character))
        {
            Totals[TEXT("pulseActivations")] += FMath::Max(0, Player->PulseActivations - Previous.Pulses);
            Previous.Pulses = Player->PulseActivations;
        }
        const auto* AI = Cast<AAegisAIController>(Character->GetController());
        const bool Winding = AI && AI->bWindingUpShot;
        if (Winding && !Previous.Winding) ++Totals.FindOrAdd(Previous.Role + TEXT("Windups"));
        Previous.Winding = Winding;
        if (!Character->Health->IsAlive() && !Previous.Dead)
        {
            Previous.Dead = true;
            ++Totals.FindOrAdd(Previous.Role + TEXT("Deaths"));
            auto Entry = Event(TEXT("death"));
            Entry->SetStringField(TEXT("actor"), Character->GetName());
            Entry->SetStringField(TEXT("role"), Previous.Role);
        }
    }
    const auto& Trial = Runner->GetTrial();
    const auto& Objective = Runner->GetOperation();
    if (LastPhase != static_cast<int32>(Trial.phase) || LastWave != Trial.wave || LastRelay != Objective.relay)
    {
        LastPhase = static_cast<int32>(Trial.phase); LastWave = Trial.wave; LastRelay = Objective.relay;
        auto Entry = Event(TEXT("transition"));
        Entry->SetStringField(TEXT("phase"), PhaseName(Trial.phase));
        Entry->SetNumberField(TEXT("wave"), Trial.wave);
        Entry->SetNumberField(TEXT("relay"), Objective.relay);
    }
}
void AAegisPlayProbe::Damaged(float Amount, AActor* Source, AActor* Victim)
{
    if (!bRunning || Amount <= 0) return;
    const auto* Attacker = Cast<AAegisCharacter>(Source);
    const auto* Target = Cast<AAegisCharacter>(Victim);
    const FString SourceRole = RoleOf(Attacker), TargetRole = RoleOf(Target);
    if (SourceRole == TEXT("player")) Totals[TEXT("playerDamageDealt")] += Amount;
    if (SourceRole == TEXT("enemy")) Totals[TEXT("enemyDamageDealt")] += Amount;
    if (TargetRole == TEXT("player")) Totals[TEXT("playerDamageTaken")] += Amount;
    if (TargetRole == TEXT("companion")) Totals[TEXT("companionDamageTaken")] += Amount;
    auto Entry = Event(TEXT("damage"));
    Entry->SetNumberField(TEXT("amount"), Amount);
    Entry->SetStringField(TEXT("sourceTeam"), SourceRole);
    Entry->SetStringField(TEXT("victimTeam"), TargetRole);
    Entry->SetStringField(TEXT("source"), Source ? Source->GetName() : TEXT("none"));
    Entry->SetStringField(TEXT("victim"), Victim ? Victim->GetName() : TEXT("none"));
}
void AAegisPlayProbe::Drive(AAegisPlayerCharacter* Player, double WallAge)
{
    const FVector Position = Player->GetActorLocation();
    const FVector Goal = Runner->GetObjectiveLocation();
    if (WallAge >= NextPath)
    {
        PathPoints.Reset();
        if (UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), Position, Goal, Player))
            if (Path->IsValid() && !Path->IsPartial()) PathPoints = Path->PathPoints;
        PathIndex = PathPoints.Num() > 1 ? 1 : 0;
        NextPath = WallAge + 0.6;
    }
    while (PathPoints.IsValidIndex(PathIndex) && FVector::Dist2D(Position, PathPoints[PathIndex]) < 70) ++PathIndex;
    FVector Direction = FVector::ZeroVector;
    if (FVector::Dist2D(Position, Goal) > 120 && PathPoints.IsValidIndex(PathIndex))
        Direction = (PathPoints[PathIndex] - Position).GetSafeNormal2D();
    const FRotationMatrix Basis(FRotator(0, Player->SpringArm->GetComponentRotation().Yaw, 0));
    const double Forward = FVector::DotProduct(Direction, Basis.GetUnitAxis(EAxis::X));
    const double Right = FVector::DotProduct(Direction, Basis.GetUnitAxis(EAxis::Y));
    SetKey(EKeys::W, Forward > 0.25); SetKey(EKeys::S, Forward < -0.25);
    SetKey(EKeys::D, Right > 0.25); SetKey(EKeys::A, Right < -0.25);

    AAegisCharacter* Target = nullptr;
    FVector2D TargetScreen;
    double Best = Player->Combat->RangedRange;
    int32 Width = 0, Height = 0;
    PC->GetViewportSize(Width, Height);
    const AHUD* HUD = PC->GetHUD();
    // Only acquire currently visible on-screen hostiles; telemetry never feeds targeting.
    for (TActorIterator<AAegisCharacter> It(GetWorld()); It; ++It)
    {
        if (!It->Health->IsAlive() || !Player->IsHostile(*It) || !PC->LineOfSightTo(*It)) continue;
        const double Distance = FVector::Dist2D(Position, It->GetActorLocation());
        FVector GroundPoint = It->GetActorLocation();
        GroundPoint.Z = Position.Z - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        FVector2D Screen;
        if (Distance >= Best || !PC->ProjectWorldLocationToScreen(GroundPoint, Screen) ||
            Screen.X < 30 || Screen.X > Width-30 || Screen.Y < 30 || Screen.Y > Height-30) continue;
        // Check the exact integer cursor coordinate against the live Canvas hitboxes,
        // including the scaled/letterboxed Menu button. A world-shot press must never
        // become a HUD click; rejecting every candidate below releases held LMB.
        Screen = FVector2D(FMath::RoundToInt(Screen.X), FMath::RoundToInt(Screen.Y));
        if (HUD && HUD->GetHitBoxAtCoordinates(Screen)) continue;
        Target = *It;
        TargetScreen = Screen;
        Best = Distance;
    }
    if (Target) PC->SetMouseLocation(FMath::RoundToInt(TargetScreen.X), FMath::RoundToInt(TargetScreen.Y));
    const uint32 AimId = Target ? Target->GetUniqueID() : 0;
    if (AimId != LastAimId)
    {
        auto Entry = Event(TEXT("aim"));
        Entry->SetStringField(TEXT("actor"), Target ? Target->GetName() : TEXT("none"));
        Entry->SetBoolField(TEXT("lineOfSight"), Target != nullptr);
        Entry->SetNumberField(TEXT("distanceCm"), Target ? Best : 0);
        LastAimId = AimId;
    }
    SetKey(EKeys::LeftMouseButton, Target != nullptr);
    if (Target && Best < Player->PulseRadius * 0.9 && Player->GetPulseCooldownRemaining() <= 0) Tap(EKeys::Q);
    if (WallAge >= NextCommand)
    {
        Tap(CommandIndex % 3 == 0 ? EKeys::Z : CommandIndex % 3 == 1 ? EKeys::X : EKeys::C);
        ++CommandIndex;
        NextCommand = WallAge + 10;
    }
}
TSharedPtr<FJsonObject> AAegisPlayProbe::TotalsJson() const
{
    auto Result = MakeShared<FJsonObject>();
    for (const auto& Entry : Totals) Result->SetNumberField(Entry.Key, Entry.Value);
    return Result;
}
void AAegisPlayProbe::Sample()
{
    auto Row = MakeShared<FJsonObject>();
    Row->SetNumberField(TEXT("wallSeconds"), FPlatformTime::Seconds() - StartedWall);
    Row->SetNumberField(TEXT("gameSeconds"), GetWorld()->GetTimeSeconds());
    Row->SetStringField(TEXT("phase"), PhaseName(Runner->GetTrial().phase));
    Row->SetNumberField(TEXT("wave"), Runner->GetTrial().wave);
    Row->SetNumberField(TEXT("relay"), Runner->GetOperation().relay);
    Row->SetNumberField(TEXT("charge"), Runner->GetOperation().charge);
    Row->SetBoolField(TEXT("objectiveComplete"), Runner->GetOperation().complete);
    Row->SetBoolField(TEXT("objectiveContested"), Runner->IsObjectiveContested());
    Row->SetNumberField(TEXT("enemyAlive"), Runner->LivingEnemies());
    if (const auto* Player = Cast<AAegisCharacter>(PC->GetPawn()))
    {
        Row->SetNumberField(TEXT("playerHealth"), Player->Health->Current);
        Row->SetArrayField(TEXT("playerPosition"), VectorJson(Player->GetActorLocation()));
    }
    Row->SetArrayField(TEXT("objectivePosition"), VectorJson(Runner->GetObjectiveLocation()));
    const auto* Companion = Runner->GetCompanion();
    const auto* AI = Companion ? Cast<AAegisAIController>(Companion->GetController()) : nullptr;
    Row->SetStringField(TEXT("companionCommand"), AI ? (AI->CompanionCommand == EAegisCompanionCommand::Guard ? TEXT("guard") :
        AI->CompanionCommand == EAegisCompanionCommand::Focus ? TEXT("focus") : TEXT("rally")) : TEXT("unavailable"));
    Row->SetObjectField(TEXT("totals"), TotalsJson());
    Samples.Add(MakeShared<FJsonValueObject>(Row));
}
void AAegisPlayProbe::Capture(const TCHAR* Name)
{
    const FString Path = FPaths::Combine(OutputDirectory, Name);
    if (!bMayWrite || IFileManager::Get().FileExists(*Path)) { Finish(false); return; }
    Screenshots.Add(Path);
    FScreenshotRequest::RequestScreenshot(Path, true, false);
}
void AAegisPlayProbe::ScreenshotCompleted()
{
    if (bRunning) ++ScreenshotProcessed;
}
void AAegisPlayProbe::Finish(bool ValidRun)
{
    if (!bRunning) return;
    ReleaseControls();
    bRunning = false;
    SetActorTickEnabled(false);
    const double Age = FPlatformTime::Seconds() - StartedWall;
    bool Passed = ValidRun && bStarted && Age <= 65 && ScreenshotProcessed == 6 && Screenshots.Num() == 6;
    for (const TCHAR* Metric : {TEXT("playerShots"), TEXT("enemyShots"), TEXT("playerDistanceCm"), TEXT("enemyDistanceCm"),
                               TEXT("playerDamageDealt"), TEXT("enemyDamageDealt"), TEXT("playerDamageTaken")})
        Passed &= Totals.FindRef(Metric) > 0;
    auto Report = MakeShared<FJsonObject>();
    Report->SetNumberField(TEXT("schemaVersion"), 1);
    Report->SetStringField(TEXT("engine"), TEXT("unreal-runtime"));
    Report->SetBoolField(TEXT("syntheticInput"), true);
    Report->SetBoolField(TEXT("renderOffscreen"), FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")));
    Report->SetBoolField(TEXT("renderingEnabled"), !FParse::Param(FCommandLine::Get(), TEXT("NullRHI")));
    for (const TCHAR* Flag : {TEXT("humanUsabilityTest"), TEXT("fixtureDamage"), TEXT("frozenAI"),
                             TEXT("teleportedActors"), TEXT("forcedWaves")}) Report->SetBoolField(Flag, false);
    Report->SetBoolField(TEXT("passed"), Passed);
    Report->SetStringField(TEXT("observedOutcome"), Outcome);
    Report->SetStringField(TEXT("finalPhase"), Runner ? PhaseName(Runner->GetTrial().phase) : TEXT("unavailable"));
    Report->SetStringField(TEXT("reason"), TEXT("Observed normal gameplay through simulated player input"));
    Report->SetNumberField(TEXT("worldType"), static_cast<int32>(GetWorld()->WorldType));
    Report->SetBoolField(TEXT("begunPlay"), GetWorld()->HasBegunPlay());
    Report->SetNumberField(TEXT("observationBudgetSeconds"), 60);
    Report->SetNumberField(TEXT("wallSeconds"), Age);
    Report->SetNumberField(TEXT("screenshotRequests"), Screenshots.Num());
    Report->SetNumberField(TEXT("screenshotProcessed"), ScreenshotProcessed);
    Report->SetObjectField(TEXT("totals"), TotalsJson());
    Report->SetArrayField(TEXT("samples"), Samples);
    Report->SetArrayField(TEXT("events"), Events);
    TArray<TSharedPtr<FJsonValue>> Paths;
    for (const FString& Path : Screenshots) Paths.Add(MakeShared<FJsonValueString>(Path));
    Report->SetArrayField(TEXT("screenshots"), Paths);
    FString Json;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    if (!bMayWrite || !FFileHelper::SaveStringToFile(Json, *FPaths::Combine(OutputDirectory, TEXT("play-probe.json")))) Passed = false;
    UE_LOG(LogTemp, Display, TEXT("%s screenshots=%d wall=%.3f outcome=%s"),
           Passed ? TEXT("AEGIS_PLAY_PROBE_PASS") : TEXT("AEGIS_PLAY_PROBE_FAIL"), ScreenshotProcessed, Age, *Outcome);
    FPlatformMisc::RequestExitWithStatus(false, Passed ? 0 : 6, TEXT("Aegis observed play probe"));
}
#else
void AAegisPlayProbe::Damaged(float Amount, AActor* Source, AActor* Victim)
{
    (void)Amount; (void)Source; (void)Victim;
}
#endif
