#include "AegisCharacter.h"
#include "AegisPortfolio.h"
#include "AegisPortfolioPresentation.h"
#include "AegisAIController.h"
#include "AegisLab.h"
#include "AegisTacticalFX.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InputComponent.h"
#include "Perception/AISense_Damage.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/OverlapResult.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "InputCoreTypes.h"

UAegisHealthComponent::UAegisHealthComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}
void UAegisHealthComponent::BeginPlay()
{
    Super::BeginPlay();
    Maximum = FMath::Max(1.f, Maximum);
    Current = Maximum;
}
float UAegisHealthComponent::ApplyDamage(float Amount, AActor* Source)
{
    auto* Owner = Cast<AAegisCharacter>(GetOwner());
    auto* Attacker = Cast<AAegisCharacter>(Source);
    if (!Owner || !Attacker)
        return 0;
    aegis::Health Model{Maximum, Current};
    const float Applied = static_cast<float>(Model.damage(Amount, static_cast<aegis::Team>(Attacker->Team),
                                                          static_cast<aegis::Team>(Owner->Team)));
    Current = static_cast<float>(Model.current);
    if (Applied > 0)
    {
        UAISense_Damage::ReportDamageEvent(this, Owner, Attacker, Applied, Attacker->GetActorLocation(),
                                           Owner->GetActorLocation(), NAME_None);
        OnDamaged.Broadcast(Applied, Attacker, Owner);
        if (!IsAlive())
            OnDeath.Broadcast();
    }
    return Applied;
}
float UAegisHealthComponent::Heal(float Amount)
{
    aegis::Health Model{Maximum, Current};
    const float Applied = static_cast<float>(Model.heal(Amount));
    Current = static_cast<float>(Model.current);
    return Applied;
}
UAegisCombatComponent::UAegisCombatComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}
float UAegisCombatComponent::CooldownRemaining() const
{
    return GetWorld() ? FMath::Max(0.f, static_cast<float>(ReadyAt - GetWorld()->GetTimeSeconds())) : 0;
}
float UAegisCombatComponent::SecondsSinceLastHit() const
{
    return GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds() - LastHitAt) : 1000.f;
}
bool UAegisCombatComponent::FireAt(const FVector& AimPoint)
{
    auto* Owner = Cast<AAegisCharacter>(GetOwner());
    if (!Owner || !Owner->Health->IsAlive() || Owner->IsPulseStaggered() || CooldownRemaining() > 0)
        return false;
    const FVector Start = Owner->GetActorLocation() + FVector(0, 0, 30),
                  Direction = (AimPoint - Start).GetSafeNormal();
    if (Direction.IsNearlyZero())
        return false;
    ReadyAt = GetWorld()->GetTimeSeconds() + FMath::Max(0.05f, RangedCooldown);
    ++RangedShotsFired;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(AegisRanged), false, Owner);
    const FVector RangeEnd = Start + Direction * RangedRange;
    FVector TraceStart = Start, VisualEnd = RangeEnd;
    const int32 MaximumHits = bPiercingRounds ? 2 : 1;
    for (int32 Index = 0; Index < MaximumHits; ++Index)
    {
        FHitResult Hit;
        if (!GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, RangeEnd, ECC_Visibility, Params))
        {
            VisualEnd = RangeEnd;
            break;
        }
        VisualEnd = Hit.ImpactPoint;
        auto* Target = Cast<AAegisCharacter>(Hit.GetActor());
        if (!Target)
        {
            Owner->ShowShotImpact(Hit.ImpactPoint, Hit.ImpactNormal, EAegisShotImpact::World, false, false);
            break; // World cover always stops both normal and piercing shots.
        }
        const float Applied = Target->Health->ApplyDamage(RangedDamage, Owner);
        if (Applied > 0)
            LastHitAt = GetWorld()->GetTimeSeconds();
        Owner->ShowShotImpact(Hit.ImpactPoint, Hit.ImpactNormal,
            Applied > 0 ? EAegisShotImpact::CharacterDamaged : EAegisShotImpact::CharacterBlocked,
            false, Applied > 0 && !Target->Health->IsAlive());
        if (!Owner->IsHostile(Target)) break; // Allies are never pierced or damaged.
        Params.AddIgnoredActor(Target); // A capsule can only receive one application per shot.
        TraceStart = Hit.ImpactPoint + Direction;
        if (FVector::DotProduct(RangeEnd - TraceStart, Direction) <= 0) break;
    }
    Owner->ShowRangedShot(Start, VisualEnd);
    return true;
}
bool UAegisCombatComponent::FireChargedAt(const FVector& AimPoint)
{
    auto* Owner = Cast<AAegisPlayerCharacter>(GetOwner());
    auto* Portfolio = AAegisPortfolio::Find(GetWorld());
    if (!Owner || !Portfolio || !Portfolio->IsV2() || !Owner->Health->IsAlive() ||
        Owner->IsPulseStaggered() || CooldownRemaining() > 0) return false;
    const FVector Start = Owner->GetActorLocation() + FVector(0, 0, 30);
    const FVector Direction = (AimPoint - Start).GetSafeNormal();
    if (Direction.IsNearlyZero() || !Portfolio->TrySpendChargedShot()) return false;
    ReadyAt = GetWorld()->GetTimeSeconds() + .5;
    ++RangedShotsFired;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(AegisCharged), false, Owner);
    const FVector RangeEnd = Start + Direction * 1450.f;
    FVector TraceStart = Start, VisualEnd = RangeEnd;
    for (int32 Index = 0; Index < (bPiercingRounds ? 3 : 2); ++Index)
    {
        FHitResult Hit;
        if (!GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, RangeEnd, ECC_Visibility, Params))
        { VisualEnd = RangeEnd; break; }
        VisualEnd = Hit.ImpactPoint;
        auto* Target = Cast<AAegisCharacter>(Hit.GetActor());
        if (!Target || !Owner->IsHostile(Target))
        {
            Owner->ShowShotImpact(Hit.ImpactPoint, Hit.ImpactNormal,
                Target ? EAegisShotImpact::CharacterBlocked : EAegisShotImpact::World, true, false);
            break;
        }
        const float Applied = Target->Health->ApplyDamage(52.f, Owner);
        if (Applied > 0) LastHitAt = GetWorld()->GetTimeSeconds();
        Owner->ShowShotImpact(Hit.ImpactPoint, Hit.ImpactNormal,
            Applied > 0 ? EAegisShotImpact::CharacterDamaged : EAegisShotImpact::CharacterBlocked,
            true, Applied > 0 && !Target->Health->IsAlive());
        Params.AddIgnoredActor(Target);
        TraceStart = Hit.ImpactPoint + Direction;
        if (FVector::DotProduct(RangeEnd - TraceStart, Direction) <= 0) break;
    }
    Owner->ShowRangedShot(Start, VisualEnd, true);
    AegisPortfolioPresentation::Sound(Owner, TEXT("S_Charge"), Start, .7f);
    return true;
}
bool UAegisCombatComponent::Melee()
{
    auto* Owner = Cast<AAegisCharacter>(GetOwner());
    if (!Owner || !Owner->Health->IsAlive() || Owner->IsPulseStaggered() || CooldownRemaining() > 0)
        return false;
    ReadyAt = GetWorld()->GetTimeSeconds() + 0.9;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(AegisMelee), false, Owner);
    const FVector Start = Owner->GetActorLocation();
    if (GetWorld()->SweepSingleByChannel(Hit, Start, Start + Owner->GetActorForwardVector() * MeleeRange,
                                         FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(45),
                                         Params))
        if (auto* Target = Cast<AAegisCharacter>(Hit.GetActor()))
            if (Target->Health->ApplyDamage(MeleeDamage, Owner) > 0)
                LastHitAt = GetWorld()->GetTimeSeconds();
    Owner->ShowAttack(Start, Start + Owner->GetActorForwardVector() * MeleeRange, true);
    return true;
}
AAegisCharacter::AAegisCharacter()
{
    PrimaryActorTick.bCanEverTick = false;
    Health = CreateDefaultSubobject<UAegisHealthComponent>(TEXT("Health"));
    Combat = CreateDefaultSubobject<UAegisCombatComponent>(TEXT("Combat"));
    Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PrimitiveBody"));
    Body->SetupAttachment(GetRootComponent());
    static ConstructorHelpers::FObjectFinder<UStaticMesh> PrimitiveMesh(
        TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
        TEXT("/Game/Aegis/Materials/M_AegisActor.M_AegisActor"));
    if (PrimitiveMesh.Succeeded())
    {
        CubeMesh = PrimitiveMesh.Object;
        Body->SetStaticMesh(PrimitiveMesh.Object);
    }
    SphereMesh = Sphere.Object;
    CylinderMesh = Cylinder.Object;
    ConeMesh = Cone.Object;
    ActorMaterial = Material.Object;
    Body->SetRelativeScale3D(FVector(0.55, 0.55, 1.2));
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetCanEverAffectNavigation(false);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    // Tactical occlusion traces test level geometry; a pawn at the context endpoint is not cover.
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
    GetCharacterMovement()->MaxWalkSpeed = 420;
}
bool AAegisCharacter::IsHostile(const AActor* Other) const
{
    const auto* C = Cast<AAegisCharacter>(Other);
    return C && aegis::hostile(static_cast<aegis::Team>(Team), static_cast<aegis::Team>(C->Team));
}
void AAegisCharacter::BeginPlay()
{
    Super::BeginPlay();
    Health->OnDeath.AddDynamic(this, &AAegisCharacter::Die);
    bLayeredShotVFX = AegisPortfolioPresentation::V2Enabled();
    bPresentationEnabled = FApp::CanEverRender() && !FParse::Param(FCommandLine::Get(), TEXT("NullRHI"));
    if (bPresentationEnabled)
    {
        BuildPresentation();
        if (bLayeredShotVFX) BuildShotVFXPool();
        Health->OnDamaged.AddDynamic(this, &AAegisCharacter::FlashHit);
    }
}
void AAegisCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
    GetWorldTimerManager().ClearTimer(PortfolioPoseTimer);
    GetWorldTimerManager().ClearTimer(HitFlashTimer);
    GetWorldTimerManager().ClearTimer(WindupVisualTimer);
    GetWorldTimerManager().ClearTimer(PulseVisualTimer);
    GetWorldTimerManager().ClearTimer(ShotFXTimer);
    for (FTimerHandle& Timer : TracerTimers)
        GetWorldTimerManager().ClearTimer(Timer);
    Super::EndPlay(Reason);
}
UStaticMeshComponent* AAegisCharacter::AddDecoration(UStaticMesh* ShapeMesh, const FVector& Location,
                                                    const FVector& Scale, const FRotator& Rotation,
                                                    UMaterialInterface* Material)
{
    auto* Component = NewObject<UStaticMeshComponent>(this);
    Component->SetMobility(EComponentMobility::Movable);
    Component->SetupAttachment(GetRootComponent());
    Component->SetStaticMesh(ShapeMesh);
    Component->SetRelativeLocation(Location);
    Component->SetRelativeScale3D(Scale);
    Component->SetRelativeRotation(Rotation);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetGenerateOverlapEvents(false);
    Component->SetCanEverAffectNavigation(false);
    Component->SetCastShadow(false);
    if (Material)
        Component->SetMaterial(0, Material);
    Component->RegisterComponent();
    Decorations.Add(Component);
    return Component;
}
void AAegisCharacter::BuildPresentation()
{
    if (AegisPortfolioPresentation::Enabled()) { BuildPortfolioPresentation(); return; }
    const auto* Bot = Cast<AAegisAICharacter>(this);
    const bool Companion = Bot && Bot->bCompanion;
    const bool Elite = Bot && Bot->bElite;
    const bool TacticalEnemy = Bot && Bot->bTacticalTrial && Team == EAegisTeam::Enemy;
    IdentityColor = Team == EAegisTeam::Player
                        ? (Companion ? FLinearColor(0.12f, 0.9f, 0.44f) : FLinearColor(0.04f, 0.68f, 1.f))
                        : (Elite ? FLinearColor(0.88f, 0.15f, 0.75f) : FLinearColor(1.f, 0.23f, 0.07f));
    if (TacticalEnemy && !Elite)
    {
        if (Bot->EnemyRole == EAegisEnemyRole::Flanker) IdentityColor = FLinearColor(1.f, 0.62f, 0.06f);
        else if (Bot->EnemyRole == EAegisEnemyRole::Suppressor) IdentityColor = FLinearColor(0.82f, 0.08f, 0.16f);
    }
    if (ActorMaterial)
    {
        BodyMaterial = UMaterialInstanceDynamic::Create(ActorMaterial, this);
        BodyMaterial->SetVectorParameterValue(TEXT("Tint"), IdentityColor);
        Body->SetMaterial(0, BodyMaterial);
        AccentMaterial = UMaterialInstanceDynamic::Create(ActorMaterial, this);
        AccentMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.72f, 0.86f, 0.95f));
        TracerMaterial = UMaterialInstanceDynamic::Create(ActorMaterial, this);
        TracerMaterial->SetVectorParameterValue(TEXT("Tint"), IdentityColor * 2.5f);
    }
    // Meshes express role as well as color. Their collision remains the original capsule.
    Body->SetRelativeLocation(FVector(0, 0, -10));
    if (Companion)
    {
        Body->SetStaticMesh(SphereMesh);
        Body->SetRelativeScale3D(FVector(0.76, 0.76, 0.8));
        AddDecoration(CubeMesh, FVector(0, -40, -6), FVector(0.45, 0.12, 0.65), FRotator(0, 0, -18), BodyMaterial);
        AddDecoration(CubeMesh, FVector(0, 40, -6), FVector(0.45, 0.12, 0.65), FRotator(0, 0, 18), BodyMaterial);
    }
    else if (TacticalEnemy)
    {
        switch (Bot->EnemyRole)
        {
        case EAegisEnemyRole::Flanker:
            Body->SetStaticMesh(ConeMesh);
            Body->SetRelativeScale3D(FVector(0.6, 0.5, 1.05));
            AddDecoration(CubeMesh, FVector(-16, -29, 8), FVector(0.65, 0.12, 0.16), FRotator(0, 35, 0), BodyMaterial);
            AddDecoration(CubeMesh, FVector(-16, 29, 8), FVector(0.65, 0.12, 0.16), FRotator(0, -35, 0), BodyMaterial);
            break;
        case EAegisEnemyRole::Suppressor:
            Body->SetStaticMesh(CubeMesh);
            Body->SetRelativeScale3D(FVector(0.85, 0.9, 0.82));
            AddDecoration(CubeMesh, FVector(38, -23, 18), FVector(0.9, 0.15, 0.16), FRotator::ZeroRotator, AccentMaterial);
            AddDecoration(CubeMesh, FVector(38, 23, 18), FVector(0.9, 0.15, 0.16), FRotator::ZeroRotator, AccentMaterial);
            break;
        default:
            Body->SetStaticMesh(ConeMesh);
            Body->SetRelativeScale3D(FVector(0.85, 0.75, 1.2));
            AddDecoration(ConeMesh, FVector(27, -27, 5), FVector(0.22, 0.22, 0.5), FRotator(90, 0, 0), AccentMaterial);
            AddDecoration(ConeMesh, FVector(27, 27, 5), FVector(0.22, 0.22, 0.5), FRotator(90, 0, 0), AccentMaterial);
            break;
        }
        if (Elite)
        {
            AddDecoration(ConeMesh, FVector(0, -43, 38), FVector(0.3, 0.3, 0.62), FRotator::ZeroRotator, BodyMaterial);
            AddDecoration(ConeMesh, FVector(0, 43, 38), FVector(0.3, 0.3, 0.62), FRotator::ZeroRotator, BodyMaterial);
        }
    }
    else if (Elite)
    {
        Body->SetStaticMesh(CubeMesh);
        Body->SetRelativeScale3D(FVector(0.85, 0.78, 1.12));
        AddDecoration(ConeMesh, FVector(0, -43, 38), FVector(0.3, 0.3, 0.62), FRotator::ZeroRotator, BodyMaterial);
        AddDecoration(ConeMesh, FVector(0, 43, 38), FVector(0.3, 0.3, 0.62), FRotator::ZeroRotator, BodyMaterial);
    }
    else if (Team == EAegisTeam::Enemy)
    {
        Body->SetStaticMesh(ConeMesh);
        Body->SetRelativeScale3D(FVector(0.85, 0.85, 1.2));
    }
    else
    {
        Body->SetStaticMesh(CylinderMesh);
        Body->SetRelativeScale3D(FVector(0.57, 0.57, 0.82));
        AddDecoration(CubeMesh, FVector(0, -34, 14), FVector(0.4, 0.18, 0.24), FRotator::ZeroRotator, BodyMaterial);
        AddDecoration(CubeMesh, FVector(0, 34, 14), FVector(0.4, 0.18, 0.24), FRotator::ZeroRotator, BodyMaterial);
    }
    AddDecoration(SphereMesh, FVector(0, 0, 48), FVector(0.36, 0.36, 0.31), FRotator::ZeroRotator, AccentMaterial);
    // A raised forward barrel makes facing legible even when the actor is standing still.
    AddDecoration(CubeMesh, FVector(39, 0, 27), FVector(0.64, 0.13, 0.13), FRotator::ZeroRotator, AccentMaterial);
    const float FootZ = -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() + 3;
    AddDecoration(CylinderMesh, FVector(0, 0, FootZ), FVector(Elite ? 1.3 : 1.05, Elite ? 1.3 : 1.05, 0.022),
                  FRotator::ZeroRotator, BodyMaterial);
    // Two reusable slots bound lifetime and component count, including rapid Blueprint fire tuning.
    TracerTimers.SetNum(2);
    for (int32 I = 0; I < 2; ++I)
    {
        auto* Tracer = AddDecoration(CubeMesh, FVector::ZeroVector, FVector::OneVector,
                                     FRotator::ZeroRotator, TracerMaterial);
        Tracer->SetAbsolute(true, true, true);
        Tracer->SetVisibility(false);
        Tracers.Add(Tracer);
    }
}
void AAegisCharacter::BuildShotVFXPool()
{
    if (!bPresentationEnabled || !ActorMaterial || !ShotFXPool.IsEmpty()) return;
    ShotShardMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Aegis/V2/Meshes/SM_V2Shard.SM_V2Shard"));
    for (const auto& Tracer : Tracers)
    {
        auto* Material = UMaterialInstanceDynamic::Create(ActorMaterial, this);
        Tracer->SetMaterial(0, Material);
        ShotCoreMaterials.Add(Material);
    }
    constexpr int32 ParticleBudget = 24;
    ShotFXParticles.SetNum(ParticleBudget);
    for (int32 Index = 0; Index < ParticleBudget; ++Index)
    {
        auto* Material = UMaterialInstanceDynamic::Create(ActorMaterial, this);
        auto* Part = NewObject<UStaticMeshComponent>(this);
        Part->SetMobility(EComponentMobility::Movable);
        Part->SetupAttachment(GetRootComponent());
        Part->SetAbsolute(true, true, true);
        Part->SetStaticMesh(CubeMesh);
        Part->SetMaterial(0, Material);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetCollisionResponseToAllChannels(ECR_Ignore);
        Part->SetGenerateOverlapEvents(false);
        Part->SetCanEverAffectNavigation(false);
        Part->SetCastShadow(false);
        Part->SetVisibility(false);
        Part->RegisterComponent();
        // Deliberately separate from Decorations: the death pose hides body decorations,
        // while these snapshot particles must finish their short, bounded lifetime.
        ShotFXPool.Add(Part);
        ShotFXMaterials.Add(Material);
    }
    ShotVFXStats.PoolComponents = ShotFXPool.Num() + Tracers.Num();
}

