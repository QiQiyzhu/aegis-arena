#include "AegisCopilotProbe.h"
#if !UE_BUILD_SHIPPING
#include "AegisAIController.h"
#include "AegisLab.h"
#include "AegisSquadPlanner.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Input/Events.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"
#endif

AAegisCopilotProbe::AAegisCopilotProbe()
{
#if !UE_BUILD_SHIPPING
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
#endif
}
void AAegisCopilotProbe::Initialize(AAegisScenarioRunner* InRunner, AAegisPlayerController* InPC)
{
#if !UE_BUILD_SHIPPING
    Runner = InRunner; PC = InPC; Planner = Runner ? Runner->GetSquadPlanner() : nullptr;
    StartedWall = StageWall = FPlatformTime::Seconds();
    bRunning = true;
    bFixtureService = FParse::Param(FCommandLine::Get(), TEXT("AegisPlannerFixture"));
    if (!Planner || !PC || !FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")) ||
        FParse::Param(FCommandLine::Get(), TEXT("NullRHI")) ||
        !FParse::Value(FCommandLine::Get(), TEXT("AegisCopilotOutput="), Output)) { Finish(false); return; }
    Output = FPaths::ConvertRelativePathToFull(Output);
    if (IFileManager::Get().FileExists(*FPaths::Combine(Output, TEXT("copilot-probe.json"))) ||
        !IFileManager::Get().MakeDirectory(*Output, true)) { Output.Reset(); Finish(false); return; }
    UE_LOG(LogTemp, Display, TEXT("AEGIS_COPILOT_PROBE_BEGIN synthetic_slate=1 fixture_service=%d"), bFixtureService ? 1 : 0);
#else
    (void)InRunner; (void)InPC;
#endif
}
void AAegisCopilotProbe::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
    if (!bRunning) return;
    if (FPlatformTime::Seconds() - StartedWall > 70 || !PC || !Runner || !Planner) { Finish(false); return; }
    for (FKey Key : Releases) PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0));
    Releases.Reset();
    const double Age = FPlatformTime::Seconds() - StageWall;
    const auto& Trial = Runner->GetTrial();
    switch (Stage)
    {
    case 0:
        if (Age < 0.4) return;
        if (!Check(TEXT("begun_game_world"), GetWorld()->IsGameWorld() && GetWorld()->HasBegunPlay())) return;
        Tap(EKeys::Enter); Advance(1); break;
    case 1:
        if (Age < 0.3) return;
        if (!Check(TEXT("deployed_with_planner"), Trial.phase == aegis::TrialPhase::Active && Runner->GetCompanion())) return;
        RequestsBefore = Planner->RequestCount; AcceptedBefore = Planner->AcceptedPlans;
        Tap(EKeys::Tab); Advance(2); break;
    case 2:
        if (Age < 0.3) return;
        if (!Check(TEXT("tab_opens_paused_slate_panel"), PC->bPlannerOpen && PC->IsPaused())) return;
        PausedGame = GetWorld()->GetTimeSeconds();
        Capture(TEXT("copilot-open.png"));
        for (TCHAR Character : FString(TEXT("Secure the relay, then cover me.")))
            FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(Character, FModifierKeysState(), 0u, false));
        SlateKey(EKeys::Enter); Advance(3); break;
    case 3:
        if (Age < 0.2) return;
        if (!Check(TEXT("slate_text_and_enter_submit_real_request"), Planner->RequestCount == RequestsBefore + 1)) return;
        Capture(TEXT("copilot-request.png"));
        Advance(4); break;
    case 4:
        if (Planner->bRequestPending) return;
        if (!Check(TEXT("provider_plan_passes_runtime_validation"), Planner->AcceptedPlans == AcceptedBefore + 1 && Planner->bPlanActive)) return;
        if (!Check(TEXT("goal_preserves_capture_then_guard_order"), Planner->StepsLabel.Contains(TEXT("capture_relay")) &&
            Planner->StepsLabel.Contains(TEXT("guard")) && Planner->StepsLabel.Find(TEXT("capture_relay")) < Planner->StepsLabel.Find(TEXT("guard")))) return;
        if (!Check(TEXT("network_response_does_not_advance_paused_game"), PC->IsPaused() &&
            FMath::Abs(GetWorld()->GetTimeSeconds() - PausedGame) < 0.001)) return;
        Provider = Planner->ProviderLabel; Plan = Planner->StepsLabel;
        Capture(TEXT("copilot-plan.png")); Advance(5); break;
    case 5:
        if (Age < 0.3) return;
        CompanionStart = Runner->GetCompanion()->GetActorLocation();
        SlateKey(EKeys::Tab); Advance(6); break;
    case 6:
        if (Age < 0.3) return;
        if (!Check(TEXT("slate_tab_returns_to_combat"), !PC->bPlannerOpen && !PC->IsPaused())) return;
        MovingGame = GetWorld()->GetTimeSeconds(); Advance(7); break;
    case 7:
        if (GetWorld()->GetTimeSeconds() - MovingGame < 2) return;
        {
            auto* Companion = Runner->GetCompanion();
            auto* AI = Companion ? Cast<AAegisAIController>(Companion->GetController()) : nullptr;
            if (!Check(TEXT("validated_capture_drives_real_companion_movement"), AI &&
                AI->CompanionCommand == EAegisCompanionCommand::Rally &&
                FVector::Dist2D(CompanionStart, Companion->GetActorLocation()) > 80)) return;
        }
        Capture(TEXT("copilot-executing.png")); Tap(EKeys::Z); Advance(8); break;
    case 8:
        if (Age < 0.3) return;
        {
            auto* AI = Cast<AAegisAIController>(Runner->GetCompanion()->GetController());
            if (!Check(TEXT("manual_guard_cancels_model_plan"), !Planner->bPlanActive && !Planner->bRequestPending &&
                AI && AI->CompanionCommand == EAegisCompanionCommand::Guard)) return;
        }
        PreviousPawnId = PC->GetPawn()->GetUniqueID(); Tap(EKeys::R); Advance(9); break;
    case 9:
        if (Age < 0.4) return;
        if (!Check(TEXT("restart_clears_plan_and_replaces_pawn"), Trial.phase == aegis::TrialPhase::Briefing &&
            PC->GetPawn()->GetUniqueID() != PreviousPawnId && !Planner->bPlanActive && !Planner->bRequestPending && !PC->bPlannerOpen)) return;
        Tap(EKeys::Tab); Advance(10); break;
    case 10:
        if (Age < 0.2) return;
        if (!Check(TEXT("planner_can_reopen_after_restart"), PC->bPlannerOpen && PC->IsPaused())) return;
        SlateKey(EKeys::Escape); Advance(11); break;
    case 11:
        if (Age < 0.2) return;
        if (!Check(TEXT("slate_escape_closes_panel"), !PC->bPlannerOpen && !PC->IsPaused())) return;
        Finish(true); break;
    default: Finish(false); break;
    }
