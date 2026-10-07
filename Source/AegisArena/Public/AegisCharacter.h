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
UENUM(BlueprintType)
enum class EAegisEnemyRole : uint8
{
    Striker,
    Flanker,
    Suppressor
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FAegisDamage, float, Applied, AActor*, InstigatorActor,
                                               AActor*, Victim);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAegisDeath);

// Cosmetic accounting only. Observed events are also counted with NullRHI; rendered counters are not.
// FatalImpacts belong to the shooter; DeathsObserved/RenderedDeathBursts belong to the victim.
struct FAegisShotVFXStats
{
    int32 Shots = 0, ChargedShots = 0, RenderedShots = 0;
    int32 TraceImpacts = 0, DamageImpacts = 0, WorldImpacts = 0, BlockedCharacterImpacts = 0;
    int32 FatalImpacts = 0, DeathsObserved = 0, RenderedDeathBursts = 0;
    int32 PoolComponents = 0, PeakActiveComponents = 0, ReusedActiveSlots = 0;
};

enum class EAegisShotImpact : uint8 { World, CharacterBlocked, CharacterDamaged };

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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Trial") bool bPiercingRounds = false;
    UFUNCTION(BlueprintCallable) bool FireAt(const FVector& AimPoint);
    bool FireChargedAt(const FVector& AimPoint);
    UFUNCTION(BlueprintCallable) bool Melee();
    UFUNCTION(BlueprintPure) float CooldownRemaining() const;
    UFUNCTION(BlueprintPure) float SecondsSinceLastHit() const;
    // Accepted local attacks, used by the development input fixture; not a damage/evaluation metric.
    int32 RangedShotsFired = 0;

  private:
    double ReadyAt = 0;
    double LastHitAt = -1000;
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
    // Presentation consumes the existing trace result; it never performs a gameplay query.
    void ShowAttack(const FVector& Start, const FVector& End, bool bMelee = false);
    // Called only after an accepted ranged attack / an actual existing combat trace hit.
    void ShowRangedShot(const FVector& Start, const FVector& End, bool bCharged = false);
    void ShowShotImpact(const FVector& Point, const FVector& Normal, EAegisShotImpact Kind,
                        bool bCharged, bool bFatal);
    const FAegisShotVFXStats& GetShotVFXStats() const { return ShotVFXStats; }
    // The controller supplies a visible-target snapshot. These visuals never query a target actor.
    void ShowShotWindup(const FVector& KnownAimPoint, float Duration);
    void ClearShotWindup();
    UFUNCTION(BlueprintPure) float GetShotWindupRemaining() const;
    UFUNCTION(BlueprintPure) bool IsPulseStaggered() const;
    void ApplyPulseStagger(const FVector& OutwardDirection, float Duration);
    virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
                             AController* EventInstigator, AActor* DamageCauser) override;

  protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UFUNCTION() void Die();
    void ShowPulse(const FVector& Center, float Radius);

  private:
    UPROPERTY() TObjectPtr<class UStaticMesh> CubeMesh;
    UPROPERTY() TObjectPtr<class UStaticMesh> SphereMesh;
    UPROPERTY() TObjectPtr<class UStaticMesh> CylinderMesh;
    UPROPERTY() TObjectPtr<class UStaticMesh> ConeMesh;
    UPROPERTY() TObjectPtr<class UMaterialInterface> ActorMaterial;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> BodyMaterial;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> AccentMaterial;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> TracerMaterial;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> WarningMaterial;
    UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> PulseMaterial;
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> WarningLine;
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> WarningMarker;
    UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> PulseSegments;
    UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> Decorations;
    UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> Tracers;
    TArray<FTimerHandle> TracerTimers;
    FTimerHandle HitFlashTimer, WindupVisualTimer, PulseVisualTimer;
    double WindupVisualEndsAt = 0, WindupVisualStartedAt = 0, PulseStaggerUntil = 0, PulseVisualStartedAt = 0;
    FVector PulseVisualCenter = FVector::ZeroVector;
    float PulseVisualRadius = 0;
    FLinearColor IdentityColor = FLinearColor::White;
    int32 NextTracer = 0;
    bool bPresentationEnabled = false;
    // A fixed shared pool (24) plus the existing tracer pool (at most 3); no per-shot components.
    struct FShotFXParticle
    {
        double StartedAt = 0;
        float Lifetime = 0, Gravity = 0, Spin = 0;
        FVector Position = FVector::ZeroVector, Velocity = FVector::ZeroVector, Scale = FVector::OneVector;
        FRotator Rotation = FRotator::ZeroRotator;
        FLinearColor Color = FLinearColor::White;
    };
    FAegisShotVFXStats ShotVFXStats;
    bool bLayeredShotVFX = false;
    UPROPERTY() TObjectPtr<class UStaticMesh> ShotShardMesh;
    UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> ShotFXPool;
    UPROPERTY() TArray<TObjectPtr<class UMaterialInstanceDynamic>> ShotFXMaterials;
    UPROPERTY() TArray<TObjectPtr<class UMaterialInstanceDynamic>> ShotCoreMaterials;
    TArray<FShotFXParticle> ShotFXParticles;
    FTimerHandle ShotFXTimer;
    void BuildShotVFXPool();
    void UpdateShotVFX();
    void ShowDeathVFX();
    void EmitShotParticle(UStaticMesh* Shape, const FVector& Position, const FVector& Velocity,
                          const FVector& Scale, const FRotator& Rotation, const FLinearColor& Color,
                          float Lifetime, float Gravity = 0, float Spin = 0);
    void BuildPresentation();
    void BuildPortfolioPresentation();
    void UpdatePortfolioPose();
    FTimerHandle PortfolioPoseTimer;
    UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> PortfolioLegs;
    void UpdateWindupVisual();
    void UpdatePulseVisual();
    UStaticMeshComponent* AddDecoration(UStaticMesh* ShapeMesh, const FVector& Location, const FVector& Scale,
                                        const FRotator& Rotation, UMaterialInterface* Material);
    UFUNCTION() void FlashHit(float Applied, AActor* Source, AActor* Victim);
};