void AAegisCharacter::EmitShotParticle(UStaticMesh* Shape, const FVector& Position, const FVector& Velocity,
    const FVector& Scale, const FRotator& Rotation, const FLinearColor& Color,
    float Lifetime, float Gravity, float Spin)
{
    if (ShotFXPool.IsEmpty() || !Shape || Lifetime <= 0) return;
    const double Now = GetWorld()->GetTimeSeconds();
    int32 Slot = INDEX_NONE;
    double Oldest = TNumericLimits<double>::Max();
    const auto* FXController = Cast<AAegisPlayerController>(GetWorld()->GetFirstPlayerController());
    const int32 DecorativeSlots = FXController && FXController->bReducedEffects ? FMath::Min(6,ShotFXParticles.Num()) : ShotFXParticles.Num();
    for (int32 Index = 0; Index < DecorativeSlots; ++Index)
    {
        const auto& Particle = ShotFXParticles[Index];
        if (Particle.Lifetime <= 0 || Now >= Particle.StartedAt + Particle.Lifetime)
        { Slot = Index; break; }
        if (Particle.StartedAt < Oldest) { Oldest = Particle.StartedAt; Slot = Index; }
    }
    auto& Particle = ShotFXParticles[Slot];
    if (Particle.Lifetime > 0 && Now < Particle.StartedAt + Particle.Lifetime)
        ++ShotVFXStats.ReusedActiveSlots;
    Particle.StartedAt = Now;
    Particle.Lifetime = Lifetime;
    Particle.Position = Position;
    Particle.Velocity = Velocity;
    Particle.Scale = Scale;
    Particle.Rotation = Rotation;
    Particle.Color = Color;
    Particle.Gravity = Gravity;
    Particle.Spin = Spin;
    auto* Part = ShotFXPool[Slot].Get();
    Part->SetStaticMesh(Shape);
    Part->SetWorldLocationAndRotation(Position, Rotation);
    Part->SetWorldScale3D(Scale);
    ShotFXMaterials[Slot]->SetVectorParameterValue(TEXT("Tint"), Color);
    Part->SetVisibility(true);
    int32 Active = 0;
    for (const auto& Component : ShotFXPool) if (Component->IsVisible()) ++Active;
    for (const auto& Tracer : Tracers) if (Tracer->IsVisible()) ++Active;
    ShotVFXStats.PeakActiveComponents = FMath::Max(ShotVFXStats.PeakActiveComponents, Active);
    if (!GetWorldTimerManager().IsTimerActive(ShotFXTimer))
        GetWorldTimerManager().SetTimer(ShotFXTimer, this, &AAegisCharacter::UpdateShotVFX, 1.f / 60.f, true);
}

