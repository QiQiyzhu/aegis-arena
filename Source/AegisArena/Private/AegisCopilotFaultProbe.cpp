#include "AegisCopilotFaultProbe.h"
#if !UE_BUILD_SHIPPING
#include "AegisAIController.h"
#include "AegisLab.h"
#include "AegisSquadPlanner.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#endif

AAegisCopilotFaultProbe::AAegisCopilotFaultProbe()
{
#if !UE_BUILD_SHIPPING
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
#endif
}
void AAegisCopilotFaultProbe::Initialize(AAegisScenarioRunner* InRunner, AAegisPlayerController* InController)
{
#if !UE_BUILD_SHIPPING
    Runner = InRunner; PC = InController;
    Planner = IsValid(Runner) ? Runner->GetSquadPlanner() : nullptr;
    StartedWall = StageWall = FPlatformTime::Seconds();
    bRunning = true;
    const FString Endpoint = FPlatformMisc::GetEnvironmentVariable(TEXT("AEGIS_AI_ENDPOINT"));
    if (!IsValid(Planner) || !IsValid(PC) || !PC->IsLocalController() ||
        !FParse::Param(FCommandLine::Get(), TEXT("AegisCopilotFaultProbe")) ||
        !FParse::Param(FCommandLine::Get(), TEXT("AegisPlannerFixture")) ||
        !FParse::Param(FCommandLine::Get(), TEXT("AegisInputProbe")) ||
        !FParse::Param(FCommandLine::Get(), TEXT("NullRHI")) ||
        !FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")) ||
        !Endpoint.StartsWith(TEXT("http://127.0.0.1:")) ||
        FPlatformMisc::GetEnvironmentVariable(TEXT("AEGIS_AI_MODEL")) != TEXT("disclosed-copilot-fault-fixture") ||
        !FPlatformMisc::GetEnvironmentVariable(TEXT("AEGIS_AI_API_KEY")).IsEmpty() ||
        !FParse::Value(FCommandLine::Get(), TEXT("AegisCopilotFaultOutput="), Output))
    { Finish(false, TEXT("requires explicit NullRHI loopback fault fixture and no credential")); return; }
    Output = FPaths::ConvertRelativePathToFull(Output);
    if (IFileManager::Get().FileExists(*FPaths::Combine(Output, TEXT("copilot-fault-probe.json"))) ||
        !IFileManager::Get().MakeDirectory(*Output, true))
    { Finish(false, TEXT("output exists or is not writable")); return; }
    bMayWrite = true;
    InitialRequests = Planner->RequestCount; InitialAccepted = Planner->AcceptedPlans;
    InitialRejected = Planner->RejectedPlans; InitialFallbacks = Planner->Fallbacks;
    UE_LOG(LogTemp, Display, TEXT("AEGIS_COPILOT_FAULT_BEGIN fixture_service=1 synthetic_keyboard=1 direct_planner_api=1 actual_model=0"));
#else
    (void)InRunner; (void)InController;
#endif
}
void AAegisCopilotFaultProbe::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
    if (!bRunning) return;
    if (FPlatformTime::Seconds() - StartedWall > 45 || !IsValid(Runner) || !IsValid(PC) || !IsValid(Planner))
    { Finish(false, TEXT("fault fixture deadline or actor lifetime exceeded")); return; }
    for (FKey Key : Releases) PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0));
    Releases.Reset();
    const double Age = FPlatformTime::Seconds() - StageWall;
    const auto& Trial = Runner->GetTrial();
    switch (Stage)
    {
    case 0:
        if (Age < 0.35) return;
        if (!Check(TEXT("begun_game_world"), GetWorld()->WorldType == EWorldType::Game && GetWorld()->HasBegunPlay())) return;
        Tap(EKeys::Enter); Advance(1); break;
    case 1:
        if (Age < 0.3) return;
        if (!Check(TEXT("deployed_living_linked_companion"), Trial.phase == aegis::TrialPhase::Active && IsGuard())) return;
        Tap(EKeys::P); Advance(2); break;
    case 2:
        if (Age < 0.2) return;
        PausedGame = GetWorld()->GetTimeSeconds(); Advance(3); break;
    case 3:
        if (Age < 0.2) return;
        if (!Check(TEXT("normal_pause_freezes_gameplay"), PC->IsPaused() &&
            FMath::Abs(GetWorld()->GetTimeSeconds() - PausedGame) < 0.001)) return;
        if (!SendRequest(TEXT("malformed_request_reaches_pending"))) return;
        Advance(4); break;
    case 4:
        if (Planner->bRequestPending) return;
        if (!Check(TEXT("malformed_response_rejected"), Planner->RejectedPlans == InitialRejected + 1 &&
            Planner->AcceptedPlans == InitialAccepted && Planner->LastOutcome.Contains(TEXT("model plan must contain")))) return;
        if (!Check(TEXT("malformed_returns_classic_guard"), Planner->Fallbacks == InitialFallbacks + 1 &&
            !Planner->bPlanActive && IsGuard() && Planner->ProviderLabel == TEXT("Classic BT fallback"))) return;
        if (!SendRequest(TEXT("manual_delay_request_pending"))) return;
        Advance(5); break;
    case 5:
        if (Age < 0.3) return;
        Tap(EKeys::P); Advance(6); break;
    case 6:
        if (Age < 0.2) return;
        if (!Check(TEXT("normal_resume_before_manual_guard"), !PC->IsPaused() && Planner->bRequestPending)) return;
        Tap(EKeys::Z); Advance(7); break;
    case 7:
        if (Age < 0.2) return;
        if (!Check(TEXT("manual_guard_cancels_pending"), !Planner->bRequestPending && !Planner->bPlanActive &&
            IsGuard() && Planner->LastOutcome == TEXT("manual override"))) return;
        Tap(EKeys::P); Advance(8); break;
    case 8:
        if (Age < 2.3) return;
        if (!Check(TEXT("delayed_manual_response_never_accepted"), Planner->AcceptedPlans == InitialAccepted &&
            !Planner->bRequestPending && !Planner->bPlanActive)) return;
        if (!Check(TEXT("delayed_manual_response_preserves_guard"), IsGuard() && PC->IsPaused())) return;
        if (!SendRequest(TEXT("restart_delay_request_pending"))) return;
        PreviousPawn = PC->GetPawn()->GetUniqueID();
        PreviousCompanion = Runner->GetCompanion()->GetUniqueID();
        Advance(9); break;
    case 9:
        if (Age < 0.3) return;
        Tap(EKeys::R); Advance(10); break;
    case 10:
        if (Age < 0.3) return;
        if (!Check(TEXT("normal_restart_replaces_pawn"), Trial.phase == aegis::TrialPhase::Briefing &&
            PC->GetPawn() && PC->GetPawn()->GetUniqueID() != PreviousPawn && Runner->GetCompanion() &&
            Runner->GetCompanion()->GetUniqueID() != PreviousCompanion)) return;
        if (!Check(TEXT("restart_invalidates_pending_plan"), !Planner->bRequestPending && !Planner->bPlanActive &&
            Runner->GetSquadPlanner() == Planner && Planner->LastOutcome == TEXT("restart"))) return;
        Advance(11); break;
    case 11:
        if (Age < 2.4) return;
        if (!Check(TEXT("delayed_restart_response_never_accepted"), Planner->AcceptedPlans == InitialAccepted &&
            !Planner->bRequestPending && !Planner->bPlanActive)) return;
        if (!Check(TEXT("restarted_companion_remains_guard"), IsGuard() && Trial.phase == aegis::TrialPhase::Briefing)) return;
        Tap(EKeys::Enter); Advance(12); break;
    case 12:
        if (Age < 0.3) return;
        if (!Check(TEXT("redeploy_before_transport_failures"), Trial.phase == aegis::TrialPhase::Active && IsGuard())) return;
        Tap(EKeys::P); Advance(13); break;
    case 13:
        if (Age < 0.3) return;
        if (!Check(TEXT("transport_requests_run_in_normal_pause"), PC->IsPaused())) return;
        if (!SendRequest(TEXT("http503_request_pending"))) return;
        Advance(14); break;
    case 14:
        if (Planner->bRequestPending) return;
        if (!Check(TEXT("http503_triggers_explicit_fallback"), Planner->RejectedPlans == InitialRejected + 2 &&
            Planner->Fallbacks == InitialFallbacks + 2 && Planner->AcceptedPlans == InitialAccepted &&
            Planner->ProviderLabel == TEXT("Classic BT fallback") && Planner->LastLatencyMs < 3000 && IsGuard())) return;
        TimeoutWall = FPlatformTime::Seconds();
        if (!SendRequest(TEXT("timeout_request_pending"))) return;
        Advance(15); break;
    case 15:
        if (Planner->bRequestPending) return;
        TimeoutElapsed = FPlatformTime::Seconds() - TimeoutWall;
        if (!Check(TEXT("timeout_is_bounded_real_wait"), TimeoutElapsed >= 9 && TimeoutElapsed <= 12.5)) return;
        if (!Check(TEXT("timeout_triggers_explicit_fallback"), Planner->RejectedPlans == InitialRejected + 3 &&
            Planner->Fallbacks == InitialFallbacks + 3 && Planner->ProviderLabel == TEXT("Classic BT fallback") &&
            Planner->LastOutcome.Contains(TEXT("timed out")) && IsGuard())) return;
        if (!Check(TEXT("all_requests_accounted_no_plan_accepted"), Planner->RequestCount == InitialRequests + 5 &&
            Planner->AcceptedPlans == InitialAccepted && Planner->RejectedPlans == InitialRejected + 3 &&
            Planner->Fallbacks == InitialFallbacks + 3 && Planner->CompletedSteps == 0)) return;
        Advance(16); break;
    case 16:
        // The fixture server attempts its late response at twelve seconds. Wait
        // beyond that event even though the client may already have closed it.
        if (FPlatformTime::Seconds() - TimeoutWall < 13) return;
        if (!Check(TEXT("no_later_execution_after_timeout"), !Planner->bRequestPending && !Planner->bPlanActive &&
            Planner->AcceptedPlans == InitialAccepted && Planner->RequestCount == InitialRequests + 5 && IsGuard())) return;
        Finish(true, TEXT("Five disclosed HTTP failure/cancellation cases completed")); break;
    default: Finish(false, TEXT("unknown fault fixture stage")); break;
    }
