#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AegisPortfolioMusic.generated.h"

class UAudioComponent;
class USoundBase;
class APawn;
class AAegisScenarioRunner;
class AAegisPlayerController;

// Presentation only. State selection never queries AI, enemy actors or perception.
UCLASS()
class AEGISARENA_API AAegisPortfolioMusic : public AActor
{
    GENERATED_BODY()
  public:
    AAegisPortfolioMusic();
    static AAegisPortfolioMusic* Find(UWorld* World);
    static void Ensure(AActor* ArenaOwner);
    virtual void Tick(float DeltaSeconds) override;
    const FString& GetState() const { return State; }
    bool IsMuted() const { return bMuted; }
    int32 GetActiveComponentCount() const;
    int32 GetPlayingComponentCount() const { return GetActiveComponentCount(); }
    int32 GetLoadedTrackCount() const { return Tracks.Num(); }
    float GetChannelGain(int32 Channel) const;
    void ToggleMute();

  protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

  private:
    UPROPERTY() TObjectPtr<AAegisScenarioRunner> Runner;
    UPROPERTY() TObjectPtr<AAegisPlayerController> Controller;
    UPROPERTY() TArray<TObjectPtr<UAudioComponent>> Channels;
    UPROPERTY() TMap<FString, TObjectPtr<USoundBase>> Tracks;
    TWeakObjectPtr<APawn> LastPawn;
    FString State;
    float Gain[2] = {0, 0}, From[2] = {0, 0}, Target[2] = {0, 0};
    float FadeAge[2] = {0, 0}, FadeLength[2] = {0, 0};
    bool bMuted = false, bStopped = false, bHadPawn = false;
    int32 NextChannel = 0;
    void BindController();
    void SelectState(FString& OutState, FString& OutAsset, float& OutGain, bool& OutLoop) const;
    void Transition(const FString& NewState, const FString& Asset, float Volume, bool bLoop,
                    const TCHAR* Reason, float Fade = .65f);
    void Record(const TCHAR* NewState, const TCHAR* Asset, float Volume, float Fade,
                bool bLoop, const TCHAR* Reason, int32 Channel);
};