void AAegisCharacter::UpdateShotVFX()
{
    const double Now = GetWorld()->GetTimeSeconds();
    bool bAnyActive = false;
    for (int32 Index = 0; Index < ShotFXParticles.Num(); ++Index)
    {
        auto& Particle = ShotFXParticles[Index];
        if (Particle.Lifetime <= 0) continue;
        const float Age = static_cast<float>(Now - Particle.StartedAt);
        auto* Part = ShotFXPool[Index].Get();
        if (Age >= Particle.Lifetime)
        {
            Part->SetVisibility(false);
            Particle.Lifetime = 0;
            continue;
        }
        bAnyActive = true;
        const float Remaining = FMath::Clamp(1.f - Age / Particle.Lifetime, 0.f, 1.f);
        Part->SetWorldLocationAndRotation(Particle.Position + Particle.Velocity * Age +
            FVector(0, 0, -.5f * Particle.Gravity * Age * Age),
            Particle.Rotation + FRotator(Particle.Spin * Age, Particle.Spin * Age * .7f, 0));
        Part->SetWorldScale3D(Particle.Scale * (.12f + .88f * Remaining));
        ShotFXMaterials[Index]->SetVectorParameterValue(TEXT("Tint"), Particle.Color * Remaining);
    }
    if (!bAnyActive) GetWorldTimerManager().ClearTimer(ShotFXTimer);
}

