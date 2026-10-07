#include "AegisPortfolioMusic.h"
#include "AegisLab.h"
#include "AegisUIPreferences.h"
#include "AegisCharacter.h"
#include "AegisPortfolioCapture.h"
#include "Components/AudioComponent.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Sound/SoundBase.h"

AAegisPortfolioMusic::AAegisPortfolioMusic()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    SetActorEnableCollision(false);
    for (int32 I = 0; I < 2; ++I)
    {
        auto* Audio = CreateDefaultSubobject<UAudioComponent>(I == 0 ? TEXT("MusicA") : TEXT("MusicB"));
        if (I == 0) SetRootComponent(Audio);
        else Audio->SetupAttachment(RootComponent);
        Audio->bAutoActivate = false;
        Audio->bAutoDestroy = false;
        Audio->bIsUISound = true;
        Audio->bAllowSpatialization = false;
        Audio->bAlwaysPlay = true;
        Audio->SetVolumeMultiplier(0);
        Channels.Add(Audio);
    }
}

AAegisPortfolioMusic* AAegisPortfolioMusic::Find(UWorld* World)
{
    if (World)
        for (TActorIterator<AAegisPortfolioMusic> It(World); It; ++It)
            if (IsValid(*It)) return *It;
    return nullptr;
}

void AAegisPortfolioMusic::Ensure(AActor* ArenaOwner)
{
    if (!ArenaOwner || !ArenaOwner->GetWorld() ||
        !FParse::Param(FCommandLine::Get(), TEXT("AegisV2")) ||
        FParse::Param(FCommandLine::Get(), TEXT("NullRHI"))) return;
    auto* Music = Find(ArenaOwner->GetWorld());
    if (!Music) Music = ArenaOwner->GetWorld()->SpawnActor<AAegisPortfolioMusic>();
    if (Music)
    {
        Music->SetOwner(ArenaOwner);
        Music->Runner = Cast<AAegisScenarioRunner>(ArenaOwner);
        if (Music->Tracks.IsEmpty())
            for (const TCHAR* Asset : {TEXT("M_Briefing"), TEXT("M_Relay1"), TEXT("M_Relay2"),
                                      TEXT("M_Extraction"), TEXT("M_Upgrade"), TEXT("M_Victory"), TEXT("M_Defeat")})
            {
                const FString Path = FString::Printf(TEXT("/Game/Aegis/V21/Audio/%s.%s"), Asset, Asset);
                if (auto* Track = LoadObject<USoundBase>(nullptr, *Path)) Music->Tracks.Add(Asset, Track);
                else UE_LOG(LogTemp, Error, TEXT("AEGIS_MUSIC_MISSING asset=%s"), *Path);
            }
    }
}

void AAegisPortfolioMusic::BindController()
{
    auto* PC = GetWorld() ? Cast<AAegisPlayerController>(GetWorld()->GetFirstPlayerController()) : nullptr;
    if (!PC || PC == Controller) return;
    if (Controller) DisableInput(Controller);
    Controller = PC;
    bMuted = FAegisUIPreferences::Load(FAegisUIPreferences::DefaultPath(), FCommandLine::Get()).bMusicMuted;
    EnableInput(Controller);
    if (InputComponent)
    {
        auto& Binding = InputComponent->BindKey(EKeys::M, IE_Pressed, this, &AAegisPortfolioMusic::ToggleMute);
        Binding.bExecuteWhenPaused = true;
        Binding.bConsumeInput = false;
    }
}

void AAegisPortfolioMusic::SelectState(FString& OutState, FString& OutAsset, float& OutGain, bool& OutLoop) const
{
    OutGain = .42f;
    OutLoop = true;
    if (bMuted) { OutState = TEXT("muted"); OutAsset.Empty(); OutGain = 0; return; }
    if (!Runner || !Runner->IsInteractive()) { OutState = TEXT("briefing"); OutAsset = TEXT("M_Briefing"); return; }
    const auto& Trial = Runner->GetTrial();
    // All inputs below are already visible in the player's phase, upgrade and menu UI.
    if (Trial.phase == aegis::TrialPhase::Won)
    { OutState = TEXT("victory"); OutAsset = TEXT("M_Victory"); OutGain = .48f; OutLoop = false; }
    else if (Trial.phase == aegis::TrialPhase::Lost)
    { OutState = TEXT("defeat"); OutAsset = TEXT("M_Defeat"); OutGain = .38f; OutLoop = false; }
    else if (Controller && (Controller->bMenuOpen || Controller->bPlannerOpen))
    { OutState = TEXT("paused"); OutAsset = TEXT("M_Briefing"); OutGain = .16f; }
    else if (Runner->IsUpgradePending() || Trial.phase == aegis::TrialPhase::Intermission)
    { OutState = TEXT("upgrade"); OutAsset = TEXT("M_Upgrade"); OutGain = .32f; }
    else if (Trial.phase == aegis::TrialPhase::Briefing)
    { OutState = TEXT("briefing"); OutAsset = TEXT("M_Briefing"); OutGain = .38f; }
    else if (GetWorld()->IsPaused())
    { OutState = TEXT("paused"); OutAsset = TEXT("M_Briefing"); OutGain = .16f; }
    else if (Trial.wave >= 3)
    { OutState = TEXT("extraction"); OutAsset = TEXT("M_Extraction"); OutGain = .44f; }
    else if (Trial.wave == 2)
    { OutState = TEXT("relay2"); OutAsset = TEXT("M_Relay2"); }
    else
    { OutState = TEXT("relay1"); OutAsset = TEXT("M_Relay1"); OutGain = .40f; }
}

