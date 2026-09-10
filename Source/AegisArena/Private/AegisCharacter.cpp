#include "AegisCharacter.h"
#include "AegisAIController.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InputComponent.h"
#include "Perception/AISense_Damage.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "DrawDebugHelpers.h"

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
bool UAegisCombatComponent::FireAt(const FVector& AimPoint)
{
    auto* Owner = Cast<AAegisCharacter>(GetOwner());
    if (!Owner || !Owner->Health->IsAlive() || CooldownRemaining() > 0)
        return false;
    const FVector Start = Owner->GetActorLocation() + FVector(0, 0, 30),
                  Direction = (AimPoint - Start).GetSafeNormal();
    if (Direction.IsNearlyZero())
        return false;
    ReadyAt = GetWorld()->GetTimeSeconds() + FMath::Max(0.05f, RangedCooldown);
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(AegisRanged), false, Owner);
    if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Direction * RangedRange, ECC_Visibility,
                                             Params))
        if (auto* Target = Cast<AAegisCharacter>(Hit.GetActor()))
            Target->Health->ApplyDamage(RangedDamage, Owner);
#if !UE_BUILD_SHIPPING
    DrawDebugLine(GetWorld(), Start, Hit.bBlockingHit ? Hit.ImpactPoint : Start + Direction * RangedRange,
                  Owner->Team == EAegisTeam::Player ? FColor::Cyan : FColor::Orange, false, 0.12f, 0, 4);
#endif
    return true;
}
bool UAegisCombatComponent::Melee()
{
    auto* Owner = Cast<AAegisCharacter>(GetOwner());
    if (!Owner || !Owner->Health->IsAlive() || CooldownRemaining() > 0)
        return false;
    ReadyAt = GetWorld()->GetTimeSeconds() + 0.9;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(AegisMelee), false, Owner);
    const FVector Start = Owner->GetActorLocation();
    if (GetWorld()->SweepSingleByChannel(Hit, Start, Start + Owner->GetActorForwardVector() * MeleeRange,
                                         FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(45),
                                         Params))
        if (auto* Target = Cast<AAegisCharacter>(Hit.GetActor()))
            Target->Health->ApplyDamage(MeleeDamage, Owner);
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
    if (PrimitiveMesh.Succeeded())
        Body->SetStaticMesh(PrimitiveMesh.Object);
    Body->SetRelativeScale3D(FVector(0.55, 0.55, 1.2));
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
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
    if (auto* Material =
            LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Aegis/Materials/M_AegisActor.M_AegisActor")))
        if (auto* Instance = Body->CreateDynamicMaterialInstance(0, Material))
        {
            const auto* Bot = Cast<AAegisAICharacter>(this);
            const FLinearColor Tint = Team == EAegisTeam::Player
                                          ? (Bot && Bot->bCompanion ? FLinearColor(0.15f, 0.9f, 0.45f)
                                                                    : FLinearColor(0.05f, 0.65f, 1.f))
                                          : (Bot && Bot->bElite ? FLinearColor(0.9f, 0.15f, 0.75f)
                                                                : FLinearColor(1.f, 0.25f, 0.12f));
            Instance->SetVectorParameterValue(TEXT("Tint"), Tint);
        }
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
    GetCharacterMovement()->DisableMovement();
    SetActorEnableCollision(false);
    Body->SetVectorParameterValueOnMaterials(TEXT("Tint"), FVector(0.15, 0.15, 0.15));
    if (auto* AI = Cast<AAegisAIController>(GetController()))
        AI->ShutdownAI();
}
AAegisPlayerCharacter::AAegisPlayerCharacter()
{
    Team = EAegisTeam::Player;
    SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
    SpringArm->SetupAttachment(GetRootComponent());
    SpringArm->TargetArmLength = 1400;
    SpringArm->SetRelativeRotation(FRotator(-65, 0, 0));
    SpringArm->bUsePawnControlRotation = false;
    SpringArm->SetUsingAbsoluteRotation(true);
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(SpringArm);
    bUseControllerRotationYaw = true;
}
void AAegisPlayerCharacter::SetupPlayerInputComponent(UInputComponent* Input)
{
    Super::SetupPlayerInputComponent(Input);
    Input->BindAxis("MoveForward", this, &AAegisPlayerCharacter::Forward);
    Input->BindAxis("MoveRight", this, &AAegisPlayerCharacter::Right);
    Input->BindAxis("Turn", this, &AAegisPlayerCharacter::Turn);
    Input->BindAction("Fire", IE_Pressed, this, &AAegisPlayerCharacter::Fire);
    Input->BindAction("Melee", IE_Pressed, this, &AAegisPlayerCharacter::Strike);
}
void AAegisPlayerCharacter::Forward(float V)
{
    AddMovementInput(FVector::ForwardVector, V);
}
void AAegisPlayerCharacter::Right(float V)
{
    AddMovementInput(FVector::RightVector, V);
}
void AAegisPlayerCharacter::Turn(float V)
{
    AddControllerYawInput(V);
}
void AAegisPlayerCharacter::Fire()
{
    Combat->FireAt(GetActorLocation() + GetActorForwardVector() * 1200 + FVector(0, 0, 30));
}
void AAegisPlayerCharacter::Strike()
{
    Combat->Melee();
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
