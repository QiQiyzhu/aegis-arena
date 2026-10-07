#pragma once
#include "CoreMinimal.h"
#include "FunctionalTest.h"
#include "AegisWeaponsFunctionalTest.generated.h"

UCLASS()
class AEGISARENAEDITOR_API AAegisWeaponsFunctionalTest : public AFunctionalTest
{
    GENERATED_BODY()
  public:
    virtual void StartTest() override;
    virtual bool IsEditorOnlyLoadedInPIE() const override { return true; }
  protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
  private:
    UPROPERTY() TObjectPtr<class APlayerController> PC;
    UPROPERTY() TObjectPtr<class AAegisPlayerCharacter> Player;
    UPROPERTY() TObjectPtr<class AAegisAICharacter> VisibleEnemy;
    UPROPERTY() TObjectPtr<class AAegisCharacter> ShieldedEnemy;
    UPROPERTY() TObjectPtr<class AAegisCharacter> FarEnemy;
    UPROPERTY() TObjectPtr<class AAegisAICharacter> Ally;
    UPROPERTY() TObjectPtr<class AAegisAICharacter> CoveredAlly;
    UPROPERTY() TObjectPtr<class AAegisAICharacter> DeadAlly;
    UPROPERTY() TObjectPtr<class AAegisCharacter> First;
    UPROPERTY() TObjectPtr<class AAegisCharacter> Second;
    UPROPERTY() TObjectPtr<class AAegisCharacter> Third;
    UPROPERTY() TObjectPtr<class AAegisCharacter> BlockingAlly;
    UPROPERTY() TObjectPtr<class AStaticMeshActor> Wall;
    UPROPERTY() TArray<TObjectPtr<AActor>> Subjects;
    FTimerHandle Timer;
    double Deadline = 0, CheckAt = 0;
    int32 Stage = 0, Assertions = 0;
    bool bPassed = true;
    bool Verify(bool Condition, const TCHAR* Name);
    void Step();
    void Next(int32 NewStage, float Delay = 0.12f);
    bool FreshPlayer(bool Pulse = false, bool Restore = false, bool Pierce = false);
    class AAegisCharacter* SpawnTarget(const FVector& Location, bool Friendly = false);
    class AAegisAICharacter* SpawnBot(const FVector& Location, bool Companion);
    class AStaticMeshActor* SpawnWall(const FVector& Location, const FVector& Scale);
    void ResetLineHealth();
    bool Shoot();
    void CleanupSubjects();
    void Complete();
};