#endif
}
void AAegisCopilotFaultProbe::EndPlay(const EEndPlayReason::Type Reason)
{
#if !UE_BUILD_SHIPPING
    if (bRunning && IsValid(PC))
        for (FKey Key : Releases) PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0));
#endif
    Super::EndPlay(Reason);
}
#if !UE_BUILD_SHIPPING
bool AAegisCopilotFaultProbe::Check(const TCHAR* Name, bool Passed)
{
    auto Row = MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("name"), Name); Row->SetBoolField(TEXT("passed"), Passed);
    Row->SetNumberField(TEXT("wallSeconds"), FPlatformTime::Seconds() - StartedWall);
    Row->SetNumberField(TEXT("gameSeconds"), GetWorld()->GetTimeSeconds());
    Assertions.Add(MakeShared<FJsonValueObject>(Row));
    bAllPassed &= Passed;
    UE_LOG(LogTemp, Display, TEXT("AEGIS_COPILOT_FAULT_ASSERT_%s %s"), Passed ? TEXT("PASS") : TEXT("FAIL"), Name);
    if (!Passed) Finish(false, Name);
    return Passed;
}
bool AAegisCopilotFaultProbe::IsGuard() const
{
    const auto* Companion = IsValid(Runner) ? Runner->GetCompanion() : nullptr;
    const auto* AI = IsValid(Companion) ? Cast<AAegisAIController>(Companion->GetController()) : nullptr;
    return AI && Companion->Health->IsAlive() && AI->GetLinkedPlayer() == PC->GetPawn() &&
        AI->CompanionCommand == EAegisCompanionCommand::Guard;
}
bool AAegisCopilotFaultProbe::SendRequest(const TCHAR* Assertion)
{
    const int32 Before = Planner->RequestCount;
    const bool Started = Planner->RequestPlan(TEXT("Secure the relay then guard me."));
    return Check(Assertion, Started && Planner->RequestCount == Before + 1 && Planner->bRequestPending);
}
void AAegisCopilotFaultProbe::Tap(FKey Key)
{
    PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1));
    Releases.AddUnique(Key);
}
void AAegisCopilotFaultProbe::Advance(int32 Next) { Stage = Next; StageWall = FPlatformTime::Seconds(); }
void AAegisCopilotFaultProbe::Finish(bool Passed, const FString& Reason)
{
    if (!bRunning) return;
    if (IsValid(PC)) for (FKey Key : Releases) PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0));
    Releases.Reset();
    bRunning = false;
    SetActorTickEnabled(false);
    FailureReason = Reason;
    const double Age = FPlatformTime::Seconds() - StartedWall;
    Passed &= bAllPassed && Assertions.Num() == 25 && Age <= 45;
    auto Report = MakeShared<FJsonObject>();
    Report->SetNumberField(TEXT("schemaVersion"), 1);
    Report->SetBoolField(TEXT("passed"), Passed);
    Report->SetStringField(TEXT("engine"), TEXT("unreal-runtime"));
    Report->SetNumberField(TEXT("worldType"), static_cast<int32>(GetWorld()->WorldType));
    Report->SetBoolField(TEXT("begunPlay"), GetWorld()->HasBegunPlay());
    Report->SetStringField(TEXT("rendering"), TEXT("NullRHI"));
    for (const TCHAR* Flag : {TEXT("fixtureService"), TEXT("syntheticKeyboardInput"), TEXT("directPlannerApi"), TEXT("normalGameplayPauseUsed")})
        Report->SetBoolField(Flag, true);
    for (const TCHAR* Flag : {TEXT("actualModelInference"), TEXT("syntheticSlateInput"), TEXT("humanUsabilityTest"), TEXT("fixtureDamage"), TEXT("combatValuesChanged")})
        Report->SetBoolField(Flag, false);
    Report->SetStringField(TEXT("reason"), Reason);
    Report->SetStringField(TEXT("tracePath"), IsValid(Planner) ? Planner->TracePath : FString());
    Report->SetStringField(TEXT("plannerStatus"), IsValid(Planner) ? Planner->StatusLabel : FString());
    Report->SetStringField(TEXT("lastOutcome"), IsValid(Planner) ? Planner->LastOutcome : FString());
    Report->SetNumberField(TEXT("wallSeconds"), Age);
    Report->SetNumberField(TEXT("timeoutObservedSeconds"), TimeoutElapsed);
    Report->SetNumberField(TEXT("lastStage"), Stage);
    Report->SetArrayField(TEXT("assertions"), Assertions);
    auto Counters = MakeShared<FJsonObject>();
    Counters->SetNumberField(TEXT("requests"), IsValid(Planner) ? Planner->RequestCount - InitialRequests : 0);
    Counters->SetNumberField(TEXT("acceptedPlans"), IsValid(Planner) ? Planner->AcceptedPlans - InitialAccepted : 0);
    Counters->SetNumberField(TEXT("rejectedPlans"), IsValid(Planner) ? Planner->RejectedPlans - InitialRejected : 0);
    Counters->SetNumberField(TEXT("fallbacks"), IsValid(Planner) ? Planner->Fallbacks - InitialFallbacks : 0);
    Counters->SetNumberField(TEXT("completedSteps"), IsValid(Planner) ? Planner->CompletedSteps : 0);
    Report->SetObjectField(TEXT("plannerCounters"), Counters);
    FString Json;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    if (!bMayWrite || !FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Output, TEXT("copilot-fault-probe.json")))) Passed = false;
    UE_LOG(LogTemp, Display, TEXT("AEGIS_COPILOT_FAULT_%s checks=%d requests=%d"),
        Passed ? TEXT("PASS") : TEXT("FAIL"), Assertions.Num(), IsValid(Planner) ? Planner->RequestCount - InitialRequests : 0);
    FPlatformMisc::RequestExitWithStatus(false, Passed ? 0 : 9, TEXT("Aegis copilot fault probe"));
}
#endif