void AAegisPortfolioMusic::Record(const TCHAR* NewState, const TCHAR* Asset, float Volume, float Fade,
                                bool bLoop, const TCHAR* Reason, int32 Channel)
{
    if (GetWorld() && FParse::Param(FCommandLine::Get(), TEXT("AegisPortfolioCapture")))
        for (TActorIterator<AAegisPortfolioCapture> It(GetWorld()); It; ++It)
            It->RecordMusic(NewState, Asset, Volume, Fade, bLoop, Reason, Channel);
    UE_LOG(LogTemp, Display, TEXT("AEGIS_MUSIC state=%s asset=%s gain=%.3f fade=%.3f loop=%d channel=%d reason=%s"),
           NewState, Asset, Volume, Fade, bLoop, Channel, Reason);
}

void AAegisPortfolioMusic::Transition(const FString& NewState, const FString& Asset, float Volume,
                                    bool bLoop, const TCHAR* Reason, float Fade)
{
    State = NewState;
    for (int32 I = 0; I < 2; ++I)
    { From[I] = Gain[I]; Target[I] = 0; FadeAge[I] = 0; FadeLength[I] = Fade; }
    if (Asset.IsEmpty())
    { Record(*State, TEXT(""), 0, Fade, false, Reason, -1); return; }
    auto* Found = Tracks.Find(Asset);
    USoundBase* Sound = Found ? Found->Get() : nullptr;
    if (!Sound)
    {
        UE_LOG(LogTemp, Error, TEXT("AEGIS_MUSIC_MISSING asset=%s"), *Asset);
        Record(*State, TEXT(""), 0, Fade, false, TEXT("asset_missing"), -1);
        return;
    }
    const int32 Slot = NextChannel;
    NextChannel = 1 - NextChannel;
    // At most two voices, including rapid menu toggles. Reusing a fading slot cuts
    // that slot before Play; the channel identifier makes the offline mix exact.
    Channels[Slot]->Stop();
    Channels[Slot]->SetSound(Sound);
    Channels[Slot]->SetVolumeMultiplier(0);
    Gain[Slot] = From[Slot] = 0;
    Target[Slot] = Volume;
    Channels[Slot]->Play(0);
    Record(*State, *Asset, Volume, Fade, bLoop, Reason, Slot);
}

void AAegisPortfolioMusic::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bStopped || !GetWorld()) return;
    BindController();
    APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
    const bool bRestarted = Pawn && bHadPawn && Pawn != LastPawn.Get();
    if (Pawn) { LastPawn = Pawn; bHadPawn = true; }
    if (bRestarted)
    {
        for (int32 I = 0; I < 2; ++I) { Channels[I]->Stop(); Gain[I] = From[I] = Target[I] = 0; }
        Record(TEXT("restart"), TEXT(""), 0, 0, false, TEXT("new_player_pawn"), -1);
        State.Empty();
    }
    // Audio remains active through a real paused menu. FApp time is also the fixed
    // presentation clock used by the capture; no gameplay time or AI is advanced.
    const float Step = FMath::Max(static_cast<float>(FApp::GetDeltaTime()), 0.f);
    for (int32 I = 0; I < 2; ++I)
    {
        FadeAge[I] += Step;
        const float Alpha = FadeLength[I] > 0 ? FMath::Clamp(FadeAge[I] / FadeLength[I], 0.f, 1.f) : 1.f;
        Gain[I] = FMath::Lerp(From[I], Target[I], Alpha);
        Channels[I]->SetVolumeMultiplier(Gain[I]);
        if (Target[I] == 0 && Alpha >= 1 && Channels[I]->IsPlaying()) Channels[I]->Stop();
    }
    FString WantedState, Asset;
    float Volume;
    bool bLoop;
    SelectState(WantedState, Asset, Volume, bLoop);
    if (WantedState != State)
        Transition(WantedState, Asset, Volume, bLoop, bRestarted ? TEXT("restart") : TEXT("public_phase"));
}

void AAegisPortfolioMusic::ToggleMute()
{
    bMuted = !bMuted;
    auto Preferences = FAegisUIPreferences::Load(FAegisUIPreferences::DefaultPath(), FCommandLine::Get());
    Preferences.bMusicMuted = bMuted;
    if (!Preferences.Save(FAegisUIPreferences::DefaultPath()) && Controller)
    {
        Controller->CommandFeedback = Controller->bEnglishUI ? TEXT("Music changed; setting could not be saved") : TEXT("音乐已切换；偏好未能保存");
        Controller->CommandFeedbackUntil = GetWorld()->GetTimeSeconds() + 2;
    }
    FString WantedState, Asset;
    float Volume;
    bool bLoop;
    SelectState(WantedState, Asset, Volume, bLoop);
    Transition(WantedState, Asset, Volume, bLoop, bMuted ? TEXT("user_mute") : TEXT("user_unmute"), .15f);
}

int32 AAegisPortfolioMusic::GetActiveComponentCount() const
{
    int32 Count = 0;
    for (const auto& Audio : Channels) if (Audio && Audio->IsPlaying()) ++Count;
    return Count;
}
float AAegisPortfolioMusic::GetChannelGain(int32 Channel) const
{
    return Channel >= 0 && Channel < 2 ? Gain[Channel] : 0;
}
void AAegisPortfolioMusic::EndPlay(const EEndPlayReason::Type Reason)
{
    bStopped = true;
    for (auto& Audio : Channels) if (Audio) Audio->Stop();
    if (Controller) DisableInput(Controller);
    Record(TEXT("stopped"), TEXT(""), 0, 0, false, TEXT("end_play"), -1);
    Super::EndPlay(Reason);
}
