#include "AegisTacticalFX.h"
#include "AegisLab.h"
#include "AegisCharacter.h"
#include "AegisPortfolio.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/App.h"

AAegisTacticalFX::AAegisTacticalFX()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("TacticalFXOrigin")));
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PostUpdateWork;
    SetActorEnableCollision(false);
}
AAegisTacticalFX* AAegisTacticalFX::Find(UWorld* World)
{
    if(World) for(TActorIterator<AAegisTacticalFX> It(World);It;++It) return *It;
    return nullptr;
}
void AAegisTacticalFX::Ensure(AActor* Arena)
{
    if(!Arena || !FApp::CanEverRender() || Find(Arena->GetWorld())) return;
    auto* FX=Arena->GetWorld()->SpawnActor<AAegisTacticalFX>();
    if(FX) { FX->Runner=Cast<AAegisScenarioRunner>(Arena); FX->SetOwner(Arena); FX->Build(); }
}
void AAegisTacticalFX::Build()
{
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Aegis/V2/Materials/M_V2Energy.M_V2Energy"));
    if(!Cube || !Material) return;
    const FLinearColor Colors[]={FLinearColor(.10f,.80f,1.f),FLinearColor(.18f,1.f,.63f),FLinearColor(1.f,.52f,.10f),FLinearColor(.63f,.36f,1.f)};
    for(int32 L=0;L<7;++L) {
        auto* C=NewObject<UInstancedStaticMeshComponent>(this);
        C->SetStaticMesh(Cube); C->SetMobility(EComponentMobility::Movable);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetCanEverAffectNavigation(false); C->SetCastShadow(false);
        AddInstanceComponent(C); C->RegisterComponent();
        auto* Tint=UMaterialInstanceDynamic::Create(Material,this);
        Tint->SetVectorParameterValue(TEXT("Tint"),Colors[L<4?L:L==5?1:0]*.8f);
        C->SetMaterial(0,Tint);
        int32 Count=L<4?SlotsPerChannel:L==6?24:4;
        for(int32 I=0;I<Count;++I) C->AddInstance(FTransform(FQuat::Identity,FVector::ZeroVector,FVector::ZeroVector),true);
        if(L<4) FXLayers.Add(C); else Semantic.Add(C);
    }
}
bool AAegisTacticalFX::Reduced() const
{
    auto* PC=GetWorld()?Cast<AAegisPlayerController>(GetWorld()->GetFirstPlayerController()):nullptr;
    return PC && PC->bReducedEffects;
}
void AAegisTacticalFX::Emit(UObject* Context, EAegisTacticalCue Cue, const FVector& Position, const FVector& Direction)
{
    if(Context) if(auto* FX=Find(Context->GetWorld())) FX->Burst(Cue,Position,Direction);
}
void AAegisTacticalFX::Add(int32 Channel,const FVector& Position,const FVector& Velocity,const FVector& Scale,const FRotator& Rotation,float Life)
{
    if(!FXLayers.IsValidIndex(Channel)) return;
    // Drop newest decoration if a channel is full. Never recycle a gameplay cue.
    for(int32 I=0;I<SlotsPerChannel;++I) {
        auto& P=Motes[Channel*SlotsPerChannel+I];
        if(P.Life<=0) { P={0,Life,Position,Velocity,Scale,Rotation}; ++EmittedCount; return; }
    }
    ++DroppedCount;
}
void AAegisTacticalFX::Burst(EAegisTacticalCue Cue,const FVector& Position,const FVector& Direction)
{
    if(!GetWorld() || GetWorld()->IsPaused()) return;
    const bool Low=Reduced();
    int32 Channel=Cue==EAegisTacticalCue::Repair?1:Cue==EAegisTacticalCue::ArchiveClaim?3:Cue==EAegisTacticalCue::Overclock?2:0;
    if(Cue==EAegisTacticalCue::Dash) {
        const FVector Forward=Direction.GetSafeNormal2D(), Side(-Forward.Y,Forward.X,0);
        for(int32 I=0;I<(Low?3:9);++I) {
            float T=I/(Low?3.f:9.f);
            Add(0,Position-Forward*(T*135)+Side*((I%3)-1)*20, -Forward*60,
                FVector(.55f*(1-T*.5f),.028f,.038f),Forward.Rotation(),.30f+T*.12f);
        }
        return;
    }
    const int32 Count=Low?8:Cue==EAegisTacticalCue::RelayComplete?32:24;
    const float Radius=Cue==EAegisTacticalCue::Overclock?235:Cue==EAegisTacticalCue::RelayComplete?180:45;
    const float Life=Cue==EAegisTacticalCue::ChargeRelease?.32f:Cue==EAegisTacticalCue::Repair?.68f:.85f;
    FVector Center=Position; Center.Z=Cue==EAegisTacticalCue::ChargeRelease?Position.Z:18;
    for(int32 I=0;I<Count;++I) {
        const float A=I*2*PI/Count;
        FVector Radial(FMath::Cos(A),FMath::Sin(A),0);
        FVector V=Radial*(Cue==EAegisTacticalCue::ChargeRelease?150.f:70.f);
        if(Cue==EAegisTacticalCue::Repair || Cue==EAegisTacticalCue::ArchiveClaim) V.Z=80.f;
        Add(Channel,Center+Radial*Radius,V,FVector(Cue==EAegisTacticalCue::RelayComplete?.38f:.20f,.026f,.035f),FRotator(0,FMath::RadiansToDegrees(A)+90,0),Life);
        if(!Low && I%4==0) Add(Channel,Center+Radial*Radius,V+FVector(0,0,80),FVector(.025f,.025f,.35f),FRotator::ZeroRotator,Life*.75f);
    }
}
void AAegisTacticalFX::ResetTransient()
{
    for(auto& P:Motes) P.Life=0;
    for(const auto& C:FXLayers) if(C) for(int32 I=0;I<SlotsPerChannel;++I)
        C->UpdateInstanceTransform(I,FTransform(FQuat::Identity,FVector::ZeroVector,FVector::ZeroVector),true,I==SlotsPerChannel-1,true);
    ActiveCount=0;
}
void AAegisTacticalFX::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(!Runner || FXLayers.Num()!=4 || Semantic.Num()!=3 || !GetWorld()) return;
    auto* PC=Cast<AAegisPlayerController>(GetWorld()->GetFirstPlayerController());
    auto* Player=PC?Cast<AAegisPlayerCharacter>(PC->GetPawn()):nullptr;
    auto* Ally=Runner->GetCompanion();
    auto* Portfolio=AAegisPortfolio::Find(GetWorld());
    if(Player && Player!=LastPawn.Get()) { ResetTransient(); LastPawn=Player; bComplete=false; LastArchiveMask=0; }
    const bool Active=Runner->GetTrial().phase==aegis::TrialPhase::Active;
    if(Active && Portfolio) {
        if(Runner->GetOperation().complete && !bComplete) Burst(EAegisTacticalCue::RelayComplete,Runner->GetObjectiveLocation(),FVector::ForwardVector);
        bComplete=Runner->GetOperation().complete;
        const int32 Mask=Portfolio->GetSurveyClaimedMask();
        for(int32 I=0;I<2;++I) if((Mask&(1<<I)) && !(LastArchiveMask&(1<<I))) Burst(EAegisTacticalCue::ArchiveClaim,Portfolio->GetSurveyLocation(I),FVector::ForwardVector);
        LastArchiveMask=Mask;
    }
    ActiveCount=0;
    for(int32 L=0;L<4;++L) for(int32 I=0;I<SlotsPerChannel;++I) {
        auto& P=Motes[L*SlotsPerChannel+I]; FTransform Transform(FQuat::Identity,FVector::ZeroVector,FVector::ZeroVector);
        if(P.Life>0) {
            P.Age+=DeltaSeconds;
            if(P.Age>=P.Life) P.Life=0;
            else { ++ActiveCount; float T=P.Age/P.Life; float Envelope=FMath::Min(1.f,T/.08f)*FMath::Square(1-T);
                Transform=FTransform(P.Rotation,P.Position+P.Velocity*P.Age,P.Scale*FVector(1.f+.25f*T,Envelope,Envelope)); }
        }
        FXLayers[L]->UpdateInstanceTransform(I,Transform,true,I==SlotsPerChannel-1,true);
    }
    PeakCount=FMath::Max(PeakCount,ActiveCount);
    // Stable identity shapes remain in reduced effects. Nothing reveals hidden enemies.
    AAegisCharacter* Team[]={Player,Ally};
    for(int32 TeamIndex=0;TeamIndex<2;++TeamIndex) for(int32 I=0;I<4;++I) {
        FTransform T(FQuat::Identity,FVector::ZeroVector,FVector::ZeroVector);
        if(Team[TeamIndex] && Team[TeamIndex]->Health->IsAlive() && Active) {
            FVector C=Team[TeamIndex]->GetActorLocation(); C.Z-=Team[TeamIndex]->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-7;
            float A=PI*.5f*I+PI*.25f;
            T=FTransform(FRotator(0,FMath::RadiansToDegrees(A)+90,0),C+FVector(FMath::Cos(A),FMath::Sin(A),0)*55,
                FVector(TeamIndex==0?.52f:.30f,.035f,.025f));
        }
        Semantic[TeamIndex]->UpdateInstanceTransform(I,T,true,I==3,true);
    }
    bSemanticCharge=Player && Player->IsCharging() && Active;
    for(int32 I=0;I<24;++I) {
        FTransform T(FQuat::Identity,FVector::ZeroVector,FVector::ZeroVector);
        if(bSemanticCharge) {
            float F=Player->GetChargeFraction(), A=I*2*PI/24;
            FVector C=Player->GetActorLocation(); C.Z-=Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-10;
            float Radius=FMath::Lerp(108.f,64.f,F);
            bool Lit=I< FMath::CeilToInt(F*24);
            T=FTransform(FRotator(0,FMath::RadiansToDegrees(A)+90,0),C+FVector(FMath::Cos(A),FMath::Sin(A),0)*Radius,FVector(.13f,Lit?.042f:.008f,.025f));
        }
        Semantic[2]->UpdateInstanceTransform(I,T,true,I==23,true);
    }
}