void AAegisCharacter::ShowRangedShot(const FVector& Start, const FVector& End, bool bCharged)
{
    if (!bLayeredShotVFX) { ShowAttack(Start, End); return; }
    ++ShotVFXStats.Shots;
    if (bCharged) ++ShotVFXStats.ChargedShots;
    AegisPortfolioPresentation::Sound(this, TEXT("S_Shot"), Start, IsPlayerControlled() ? .45f : .23f);
    if (!bPresentationEnabled || Tracers.IsEmpty() || ShotFXPool.IsEmpty()) return;
    ++ShotVFXStats.RenderedShots;
    const FVector Segment = End - Start;
    const float Length = static_cast<float>(Segment.Size());
    const FVector Direction = Segment.GetSafeNormal();
    const FRotator Facing = Direction.Rotation();
    const FLinearColor Color = bCharged ? FLinearColor(.12f, .75f, 1.f)
        : Team == EAegisTeam::Player ? FLinearColor(.05f, .92f, .82f) : FLinearColor(1.f, .58f, .12f);
    // This remains the existing tracer pool, so the real shot also drives V2Gun's cosmetic recoil.
    const int32 Slot = NextTracer;
    NextTracer = (NextTracer + 1) % Tracers.Num();
    auto* Core = Tracers[Slot].Get();
    ShotCoreMaterials[Slot]->SetVectorParameterValue(TEXT("Tint"), bCharged ? FLinearColor(.78f, .95f, 1.f)
        : Team == EAegisTeam::Player ? FLinearColor(.58f, 1.f, .96f) : FLinearColor(1.f, .90f, .60f));
    Core->SetWorldLocationAndRotation((Start + End) * .5f, Facing);
    Core->SetWorldScale3D(FVector(Length / 100.f, bCharged ? .029f : .014f, bCharged ? .029f : .014f));
    Core->SetVisibility(true);
    GetWorldTimerManager().SetTimer(TracerTimers[Slot], FTimerDelegate::CreateWeakLambda(this, [this, Slot]()
        { if (Tracers.IsValidIndex(Slot) && Tracers[Slot]) Tracers[Slot]->SetVisibility(false); }),
        bCharged ? .14f : .075f, false);
    const FVector Muzzle = Start + Direction * FMath::Min(55.f, Length * .25f);
    EmitShotParticle(SphereMesh, Muzzle, FVector::ZeroVector,
        FVector(bCharged ? .21f : .12f), Facing, FLinearColor(.78f, .96f, 1.f), bCharged ? .09f : .055f);
    EmitShotParticle(CubeMesh, Muzzle, FVector::ZeroVector,
        FVector(bCharged ? .48f : .26f, .035f, bCharged ? .09f : .055f), Facing, Color, bCharged ? .12f : .08f);
    const float TailLength = FMath::Min(Length, bCharged ? 160.f : 85.f);
    EmitShotParticle(CubeMesh, End - Direction * TailLength * .5f, FVector::ZeroVector,
        FVector(TailLength / 100.f, bCharged ? .048f : .022f, bCharged ? .048f : .022f),
        Facing, Color, bCharged ? .21f : .12f);
    if (bCharged)
    {
        EmitShotParticle(CubeMesh, Muzzle, FVector::ZeroVector, FVector(.045f, .18f, .36f), Facing,
            Color, .12f);
        EmitShotParticle(CubeMesh, Start + Direction * FMath::Min(110.f, Length * .5f),
            FVector::ZeroVector, FVector(FMath::Min(Length, 90.f) / 100.f, .032f, .032f), Facing, Color, .16f);
    }
}

