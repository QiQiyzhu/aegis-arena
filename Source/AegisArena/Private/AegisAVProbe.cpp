#include "AegisAVProbe.h"
#if !UE_BUILD_SHIPPING
#include "AegisLab.h"
#include "AegisPortfolioMusic.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Components/AudioComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#endif

AAegisAVProbe::AAegisAVProbe()
{
#if !UE_BUILD_SHIPPING
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
#endif
}
void AAegisAVProbe::Initialize(AAegisScenarioRunner* InRunner)
{
#if !UE_BUILD_SHIPPING
    Runner = InRunner;
    PC = Cast<AAegisPlayerController>(GetWorld()->GetFirstPlayerController());
    StartedAt = StageAt = FPlatformTime::Seconds();
    bRunning = true;
    if (!Runner || !PC || !FParse::Param(FCommandLine::Get(), TEXT("AegisV2")) ||
        !FParse::Param(FCommandLine::Get(), TEXT("AegisInputProbe")) ||
        !FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")) ||
        FParse::Param(FCommandLine::Get(), TEXT("nosound")) ||
        !FParse::Value(FCommandLine::Get(), TEXT("AegisAVProbeOutput="), Output))
    { Finish(false, TEXT("Explicit rendered, sound-enabled probe flags required")); return; }
    Output = FPaths::ConvertRelativePathToFull(Output);
    if (IFileManager::Get().FileExists(*FPaths::Combine(Output, TEXT("av-probe.json"))))
    { Finish(false, TEXT("Refusing existing evidence")); return; }
    bMayWrite = IFileManager::Get().MakeDirectory(*FPaths::Combine(Output, TEXT("audio")), true);
    if (!bMayWrite) { Finish(false, TEXT("Output not writable")); return; }
    UE_LOG(LogTemp, Display, TEXT("AEGIS_AV_PROBE_BEGIN keyboard=1 fixtureDamage=0 fixturePlacement=0 soundEnabled=1"));
#else
    (void)InRunner;
#endif
}
void AAegisAVProbe::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
    if (!bRunning) return;
    if (!IsValid(Runner) || !IsValid(PC) || FPlatformTime::Seconds() - StartedAt > 35)
    { Finish(false, TEXT("Lifetime or deadline failed")); return; }
    auto Keys = Releases; Releases.Reset();
    for (FKey Key : Keys) PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0));
    auto* Music = AAegisPortfolioMusic::Find(GetWorld());
    const double Age = FPlatformTime::Seconds() - StageAt;
    if (Age < .25) return;
    if (!Music) { Finish(false, TEXT("Music actor missing")); return; }
    int32 Actors = 0;
    for (TActorIterator<AAegisPortfolioMusic> It(GetWorld()); It; ++It) ++Actors;
    TArray<UAudioComponent*> Components; Music->GetComponents(Components);
    if (Actors != 1 || Components.Num() != 2 || Music->GetPlayingComponentCount() > 2)
    { Finish(false, TEXT("Music actor/component budget violated")); return; }
    const bool OneVoice = Music->GetPlayingComponentCount() == 1;
    switch (Stage)
    {
    case 0:
        if (Age < 1.2) return;
        if (!Check(TEXT("seven_tracks_loaded"), Music->GetLoadedTrackCount() == 7)) return;
        if (!Check(TEXT("one_music_actor_two_channels_budget"), Actors == 1 && Components.Num() == 2)) return;
        if (!Check(TEXT("briefing_voice_playing"), Music->GetState() == TEXT("briefing") && OneVoice)) return;
        StartAudio(TEXT("bgm-on")); Advance(1); break;
    case 1:
        if (Age < 1.5) return;
        StopAudio(); Tap(EKeys::M); Advance(2); break;
    case 2:
        if (Age < .4) return;
        if (!Check(TEXT("m_mutes_and_stops_voices"), Music->IsMuted() && Music->GetPlayingComponentCount() == 0 &&
            Music->GetChannelGain(0) == 0 && Music->GetChannelGain(1) == 0)) return;
        StartAudio(TEXT("bgm-muted")); Advance(3); break;
    case 3:
        if (Age < 1.2) return;
        StopAudio(); Tap(EKeys::M); Advance(4); break;
    case 4:
        if (Age < .5) return;
        if (!Check(TEXT("m_restores_music_playback"), !Music->IsMuted() && OneVoice)) return;
        StartAudio(TEXT("bgm-restored")); Advance(5); break;
    case 5:
        if (Age < 1.5) return;
        StopAudio(); Tap(EKeys::Enter); Advance(6); break;
    case 6:
        if (Age < 1) return;
        if (!Check(TEXT("enter_switches_to_relay_score"), Runner->GetTrial().phase == aegis::TrialPhase::Active &&
            Music->GetState() == TEXT("relay1") && OneVoice)) return;
        Tap(EKeys::P); Advance(7); break;
    case 7:
        if (Age < 1) return;
        if (!Check(TEXT("paused_menu_has_quiet_score"), PC->IsPaused() && PC->bMenuOpen &&
            Music->GetState() == TEXT("paused") && OneVoice &&
            Music->GetChannelGain(0) + Music->GetChannelGain(1) <= .161f)) return;
        PausedGame = GetWorld()->GetTimeSeconds(); Tap(EKeys::M); Advance(8); break;
    case 8:
        if (Age < .4) return;
        if (!Check(TEXT("paused_mute_freezes_game_clock"), Music->IsMuted() && Music->GetPlayingComponentCount() == 0 &&
            PC->IsPaused() && FMath::Abs(GetWorld()->GetTimeSeconds() - PausedGame) < .001)) return;
        Tap(EKeys::M); Advance(9); break;
    case 9:
        if (Age < .8) return;
        if (!Check(TEXT("paused_unmute_restores_one_voice"), !Music->IsMuted() && OneVoice && PC->IsPaused() &&
            Music->GetState() == TEXT("paused"))) return;
        Tap(EKeys::P); Advance(10); break;
    case 10:
        if (Age < 1) return;
        if (!Check(TEXT("resume_restores_relay_score"), !PC->IsPaused() && Music->GetState() == TEXT("relay1") && OneVoice)) return;
        PreviousPawn = PC->GetPawn(); Tap(EKeys::R); Advance(11); break;
    case 11:
        if (Age < 1) return;
        if (!Check(TEXT("restart_creates_new_player"), PC->GetPawn() && PC->GetPawn() != PreviousPawn.Get())) return;
        if (!Check(TEXT("restart_restores_one_briefing_voice"), Music->GetState() == TEXT("briefing") && OneVoice &&
            Runner->GetTrial().phase == aegis::TrialPhase::Briefing)) return;
        PreviousPawn = PC->GetPawn(); Tap(EKeys::R); Advance(12); break;
    case 12:
        if (Age < 1) return;
        if (!Check(TEXT("second_restart_has_no_music_actor_leak"), PC->GetPawn() && PC->GetPawn() != PreviousPawn.Get() &&
            Actors == 1 && Components.Num() == 2 && OneVoice && Music->GetState() == TEXT("briefing"))) return;
        Tap(EKeys::P); Advance(13); break;
    case 13:
        if (Age < .4) return;
        {
            bool Files = true;
            for (const TCHAR* Name : {TEXT("bgm-on.wav"), TEXT("bgm-muted.wav"), TEXT("bgm-restored.wav")})
                Files &= IFileManager::Get().FileSize(*FPaths::Combine(Output, TEXT("audio"), Name)) > 88;
            if (!Check(TEXT("three_master_mix_wavs_exported"), Files)) return;
            if (!Check(TEXT("normal_menu_quit_available"), PC->IsPaused() && PC->bMenuOpen)) return;
            Finish(true, TEXT("Completed sound-enabled native music lifecycle with master mix exports"));
        }
        break;
    }