UCLASS()
class AEGISARENA_API AAegisPlayerCharacter : public AAegisCharacter
{
    GENERATED_BODY()
  public:
    AAegisPlayerCharacter();
    UPROPERTY(VisibleAnywhere) TObjectPtr<class USpringArmComponent> SpringArm;
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UCameraComponent> Camera;
    UPROPERTY(BlueprintReadWrite, Category = "Player") bool bCombatEnabled = true;
    UPROPERTY(EditDefaultsOnly, Category = "Player", meta = (ClampMin = "0.1")) float DashCooldown = 2.4f;
    UPROPERTY(BlueprintReadWrite, Category = "Player|Pulse") bool bPulseEnabled = false;
    UPROPERTY(BlueprintReadWrite, Category = "Player|Pulse") bool bRestorativePulse = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Pulse", meta = (ClampMin = "0.1")) float PulseCooldownSeconds = 6.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Pulse", meta = (ClampMin = "0")) float PulseDamage = 32.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Player|Pulse", meta = (ClampMin = "50", ClampMax = "1000")) float PulseRadius = 420.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Player|Pulse") int32 PulseActivations = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Player|Pulse") int32 LastPulseTargetsHit = 0;
    UFUNCTION(BlueprintPure) float GetDashCooldownRemaining() const;
    UFUNCTION(BlueprintPure) float GetDashCooldownDuration() const { return DashCooldown; }
    UFUNCTION(BlueprintPure) FVector GetAimPoint() const { return AimPoint; }
    UFUNCTION(BlueprintPure) float GetPulseCooldownRemaining() const;
    UFUNCTION(BlueprintPure) float GetPulseCooldownDuration() const { return PulseCooldownSeconds; }
    UFUNCTION(BlueprintCallable) bool TryPulse();
    bool IsCharging() const { return bCharging; }
    float GetChargeFraction() const;
    void CancelCharge();
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

  private:
    FVector AimPoint = FVector::ZeroVector;
    FVector DashDirection = FVector::ZeroVector;
    float ForwardInput = 0, RightInput = 0;
    float WalkSpeedBeforeDash = 420, AccelerationBeforeDash = 2048;
    double DashReadyAt = 0, DashEndsAt = 0, PulseReadyAt = 0;
    bool bFireHeld = false, bDashing = false;
    bool bCharging = false;
    bool bChargeReadySoundPlayed = false;
    double ChargeStartedAt = 0;
    void Forward(float Value);
    void Right(float Value);
    FVector GetScreenMovementDirection() const;
    bool CanAcceptCombatInput() const;
    void UpdateAim();
    void Fire();
    void StopFire();
    void Strike();
    void ReleaseCharge();
    void Dash();
    void EndDash();
    void PulseInput();
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
    // Only interactive trial spawns opt into the richer controller and role presentation.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|Trial") bool bTacticalTrial = false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|Trial") EAegisEnemyRole EnemyRole = EAegisEnemyRole::Striker;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI") int32 DecisionSeed = 1001;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI") TObjectPtr<class UBehaviorTree> Behavior;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI") TObjectPtr<class UEnvQuery> CoverQuery;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI") TObjectPtr<class UEnvQuery> AttackQuery;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI") TObjectPtr<class UEnvQuery> RetreatQuery;

  protected:
    virtual void BeginPlay() override;
};
