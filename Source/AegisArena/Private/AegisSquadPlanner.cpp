#include "AegisSquadPlanner.h"
#include "AegisAIController.h"
#include "AegisCharacter.h"
#include "AegisLab.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
constexpr double StepTimeoutSeconds = 10;
constexpr int32 MaximumResponseBytes = 32768;
FString JsonText(const TSharedPtr<FJsonObject>& Object)
{
    FString Text;
    FJsonSerializer::Serialize(Object.ToSharedRef(),
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
    return Text;
}
TArray<TSharedPtr<FJsonValue>> Strings(std::initializer_list<const TCHAR*> Values)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    for (const TCHAR* Value : Values) Result.Add(MakeShared<FJsonValueString>(Value));
    return Result;
}
bool ExactKeys(const TSharedPtr<FJsonObject>& Object, std::initializer_list<const TCHAR*> Keys)
{
    if (!Object || Object->Values.Num() != static_cast<int32>(Keys.size())) return false;
    for (const TCHAR* Key : Keys) if (!Object->HasField(Key)) return false;
    return true;
}
bool PrintableAscii(const FString& Value, int32 Maximum)
{
    if (Value.IsEmpty() || Value.Len() > Maximum) return false;
    for (TCHAR Character : Value) if (Character < 32 || Character > 126) return false;
    return true;
}
bool UniqueObjectKeys(const FString& Text)
{
    struct FContainer { bool bObject = false; TSet<FString> Keys; };
    TArray<FContainer> Stack;
    auto Reader = TJsonReaderFactory<>::Create(Text);
    EJsonNotation Notation;
    while (Reader->ReadNext(Notation))
    {
        if (Notation == EJsonNotation::Error) return false;
        if (Notation == EJsonNotation::ObjectEnd || Notation == EJsonNotation::ArrayEnd)
        {
            if (Stack.IsEmpty()) return false;
            Stack.Pop();
            continue;
        }
        if (!Stack.IsEmpty() && Stack.Last().bObject)
        {
            const FString& Key = Reader->GetIdentifier();
            if (Stack.Last().Keys.Contains(Key)) return false;
            Stack.Last().Keys.Add(Key);
        }
        if (Notation == EJsonNotation::ObjectStart || Notation == EJsonNotation::ArrayStart)
        {
            FContainer Container;
            Container.bObject = Notation == EJsonNotation::ObjectStart;
            Stack.Add(MoveTemp(Container));
        }
    }
    return Stack.IsEmpty() && Reader->GetErrorMessage().IsEmpty();
}
bool ValidEndpoint(const FString& Endpoint, FString& Host, bool& bLoopback)
{
    if (Endpoint.Len() > 2048 || Endpoint.Contains(TEXT("?")) || Endpoint.Contains(TEXT("#")) || Endpoint.Contains(TEXT("\\"))) return false;
    for (TCHAR Character : Endpoint) if (Character <= 32 || Character >= 127) return false;
    const bool Secure = Endpoint.StartsWith(TEXT("https://"), ESearchCase::CaseSensitive);
    const bool Plain = Endpoint.StartsWith(TEXT("http://"), ESearchCase::CaseSensitive);
    if (!Secure && !Plain) return false;
    const FString Rest = Endpoint.Mid(Secure ? 8 : 7);
    int32 Slash = INDEX_NONE;
    if (!Rest.FindChar('/', Slash) || Slash == 0 || Slash == Rest.Len() - 1) return false;
    FString Authority = Rest.Left(Slash), Port;
    Host = Authority;
    if (Authority.Split(TEXT(":"), &Host, &Port))
    {
        if (Port.IsEmpty() || Port.Len() > 5) return false;
        for (TCHAR Character : Port) if (Character < '0' || Character > '9') return false;
        const int32 Number = FCString::Atoi(*Port);
        if (Number < 1 || Number > 65535) return false;
    }
    if (Host.IsEmpty()) return false;
    for (TCHAR Character : Host)
        if (!FChar::IsAlnum(Character) && Character != '.' && Character != '-') return false;
    Host = Host.ToLower();
    bLoopback = Host == TEXT("127.0.0.1");
    return Secure || bLoopback;
}
}