#endif
}
#if !UE_BUILD_SHIPPING
void AAegisAVProbe::Tap(FKey Key)
{
    PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1));
    Releases.AddUnique(Key);
}
void AAegisAVProbe::Advance(int32 Next) { Stage = Next; StageAt = FPlatformTime::Seconds(); }
void AAegisAVProbe::StartAudio(const TCHAR* Name)
{
    RecordingName = Name;
    UAudioMixerBlueprintLibrary::StartRecordingOutput(this, 2.f);
}
void AAegisAVProbe::StopAudio()
{
    if (RecordingName.IsEmpty()) return;
    UAudioMixerBlueprintLibrary::StopRecordingOutput(this, EAudioRecordingExportType::WavFile,
        RecordingName, FPaths::Combine(Output, TEXT("audio")));
    RecordingName.Empty();
}
bool AAegisAVProbe::Check(const TCHAR* Name, bool Passed)
{
    auto Row = MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("name"), Name); Row->SetBoolField(TEXT("passed"), Passed);
    Row->SetNumberField(TEXT("wallSeconds"), FPlatformTime::Seconds() - StartedAt);
    Row->SetNumberField(TEXT("gameSeconds"), GetWorld()->GetTimeSeconds());
    Row->SetBoolField(TEXT("paused"), PC->IsPaused());
    if (auto* Music = AAegisPortfolioMusic::Find(GetWorld()))
    {
        Row->SetStringField(TEXT("musicState"), Music->GetState()); Row->SetBoolField(TEXT("muted"), Music->IsMuted());
        Row->SetNumberField(TEXT("playingVoices"), Music->GetPlayingComponentCount());
        Row->SetNumberField(TEXT("loadedTracks"), Music->GetLoadedTrackCount());
        Row->SetArrayField(TEXT("gains"), {MakeShared<FJsonValueNumber>(Music->GetChannelGain(0)),
                                         MakeShared<FJsonValueNumber>(Music->GetChannelGain(1))});
    }
    Assertions.Add(MakeShared<FJsonValueObject>(Row));
    UE_LOG(LogTemp, Display, TEXT("AEGIS_AV_PROBE_ASSERT_%s %s"), Passed ? TEXT("PASS") : TEXT("FAIL"), Name);
    if (!Passed) Finish(false, Name);
    return Passed;
}
void AAegisAVProbe::Finish(bool Passed, const TCHAR* Reason)
{
    if (!bRunning) return;
    StopAudio(); bRunning = false; SetActorTickEnabled(false);
    Passed &= Assertions.Num() == 15;
    auto Report = MakeShared<FJsonObject>();
    Report->SetNumberField(TEXT("schemaVersion"), 1); Report->SetBoolField(TEXT("passed"), Passed);
    Report->SetStringField(TEXT("presentationVersion"), TEXT("2.1"));
    Report->SetStringField(TEXT("reason"), Reason);
    Report->SetStringField(TEXT("engine"), TEXT("unreal-runtime"));
    Report->SetBoolField(TEXT("soundEnabled"), true); Report->SetBoolField(TEXT("syntheticKeyboardInput"), true);
    Report->SetBoolField(TEXT("fixtureDamage"), false); Report->SetBoolField(TEXT("fixturePlacement"), false);
    Report->SetBoolField(TEXT("humanPlaytest"), false); Report->SetBoolField(TEXT("hardwareLoopback"), false);
    Report->SetStringField(TEXT("audioEvidence"), TEXT("Unreal master submix rendering, not physical speaker or microphone measurement"));
    Report->SetStringField(TEXT("quitPath"), Passed ? TEXT("normal menu X") : TEXT("probe failure"));
    Report->SetNumberField(TEXT("wallSeconds"), FPlatformTime::Seconds() - StartedAt);
    Report->SetArrayField(TEXT("assertions"), Assertions);
    FString Json; FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    if (!bMayWrite || !FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Output, TEXT("av-probe.json")))) Passed = false;
    UE_LOG(LogTemp, Display, TEXT("AEGIS_AV_PROBE_%s checks=%d"), Passed ? TEXT("PASS") : TEXT("FAIL"), Assertions.Num());
    if (Passed) PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::X, IE_Pressed, 1));
    else FPlatformMisc::RequestExitWithStatus(false, 10, TEXT("Aegis AV probe failure"));
}
#endif