#endif
}
#if !UE_BUILD_SHIPPING
bool AAegisCopilotProbe::Check(const TCHAR* Name, bool Passed)
{
    auto Row = MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("name"), Name); Row->SetBoolField(TEXT("passed"), Passed);
    Assertions.Add(MakeShared<FJsonValueObject>(Row));
    bAllPassed &= Passed;
    UE_LOG(LogTemp, Display, TEXT("AEGIS_COPILOT_ASSERT_%s %s"), Passed ? TEXT("PASS") : TEXT("FAIL"), Name);
    if (!Passed) Finish(false);
    return Passed;
}
void AAegisCopilotProbe::Capture(const TCHAR* Name)
{
    const FString Path = FPaths::Combine(Output, Name);
    Screenshots.Add(Path); FScreenshotRequest::RequestScreenshot(Path, true, false);
}
void AAegisCopilotProbe::Tap(FKey Key)
{
    PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1)); Releases.Add(Key);
}
void AAegisCopilotProbe::SlateKey(FKey Key)
{
    FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), 0u, false, 0, 0));
    FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), 0u, false, 0, 0));
}
void AAegisCopilotProbe::Advance(int32 Next) { Stage = Next; StageWall = FPlatformTime::Seconds(); }
void AAegisCopilotProbe::Finish(bool Passed)
{
    if (!bRunning) return;
    bRunning = false;
    Passed &= bAllPassed && Assertions.Num() == 13 && Screenshots.Num() == 4;
    for (const FString& File : Screenshots) Passed &= IFileManager::Get().FileSize(*File) > 1024;
    auto Report = MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("passed"), Passed);
    Report->SetStringField(TEXT("engine"), TEXT("unreal-runtime"));
    Report->SetNumberField(TEXT("worldType"), static_cast<int32>(GetWorld()->WorldType));
    Report->SetBoolField(TEXT("begunPlay"), GetWorld()->HasBegunPlay());
    Report->SetBoolField(TEXT("syntheticSlateInput"), true);
    Report->SetBoolField(TEXT("humanUsabilityTest"), false);
    Report->SetBoolField(TEXT("fixtureService"), bFixtureService);
    Report->SetStringField(TEXT("provider"), Provider);
    Report->SetStringField(TEXT("plan"), Plan);
    Report->SetStringField(TEXT("tracePath"), Planner ? Planner->TracePath : FString());
    Report->SetStringField(TEXT("plannerStatus"), Planner ? Planner->StatusLabel : FString());
    Report->SetNumberField(TEXT("wallSeconds"), FPlatformTime::Seconds() - StartedWall);
    Report->SetNumberField(TEXT("lastStage"), Stage);
    Report->SetArrayField(TEXT("assertions"), Assertions);
    TArray<TSharedPtr<FJsonValue>> Paths;
    for (const FString& File : Screenshots) Paths.Add(MakeShared<FJsonValueString>(File));
    Report->SetArrayField(TEXT("screenshots"), Paths);
    FString Json; FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    if (Output.IsEmpty() || !FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Output, TEXT("copilot-probe.json")))) Passed = false;
    UE_LOG(LogTemp, Display, TEXT("AEGIS_COPILOT_PROBE_%s checks=%d"), Passed ? TEXT("PASS") : TEXT("FAIL"), Assertions.Num());
    FPlatformMisc::RequestExitWithStatus(false, Passed ? 0 : 7, TEXT("Aegis copilot probe"));
}
#endif