void AAegisCharacter::ShowShotImpact(const FVector& Point, const FVector& Normal, EAegisShotImpact Kind,
    bool bCharged, bool bFatal)
{
    if (!bLayeredShotVFX) return;
    ++ShotVFXStats.TraceImpacts;
    if (Kind == EAegisShotImpact::World) ++ShotVFXStats.WorldImpacts;
    else if (Kind == EAegisShotImpact::CharacterDamaged) ++ShotVFXStats.DamageImpacts;
    else ++ShotVFXStats.BlockedCharacterImpacts;
    if (bFatal && Kind == EAegisShotImpact::CharacterDamaged) ++ShotVFXStats.FatalImpacts;
    if (!bPresentationEnabled || ShotFXPool.IsEmpty()) return;
    const bool bDamage = Kind == EAegisShotImpact::CharacterDamaged;
    const FLinearColor Color = bCharged ? FLinearColor(.12f, .75f, 1.f)
        : bDamage ? (Team == EAegisTeam::Player ? FLinearColor(.16f, .95f, .84f) : FLinearColor(1.f, .65f, .20f))
        : FLinearColor(.58f, .67f, .75f);
    const FVector Outward = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    FVector Side, Up;
    Outward.FindBestAxisVectors(Side, Up);
    EmitShotParticle(SphereMesh, Point, FVector::ZeroVector,
        FVector(bCharged ? .19f : .10f), FRotator::ZeroRotator, FLinearColor(.82f, .95f, 1.f), .075f);
    const int32 Shards = bCharged ? 5 : 3;
    for (int32 Index = 0; Index < Shards; ++Index)
    {
        // Deterministic fan: cosmetic variation never consumes the gameplay random stream.
        const float Angle = (Index + .35f) * 2.f * PI / Shards;
        const FVector Direction = (Outward * .75f + Side * FMath::Cos(Angle) + Up * FMath::Sin(Angle)).GetSafeNormal();
        EmitShotParticle(ShotShardMesh ? ShotShardMesh.Get() : ConeMesh.Get(), Point,
            Direction * (bCharged ? 155.f : 110.f), FVector(.055f, .055f, bCharged ? .23f : .15f),
            Direction.Rotation() + FRotator(90, 0, 0), Color,
            bCharged ? .32f : .24f, 170.f, Index % 2 ? -180.f : 180.f);
    }
}

void AAegisCharacter::ShowDeathVFX()
{
    if (!bLayeredShotVFX || ShotVFXStats.DeathsObserved > 0) return;
    ++ShotVFXStats.DeathsObserved;
    if (!bPresentationEnabled || ShotFXPool.IsEmpty()) return;
    ++ShotVFXStats.RenderedDeathBursts;
    const FVector Center = GetActorLocation();
    const FLinearColor Color = Team == EAegisTeam::Player ? FLinearColor(.1f, .82f, .72f) : FLinearColor(.70f, .38f, 1.f);
    EmitShotParticle(SphereMesh, Center, FVector::ZeroVector, FVector(.28f), FRotator::ZeroRotator,
        FLinearColor(.72f, .88f, 1.f), .10f);
    for (int32 Index = 0; Index < 7; ++Index)
    {
        const float Angle = Index * 2.f * PI / 7;
        const FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), .25f + .15f * (Index % 3));
        EmitShotParticle(ShotShardMesh ? ShotShardMesh.Get() : ConeMesh.Get(), Center,
            Direction * 155.f, FVector(.09f, .09f, .29f), Direction.Rotation() + FRotator(90, 0, 0),
            Color, .42f, 210.f, Index % 2 ? -210.f : 210.f);
    }
}