AAegisSquadPlanner::AAegisSquadPlanner()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    PrimaryActorTick.TickInterval = 0;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}
void AAegisSquadPlanner::Initialize(AAegisScenarioRunner* InRunner)
{
    CancelPlan(TEXT("planner initialized"), false);
    Runner = InRunner;
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AegisPlans"));
    if (IFileManager::Get().MakeDirectory(*Directory, true))
    {
        TracePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory,
            FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".jsonl")));
        RecordEvent(TEXT("session"), TEXT("Optional cloud or local planner; classic BT executes all real-time movement and combat"));
    }
    StatusLabel = TEXT("Classic squad control ready");
    ProviderLabel = TEXT("Classic BT");
}
bool AAegisSquadPlanner::ResolveActors(AAegisCharacter*& Player, AAegisAICharacter*& Companion,
                                      AAegisAIController*& Controller) const
{
    Player = nullptr; Companion = nullptr; Controller = nullptr;
    if (!IsValid(Runner) || !GetWorld() || !Runner->IsInteractive() || !Runner->bObjectiveTrial) return false;
    const auto* PC = GetWorld()->GetFirstPlayerController();
    Player = PC ? Cast<AAegisCharacter>(PC->GetPawn()) : nullptr;
    Companion = Runner->GetCompanion();
    Controller = IsValid(Companion) ? Cast<AAegisAIController>(Companion->GetController()) : nullptr;
    return IsValid(Player) && IsValid(Companion) && IsValid(Controller) &&
        Player->Health->IsAlive() && Companion->Health->IsAlive() && Companion->bCompanion &&
        Controller->IsTacticalTrialEnabled() && Controller->GetLinkedPlayer() == Player;
}
bool AAegisSquadPlanner::ContextMatches(FString& Reason) const
{
    AAegisCharacter* Player; AAegisAICharacter* Companion; AAegisAIController* Controller;
    if (!ResolveActors(Player, Companion, Controller)) { Reason = TEXT("player or companion unavailable"); return false; }
    if (Player != BoundPlayer.Get() || Companion != BoundCompanion.Get() || Controller != BoundController.Get())
    { Reason = TEXT("squad changed or restarted"); return false; }
    if (Runner->GetTrial().phase != aegis::TrialPhase::Active)
    { Reason = TEXT("combat phase ended"); return false; }
    if (Runner->GetTrial().wave != BoundWave || Runner->GetOperation().relay != BoundRelay)
    { Reason = TEXT("wave or relay changed"); return false; }
    return true;
}
bool AAegisSquadPlanner::IsVisibleTarget(const AAegisCharacter* Target) const
{
    const auto* Companion = BoundCompanion.Get();
    const auto* Controller = BoundController.Get();
    // Test the active Sight stimulus before reading a previously nominated
    // hostile's live state. Old model tokens never authorize hidden tracking.
    return IsValid(Target) && IsValid(Companion) && IsValid(Controller) &&
        Controller->Senses->HasActiveStimulus(*Target, UAISense::GetSenseID<UAISense_Sight>()) &&
        Companion->IsHostile(Target) && Target->Health->IsAlive();
}
bool AAegisSquadPlanner::ResolveNavigableGoal(const FVector& Desired, FVector& Goal) const
{
    const auto* Companion = BoundCompanion.Get();
    auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    FNavLocation Projected;
    if (!IsValid(Companion) || !Nav || Desired.ContainsNaN() ||
        !Nav->ProjectPointToNavigation(Desired, Projected, FVector(110, 110, 250))) return false;
    UNavigationPath* Path = Nav->FindPathToLocationSynchronously(GetWorld(),
        Companion->GetActorLocation(), Projected.Location, const_cast<AAegisAICharacter*>(Companion));
    if (!Path || !Path->IsValid() || Path->IsPartial()) return false;
    Goal = Projected.Location;
    return true;
}
TSharedPtr<FJsonObject> AAegisSquadPlanner::BuildObservation()
{
    auto Observation = MakeShared<FJsonObject>();
    const auto* Player = BoundPlayer.Get();
    const auto* Companion = BoundCompanion.Get();
    const auto* Controller = BoundController.Get();
    Observation->SetNumberField(TEXT("wave"), BoundWave);
    Observation->SetNumberField(TEXT("relay"), BoundRelay);
    auto PlayerRow = MakeShared<FJsonObject>();
    PlayerRow->SetNumberField(TEXT("health_fraction"), Player->Health->Current / Player->Health->Maximum);
    PlayerRow->SetNumberField(TEXT("distance_cm"), FVector::Dist2D(Player->GetActorLocation(), Companion->GetActorLocation()));
    Observation->SetObjectField(TEXT("player"), PlayerRow);
    auto CompanionRow = MakeShared<FJsonObject>();
    CompanionRow->SetNumberField(TEXT("health_fraction"), Companion->Health->Current / Companion->Health->Maximum);
    Observation->SetObjectField(TEXT("companion"), CompanionRow);
    auto Objective = MakeShared<FJsonObject>();
    Objective->SetBoolField(TEXT("complete"), Runner->GetOperation().complete);
    Objective->SetBoolField(TEXT("contested"), Runner->IsObjectiveContested());
    Objective->SetNumberField(TEXT("charge"), Runner->GetOperation().charge);
    Objective->SetBoolField(TEXT("requires_player"), BoundWave == 3);
    Objective->SetBoolField(TEXT("player_in_ring"),
        FVector::DistSquared2D(Player->GetActorLocation(), Runner->GetObjectiveLocation()) <= 260.f * 260.f);
    Objective->SetBoolField(TEXT("companion_in_ring"),
        FVector::DistSquared2D(Companion->GetActorLocation(), Runner->GetObjectiveLocation()) <= 260.f * 260.f);
    Observation->SetObjectField(TEXT("objective"), Objective);
    TArray<AActor*> Sight;
    Controller->Senses->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Sight);
    TArray<AAegisCharacter*> Candidates;
    for (AActor* Actor : Sight)
        if (auto* Candidate = Cast<AAegisCharacter>(Actor))
            if (IsVisibleTarget(Candidate)) Candidates.Add(Candidate);
    Candidates.Sort([Companion](const AAegisCharacter& A, const AAegisCharacter& B) {
        return FVector::DistSquared2D(A.GetActorLocation(), Companion->GetActorLocation()) <
            FVector::DistSquared2D(B.GetActorLocation(), Companion->GetActorLocation());
    });
    RequestTargets.Reset();
    TArray<TSharedPtr<FJsonValue>> Hostiles;
    for (int32 Index = 0; Index < FMath::Min(6, Candidates.Num()); ++Index)
    {
        auto* Candidate = Candidates[Index];
        const FString Token = FString::Printf(TEXT("t%d"), Index);
        RequestTargets.Add(Token, Candidate);
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("target"), Token);
        Row->SetNumberField(TEXT("distance_cm"), FVector::Dist2D(Candidate->GetActorLocation(), Companion->GetActorLocation()));
        Row->SetNumberField(TEXT("health_fraction"), Candidate->Health->Current / Candidate->Health->Maximum);
        Hostiles.Add(MakeShared<FJsonValueObject>(Row));
    }
    Observation->SetArrayField(TEXT("visible_hostiles"), Hostiles);
    return Observation;
}
TSharedPtr<FJsonObject> AAegisSquadPlanner::BuildSchema() const
{
    auto Skill = MakeShared<FJsonObject>();
    Skill->SetStringField(TEXT("type"), TEXT("string"));
    Skill->SetArrayField(TEXT("enum"), Strings({TEXT("guard"), TEXT("capture_relay"), TEXT("focus_visible"), TEXT("regroup")}));
    auto Target = MakeShared<FJsonObject>();
    Target->SetStringField(TEXT("type"), TEXT("string"));
    Target->SetArrayField(TEXT("enum"), Strings({TEXT("none"), TEXT("t0"), TEXT("t1"), TEXT("t2"), TEXT("t3"), TEXT("t4"), TEXT("t5")}));
    auto StepProperties = MakeShared<FJsonObject>();
    StepProperties->SetObjectField(TEXT("skill"), Skill);
    StepProperties->SetObjectField(TEXT("target"), Target);
    auto Step = MakeShared<FJsonObject>();
    Step->SetStringField(TEXT("type"), TEXT("object"));
    Step->SetBoolField(TEXT("additionalProperties"), false);
    Step->SetArrayField(TEXT("required"), Strings({TEXT("skill"), TEXT("target")}));
    Step->SetObjectField(TEXT("properties"), StepProperties);
    auto PlanSteps = MakeShared<FJsonObject>();
    PlanSteps->SetStringField(TEXT("type"), TEXT("array"));
    PlanSteps->SetNumberField(TEXT("minItems"), 1);
    PlanSteps->SetNumberField(TEXT("maxItems"), 3);
    PlanSteps->SetObjectField(TEXT("items"), Step);
    auto Label = MakeShared<FJsonObject>();
    Label->SetStringField(TEXT("type"), TEXT("string"));
    Label->SetNumberField(TEXT("minLength"), 1);
    Label->SetNumberField(TEXT("maxLength"), 80);
    Label->SetStringField(TEXT("pattern"), TEXT("^[ -~]+$"));
    auto Properties = MakeShared<FJsonObject>();
    Properties->SetObjectField(TEXT("label"), Label);
    Properties->SetObjectField(TEXT("steps"), PlanSteps);
    auto Schema = MakeShared<FJsonObject>();
    Schema->SetStringField(TEXT("type"), TEXT("object"));
    Schema->SetBoolField(TEXT("additionalProperties"), false);
    Schema->SetArrayField(TEXT("required"), Strings({TEXT("label"), TEXT("steps")}));
    Schema->SetObjectField(TEXT("properties"), Properties);
    return Schema;
}
bool AAegisSquadPlanner::RequestPlan(const FString& Instruction)
{
    CancelPlan(TEXT("superseded by a new request"));
    const FTCHARToUTF8 Encoded(*Instruction);
    bool ValidInput = Encoded.Length() > 0 && Encoded.Length() <= 512;
    for (TCHAR Character : Instruction)
        if (Character < 32 && Character != '\n' && Character != '\t') ValidInput = false;
    if (!ValidInput) { RejectAndFallback(TEXT("instruction must contain 1-512 UTF-8 bytes without control characters")); return false; }
    AAegisCharacter* Player; AAegisAICharacter* Companion; AAegisAIController* Controller;
    if (!ResolveActors(Player, Companion, Controller) || Runner->GetTrial().phase != aegis::TrialPhase::Active)
    { RejectAndFallback(TEXT("deploy with a living linked companion before requesting a plan")); return false; }
    FString Endpoint = FPlatformMisc::GetEnvironmentVariable(TEXT("AEGIS_AI_ENDPOINT"));
    RequestedModel = FPlatformMisc::GetEnvironmentVariable(TEXT("AEGIS_AI_MODEL"));
    FString FormatName = FPlatformMisc::GetEnvironmentVariable(TEXT("AEGIS_AI_FORMAT"));
    RequestSecret = FPlatformMisc::GetEnvironmentVariable(TEXT("AEGIS_AI_API_KEY"));
    if (Endpoint.IsEmpty()) Endpoint = TEXT("http://127.0.0.1:18765/v1/chat/completions");
    FString Host;
    bool bLoopback = false;
    if (!ValidEndpoint(Endpoint, Host, bLoopback))
    { RejectAndFallback(TEXT("invalid provider URL: HTTPS required except 127.0.0.1")); return false; }
    if (RequestedModel.IsEmpty() && bLoopback) RequestedModel = TEXT("aegis-local");
    if (FormatName.IsEmpty()) FormatName = bLoopback ? TEXT("json_schema") : TEXT("json_object");
    if (!PrintableAscii(RequestedModel, 128) ||
        (FormatName != TEXT("json_object") && FormatName != TEXT("json_schema")))
    { RejectAndFallback(TEXT("provider model or JSON format is missing or invalid")); return false; }
    if ((!bLoopback && RequestSecret.IsEmpty()) || RequestSecret.Len() > 4096 ||
        RequestSecret.Contains(TEXT("\r")) || RequestSecret.Contains(TEXT("\n")))
    { RejectAndFallback(TEXT("provider credential is missing or invalid; configure it outside the game")); return false; }
    RequestBudgetSeconds = bLoopback ? 10 : 20;
    BoundPlayer = Player; BoundCompanion = Companion; BoundController = Controller;
    BoundWave = Runner->GetTrial().wave; BoundRelay = Runner->GetOperation().relay;
    auto Input = MakeShared<FJsonObject>();
    Input->SetStringField(TEXT("instruction"), Instruction);
    Input->SetObjectField(TEXT("observation"), BuildObservation());
    FString System = TEXT("You translate the player's instruction into the smallest practical plan for ONE allied companion. Follow the requested intent and order. Observations restrict what is legal; seeing enemies is NOT permission to replace a protect/capture/regroup request with an unsolicited attack. Output ONLY the required JSON, with 1-3 steps and a printable ASCII label of 1-80 characters. Skills: guard means protect, follow and cover the player for five seconds; the existing combat AI still handles nearby threats. regroup means first move close to the explicitly linked player. capture_relay means move to the current public relay; it is illegal if objective.complete is true, and extraction requires the player too. focus_visible means engage a currently visible target token; choose only a token in visible_hostiles. EVERY guard, regroup and capture_relay step MUST have target=\"none\". ONLY focus_visible may have target=\"t0\" through \"t5\", and that token must exist in the observation. Never invent coordinates, targets, skills or hidden world facts. Use the instruction's requested skills; do not add unrelated steps. If an attack is requested but no target is visible, choose guard and label the missing target. Examples: 'Protect me' -> {\"label\":\"Protect the player\",\"steps\":[{\"skill\":\"guard\",\"target\":\"none\"}]}; 'Capture the relay then cover me' -> {\"label\":\"Capture then protect\",\"steps\":[{\"skill\":\"capture_relay\",\"target\":\"none\"},{\"skill\":\"guard\",\"target\":\"none\"}]}; 'Attack the visible threat then come back' with t0 visible -> {\"label\":\"Engage then regroup\",\"steps\":[{\"skill\":\"focus_visible\",\"target\":\"t0\"},{\"skill\":\"regroup\",\"target\":\"none\"}]}; 'Come back to me' -> {\"label\":\"Regroup with the player\",\"steps\":[{\"skill\":\"regroup\",\"target\":\"none\"}]}. /no_think");
    if (FormatName == TEXT("json_object"))
        System += TEXT(" The exact JSON schema is: ") + JsonText(BuildSchema());
    TArray<TSharedPtr<FJsonValue>> Messages;
    for (const auto& Pair : {TPair<FString, FString>(TEXT("system"), System),
                             TPair<FString, FString>(TEXT("user"), JsonText(Input))})
    {
        auto Message = MakeShared<FJsonObject>();
        Message->SetStringField(TEXT("role"), Pair.Key);
        Message->SetStringField(TEXT("content"), Pair.Value);
        Messages.Add(MakeShared<FJsonValueObject>(Message));
    }
    auto SchemaContainer = MakeShared<FJsonObject>();
    SchemaContainer->SetStringField(TEXT("name"), TEXT("squad_plan"));
    SchemaContainer->SetBoolField(TEXT("strict"), true);
    SchemaContainer->SetObjectField(TEXT("schema"), BuildSchema());
    auto Format = MakeShared<FJsonObject>();
    Format->SetStringField(TEXT("type"), FormatName);
    if (FormatName == TEXT("json_schema")) Format->SetObjectField(TEXT("json_schema"), SchemaContainer);
    auto Template = MakeShared<FJsonObject>();
    Template->SetBoolField(TEXT("enable_thinking"), false);
    auto Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("model"), RequestedModel);
    Body->SetNumberField(TEXT("temperature"), 0.1);
    Body->SetNumberField(TEXT("max_tokens"), 384);
    Body->SetBoolField(TEXT("stream"), false);
    Body->SetArrayField(TEXT("messages"), Messages);
    Body->SetObjectField(TEXT("response_format"), Format);
    if (bLoopback) Body->SetObjectField(TEXT("chat_template_kwargs"), Template);
    // DeepSeek Chat's documented switch is a top-level thinking object. Other
    // cloud providers receive no llama.cpp or DeepSeek-specific extension.
    if (!bLoopback && Host == TEXT("api.deepseek.com"))
    {
        auto Thinking = MakeShared<FJsonObject>();
        Thinking->SetStringField(TEXT("type"), TEXT("disabled"));
        Body->SetObjectField(TEXT("thinking"), Thinking);
    }
    RequestWall = FPlatformTime::Seconds();
    LastLatencyMs = 0;
    bRequestPending = true;
    StatusLabel = TEXT("Waiting for model plan; classic BT remains active");
    ProviderLabel = (bLoopback ? TEXT("Local / ") : TEXT("Cloud / ")) + RequestedModel + TEXT(" (pending)");
    DecisionLabel.Reset(); StepsLabel.Reset(); LastOutcome.Reset();
    ActiveRequest = FHttpModule::Get().CreateRequest();
    ActiveRequest->SetURL(Endpoint);
    ActiveRequest->SetVerb(TEXT("POST"));
    ActiveRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    if (!RequestSecret.IsEmpty()) ActiveRequest->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + RequestSecret);
    ActiveRequest->SetContentAsString(JsonText(Body));
    ActiveRequest->SetTimeout(RequestBudgetSeconds);
    ActiveRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
    const uint64 RequestGeneration = Generation;
    TWeakObjectPtr<AAegisSquadPlanner> WeakThis(this);
    ActiveRequest->OnProcessRequestComplete().BindLambda([WeakThis, RequestGeneration](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess) {
        if (WeakThis.IsValid()) WeakThis->ResponseReceived(RequestGeneration, Request, Response, bSuccess);
    });
    ++RequestCount;
    Input->SetStringField(TEXT("provider_host"), Host);
    Input->SetStringField(TEXT("model"), RequestedModel);
    Input->SetStringField(TEXT("json_format"), FormatName);
    Input->SetNumberField(TEXT("request_timeout_seconds"), RequestBudgetSeconds);
    Input->SetObjectField(TEXT("request_body"), Body); // Exact prompt/schema/options; never HTTP headers.
    RecordEvent(TEXT("request"), TEXT("Actual HTTP inference requested; no hidden enemy registry supplied"), Input);
    if (!ActiveRequest->ProcessRequest())
    { RejectAndFallback(TEXT("model request could not start")); return false; }
    return true;
}
bool AAegisSquadPlanner::DecodePlan(const FString& Content, TArray<FStep>& Result,
                                   FString& Label, FString& Reason) const
{
    TSharedPtr<FJsonObject> Object;
    if (!UniqueObjectKeys(Content) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Content), Object) ||
        !ExactKeys(Object, {TEXT("label"), TEXT("steps")}) ||
        !Object->TryGetStringField(TEXT("label"), Label) || !PrintableAscii(Label, 80))
    { Reason = TEXT("model plan must contain only a printable ASCII label and steps"); return false; }
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    if (!Object->TryGetArrayField(TEXT("steps"), Rows) || Rows->Num() < 1 || Rows->Num() > 3)
    { Reason = TEXT("model plan must contain 1-3 steps"); return false; }
    for (const auto& Value : *Rows)
    {
        const TSharedPtr<FJsonObject>* Row = nullptr;
        FString Skill, Token;
        if (!Value || !Value->TryGetObject(Row) || !ExactKeys(*Row, {TEXT("skill"), TEXT("target")}) ||
            !(*Row)->TryGetStringField(TEXT("skill"), Skill) || !(*Row)->TryGetStringField(TEXT("target"), Token))
        { Reason = TEXT("step contains missing or unsupported fields"); return false; }
        FStep Step;
        if (Skill == TEXT("guard")) Step.Skill = ESkill::Guard;
        else if (Skill == TEXT("capture_relay")) Step.Skill = ESkill::CaptureRelay;
        else if (Skill == TEXT("focus_visible")) Step.Skill = ESkill::FocusVisible;
        else if (Skill == TEXT("regroup")) Step.Skill = ESkill::Regroup;
        else { Reason = TEXT("unknown skill"); return false; }
        Step.Token = Token;
        if (Step.Skill == ESkill::FocusVisible)
        {
            const auto* Known = RequestTargets.Find(Token);
            if (!Known || !IsVisibleTarget(Known->Get()))
            { Reason = TEXT("focus token was not observed or is no longer visible"); return false; }
            Step.Target = *Known;
        }
        else if (Token != TEXT("none")) { Reason = TEXT("only focus_visible can name a target"); return false; }
        if (Step.Skill == ESkill::CaptureRelay && Runner->GetOperation().complete)
        { Reason = TEXT("current relay is already complete"); return false; }
        Result.Add(Step);
    }
    return true;
}
void AAegisSquadPlanner::ResponseReceived(uint64 RequestGeneration, FHttpRequestPtr Request,
                                         FHttpResponsePtr Response, bool bSuccess)
{
    if (RequestGeneration != Generation || !bRequestPending || Request != ActiveRequest) return;
    LastLatencyMs = static_cast<float>((FPlatformTime::Seconds() - RequestWall) * 1000);
    bRequestPending = false;
    ActiveRequest.Reset();
    const FString Raw = Response ? Response->GetContentAsString() : TEXT("");
    const bool Oversized = FTCHARToUTF8(*Raw).Length() > MaximumResponseBytes;
    const bool EchoedCredential = !RequestSecret.IsEmpty() && Raw.Contains(RequestSecret);
    FString TraceRaw = Raw;
    if (!RequestSecret.IsEmpty()) TraceRaw.ReplaceInline(*RequestSecret, TEXT("[REDACTED]"), ESearchCase::CaseSensitive);
    if (FTCHARToUTF8(*TraceRaw).Length() > MaximumResponseBytes) TraceRaw.LeftInline(MaximumResponseBytes / 4);
    auto Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("http_status"), Response ? Response->GetResponseCode() : 0);
    Data->SetNumberField(TEXT("latency_ms"), LastLatencyMs);
    Data->SetStringField(TEXT("raw_response"), TraceRaw);
    Data->SetBoolField(TEXT("raw_truncated"), Oversized);
    Data->SetBoolField(TEXT("credential_echo_redacted"), EchoedCredential);
    RecordEvent(TEXT("response"), TEXT("Provider response; credential echoes redacted if present"), Data);
    FString Reason;
    if (!ContextMatches(Reason)) { RejectAndFallback(Reason); return; }
    if (!bSuccess || !Response || Response->GetResponseCode() != 200 || LastLatencyMs > RequestBudgetSeconds * 1000)
    { RejectAndFallback(TEXT("model provider unavailable, failed or timed out")); return; }
    if (Oversized) { RejectAndFallback(TEXT("model response exceeded size limit")); return; }
    TSharedPtr<FJsonObject> Envelope;
    const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
    const TSharedPtr<FJsonObject>* Choice = nullptr;
    const TSharedPtr<FJsonObject>* Message = nullptr;
    FString Content, FinishReason;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Raw), Envelope) || !Envelope ||
        !Envelope->TryGetArrayField(TEXT("choices"), Choices) || Choices->Num() != 1 ||
        !(*Choices)[0]->TryGetObject(Choice) || !(*Choice)->TryGetObjectField(TEXT("message"), Message) ||
        !(*Message)->TryGetStringField(TEXT("content"), Content) ||
        !(*Choice)->TryGetStringField(TEXT("finish_reason"), FinishReason) || FinishReason != TEXT("stop"))
    { RejectAndFallback(TEXT("model response is incomplete or is not a chat completion")); return; }
    if (!RequestSecret.IsEmpty() && Content.Contains(RequestSecret))
    { RejectAndFallback(TEXT("provider returned credential material instead of a usable plan")); return; }
    TArray<FStep> Validated;
    FString Label;
    if (!DecodePlan(Content, Validated, Label, Reason)) { RejectAndFallback(Reason); return; }
    Steps = MoveTemp(Validated);
    StepIndex = 0; bStepStarted = false; PlanStartedGame = -1;
    bPlanActive = true;
    ++AcceptedPlans;
    DecisionLabel = Label;
    ProviderLabel.RemoveFromEnd(TEXT(" (pending)"));
    FString ReturnedModel;
    if (Envelope->TryGetStringField(TEXT("model"), ReturnedModel) && PrintableAscii(ReturnedModel, 128) &&
        ReturnedModel != RequestedModel)
        ProviderLabel += TEXT(" [response: ") + ReturnedModel + TEXT("]");
    RequestSecret.Reset();
    StepsLabel.Reset();
    for (int32 Index = 0; Index < Steps.Num(); ++Index)
        StepsLabel += (Index ? TEXT(" > ") : TEXT("")) + SkillName(Steps[Index].Skill);
    LastOutcome = TEXT("Plan validated; execution still pending");
    StatusLabel = GetWorld()->IsPaused() ? TEXT("Plan ready - return to combat to execute") : TEXT("Plan ready");
    auto ValidatedData = MakeShared<FJsonObject>();
    ValidatedData->SetStringField(TEXT("validated_json"), Content);
    ValidatedData->SetNumberField(TEXT("step_count"), Steps.Num());
    RecordEvent(TEXT("plan_accepted"), Label, ValidatedData);
}
FString AAegisSquadPlanner::SkillName(ESkill Skill)
{
    switch (Skill)
    {
    case ESkill::Guard: return TEXT("guard");
    case ESkill::CaptureRelay: return TEXT("capture_relay");
    case ESkill::FocusVisible: return TEXT("focus_visible");
    case ESkill::Regroup: return TEXT("regroup");
    }
    return TEXT("unknown");
}
void AAegisSquadPlanner::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bRequestPending && !bPlanActive) return;
    // A relay increment caused while executing capture is an observed success,
    // not permission for a pending response or another skill to follow it.
    if (bPlanActive && bStepStarted && Steps.IsValidIndex(StepIndex) &&
        Steps[StepIndex].Skill == ESkill::CaptureRelay && IsValid(Runner) &&
        Runner->GetTrial().wave == BoundWave && Runner->GetOperation().relay == BoundRelay + 1)
    {
        ++BoundRelay;
        CompleteStep(TEXT("relay completed in world; contribution is not inferred"));
    }
    FString Reason;
    if (!ContextMatches(Reason)) { CancelPlan(Reason); return; }
    if (bRequestPending)
    {
        if (FPlatformTime::Seconds() - RequestWall > RequestBudgetSeconds)
        {
            LastLatencyMs = static_cast<float>((FPlatformTime::Seconds() - RequestWall) * 1000);
            RejectAndFallback(TEXT("model provider timed out"));
        }
        return;
    }
    if (!bPlanActive) return;
    if (GetWorld()->IsPaused())
    {
        StatusLabel = TEXT("Plan paused - return to combat to execute");
        return;
    }
    const double Now = GetWorld()->GetTimeSeconds();
    if (PlanStartedGame < 0) PlanStartedGame = Now;
    if (Now - PlanStartedGame > 35) { RejectAndFallback(TEXT("plan execution deadline reached")); return; }
    if (!bStepStarted && !BeginStep()) return;
    if (bPlanActive && bStepStarted) UpdateStep();
}
bool AAegisSquadPlanner::BeginStep()
{
    if (!Steps.IsValidIndex(StepIndex)) return false;
    auto* Controller = BoundController.Get();
    auto* Companion = BoundCompanion.Get();
    auto* Player = BoundPlayer.Get();
    const FStep& Step = Steps[StepIndex];
    FVector Goal;
    if (Step.Skill == ESkill::CaptureRelay)
    {
        if (Runner->GetOperation().complete || !ResolveNavigableGoal(Runner->GetObjectiveLocation(), Goal))
        { RejectAndFallback(TEXT("relay goal is complete or has no full navigation path")); return false; }
        Controller->SetCompanionCommand(EAegisCompanionCommand::Rally, nullptr, Goal);
        BestDistance = FVector::Dist2D(Companion->GetActorLocation(), Goal);
    }
    else if (Step.Skill == ESkill::FocusVisible)
    {
        if (!IsVisibleTarget(Step.Target.Get()))
        { RejectAndFallback(TEXT("focus target lost before execution")); return false; }
        Controller->SetCompanionCommand(EAegisCompanionCommand::Focus, Step.Target.Get(), Step.Target->GetActorLocation());
        DamageListenerTarget = Step.Target;
        Step.Target->Health->OnDamaged.AddUniqueDynamic(this, &AAegisSquadPlanner::ObserveDamage);
    }
    else if (Step.Skill == ESkill::Regroup)
    {
        // Regroup uses a temporary Rally at a grounded point near the allied
        // link. It prioritizes closing distance even with a visible threat,
        // whereas normal Guard can continue fighting inside its wider leash.
        const FVector Forward = Player->GetActorForwardVector().GetSafeNormal2D();
        const FVector Right(-Forward.Y, Forward.X, 0);
        if (!ResolveNavigableGoal(Player->GetActorLocation() - Forward * 200 + Right * 180, Goal))
        { RejectAndFallback(TEXT("regroup has no full navigation path")); return false; }
        RegroupPoint = Goal;
        Controller->SetCompanionCommand(EAegisCompanionCommand::Rally, nullptr, Goal);
        BestDistance = FVector::Dist2D(Companion->GetActorLocation(), Player->GetActorLocation());
    }
    else Controller->SetCompanionCommand(EAegisCompanionCommand::Guard);
    StepStartedGame = ProgressGame = GetWorld()->GetTimeSeconds();
    NextRegroupUpdateGame = StepStartedGame + 1;
    StepDamage = 0;
    StepCharge = Runner->GetOperation().charge;
    bStepStarted = true;
    StatusLabel = FString::Printf(TEXT("Executing %d/%d: %s"), StepIndex + 1, Steps.Num(), *SkillName(Step.Skill));
    LastOutcome = TEXT("Skill accepted by controller; world result pending");
    auto Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("skill"), SkillName(Step.Skill));
    Data->SetStringField(TEXT("target"), Step.Token);
    Data->SetNumberField(TEXT("step_index"), StepIndex);
    RecordEvent(TEXT("step_started"), StatusLabel, Data);
    return true;
}
void AAegisSquadPlanner::UpdateStep()
{
    const FStep& Step = Steps[StepIndex];
    auto* Companion = BoundCompanion.Get();
    auto* Player = BoundPlayer.Get();
    const double Now = GetWorld()->GetTimeSeconds();
    const double Age = Now - StepStartedGame;
    StatusLabel = FString::Printf(TEXT("Executing %d/%d: %s"), StepIndex + 1, Steps.Num(), *SkillName(Step.Skill));
    if (Step.Skill == ESkill::FocusVisible)
    {
        if (StepDamage > 0) { CompleteStep(TEXT("companion applied observed damage to the nominated target")); return; }
        if (!IsVisibleTarget(Step.Target.Get()))
        { RejectAndFallback(TEXT("focus target no longer in companion Sight")); return; }
    }
    if (Step.Skill == ESkill::Guard && Age >= 5)
    { CompleteStep(TEXT("guard maintained for five game seconds; no hit or safety guarantee implied")); return; }
    if (Step.Skill == ESkill::Regroup || Step.Skill == ESkill::CaptureRelay)
    {
        const FVector Goal = Step.Skill == ESkill::Regroup ? Player->GetActorLocation() : Runner->GetObjectiveLocation();
        const double Distance = FVector::Dist2D(Companion->GetActorLocation(), Goal);
        if (Step.Skill == ESkill::Regroup && Distance <= 350)
        { CompleteStep(TEXT("companion reached the linked player's vicinity")); return; }
        if (Distance + 25 < BestDistance ||
            (Step.Skill == ESkill::CaptureRelay && Runner->GetOperation().charge > StepCharge + 0.05))
        {
            BestDistance = Distance; StepCharge = Runner->GetOperation().charge; ProgressGame = Now;
        }
        if (Step.Skill == ESkill::Regroup && Now >= NextRegroupUpdateGame)
        {
            NextRegroupUpdateGame = Now + 1;
            const FVector Forward = Player->GetActorForwardVector().GetSafeNormal2D();
            const FVector Right(-Forward.Y, Forward.X, 0);
            const FVector Desired = Player->GetActorLocation() - Forward * 200 + Right * 180;
            if (FVector::DistSquared2D(Desired, RegroupPoint) > 180 * 180)
            {
                FVector Projected;
                if (!ResolveNavigableGoal(Desired, Projected))
                { RejectAndFallback(TEXT("moving ally's regroup goal became unreachable")); return; }
                RegroupPoint = Projected;
                BoundController->SetCompanionCommand(EAegisCompanionCommand::Rally, nullptr, RegroupPoint);
                RecordEvent(TEXT("grounded_goal_updated"), TEXT("regroup follows the explicit allied link"));
            }
        }
        if (Now - ProgressGame > 3 && Distance > (Step.Skill == ESkill::CaptureRelay ? 260 : 350))
        { RejectAndFallback(TEXT("navigation made no progress toward the grounded skill goal")); return; }
    }
    if (Age > StepTimeoutSeconds) RejectAndFallback(TEXT("skill deadline reached without its observed outcome"));
}
void AAegisSquadPlanner::ObserveDamage(float Amount, AActor* Source, AActor* Victim)
{
    if (!bPlanActive || !bStepStarted || !Steps.IsValidIndex(StepIndex) || Amount <= 0 ||
        Steps[StepIndex].Skill != ESkill::FocusVisible || Source != BoundCompanion.Get() ||
        Victim != Steps[StepIndex].Target.Get()) return;
    // OnDamaged runs after health changes, including a lethal hit, so check the
    // still-active Sight stimulus rather than the target's now-zero health.
    const auto* Controller = BoundController.Get();
    if (!Controller || !Controller->Senses->HasActiveStimulus(*Victim, UAISense::GetSenseID<UAISense_Sight>())) return;
    StepDamage += Amount;
    auto Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("applied_damage"), Amount);
    Data->SetStringField(TEXT("target"), Steps[StepIndex].Token);
    RecordEvent(TEXT("observed_target_damage"), TEXT("real companion damage delegate"), Data);
}
void AAegisSquadPlanner::CompleteStep(const FString& Outcome)
{
    RemoveDamageListener();
    ++CompletedSteps;
    auto Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("step_index"), StepIndex);
    Data->SetStringField(TEXT("skill"), SkillName(Steps[StepIndex].Skill));
    Data->SetNumberField(TEXT("observed_companion_damage"), StepDamage);
    RecordEvent(TEXT("step_completed"), Outcome, Data);
    LastOutcome = Outcome;
    bStepStarted = false;
    ++StepIndex;
    if (StepIndex >= Steps.Num())
    {
        bPlanActive = false;
        ReturnToGuard();
        StatusLabel = TEXT("Plan completed - classic Guard continues");
        RecordEvent(TEXT("plan_completed"), TEXT("all bounded skill outcomes observed"));
    }
}
void AAegisSquadPlanner::ReturnToGuard()
{
    AAegisCharacter* Player; AAegisAICharacter* Companion; AAegisAIController* Controller;
    if (ResolveActors(Player, Companion, Controller)) Controller->SetCompanionCommand(EAegisCompanionCommand::Guard);
}
void AAegisSquadPlanner::RemoveDamageListener()
{
    if (auto* Target = DamageListenerTarget.Get())
        Target->Health->OnDamaged.RemoveDynamic(this, &AAegisSquadPlanner::ObserveDamage);
    DamageListenerTarget.Reset();
}
void AAegisSquadPlanner::CancelPlan(const FString& Reason, bool bReturnToGuard)
{
    const bool HadWork = bRequestPending || bPlanActive;
    ++Generation; // Invalidate even a completion already queued on the game thread.
    if (ActiveRequest)
    {
        ActiveRequest->OnProcessRequestComplete().Unbind();
        ActiveRequest->CancelRequest();
        ActiveRequest.Reset();
    }
    bRequestPending = bPlanActive = bStepStarted = false;
    RequestSecret.Reset();
    RemoveDamageListener();
    Steps.Reset(); RequestTargets.Reset();
    if (HadWork)
    {
        if (bReturnToGuard) ReturnToGuard();
        StatusLabel = TEXT("Plan cancelled - classic squad control");
        ProviderLabel = TEXT("Classic BT");
        LastOutcome = Reason;
        RecordEvent(TEXT("plan_cancelled"), Reason);
    }
}
void AAegisSquadPlanner::RejectAndFallback(const FString& Reason)
{
    if (!bPlanActive) ++RejectedPlans;
    ++Fallbacks;
    CancelPlan(Reason, false);
    ReturnToGuard();
    ProviderLabel = TEXT("Classic BT fallback");
    StatusLabel = TEXT("Classic Guard - model plan unavailable or invalid");
    LastOutcome = Reason;
    RecordEvent(TEXT("fallback"), Reason);
}
void AAegisSquadPlanner::RecordEvent(const TCHAR* Kind, const FString& Detail, TSharedPtr<FJsonObject> Data)
{
    UE_LOG(LogTemp, Display, TEXT("AEGIS_PLAN_EVENT generation=%llu kind=%s detail=%s"), Generation, Kind, *Detail.Left(180));
    if (TracePath.IsEmpty() || bTraceCapped) return;
    auto Event = MakeShared<FJsonObject>();
    Event->SetStringField(TEXT("event"), Kind);
    Event->SetStringField(TEXT("detail"), Detail);
    Event->SetNumberField(TEXT("generation"), static_cast<double>(Generation));
    Event->SetNumberField(TEXT("wall_seconds"), FPlatformTime::Seconds());
    Event->SetNumberField(TEXT("game_seconds"), GetWorld() ? GetWorld()->GetTimeSeconds() : 0);
    Event->SetNumberField(TEXT("wave"), BoundWave);
    Event->SetNumberField(TEXT("relay"), BoundRelay);
    if (Data) Event->SetObjectField(TEXT("data"), Data);
    const FString Line = JsonText(Event) + TEXT("\n");
    const int32 Bytes = FTCHARToUTF8(*Line).Length();
    if (++TraceEvents > 512 || TraceBytes + Bytes > 2 * 1024 * 1024)
    {
        bTraceCapped = true;
        UE_LOG(LogTemp, Warning, TEXT("AEGIS_PLAN_TRACE_LIMIT event or byte cap reached"));
        return;
    }
    if (FFileHelper::SaveStringToFile(Line, *TracePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
                                    &IFileManager::Get(), FILEWRITE_Append)) TraceBytes += Bytes;
    else
    {
        bTraceCapped = true;
        UE_LOG(LogTemp, Warning, TEXT("AEGIS_PLAN_TRACE_WRITE_FAILED %s"), *TracePath);
    }
}
void AAegisSquadPlanner::EndPlay(const EEndPlayReason::Type Reason)
{
    CancelPlan(TEXT("planner world ended"), false);
    Runner = nullptr;
    Super::EndPlay(Reason);
}
