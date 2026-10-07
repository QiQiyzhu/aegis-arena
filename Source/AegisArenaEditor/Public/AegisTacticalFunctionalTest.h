#pragma once
#include "CoreMinimal.h"
#include "FunctionalTest.h"
#include "AegisTacticalFunctionalTest.generated.h"

enum class EAegisEnemyRole : uint8;

UCLASS()
class AEGISARENAEDITOR_API AAegisTacticalFunctionalTest : public AFunctionalTest
{
    GENERATED_BODY()
  public:
    virtual void StartTest() override;
    virtual bool IsEditorOnlyLoadedInPIE() const override { return true; }
  protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
  private:
    UPROPERTY() TObjectPtr<class AAegisCharacter> Leader;
    UPROPERTY() TObjectPtr<class AAegisAICharacter> Bot;
    UPROPERTY() TObjectPtr<class AAegisAICharacter> Nominee;
    UPROPERTY() TObjectPtr<class AAegisAIController> AI;
    UPROPERTY() TObjectPtr<class AStaticMeshActor> Wall;
    FTimerHandle Timer;
    FVector StartedPosition = FVector::ZeroVector, ShotPosition = FVector::ZeroVector;
    FVector Signal = FVector(-200, 600, 100);
    double Deadline = 0, CheckAt = 0;
    int32 Stage = 0, EnemyRoleIndex = 0, Assertions = 0, ShotsBefore = 0, MovesBefore = 0;
    bool bAllPassed = true;
    class AAegisAICharacter* SpawnBot(const FVector& Location, bool Companion, EAegisEnemyRole EnemyRole);
    bool Check(bool Condition, const FString& Name);
    void Step();
    void StartEnemyRole();
    void DestroyBot(class AAegisAICharacter* Actor);
    void Cleanup();
    void Complete(bool Pass, const FString& Reason);
};