void AAegisCharacter::ShowAttack(const FVector& Start, const FVector& End, bool bMelee)
{
    AegisPortfolioPresentation::Sound(this, bMelee ? TEXT("S_Impact") : TEXT("S_Shot"), Start, Team == EAegisTeam::Player ? 0.45f : 0.23f);
    if (!bPresentationEnabled || Tracers.IsEmpty())
        return;
    const int32 Slot = NextTracer;
    NextTracer = (NextTracer + 1) % Tracers.Num();
    auto* Tracer = Tracers[Slot].Get();
    const FVector Segment = End - Start;
    Tracer->SetWorldLocation((Start + End) * 0.5);
    Tracer->SetWorldRotation(Segment.Rotation());
    Tracer->SetWorldScale3D(FVector(FMath::Max(1.0, Segment.Length()) / 100.0, bMelee ? 0.5 : 0.035, 0.035));
    Tracer->SetVisibility(true);
    GetWorldTimerManager().SetTimer(TracerTimers[Slot], FTimerDelegate::CreateWeakLambda(this, [this, Slot]() {
        if (Tracers.IsValidIndex(Slot))
            Tracers[Slot]->SetVisibility(false);
    }), 0.11f, false);
}
float AAegisCharacter::GetShotWindupRemaining() const
{
    return GetWorld() ? FMath::Max(0.f, static_cast<float>(WindupVisualEndsAt - GetWorld()->GetTimeSeconds())) : 0;
}
void AAegisCharacter::ShowShotWindup(const FVector& KnownAimPoint, float Duration)
{
    ClearShotWindup();
    if (!Health->IsAlive() || IsPulseStaggered() || !FMath::IsFinite(Duration) || KnownAimPoint.ContainsNaN()) return;
    WindupVisualStartedAt = GetWorld()->GetTimeSeconds();
    WindupVisualEndsAt = WindupVisualStartedAt + FMath::Clamp(Duration, 0.05f, 5.f);
    if (!bPresentationEnabled) return;
    if (!WarningLine)
    {
        if (ActorMaterial)
        {
            WarningMaterial = UMaterialInstanceDynamic::Create(ActorMaterial, this);
            WarningMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor(1.8f, 0.65f, 0.04f));
        }
        WarningLine = AddDecoration(CubeMesh, FVector::ZeroVector, FVector::OneVector,
                                    FRotator::ZeroRotator, WarningMaterial);
        WarningMarker = AddDecoration(CylinderMesh, FVector::ZeroVector, FVector::OneVector,
                                      FRotator::ZeroRotator, WarningMaterial);
        WarningLine->SetAbsolute(true, true, true);
        WarningMarker->SetAbsolute(true, true, true);
    }
    // Both endpoints are immutable for this warning. Moving or hidden targets are not read here.
    const FVector Start = GetActorLocation() + FVector(0, 0, 30);
    const FVector Segment = KnownAimPoint - Start;
    WarningLine->SetWorldLocation((Start + KnownAimPoint) * 0.5);
    WarningLine->SetWorldRotation(Segment.Rotation());
    WarningLine->SetWorldScale3D(FVector(FMath::Max(1.0, Segment.Length()) / 100.0, 0.025, 0.025));
    WarningMarker->SetWorldLocation(FVector(KnownAimPoint.X, KnownAimPoint.Y,
                                           GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 6));
    WarningMarker->SetWorldRotation(FRotator::ZeroRotator);
    WarningMarker->SetWorldScale3D(FVector(0.78, 0.78, 0.025));
    WarningLine->SetVisibility(true);
    WarningMarker->SetVisibility(true);
    GetWorldTimerManager().SetTimer(WindupVisualTimer, this, &AAegisCharacter::UpdateWindupVisual, 0.04f, true);
}
void AAegisCharacter::UpdateWindupVisual()
{
    if (GetShotWindupRemaining() <= 0 || !Health->IsAlive())
    {
        ClearShotWindup();
        return;
    }
    const float Progress = FMath::Clamp(static_cast<float>((GetWorld()->GetTimeSeconds() - WindupVisualStartedAt) /
                                                          FMath::Max(0.05, WindupVisualEndsAt - WindupVisualStartedAt)), 0.f, 1.f);
    if (WarningMaterial)
        WarningMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor::LerpUsingHSV(
            FLinearColor(1.1f, 0.55f, 0.035f), FLinearColor(3.f, 0.2f, 0.025f), Progress));
    if (WarningMarker) WarningMarker->SetWorldScale3D(FVector(0.78f - Progress * 0.36f, 0.78f - Progress * 0.36f, 0.025));
}
void AAegisCharacter::ClearShotWindup()
{
    WindupVisualEndsAt = 0;
    if (GetWorld()) GetWorldTimerManager().ClearTimer(WindupVisualTimer);
    if (WarningLine) WarningLine->SetVisibility(false);
    if (WarningMarker) WarningMarker->SetVisibility(false);
}
bool AAegisCharacter::IsPulseStaggered() const
{
    return GetWorld() && GetWorld()->GetTimeSeconds() < PulseStaggerUntil;
}
void AAegisCharacter::ApplyPulseStagger(const FVector& OutwardDirection, float Duration)
{
    if (!Health->IsAlive() || !GetWorld()) return;
    PulseStaggerUntil = FMath::Max(PulseStaggerUntil, GetWorld()->GetTimeSeconds() + FMath::Clamp(Duration, 0.f, 2.f));
    ClearShotWindup();
    if (auto* AI = Cast<AAegisAIController>(GetController())) AI->CancelTacticalWindup();
    LaunchCharacter(OutwardDirection.GetSafeNormal2D() * 850 + FVector(0, 0, 120), true, true);
}
void AAegisCharacter::ShowPulse(const FVector& Center, float Radius)
{
    AegisPortfolioPresentation::Sound(this, TEXT("S_Pulse"), Center, 0.7f);
    if (!bPresentationEnabled) return;
    if (PulseSegments.IsEmpty())
    {
        if (ActorMaterial)
        {
            PulseMaterial = UMaterialInstanceDynamic::Create(ActorMaterial, this);
            PulseMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.3f, 2.5f, 3.f));
        }
        for (int32 Index = 0; Index < 20; ++Index)
        {
            auto* Segment = AddDecoration(CubeMesh, FVector::ZeroVector, FVector::OneVector,
                                           FRotator::ZeroRotator, PulseMaterial);
            Segment->SetAbsolute(true, true, true);
            PulseSegments.Add(Segment);
        }
    }
    PulseVisualCenter = Center;
    PulseVisualRadius = Radius;
    PulseVisualStartedAt = GetWorld()->GetTimeSeconds();
    UpdatePulseVisual();
    GetWorldTimerManager().SetTimer(PulseVisualTimer, this, &AAegisCharacter::UpdatePulseVisual, 0.025f, true);
}
void AAegisCharacter::UpdatePulseVisual()
{
    const float Progress = static_cast<float>((GetWorld()->GetTimeSeconds() - PulseVisualStartedAt) / 0.35);
    if (Progress >= 1)
    {
        for (UStaticMeshComponent* Segment : PulseSegments) Segment->SetVisibility(false);
        GetWorldTimerManager().ClearTimer(PulseVisualTimer);
        return;
    }
    const float Radius = PulseVisualRadius * FMath::Lerp(0.18f, 1.f, FMath::Clamp(Progress * 1.6f, 0.f, 1.f));
    const float SegmentLength = 2.f * Radius * FMath::Sin(PI / PulseSegments.Num()) * 0.85f;
    for (int32 Index = 0; Index < PulseSegments.Num(); ++Index)
    {
        const float Angle = 2.f * PI * Index / PulseSegments.Num();
        auto* Segment = PulseSegments[Index].Get();
        Segment->SetWorldLocation(PulseVisualCenter + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Radius);
        Segment->SetWorldRotation(FRotator(0, FMath::RadiansToDegrees(Angle) + 90, 0));
        Segment->SetWorldScale3D(FVector(SegmentLength / 100.f, 0.065, 0.025));
        Segment->SetVisibility(true);
    }
    if (PulseMaterial)
        PulseMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.22f, 1.8f, 2.6f) * (1.6f - FMath::Clamp(Progress, 0.f, 1.f)));
}
void AAegisCharacter::FlashHit(float Applied, AActor* Source, AActor* Victim)
{
    AegisPortfolioPresentation::Sound(this, TEXT("S_Impact"), GetActorLocation(), 0.25f);
    (void)Applied;
    (void)Source;
    (void)Victim;
    if (!BodyMaterial)
        return;
    BodyMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor(2.5f, 2.5f, 2.5f));
    GetWorldTimerManager().SetTimer(HitFlashTimer, FTimerDelegate::CreateWeakLambda(this, [this]() {
        if (BodyMaterial)
            BodyMaterial->SetVectorParameterValue(TEXT("Tint"), Health->IsAlive() ? IdentityColor : FLinearColor(0.09f, 0.1f, 0.13f));
    }), 0.1f, false);
}
float AAegisCharacter::TakeDamage(float Amount, const FDamageEvent& Event, AController* InstigatorController,
                                  AActor* Causer)
{
    Super::TakeDamage(Amount, Event, InstigatorController, Causer);
    return Health->ApplyDamage(
        Amount, Causer ? Causer : (InstigatorController ? InstigatorController->GetPawn() : nullptr));
}
void AAegisCharacter::Die()
{
    ShowDeathVFX();
    AegisPortfolioPresentation::Sound(this, TEXT("S_Break"), GetActorLocation(), 0.5f);
    ClearShotWindup();
    GetCharacterMovement()->DisableMovement();
    SetActorEnableCollision(false);
    GetWorldTimerManager().ClearTimer(HitFlashTimer);
    if (BodyMaterial)
        BodyMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.09f, 0.1f, 0.13f));
    if (AccentMaterial)
        AccentMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.15f, 0.17f, 0.2f));
    if (auto* AI = Cast<AAegisAIController>(GetController()))
        AI->ShutdownAI();
}
AAegisPlayerCharacter::AAegisPlayerCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    Team = EAegisTeam::Player;
    SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
    SpringArm->SetupAttachment(GetRootComponent());
    SpringArm->TargetArmLength = 1400;
    SpringArm->SetRelativeRotation(FRotator(-65, 0, 0));
    SpringArm->bUsePawnControlRotation = false;
    SpringArm->SetUsingAbsoluteRotation(true);
    SpringArm->bDoCollisionTest = false;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(SpringArm);
    bUseControllerRotationYaw = false;
    GetCharacterMovement()->bOrientRotationToMovement = false;
}
void AAegisPlayerCharacter::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindAxis("MoveForward", this, &AAegisPlayerCharacter::Forward);
    Input->BindAxis("MoveRight", this, &AAegisPlayerCharacter::Right);
    Input->BindAction("Fire", IE_Pressed, this, &AAegisPlayerCharacter::Fire);
    Input->BindAction("Fire", IE_Released, this, &AAegisPlayerCharacter::StopFire);
    Input->BindAction("Melee", IE_Pressed, this, &AAegisPlayerCharacter::Strike);
    Input->BindAction("Melee", IE_Released, this, &AAegisPlayerCharacter::ReleaseCharge);
    Input->BindAction("Dash", IE_Pressed, this, &AAegisPlayerCharacter::Dash);
    Input->BindAction("Pulse", IE_Pressed, this, &AAegisPlayerCharacter::PulseInput);
}
void AAegisPlayerCharacter::Forward(float V)
{
    ForwardInput = V;
    if (CanAcceptCombatInput() && !bDashing)
        AddMovementInput(FRotationMatrix(FRotator(0, SpringArm->GetComponentRotation().Yaw, 0)).GetUnitAxis(EAxis::X), V);
}
void AAegisPlayerCharacter::Right(float V)
{
    RightInput = V;
    if (CanAcceptCombatInput() && !bDashing)
        AddMovementInput(FRotationMatrix(FRotator(0, SpringArm->GetComponentRotation().Yaw, 0)).GetUnitAxis(EAxis::Y), V);
}
FVector AAegisPlayerCharacter::GetScreenMovementDirection() const
{
    const FRotationMatrix Basis(FRotator(0, SpringArm->GetComponentRotation().Yaw, 0));
    return (Basis.GetUnitAxis(EAxis::X) * ForwardInput + Basis.GetUnitAxis(EAxis::Y) * RightInput).GetSafeNormal();
}
bool AAegisPlayerCharacter::CanAcceptCombatInput() const
{
    const auto* PC = Cast<APlayerController>(GetController());
    if (!bCombatEnabled || !Health->IsAlive() || !PC || !PC->IsLocalController() || PC->IsMoveInputIgnored() ||
        !GetWorld() || GetWorld()->IsPaused())
        return false;
#if !UE_BUILD_SHIPPING
    // Offscreen Windows uses an in-memory NullCursor; this opt-in fixture does not
    // focus a native window or alter interactive/Shipping focus-loss behavior.
    if (FParse::Param(FCommandLine::Get(), TEXT("AegisInputProbe")) &&
        FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")))
        return true;
#endif
    // Losing focus must release held fire rather than continue shooting after an Alt-Tab.
    const auto* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    return !ViewportClient || !ViewportClient->Viewport || ViewportClient->Viewport->HasFocus();
}
void AAegisPlayerCharacter::UpdateAim()
{
    AimPoint = GetActorLocation() + GetActorForwardVector() * 1200 + FVector(0, 0, 30);
    auto* PC = Cast<APlayerController>(GetController());
    FVector RayOrigin, RayDirection;
    if (!PC || !PC->DeprojectMousePositionToWorld(RayOrigin, RayDirection) || RayDirection.Z >= -KINDA_SMALL_NUMBER)
        return;
    const double GroundZ = GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const double Distance = (GroundZ - RayOrigin.Z) / RayDirection.Z;
    if (Distance < 0)
        return;
    const FVector GroundPoint = RayOrigin + RayDirection * Distance;
    AimPoint = FVector(GroundPoint.X, GroundPoint.Y, GetActorLocation().Z + 30);
    const FVector Facing = (AimPoint - GetActorLocation()).GetSafeNormal2D();
    if (!Facing.IsNearlyZero())
        SetActorRotation(Facing.Rotation());
}
void AAegisPlayerCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!CanAcceptCombatInput())
    {
        bFireHeld = false;
        CancelCharge();
        EndDash();
        if (IsPlayerControlled())
            GetCharacterMovement()->StopMovementImmediately();
        return;
    }
    UpdateAim();
    if (bDashing)
    {
        if (GetWorld()->GetTimeSeconds() >= DashEndsAt)
            EndDash();
        else
            AddMovementInput(DashDirection);
    }
    const auto* PC = Cast<APlayerController>(GetController());
    if (bFireHeld && !PC->IsInputKeyDown(EKeys::LeftMouseButton))
        bFireHeld = false;
    if (bCharging)
    {
        const auto* Portfolio = AAegisPortfolio::Find(GetWorld());
        if (!PC->IsInputKeyDown(EKeys::RightMouseButton) || !Portfolio || !Portfolio->CanCharge()) CancelCharge();
        else if (!bChargeReadySoundPlayed && GetChargeFraction() >= 1.f && AegisPortfolioPresentation::V2Enabled())
        {
            bChargeReadySoundPlayed = true;
            AegisPortfolioPresentation::Sound(this, TEXT("S_ChargeReady"), GetActorLocation(), .32f);
        }
    }
    if (bFireHeld && !bCharging)
        Combat->FireAt(AimPoint);
}
void AAegisPlayerCharacter::Fire()
{
    if (!CanAcceptCombatInput())
        return;
    bFireHeld = true;
    UpdateAim();
    if (!bCharging) Combat->FireAt(AimPoint);
}
void AAegisPlayerCharacter::StopFire()
{
    bFireHeld = false;
}
void AAegisPlayerCharacter::Strike()
{
    if (CanAcceptCombatInput())
    {
        UpdateAim();
        auto* Portfolio = AAegisPortfolio::Find(GetWorld());
        if (Portfolio && Portfolio->IsV2())
        {
            if (bCharging || !Portfolio->CanCharge()) return;
            bCharging = true;
            bChargeReadySoundPlayed = false;
            ChargeStartedAt = GetWorld()->GetTimeSeconds();
        }
        else Combat->Melee();
    }
}
float AAegisPlayerCharacter::GetChargeFraction() const
{
    return bCharging && GetWorld() ? FMath::Clamp(static_cast<float>((GetWorld()->GetTimeSeconds() - ChargeStartedAt) / .7), 0.f, 1.f) : 0.f;
}
void AAegisPlayerCharacter::CancelCharge() { bCharging = false; ChargeStartedAt = 0; bChargeReadySoundPlayed = false; }
void AAegisPlayerCharacter::ReleaseCharge()
{
    const bool bReady = bCharging && GetChargeFraction() >= 1.f && CanAcceptCombatInput();
    CancelCharge();
    if (bReady) { UpdateAim(); Combat->FireChargedAt(AimPoint); }
}
float AAegisPlayerCharacter::GetDashCooldownRemaining() const
{
    return GetWorld() ? FMath::Max(0.f, static_cast<float>(DashReadyAt - GetWorld()->GetTimeSeconds())) : 0;
}
float AAegisPlayerCharacter::GetPulseCooldownRemaining() const
{
    return GetWorld() ? FMath::Max(0.f, static_cast<float>(PulseReadyAt - GetWorld()->GetTimeSeconds())) : 0;
}
void AAegisPlayerCharacter::PulseInput()
{
    TryPulse();
}
bool AAegisPlayerCharacter::TryPulse()
{
    if (!bPulseEnabled || !CanAcceptCombatInput() || GetPulseCooldownRemaining() > 0 ||
        !GetWorld() || GetWorld()->IsPaused()) return false;
    if (auto* Portfolio = AAegisPortfolio::Find(GetWorld())) if (!Portfolio->TrySpendPulse()) return false;
    PulseReadyAt = GetWorld()->GetTimeSeconds() + FMath::Max(0.1f, PulseCooldownSeconds);
    ++PulseActivations;
    LastPulseTargetsHit = 0;
    const float Radius = FMath::Clamp(PulseRadius, 50.f, 1000.f);
    const FVector Center = GetActorLocation();
    ShowPulse(Center - FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 6), Radius);
    if (bRestorativePulse) Health->Heal(12);
    TArray<FOverlapResult> Overlaps;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(AegisPulse), false, this);
    GetWorld()->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity,
        FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(Radius), Params);
    TSet<AAegisCharacter*> Visited;
    for (const FOverlapResult& Overlap : Overlaps)
    {
        auto* Target = Cast<AAegisCharacter>(Overlap.GetActor());
        if (!Target || !Target->Health->IsAlive() || Visited.Contains(Target) ||
            FVector::DistSquared(Center, Target->GetActorLocation()) > FMath::Square(Radius)) continue;
        Visited.Add(Target);
        const auto* Bot = Cast<AAegisAICharacter>(Target);
        const bool Hostile = IsHostile(Target);
        const bool RestoreAlly = bRestorativePulse && Bot && Bot->bCompanion && Target->Team == Team;
        if (!Hostile && !RestoreAlly) continue;
        // The cover channel ignores pawn capsules, so allies cannot mask an enemy and
        // geometry blocks damage, stagger and restoration consistently on both sides.
        if (GetWorld()->LineTraceTestByChannel(Center + FVector(0, 0, 20),
                Target->GetActorLocation() + FVector(0, 0, 20), ECC_GameTraceChannel1, Params)) continue;
        if (RestoreAlly)
        {
            Target->Health->Heal(20);
            continue;
        }
        if (Target->Health->ApplyDamage(FMath::Max(0.f, PulseDamage), this) > 0)
        {
            ++LastPulseTargetsHit;
            if (Target->Health->IsAlive()) Target->ApplyPulseStagger(Target->GetActorLocation() - Center, 0.45f);
        }
    }
    return true;
}
void AAegisPlayerCharacter::Dash()
{
    if (!CanAcceptCombatInput() || GetDashCooldownRemaining() > 0 || !GetCharacterMovement()->IsMovingOnGround())
        return;
    DashDirection = GetScreenMovementDirection();
    if (DashDirection.IsNearlyZero())
        DashDirection = GetActorForwardVector();
    const double Now = GetWorld()->GetTimeSeconds();
    DashReadyAt = Now + FMath::Max(0.1f, DashCooldown);
    DashEndsAt = Now + 0.18;
    auto* Movement = GetCharacterMovement();
    WalkSpeedBeforeDash = Movement->MaxWalkSpeed;
    AccelerationBeforeDash = Movement->MaxAcceleration;
    Movement->MaxWalkSpeed = 1600;
    Movement->MaxAcceleration = 20000;
    Movement->Velocity = DashDirection * 1600;
    bDashing = true;
    // CharacterMovement keeps capsule sweeps and wall blocking during the dash; no teleport or immunity.
    if (AegisPortfolioPresentation::V2Enabled()) AAegisTacticalFX::Emit(this,EAegisTacticalCue::Dash,GetActorLocation(),DashDirection);
    else ShowAttack(GetActorLocation(), GetActorLocation() - DashDirection * 100);
}
void AAegisPlayerCharacter::EndDash()
{
    if (!bDashing)
        return;
    auto* Movement = GetCharacterMovement();
    Movement->MaxWalkSpeed = WalkSpeedBeforeDash;
    Movement->MaxAcceleration = AccelerationBeforeDash;
    Movement->Velocity = Movement->Velocity.GetClampedToMaxSize2D(WalkSpeedBeforeDash);
    bDashing = false;
}
AAegisAICharacter::AAegisAICharacter()
{
    AIControllerClass = AAegisAIController::StaticClass();
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}
void AAegisAICharacter::BeginPlay()
{
    if (bCompanion)
        Team = EAegisTeam::Player;
    if (bElite)
    {
        Health->Maximum = 160;
        Health->Current = 160;
        Combat->RangedDamage = 20;
        Body->SetRelativeScale3D(FVector(0.85, 0.85, 1.4));
    }
    Super::BeginPlay();
}
