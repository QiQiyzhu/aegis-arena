#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Components/ActorComponent.h"
#include "aegis/rules.hpp"
#include "AegisCharacter.generated.h"

UENUM(BlueprintType)
enum class EAegisTeam : uint8
{
    Neutral,
    Player,
    Enemy
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FAegisDamage, float, Applied, AActor*, InstigatorActor,
                                               AActor*, Victim);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAegisDeath);

UCLASS(ClassGroup = (Aegis), meta = (BlueprintSpawnableComponent))
class AEGISARENA_API UAegisHealthComponent : public UActorComponent
{
    GENERATED_BODY()
  public:
    UAegisHealthComponent();
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health", meta = (ClampMin = "1"))
    float Maximum = 100;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Health") float Current = 100;
    UPROPERTY(BlueprintAssignable) FAegisDamage OnDamaged;
    UPROPERTY(BlueprintAssignable) FAegisDeath OnDeath;
    UFUNCTION(BlueprintPure) bool IsAlive() const
    {
        return Current > 0;
    }
    UFUNCTION(BlueprintCallable) float ApplyDamage(float Amount, AActor* Source);
    UFUNCTION(BlueprintCallable) float Heal(float Amount);

  protected:
    virtual void BeginPlay() override;
};

UCLASS(ClassGroup = (Aegis), meta = (BlueprintSpawnableComponent))
class AEGISARENA_API UAegisCombatComponent : public UActorComponent
{
    GENERATED_BODY()
  public:
    UAegisCombatComponent();
    UPROPERTY(EditDefaultsOnly, Category = "Combat") float RangedDamage = 14;
    UPROPERTY(EditDefaultsOnly, Category = "Combat") float RangedRange = 1100;
    UPROPERTY(EditDefaultsOnly, Category = "Combat") float RangedCooldown = 0.7f;
    UPROPERTY(EditDefaultsOnly, Category = "Combat") float MeleeDamage = 22;
    UPROPERTY(EditDefaultsOnly, Category = "Combat") float MeleeRange = 180;
    UFUNCTION(BlueprintCallable) bool FireAt(const FVector& AimPoint);
    UFUNCTION(BlueprintCallable) bool Melee();
    UFUNCTION(BlueprintPure) float CooldownRemaining() const;

  private:
    double ReadyAt = 0;
};

UCLASS()
class AEGISARENA_API AAegisCharacter : public ACharacter
{
    GENERATED_BODY()
  public:
    AAegisCharacter();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UAegisHealthComponent> Health;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UAegisCombatComponent> Combat;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Faction") EAegisTeam Team = EAegisTeam::Enemy;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> Body;
    UFUNCTION(BlueprintPure) bool IsHostile(const AActor* Other) const;
    virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
                             AController* EventInstigator, AActor* DamageCauser) override;

  protected:
    virtual void BeginPlay() override;
    UFUNCTION() void Die();
};

UCLASS()
class AEGISARENA_API AAegisPlayerCharacter : public AAegisCharacter
{
    GENERATED_BODY()
  public:
    AAegisPlayerCharacter();
    UPROPERTY(VisibleAnywhere) TObjectPtr<class USpringArmComponent> SpringArm;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UCameraComponent> Camera;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

  private:
    void Forward(float Value);
    void Right(float Value);
    void Turn(float Value);
    void Fire();
    void Strike();
};

UCLASS()
class AEGISARENA_API AAegisAICharacter : public AAegisCharacter
{
    GENERATED_BODY()
  public:
    AAegisAICharacter();
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI") bool bCompanion = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI") bool bElite = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI") bool bUtilityCompanionPolicy = true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI") int32 DecisionSeed = 1001;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI") TObjectPtr<class UBehaviorTree> Behavior;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI") TObjectPtr<class UEnvQuery> CoverQuery;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI") TObjectPtr<class UEnvQuery> AttackQuery;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI") TObjectPtr<class UEnvQuery> RetreatQuery;

  protected:
    virtual void BeginPlay() override;
};
