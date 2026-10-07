#include "AegisPortfolioPresentation.h"
#include "AegisCharacter.h"
#include "AegisPortfolio.h"
#include "AegisPortfolioCapture.h"
#include "AegisPortfolioMusic.h"
#include "Components/AudioComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/App.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

namespace AegisPortfolioPresentation
{
namespace
{
void BuildV2Arena(AActor* Owner);
void V2Event(UObject* Context, const TCHAR* Name, const FVector& Location);

// Cosmetic instances only. Navigation, cover traces and combat collision remain authored by the arena.
UInstancedStaticMeshComponent* V2Layer(AActor* Owner, const TCHAR* Name, UStaticMesh* Mesh,
                                      UMaterialInterface* Material, FLinearColor Tint, bool Glow = false)
{
    if (!Owner || !Mesh || !Material) return nullptr;
    auto* C = NewObject<UInstancedStaticMeshComponent>(Owner, Name);
    C->SetStaticMesh(Mesh);
    C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    C->SetCanEverAffectNavigation(false);
    C->SetCastShadow(!Glow);
    Owner->AddInstanceComponent(C);
    C->RegisterComponent();
    auto* M = UMaterialInstanceDynamic::Create(Material, C);
    M->SetVectorParameterValue(TEXT("Tint"), Tint);
    C->SetMaterial(0, M);
    return C;
}
void V2Add(UInstancedStaticMeshComponent* C, FVector Position, FVector Scale,
           FRotator Rotation = FRotator::ZeroRotator)
{
    if (C) C->AddInstance(FTransform(Rotation, Position, Scale), true);
}

// v2.2 role dressing is kept with the arena dressing. It runs after the
// scenario has spawned pawns and only adds no-collision meshes; combat,
// navigation and authored character components remain untouched.
UStaticMeshComponent* V22Attach(AActor* Owner, UStaticMesh* Mesh, UMaterialInterface* Material,
                                const FVector& Location, const FVector& Scale,
                                const FRotator& Rotation, const FName& Tag)
{
    if (!Owner || !Mesh || !Material || !Owner->GetRootComponent()) return nullptr;
    auto* C = NewObject<UStaticMeshComponent>(Owner);
    C->SetMobility(EComponentMobility::Movable);
    C->SetupAttachment(Owner->GetRootComponent());
    C->SetStaticMesh(Mesh);
    C->SetRelativeLocation(Location);
    C->SetRelativeScale3D(Scale);
    C->SetRelativeRotation(Rotation);
    C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    C->SetGenerateOverlapEvents(false);
    C->SetCanEverAffectNavigation(false);
    C->SetCastShadow(false);
    C->ComponentTags.Add(Tag);
    C->SetMaterial(0, Material);
    Owner->AddInstanceComponent(C);
    C->RegisterComponent();
    return C;
}

void ApplyV22CharacterStyle(UWorld* World)
{
    if (!World || !FApp::CanEverRender()) return;
    auto* Armor = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Aegis/V2/Meshes/SM_V2Armor.SM_V2Armor"));
    auto* Shard = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Aegis/V2/Meshes/SM_V2Shard.SM_V2Shard"));
    auto* Energy = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Aegis/V2/Materials/M_V2Energy.M_V2Energy"));
    auto* Basalt = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Aegis/V2/Materials/M_V2Basalt.M_V2Basalt"));
    if (!Armor || !Shard || !Energy || !Basalt) return;

    for (TActorIterator<AAegisCharacter> It(World); It; ++It)
    {
        AAegisCharacter* Character = *It;
        if (!IsValid(Character) || !Character->GetRootComponent()) continue;
        TArray<UStaticMeshComponent*> Existing;
        Character->GetComponents<UStaticMeshComponent>(Existing);
        bool AlreadyStyled = false;
        for (const auto* Component : Existing)
            if (Component && Component->ComponentHasTag(TEXT("V22RoleAccent"))) { AlreadyStyled = true; break; }
        if (AlreadyStyled) continue;

        const auto* Bot = Cast<AAegisAICharacter>(Character);
        const bool Friend = Character->Team == EAegisTeam::Player;
        const bool Companion = Bot && Bot->bCompanion;
        const bool Flank = Bot && Bot->EnemyRole == EAegisEnemyRole::Flanker && !Friend;
        const bool Heavy = Bot && Bot->EnemyRole == EAegisEnemyRole::Suppressor && !Friend;
        const FLinearColor RoleColor = Friend
            ? (Companion ? FLinearColor(.04f, 1.f, .66f) : FLinearColor(.06f, .82f, .95f))
            : (Heavy ? FLinearColor(1.f, .20f, .07f)
                   : Flank ? FLinearColor(1.f, .52f, .06f)
                           : FLinearColor(.70f, .26f, 1.f));
        auto* RoleMaterial = UMaterialInstanceDynamic::Create(Energy, Character);
        RoleMaterial->SetVectorParameterValue(TEXT("Tint"), RoleColor);
        auto* DarkMaterial = UMaterialInstanceDynamic::Create(Basalt, Character);
        DarkMaterial->SetVectorParameterValue(TEXT("Tint"), Friend
            ? FLinearColor(.015f, .08f, .10f) : FLinearColor(.07f, .018f, .08f));

        // One rear fin makes role/side readable from the tactical camera without
        // enlarging the collision silhouette. Suppressors get a second shoulder.
        if (Companion)
        {
            V22Attach(Character, Shard, RoleMaterial, FVector(-25, 0, 24),
                      FVector(.26f, .20f, .68f), FRotator(-18, 0, 0), TEXT("V22RoleAccent"));
            V22Attach(Character, Armor, DarkMaterial, FVector(38, 0, 8),
                      FVector(.72f, .16f, .18f), FRotator::ZeroRotator, TEXT("V22RoleAccent"));
        }
        else if (Flank)
        {
            V22Attach(Character, Shard, RoleMaterial, FVector(-28, 0, 34),
                      FVector(.18f, .22f, .78f), FRotator(-38, 0, 0), TEXT("V22RoleAccent"));
            V22Attach(Character, Armor, DarkMaterial, FVector(42, -20, 8),
                      FVector(.68f, .11f, .14f), FRotator(0, 0, -12), TEXT("V22RoleAccent"));
        }
        else if (Heavy)
        {
            V22Attach(Character, Armor, DarkMaterial, FVector(42, -25, 9),
                      FVector(.92f, .23f, .22f), FRotator::ZeroRotator, TEXT("V22RoleAccent"));
            V22Attach(Character, Shard, RoleMaterial, FVector(-12, 0, 48),
                      FVector(.22f, .26f, .52f), FRotator(0, 0, 18), TEXT("V22RoleAccent"));
        }
        else
        {
            V22Attach(Character, Shard, RoleMaterial, FVector(-26, 0, 38),
                      FVector(.20f, .20f, .56f), FRotator(-22, 0, 0), TEXT("V22RoleAccent"));
            V22Attach(Character, Armor, DarkMaterial, FVector(44, -18, 7),
                      FVector(.72f, .13f, .16f), FRotator::ZeroRotator, TEXT("V22RoleAccent"));
        }
    }
}

void BuildV2Arena(AActor* Owner)
{
    auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    auto* Shard = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Aegis/V2/Meshes/SM_V2Shard.SM_V2Shard"));
    auto* Pylon = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Aegis/V2/Meshes/SM_V2Pylon.SM_V2Pylon"));
    auto* Basalt = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Aegis/V2/Materials/M_V2Basalt.M_V2Basalt"));
    auto* Ceramic = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Aegis/V2/Materials/M_V2Ceramic.M_V2Ceramic"));
    auto* Crystal = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Aegis/V2/Materials/M_V2Crystal.M_V2Crystal"));
    auto* Energy = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Aegis/V2/Materials/M_V2Energy.M_V2Energy"));
    if (!Cube || !Cylinder || !Shard || !Pylon || !Basalt || !Ceramic || !Crystal || !Energy)
    {
        UE_LOG(LogTemp, Error, TEXT("AEGIS_V2_PRESENTATION_MISSING_ASSETS"));
        return;
    }
    auto* Rock = V2Layer(Owner, TEXT("V2BasaltStrata"), Cube, Basalt, FLinearColor(.028f, .047f, .065f));
    auto* Floor = V2Layer(Owner, TEXT("V2FloorFacets"), Cube, Basalt, FLinearColor(.10f, .15f, .18f));
    auto* FloorAlt = V2Layer(Owner, TEXT("V2FloorWeathered"), Cube, Basalt, FLinearColor(.14f, .20f, .22f));
    auto* Pale = V2Layer(Owner, TEXT("V2RelicCeramic"), Cube, Ceramic, FLinearColor(.42f, .53f, .55f));
    auto* Bronze = V2Layer(Owner, TEXT("V2RelicBronze"), Cube, Ceramic, FLinearColor(.40f, .25f, .12f));
    auto* Mint = V2Layer(Owner, TEXT("V2EnergyVeins"), Cube, Energy, FLinearColor(.035f, .70f, .58f), true);
    auto* Gold = V2Layer(Owner, TEXT("V2AmberInscriptions"), Cube, Energy, FLinearColor(.90f, .34f, .045f), true);
    auto* GroundGlow = V2Layer(Owner, TEXT("V22GroundLightBands"), Cube, Energy, FLinearColor(.02f, .32f, .38f), true);
    auto* RelicEtch = V2Layer(Owner, TEXT("V22RelicEtching"), Cube, Ceramic, FLinearColor(.24f, .35f, .38f));
    // Cosmetic route marks preserve authored cover. The two optional cache
    // holograms below share their anchors with Portfolio's real G/H interaction;
    // its live ring supplies scan progress and claimed state.
    auto* SurveyGlow = V23Enabled()
        ? V2Layer(Owner, TEXT("V23SurveyBeacons"), Cube, Energy, FLinearColor(.45f, .23f, 1.f), true)
        : nullptr;
    auto* CacheCrystal = V23Enabled()
        ? V2Layer(Owner, TEXT("V23CacheHolograms"), Shard, Crystal, FLinearColor(.60f, .32f, .95f), true)
        : nullptr;
    auto* RouteMark = V23Enabled()
        ? V2Layer(Owner, TEXT("V23RouteMarks"), Cube, Energy, FLinearColor(.95f, .52f, .16f), true)
        : nullptr;
    auto* ArchiveTrim = V23Enabled()
        ? V2Layer(Owner, TEXT("V23ArchiveTrim"), Cube, Ceramic, FLinearColor(.28f, .36f, .42f))
        : nullptr;
    // A small set of V23-only inlays breaks up the large floor slabs and gives
    // the route language a quiet material hierarchy. Every layer is cosmetic,
    // instanced and non-colliding, so the authored cover and navigation remain
    // the source of truth.
    auto* TileInlay = V23Enabled()
        ? V2Layer(Owner, TEXT("V23TileInlay"), Cube, Ceramic, FLinearColor(.22f, .31f, .34f))
        : nullptr;
    auto* BeaconHalo = V23Enabled()
        ? V2Layer(Owner, TEXT("V23BeaconHalo"), Cube, Energy, FLinearColor(.22f, .54f, .72f), true)
        : nullptr;
    auto* WarmInset = V23Enabled()
        ? V2Layer(Owner, TEXT("V23WarmInset"), Cube, Energy, FLinearColor(.92f, .30f, .08f), true)
        : nullptr;
    auto* StrataFlake = V23Enabled()
        ? V2Layer(Owner, TEXT("V23StrataFlakes"), Shard, Crystal, FLinearColor(.13f, .31f, .42f))
        : nullptr;
    auto* Growth = V2Layer(Owner, TEXT("V2CrystalGrowth"), Shard, Crystal, FLinearColor(.045f, .53f, .50f));
    auto* Violet = V2Layer(Owner, TEXT("V2AmethystGrowth"), Shard, Crystal, FLinearColor(.32f, .15f, .50f));
    auto* Columns = V2Layer(Owner, TEXT("V2RuinedPylons"), Pylon, Basalt, FLinearColor(.08f, .12f, .17f));
    auto* Pads = V2Layer(Owner, TEXT("V2RelayStone"), Cylinder, Basalt, FLinearColor(.045f, .085f, .10f));
    V2Add(Rock, FVector(0, 0, -160), FVector(90, 76, 1.5f));
    V2Add(Rock, FVector(0, 0, -65), FVector(43, 33, 1.2f));
    for (int X = -4; X <= 4; ++X)
        for (int Y = -3; Y <= 3; ++Y)
        {
            V2Add((X + Y) % 3 ? Floor : FloorAlt, FVector(X * 440, Y * 440, 2), FVector(4.33f, 4.33f, .025f));
            // Fragmented engravings make the floor readable without competing with live objective rings.
            if ((X + 2 * Y) % 3 == 0)
                V2Add(Bronze, FVector(X * 440 + 185, Y * 440 + 100, 4), FVector(.035f, 1.65f, .02f));
        }
    // Broken diagonal light bands sit inside the floor plane. They make the
    // space read as a powered relic without competing with objective rings.
    for (int I = -4; I <= 4; ++I)
    {
        const float Offset = (I & 1) ? 70.f : -70.f;
        V2Add(GroundGlow, FVector(I * 430.f + Offset, -1180.f, 6), FVector(1.55f, .026f, .018f), FRotator(0, 18, 0));
        V2Add(GroundGlow, FVector(I * 430.f - Offset, 1180.f, 6), FVector(1.55f, .026f, .018f), FRotator(0, -18, 0));
        V2Add(RelicEtch, FVector(I * 440.f, 690.f, 5), FVector(.035f, 1.55f, .018f), FRotator(0, I & 1 ? 8 : -8, 0));
        if (V23Enabled())
        {
            // Offset seams sit inside the tile rhythm instead of drawing a
            // second grid over it; the alternating breaks keep the camera
            // readable during combat and make the space feel assembled.
            V2Add(TileInlay, FVector(I * 620.f + ((I & 1) ? 120.f : -120.f), -885.f, 7),
                  FVector(1.35f, .022f, .018f), FRotator(0, 12, 0));
            V2Add(TileInlay, FVector(I * 620.f - ((I & 1) ? 80.f : -80.f), 885.f, 7),
                  FVector(1.35f, .022f, .018f), FRotator(0, -12, 0));
        }
    }
    for (int Side : {-1, 1})
    {
        V2Add(Pale, FVector(0, Side * 1470, 17), FVector(40, .38f, .30f));
        V2Add(Rock, FVector(Side * 2010, 0, 12), FVector(.65f, 30, .5f));
        for (int I = -4; I <= 4; ++I)
        {
            const float D = static_cast<float>((I * I + 3) % 4);
            V2Add(Rock, FVector(Side * (2180 + D * 55), I * 345, -22 - D * 15),
                  FVector(3.2f, 2.7f, 1.3f), FRotator(0, Side * (5 + D * 5), 0));
            V2Add(Columns, FVector(Side * 2220, I * 345, 80), FVector(1.35f, 1.35f, 2.1f + D * .2f));
            V2Add(Growth, FVector(Side * (2140 + D * 22), I * 345 - 60, 85),
                  FVector(.8f, .8f, 1.7f + D * .2f), FRotator(12, I * 41, Side * 17));
            V2Add(Violet, FVector(Side * 2310, I * 345 + 50, 60), FVector(.6f, .7f, 1.2f), FRotator(-20, I * 29, 12));
            V2Add(RelicEtch, FVector(Side * (2110 + D * 22), I * 345, 28),
                  FVector(.92f, .92f, .045f), FRotator(0, Side * (I * 9), 0));
            V2Add(Mint, FVector(I * 425, Side * 1470, 34), FVector(2.65f, .035f, .025f));
        }
        for (int I = -3; I <= 3; ++I)
        {
            V2Add(Rock, FVector(I * 600, Side * 1730, -30), FVector(4.4f, 2.3f, .8f), FRotator(0, I * 4, 0));
            V2Add(Growth, FVector(I * 600 + 65, Side * 1770, 50), FVector(1.0f, .8f, 1.5f), FRotator(8, I * 35, Side * 20));
        }
    }
    // A broken sky ring sits beyond the playable north edge, never over an objective.
    for (int I = 0; I < 24; ++I)
    {
        if (I == 2 || I == 3 || I == 13 || I == 18) continue;
        const float A = 2 * PI * I / 24;
        const FVector P(FMath::Cos(A) * 820, 2110, 560 + FMath::Sin(A) * 620);
        V2Add(Pale, P, FVector(1.75f, .78f, .82f), FRotator(-FMath::RadiansToDegrees(A) - 90, 0, 0));
        V2Add(Mint, P + FVector(0, -42, 0), FVector(1.35f, .028f, .08f), FRotator(-FMath::RadiansToDegrees(A) - 90, 0, 0));
    }
    // Extraction landmark: two fractured uprights outside the west boundary, not a new gameplay gate.
    for (int Side : {-1, 1})
    {
        V2Add(Columns, FVector(-2350, Side * 430, 215), FVector(1.8f, 1.8f, 4.4f), FRotator(0, 30, Side * 5));
        V2Add(Pale, FVector(-2350, Side * 315, 465), FVector(1.0f, 2.5f, .55f), FRotator(0, 0, Side * -12));
        V2Add(Gold, FVector(-2290, Side * 430, 240), FVector(.04f, .16f, 2.4f));
    }
    const FVector Covers[] = {FVector(-300, 400, 301), FVector(300, -400, 301),
                             FVector(600, 600, 301), FVector(-700, -600, 301)};
    for (int I = 0; I < 4; ++I)
    {
        const float W = I < 2 ? 3.6f : 2.8f;
        V2Add(Rock, Covers[I] + FVector(0, 0, 1), FVector(W + .08f, W + .08f, .08f));
        V2Add(Pale, Covers[I] + FVector(0, 0, 7), FVector(W - .18f, W - .18f, .04f));
        for (int Side : {-1, 1})
        {
            V2Add(Rock, Covers[I] + FVector(0, Side * (W * 50 + 2), -105), FVector(W, .05f, 1.95f));
            V2Add(Bronze, Covers[I] + FVector(Side * (W * 50 + 2), 0, -105), FVector(.05f, W, 1.95f));
            V2Add(Mint, Covers[I] + FVector(0, Side * (W * 50 + 5), -16), FVector(W - .5f, .028f, .07f));
        }
        // Put the growth on already-solid cover so its silhouette is visible in the
        // normal combat camera without adding a misleading walkable obstacle.
        V2Add(Growth, Covers[I] + FVector(-W * 24, W * 23, 62), FVector(.68f, .65f, 1.25f), FRotator(-15, I * 50, 18));
        V2Add(Growth, Covers[I] + FVector(-W * 10, W * 29, 37), FVector(.42f, .38f, .72f), FRotator(16, I * 50 + 75, -24));
        V2Add(Violet, Covers[I] + FVector(-W * 30, W * 9, 34), FVector(.33f, .38f, .62f), FRotator(-8, I * 50 - 35, 26));
        for (int J = -2; J <= 2; ++J)
            V2Add(Bronze, Covers[I] + FVector(J * 38, 0, 10), FVector(.07f, W * .65f, .02f));
    }
    const FVector Nodes[] = {FVector(-500, -1000, 5), FVector(850, -850, 5), FVector(0, 1000, 5), FVector(-1250, 0, 5)};
    for (const FVector& Node : Nodes)
    {
        V2Add(Pads, Node, FVector(4.7f, 4.7f, .025f));
        for (int I = 0; I < 16; ++I)
        {
            const float A = 2 * PI * I / 16;
            V2Add(Bronze, Node + FVector(FMath::Cos(A) * 228, FMath::Sin(A) * 228, 2),
                  FVector(.36f, .05f, .02f), FRotator(0, FMath::RadiansToDegrees(A) + 90, 0));
            if (V23Enabled() && (I % 2 == 0))
            {
                // A second, cooler halo makes survey nodes legible at a glance
                // without turning them into new objective volumes.
                V2Add(BeaconHalo, Node + FVector(FMath::Cos(A) * 280, FMath::Sin(A) * 280, 8),
                      FVector(.28f, .035f, .018f), FRotator(0, FMath::RadiansToDegrees(A) + 90, 0));
            }
        }
    }
    if (V23Enabled())
    {
        // Distinct holographic cache silhouettes mark real optional detours.
        // These never block movement or provide cover; the acquisition radius
        // is the live 1.8m ring built by AAegisPortfolio.
        for (const FVector& Offset : {FVector(-1050, -900, 8), FVector(1050, 650, 8)})
        {
            const FVector Cache = Owner->GetActorLocation() + Offset;
            V2Add(Pale, Cache + FVector(0, 0, -2), FVector(1.65f, 1.1f, .045f), FRotator(0, 45, 0));
            V2Add(CacheCrystal, Cache + FVector(0, 0, 84), FVector(.35f, .35f, .68f), FRotator(0, 45, 0));
            for (float Side : {-1.f, 1.f})
            {
                V2Add(SurveyGlow, Cache + FVector(Side * 64, 0, 42), FVector(.022f, .025f, .62f));
                V2Add(SurveyGlow, Cache + FVector(Side * 48, 0, 76), FVector(.33f, .025f, .025f), FRotator(0, 0, Side * 35));
                V2Add(Bronze, Cache + FVector(Side * 92, 0, 1), FVector(.20f, .06f, .02f), FRotator(0, 45, 0));
            }
            for (int32 RingIndex = 0; RingIndex < 4; ++RingIndex)
            {
                const float Angle = RingIndex * 90.f + 45.f;
                const float Radians = FMath::DegreesToRadians(Angle);
                V2Add(SurveyGlow, Cache + FVector(FMath::Cos(Radians) * 112, FMath::Sin(Radians) * 112, 2),
                    FVector(.32f, .025f, .02f), FRotator(0, Angle + 90, 0));
            }
        }
        // Each route node gets a compact beacon silhouette and a short broken
        // arrow that points toward the next decision space. They are deliberately
        // offset from objective rings so the map remains readable in combat.
        for (int32 NodeIndex = 0; NodeIndex < UE_ARRAY_COUNT(Nodes); ++NodeIndex)
        {
            const FVector Node = Nodes[NodeIndex];
            V2Add(SurveyGlow, Node + FVector(0, 0, 48), FVector(.12f, .12f, 1.9f));
            V2Add(SurveyGlow, Node + FVector(0, 0, 112), FVector(.45f, .06f, .05f));
            for (int32 Segment = -2; Segment <= 2; ++Segment)
            {
                const float Offset = Segment * 110.f;
                V2Add(RouteMark, Node + FVector(Offset, (NodeIndex & 1) ? -300.f : 300.f, 7),
                      FVector(.42f, .035f, .018f), FRotator(0, (NodeIndex & 1) ? -12.f : 12.f, 0));
            }
            // One warm inset anchors the approach side of each node. It is an
            // environmental breadcrumb, not a trigger or a traversable gate.
            const float Approach = (NodeIndex & 1) ? -1.f : 1.f;
            V2Add(WarmInset, Node + FVector(Approach * 360.f, 0, 8),
                  FVector(.55f, .035f, .018f), FRotator(0, 90, 0));
        }
        // A framed archive landmark gives the transfer leg a visual destination.
        for (int32 Side : {-1, 1})
        {
            V2Add(ArchiveTrim, FVector(Side * 780.f, 1890.f, 240), FVector(.12f, 2.8f, 3.4f), FRotator(0, Side * 7.f, 0));
            V2Add(ArchiveTrim, FVector(0, 1890.f, 500), FVector(7.9f, .12f, .14f));
            V2Add(SurveyGlow, FVector(Side * 790.f, 1845.f, 300), FVector(.04f, .08f, 2.1f));
        }
        // Low, offset plinth strips give the distant frame a grounded rhythm
        // and a visual destination without creating a new collision surface.
        V2Add(ArchiveTrim, FVector(0, 1775.f, 9), FVector(11.5f, .08f, .025f));
        V2Add(TileInlay, FVector(0, 1695.f, 8), FVector(8.5f, .04f, .018f));
        for (int32 I = -4; I <= 4; ++I)
        {
            V2Add(WarmInset, FVector(I * 240.f, 1705.f + ((I & 1) ? 18.f : -18.f), 10),
                  FVector(.05f, .22f, .018f), FRotator(0, 0, I & 1 ? 8.f : -8.f));
        }
        // Sparse crystal flakes break the silhouette of the boundary strata;
        // their count is capped and they are instanced on a single layer.
        for (int32 I = -3; I <= 3; ++I)
        {
            const float Y = I * 410.f;
            V2Add(StrataFlake, FVector(-1880.f, Y + 85.f, 115.f), FVector(.38f, .42f, .72f), FRotator(12, I * 19, -18));
            V2Add(StrataFlake, FVector(1880.f, Y - 60.f, 92.f), FVector(.30f, .36f, .58f), FRotator(-16, I * 23, 24));
        }
    }
    for (TActorIterator<ADirectionalLight> It(Owner->GetWorld()); It; ++It)
    {
        It->SetActorRotation(FRotator(-55, -35, 0));
        It->GetLightComponent()->SetIntensity(4.0f);
        It->GetLightComponent()->SetLightColor(FLinearColor(.78f, .88f, 1.f));
        if (auto* L = Cast<UDirectionalLightComponent>(It->GetLightComponent())) L->SetForwardShadingPriority(1);
    }
    for (TActorIterator<ASkyLight> It(Owner->GetWorld()); It; ++It) It->GetLightComponent()->SetIntensity(.75f);
    auto* Fill = NewObject<UDirectionalLightComponent>(Owner, TEXT("V2WarmFill"));
    Fill->SetIntensity(.65f); Fill->SetForwardShadingPriority(0); Fill->SetCastShadows(false);
    Fill->SetLightColor(FLinearColor(1.f, .63f, .38f)); Fill->SetWorldRotation(FRotator(-30, 150, 0));
    Owner->AddInstanceComponent(Fill); Fill->RegisterComponent();
    auto* PP = NewObject<UPostProcessComponent>(Owner, TEXT("V2RelicGrade"));
    PP->bUnbound = true; PP->Priority = 11;
    PP->Settings.bOverride_AutoExposureBias = true; PP->Settings.AutoExposureBias = 0;
    PP->Settings.bOverride_BloomIntensity = true; PP->Settings.BloomIntensity = .25f;
    PP->Settings.bOverride_VignetteIntensity = true; PP->Settings.VignetteIntensity = .22f;
    PP->Settings.bOverride_MotionBlurAmount = true; PP->Settings.MotionBlurAmount = 0;
    Owner->AddInstanceComponent(PP); PP->RegisterComponent();

    // Characters are spawned by the scenario runner after this presentation
    // actor is built. A short bounded pass attaches v2.2 role dressing after
    // spawn, then stops; it never participates in gameplay or per-frame logic.
    if (UWorld* World = Owner->GetWorld())
    {
        const TWeakObjectPtr<UWorld> WeakWorld(World);
        const TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
        const double Started = World->GetTimeSeconds();
        World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda(
            [WeakWorld, Handle, Started]()
            {
                UWorld* W = WeakWorld.Get();
                if (!W) return;
                ApplyV22CharacterStyle(W);
                if (W->GetTimeSeconds() - Started > 8.0)
                    W->GetTimerManager().ClearTimer(*Handle);
            }), .25f, true);
    }
}
void V2Event(UObject* Context, const TCHAR* Name, const FVector& Location)
{
    const bool Overclock = FCString::Strcmp(Name, TEXT("S_Overclock")) == 0;
    if (!Overclock && FCString::Strcmp(Name, TEXT("S_Charge")) != 0) return;
    UWorld* World = Context ? Context->GetWorld() : nullptr;
    AActor* Owner = Cast<AActor>(Context);
    if (!World || !Owner || !FApp::CanEverRender() || FParse::Param(FCommandLine::Get(), TEXT("NullRHI"))) return;
    auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Aegis/V2/Materials/M_V2Energy.M_V2Energy"));
    static TArray<TWeakObjectPtr<UInstancedStaticMeshComponent>> Pool;
    Pool.RemoveAll([](const TWeakObjectPtr<UInstancedStaticMeshComponent>& Item) { return !Item.IsValid(); });
    if (Pool.Num() >= 8)
    {
        if (Pool[0].IsValid()) Pool[0]->DestroyComponent();
        Pool.RemoveAt(0);
    }
    auto* C = V2Layer(Owner, TEXT(""), Cube, Material,
                      Overclock ? FLinearColor(1.f, .47f, .08f) : FLinearColor(.15f, .85f, 1.f), true);
    if (!C) return;
    Pool.Add(C);
    const int32 Count = Overclock ? 32 : 16;
    const FVector Center = Overclock ? FVector(Location.X, Location.Y, 12) : Location;
    for (int32 I = 0; I < Count; ++I)
    {
        const float A = 2 * PI * I / Count;
        const float Radius = Overclock ? 252.f : 28.f;
        V2Add(C, Center + FVector(FMath::Cos(A) * Radius, FMath::Sin(A) * Radius, 0),
              FVector(Overclock ? .30f : .12f, .025f, .035f), FRotator(0, FMath::RadiansToDegrees(A) + 90, 0));
    }
    const float Started = World->GetTimeSeconds();
    const TWeakObjectPtr<UWorld> WeakWorld(World);
    const TWeakObjectPtr<UInstancedStaticMeshComponent> WeakComponent(C);
    const TWeakObjectPtr<AAegisPortfolio> Portfolio(AAegisPortfolio::Find(World));
    const TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
    World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda(
        [WeakWorld, WeakComponent, Portfolio, Handle, Started, Overclock, Center, Count]()
        {
            if (!WeakWorld.IsValid()) return;
            auto* W = WeakWorld.Get();
            auto* Component = WeakComponent.Get();
            const float Age = W->GetTimeSeconds() - Started;
            if (!Component || Age >= (Overclock ? 6.f : .32f) ||
                (Overclock && (!Portfolio.IsValid() || Portfolio->GetOverclockRemaining() <= 0)))
            {
                if (Component) Component->DestroyComponent();
                W->GetTimerManager().ClearTimer(*Handle);
                return;
            }
            for (int32 I = 0; I < Count; ++I)
            {
                const float A = 2 * PI * I / Count + (Overclock ? Age * .16f : 0.f);
                const float R = Overclock ? 252.f : 28.f + Age * 150.f;
                const float Width = Overclock ? .025f + .01f * FMath::Sin(Age * 6) : .03f * (1 - Age / .32f);
                Component->UpdateInstanceTransform(I, FTransform(FRotator(0, FMath::RadiansToDegrees(A) + 90, 0),
                    Center + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 0),
                    FVector(Overclock ? .30f : .12f, Width, .035f)), true, I == Count - 1, true);
            }
        }), .033f, true);
}
}
bool V2Enabled()
{
    return FParse::Param(FCommandLine::Get(), TEXT("AegisV2"));
}
bool V23Enabled()
{
    return V2Enabled() && FParse::Param(FCommandLine::Get(), TEXT("AegisV23"));
}
bool Enabled()
{
    return V2Enabled() || FParse::Param(FCommandLine::Get(), TEXT("AegisPortfolio"));
}
void Sound(UObject* Context, const TCHAR* Name, const FVector& Location, float Volume)
{
    if (!Enabled() || !Context)
        return;
    if (V2Enabled()) V2Event(Context, Name, Location);
    UWorld* World = Context->GetWorld();
    if (!World) return;
    FString ActualName(Name);
    if (V2Enabled())
    {
        // Variants are part of the authored WAVs, not randomized playback pitch.
        // Each world/role has its own deterministic sequence; no gameplay RNG is touched.
        static TWeakObjectPtr<UWorld> SequenceWorld;
        static int32 ShotSequence[3] = {0, 0, 0};
        if (SequenceWorld.Get() != World)
        { SequenceWorld = World; ShotSequence[0] = ShotSequence[1] = ShotSequence[2] = 0; }
        if (ActualName == TEXT("S_Shot"))
        {
            const auto* Character = Cast<AAegisCharacter>(Context);
            const int32 Role = Cast<AAegisPlayerCharacter>(Context) ? 0 :
                              (Character && Character->Team == EAegisTeam::Player ? 1 : 2);
            const TCHAR* Prefix[] = {TEXT("S_PlayerShot"), TEXT("S_AllyShot"), TEXT("S_EnemyShot")};
            const int32 Variant = ShotSequence[Role]++ % 3 + 1;
            ActualName = FString::Printf(TEXT("%s%02d"), Prefix[Role], Variant);
        }
        else if (ActualName == TEXT("S_Impact")) ActualName = TEXT("S_CrystalImpact");
        else if (ActualName == TEXT("S_Break")) ActualName = TEXT("S_CrystalBreak");
        else if (ActualName == TEXT("S_Pulse")) ActualName = TEXT("S_PulseV21");
        else if (ActualName == TEXT("S_Repair")) ActualName = TEXT("S_RepairV21");
        else if (ActualName == TEXT("S_Upgrade")) ActualName = TEXT("S_UpgradeV21");
        else if (ActualName == TEXT("S_Charge")) ActualName = TEXT("S_ChargeV21");
        else if (ActualName == TEXT("S_Overclock")) ActualName = TEXT("S_OverclockV21");
    }
    const FString Path = FString::Printf(TEXT("%s/%s.%s"),
        V2Enabled() ? TEXT("/Game/Aegis/V21/Audio") : TEXT("/Game/Aegis/Audio"), *ActualName, *ActualName);
    auto* Asset = LoadObject<USoundBase>(nullptr, *Path);
    if (!Asset)
    { UE_LOG(LogTemp, Error, TEXT("AEGIS_SOUND_MISSING asset=%s"), *Path); return; }
    // Short one-shots have an explicit per-world cap. Cull completed components,
    // then drop the newest request instead of creating unbounded sound objects.
    struct FSoundLease { TWeakObjectPtr<UWorld> World; double Until; };
    static TArray<FSoundLease> Leases;
    const double Now = World->GetTimeSeconds();
    Leases.RemoveAll([World, Now](const FSoundLease& Lease)
    { return Lease.World.Get() != World || Lease.Until <= Now; });
    if (Leases.Num() >= 16) return;
    static TArray<TWeakObjectPtr<UAudioComponent>> Voices;
    Voices.RemoveAll([World](const TWeakObjectPtr<UAudioComponent>& Voice)
    { return !Voice.IsValid() || Voice->GetWorld() != World || !Voice->IsPlaying(); });
    if (Voices.Num() >= 16) return;
    // Logical leases also apply with -nosound, so the offline capture cannot issue
    // more simultaneous requests merely because no hardware voices were created.
    Leases.Add({World, Now + Asset->GetDuration()});
    if (auto* Voice = UGameplayStatics::SpawnSoundAtLocation(Context, Asset, Location, FRotator::ZeroRotator,
                                                            Volume, 1.f, 0, nullptr, nullptr, true))
        Voices.Add(Voice);
    // Event = issued playback request, not a claim about hardware output. The same
    // exact source/volume/pitch is available when a -nosound capture is remixed.
    if (FParse::Param(FCommandLine::Get(), TEXT("AegisPortfolioCapture")))
        for (TActorIterator<AAegisPortfolioCapture> It(World); It; ++It)
            It->RecordSound(*ActualName, Location, Volume, 1.f, *Path);
}
void BuildArena(AActor* Owner)
{
    if (!Owner || !FApp::CanEverRender() || FParse::Param(FCommandLine::Get(), TEXT("NullRHI")))
        return;
    if (V2Enabled()) { BuildV2Arena(Owner); AAegisPortfolioMusic::Ensure(Owner); return; }
    auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    auto* Metal = LoadObject<UMaterialInterface>(
        nullptr, TEXT("/Game/Aegis/Materials/M_PortfolioMetal.M_PortfolioMetal"));
    auto* Glow = LoadObject<UMaterialInterface>(
        nullptr, TEXT("/Game/Aegis/Materials/M_PortfolioGlow.M_PortfolioGlow"));
    if (!Cube || !Cylinder || !Metal || !Glow)
        return;
    auto Layer = [&](const TCHAR* Name, FLinearColor Tint, bool Emissive = false, bool Round = false)
    {
        auto* C = NewObject<UInstancedStaticMeshComponent>(Owner, Name);
        C->SetStaticMesh(Round ? Cylinder : Cube);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetCanEverAffectNavigation(false);
        C->SetCastShadow(!Emissive);
        Owner->AddInstanceComponent(C);
        C->RegisterComponent();
        auto* M = UMaterialInstanceDynamic::Create(Emissive ? Glow : Metal, Owner);
        M->SetVectorParameterValue(TEXT("Tint"), Tint);
        C->SetMaterial(0, M);
        return C;
    };
    auto* Floor = Layer(TEXT("PrismFloor"), FLinearColor(0.13f, 0.20f, 0.25f));
    auto* FloorAlt = Layer(TEXT("PrismFloorAlt"), FLinearColor(0.16f, 0.24f, 0.29f));
    auto* Dark = Layer(TEXT("PrismStructure"), FLinearColor(0.025f, 0.06f, 0.085f));
    auto* Panel = Layer(TEXT("PrismIvory"), FLinearColor(0.62f, 0.69f, 0.68f));
    auto* Copper = Layer(TEXT("PrismCopper"), FLinearColor(0.52f, 0.25f, 0.08f));
    auto* Neon = Layer(TEXT("PrismMintLight"), FLinearColor(0.07f, 0.8f, 0.62f), true);
    auto* Amber = Layer(TEXT("PrismAmberLight"), FLinearColor(1.0f, 0.36f, 0.035f), true);
    auto* Hub = Layer(TEXT("PrismHub"), FLinearColor(0.075f, 0.14f, 0.17f), false, true);
    auto* HubMark = Layer(TEXT("PrismHubMark"), FLinearColor(0.20f, 0.34f, 0.33f));
    auto Add = [](UInstancedStaticMeshComponent* M, FVector P, FVector S, FRotator R = FRotator::ZeroRotator)
    { M->AddInstance(FTransform(R, P, S), true); };
    // All scenery is cosmetic. The original collision and connected navigation are unchanged.
    Add(Dark, FVector(0, 0, -95), FVector(95, 85, 1.0f));
    Add(Dark, FVector(0, 0, -42), FVector(41, 31, 0.8f));
    for (int X = -3; X <= 3; ++X)
        for (int Y = -2; Y <= 2; ++Y)
        {
            Add((X + Y) % 2 ? Floor : FloorAlt, FVector(X * 550, Y * 550, 2), FVector(5.42f, 5.42f, 0.025f));
            for (int Side : {-1, 1})
                Add(Dark, FVector(X * 550 + Side * 252, Y * 550 - 238, 4), FVector(0.06f, 0.35f, 0.04f));
        }
    for (int Side : {-1, 1})
    {
        Add(Panel, FVector(0, Side * 1490, 15), FVector(40, 0.48f, 0.34f));
        Add(Dark, FVector(Side * 2010, 0, 35), FVector(0.5f, 30.3f, 0.75f));
        Add(Neon, FVector(0, Side * 1465, 35), FVector(38.6f, 0.038f, 0.035f));
        Add(Neon, FVector(Side * 1980, 0, 77), FVector(0.035f, 28.5f, 0.035f));
        for (int I = -4; I <= 4; ++I)
        {
            Add(Dark, FVector(I * 410, Side * 1550, 38), FVector(0.22f, 1.0f, 0.85f));
            Add(Amber, FVector(I * 410, Side * 1490, 37), FVector(0.6f, 0.2f, 0.04f));
            Add(Dark, FVector(Side * 2260, I * 350, -10), FVector(3.6f, 2.3f, 1.4f));
            Add(Copper, FVector(Side * 2220, I * 350, 66), FVector(2.9f, 0.28f, 0.13f));
            Add(Neon, FVector(Side * 2130, I * 350, 72), FVector(0.10f, 1.55f, 0.025f));
        }
    }
    const FVector Covers[] = {FVector(-300, 400, 301), FVector(300, -400, 301), FVector(600, 600, 301),
                              FVector(-700, -600, 301)};
    for (int I = 0; I < 4; ++I)
    {
        const float W = I < 2 ? 3.6f : 2.8f;
        Add(Dark, Covers[I] + FVector(0, 0, 3), FVector(W + 0.10f, W + 0.10f, 0.12f));
        Add(Panel, Covers[I] + FVector(0, 0, 11), FVector(W - 0.20f, W - 0.20f, 0.045f));
        for (int Side : {-1, 1})
        {
            Add(Copper, Covers[I] + FVector(Side * (W * 50 + 3), 0, -100), FVector(0.14f, W + 0.1f, 1.15f));
            Add(Dark, Covers[I] + FVector(0, Side * (W * 50 + 3), -85), FVector(W, 0.08f, 1.75f));
            Add(Neon, Covers[I] + FVector(0, Side * (W * 50 + 8), -23), FVector(W - 0.4f, 0.038f, 0.08f));
        }
        for (int Slot = -2; Slot <= 2; ++Slot)
            Add(Dark, Covers[I] + FVector(Slot * 32, 0, 15), FVector(0.13f, W * 0.55f, 0.025f));
        Add(Amber, Covers[I] + FVector(W * 32, -W * 31, 17), FVector(0.32f, 0.32f, 0.025f));
    }
    const FVector Nodes[] = {FVector(-500, -1000, 5), FVector(850, -850, 5), FVector(0, 1000, 5),
                             FVector(-1250, 0, 5)};
    for (int I = 0; I < 4; ++I)
    {
        Add(Hub, Nodes[I], FVector(4.7f, 4.7f, 0.03f));
        for (int Seg = 0; Seg < 24; ++Seg)
        {
            const float A = 2 * PI * Seg / 24;
            Add(HubMark, Nodes[I] + FVector(FMath::Cos(A) * 210, FMath::Sin(A) * 210, 3),
                FVector(0.24f, 0.035f, 0.02f), FRotator(0, FMath::RadiansToDegrees(A) + 90, 0));
        }
        Add(Copper, Nodes[I] + FVector(0, 0, 4), FVector(0.16f, 1.0f, 0.02f));
        Add(Copper, Nodes[I] + FVector(0, 0, 4), FVector(1.0f, 0.16f, 0.02f));
    }
    auto Text = [&](const TCHAR* Label, FVector P, float Size, FColor C, FRotator R)
    {
        auto* T = NewObject<UTextRenderComponent>(Owner);
        T->SetText(FText::FromString(Label));
        T->SetTextRenderColor(C);
        T->SetWorldSize(Size);
        T->SetHorizontalAlignment(EHTA_Center);
        T->SetVerticalAlignment(EVRTA_TextCenter);
        T->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        T->SetWorldLocation(P);
        T->SetWorldRotation(R);
        Owner->AddInstanceComponent(T);
        T->RegisterComponent();
    };
    Text(TEXT("PRISM / 07"), FVector(0, 1340, 8), 115, FColor(174, 224, 218), FRotator(90, 180, 0));
    Text(TEXT("A E G I S"), FVector(600, -1310, 8), 80, FColor(164, 193, 193), FRotator(90, 180, 0));
    for (TActorIterator<ADirectionalLight> It(Owner->GetWorld()); It; ++It)
    {
        It->SetActorRotation(FRotator(-58, -35, 0));
        It->GetLightComponent()->SetIntensity(4.0f);
        It->GetLightComponent()->SetLightColor(FLinearColor(0.84f, 0.92f, 1.0f));
        if (auto* KeyLight = Cast<UDirectionalLightComponent>(It->GetLightComponent()))
            KeyLight->SetForwardShadingPriority(1);
    }
    for (TActorIterator<ASkyLight> It(Owner->GetWorld()); It; ++It)
        It->GetLightComponent()->SetIntensity(0.6f);
    auto* Fill = NewObject<UDirectionalLightComponent>(Owner, TEXT("PrismFill"));
    Fill->SetIntensity(0.55f);
    // Keep the scene key light as the unique forward/translucent light selection.
    Fill->SetForwardShadingPriority(0);
    Fill->SetLightColor(FLinearColor(0.53f, 0.75f, 0.85f));
    Fill->SetCastShadows(false);
    Fill->SetWorldRotation(FRotator(-35, 145, 0));
    Owner->AddInstanceComponent(Fill);
    Fill->RegisterComponent();
    auto* PP = NewObject<UPostProcessComponent>(Owner, TEXT("PrismGrade"));
    PP->bUnbound = true;
    PP->Priority = 10;
    PP->Settings.bOverride_AutoExposureBias = true;
    PP->Settings.AutoExposureBias = 0.0f;
    PP->Settings.bOverride_BloomIntensity = true;
    PP->Settings.BloomIntensity = 0.35f;
    PP->Settings.bOverride_VignetteIntensity = true;
    PP->Settings.VignetteIntensity = 0.20f;
    PP->Settings.bOverride_MotionBlurAmount = true;
    PP->Settings.MotionBlurAmount = 0;
    PP->Settings.bOverride_ColorSaturation = true;
    PP->Settings.ColorSaturation = FVector4(0.90f, 0.96f, 1.0f, 1.0f);
    Owner->AddInstanceComponent(PP);
    PP->RegisterComponent();
}
} // namespace AegisPortfolioPresentation

void AAegisCharacter::BuildPortfolioPresentation()
{
    const auto* Bot = Cast<AAegisAICharacter>(this);
    const bool Friend = Team == EAegisTeam::Player, Companion = Bot && Bot->bCompanion;
    const bool Elite = Bot && Bot->bElite;
    const bool Heavy = Bot && Bot->EnemyRole == EAegisEnemyRole::Suppressor && !Friend;
    const bool Flank = Bot && Bot->EnemyRole == EAegisEnemyRole::Flanker && !Friend;
    IdentityColor = Friend
                        ? (Companion ? FLinearColor(0.15f, 0.59f, 0.43f) : FLinearColor(0.58f, 0.75f, 0.79f))
                        : (Elite   ? FLinearColor(0.46f, 0.10f, 0.35f)
                           : Flank ? FLinearColor(0.71f, 0.33f, 0.055f)
                                   : FLinearColor(0.57f, 0.13f, 0.09f));
    if (AegisPortfolioPresentation::V2Enabled())
    {
        auto* Armor = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Aegis/V2/Meshes/SM_V2Armor.SM_V2Armor"));
        auto* Shard = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Aegis/V2/Meshes/SM_V2Shard.SM_V2Shard"));
        auto* Ceramic = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Aegis/V2/Materials/M_V2Ceramic.M_V2Ceramic"));
        auto* Basalt = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Aegis/V2/Materials/M_V2Basalt.M_V2Basalt"));
        auto* Energy = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Aegis/V2/Materials/M_V2Energy.M_V2Energy"));
        if (Armor && Shard && Ceramic && Basalt && Energy)
        {
            IdentityColor = Friend ? (Companion ? FLinearColor(.10f, .64f, .46f) : FLinearColor(.66f, .78f, .83f))
                : (Elite ? FLinearColor(.45f, .12f, .52f) : Flank ? FLinearColor(.76f, .38f, .075f) : FLinearColor(.59f, .16f, .12f));
            ActorMaterial = Energy;
            BodyMaterial = UMaterialInstanceDynamic::Create(Ceramic, this);
            BodyMaterial->SetVectorParameterValue(TEXT("Tint"), IdentityColor);
            AccentMaterial = UMaterialInstanceDynamic::Create(Basalt, this);
            AccentMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor(.018f, .038f, .055f));
            TracerMaterial = UMaterialInstanceDynamic::Create(Energy, this);
            TracerMaterial->SetVectorParameterValue(TEXT("Tint"), Friend ? FLinearColor(.035f, .88f, .70f) : FLinearColor(1.f, .23f, .06f));
            Body->SetStaticMesh(Armor); Body->SetMaterial(0, BodyMaterial);
            Body->SetRelativeLocation(FVector(0, 0, 0));
            Body->SetRelativeScale3D(FVector(Companion ? .76f : Heavy ? .74f : .56f, Heavy ? .93f : .57f, Companion ? .30f : .58f));
            const float FootZ = -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() + 4;
            for (int Side : {-1, 1})
            {
                if (Companion)
                {
                    // A low hovering split-wing drone stays distinct from every biped silhouette.
                    PortfolioLegs.Add(AddDecoration(Armor, FVector(-8, Side * 43, 1), FVector(.64f, .22f, .20f),
                        FRotator(0, Side * -12, Side * 18), BodyMaterial));
                    AddDecoration(Shard, FVector(-22, Side * 37, -16), FVector(.18f, .18f, .35f), FRotator(180, 0, 0), TracerMaterial);
                }
                else
                {
                    PortfolioLegs.Add(AddDecoration(Armor, FVector(-3, Side * 21, -48), FVector(.26f, .22f, .55f), FRotator::ZeroRotator, AccentMaterial));
                    AddDecoration(Armor, FVector(8, Side * 21, FootZ + 8), FVector(.42f, .28f, .17f), FRotator::ZeroRotator, BodyMaterial);
                    AddDecoration(Armor, FVector(0, Side * (Heavy ? 49 : 36), 14),
                        FVector(Heavy ? .62f : .38f, Heavy ? .36f : .28f, .35f), FRotator(0, 0, Side * 18), BodyMaterial);
                    AddDecoration(Armor, FVector(8, Side * (Heavy ? 50 : 36), -10), FVector(.30f, .20f, .36f), FRotator(0, 0, Side * 6), AccentMaterial);
                }
                if (!Friend)
                    AddDecoration(Shard, FVector(-20, Side * (Heavy ? 45 : 31), Flank ? 19 : 35),
                        FVector(.20f, .22f, Flank ? .70f : .42f), FRotator(Flank ? -48 : -15, 0, Side * 25), BodyMaterial);
            }
            AddDecoration(Armor, FVector(0, 0, Companion ? 28 : 43), FVector(.37f, .36f, .28f), FRotator::ZeroRotator, AccentMaterial);
            AddDecoration(Armor, FVector(22, 0, Companion ? 30 : 44), FVector(.045f, .28f, .075f), FRotator::ZeroRotator, TracerMaterial);
            AddDecoration(Armor, FVector(-31, 0, 8), FVector(.20f, .40f, .52f), FRotator::ZeroRotator, AccentMaterial);
            auto* Core = AddDecoration(Shard, FVector(30, 0, 6), FVector(.14f, .14f, .27f), FRotator(90, 0, 0), TracerMaterial);
            Core->ComponentTags.Add(TEXT("V2Core"));
            auto* Gun = AddDecoration(Armor, FVector(43, Heavy ? -25 : -18, 4), FVector(.80f, .17f, .20f), FRotator::ZeroRotator, AccentMaterial);
            Gun->ComponentTags.Add(TEXT("V2Gun"));
            AddDecoration(Armor, FVector(79, Heavy ? -25 : -18, 4), FVector(.10f, .18f, .10f), FRotator::ZeroRotator, TracerMaterial);
            if (Heavy) AddDecoration(Armor, FVector(43, 25, 4), FVector(.80f, .17f, .20f), FRotator::ZeroRotator, AccentMaterial);
            if (Elite)
                for (int Side : {-1, 1}) AddDecoration(Shard, FVector(-10, Side * 40, 59), FVector(.25f, .25f, .67f), FRotator(0, 0, Side * 20), TracerMaterial);
            for (int I = 0; I < 12; ++I)
            {
                const float A = 2 * PI * I / 12;
                AddDecoration(CubeMesh, FVector(FMath::Cos(A) * 60, FMath::Sin(A) * 60, FootZ),
                    FVector(.17f, .032f, .025f), FRotator(0, FMath::RadiansToDegrees(A) + 90, 0), TracerMaterial);
            }
            TracerTimers.SetNum(3);
            for (int I = 0; I < 3; ++I)
            {
                auto* T = AddDecoration(CubeMesh, FVector::ZeroVector, FVector::OneVector, FRotator::ZeroRotator, TracerMaterial);
                T->SetAbsolute(true, true, true); T->SetVisibility(false); Tracers.Add(T);
            }
            GetWorldTimerManager().SetTimer(PortfolioPoseTimer, this, &AAegisCharacter::UpdatePortfolioPose, .033f, true);
            return;
        }
        UE_LOG(LogTemp, Error, TEXT("AEGIS_V2_CHARACTER_MISSING_ASSETS"));
    }
    auto* Metal = LoadObject<UMaterialInterface>(
        nullptr, TEXT("/Game/Aegis/Materials/M_PortfolioMetal.M_PortfolioMetal"));
    auto* Glow = LoadObject<UMaterialInterface>(
        nullptr, TEXT("/Game/Aegis/Materials/M_PortfolioGlow.M_PortfolioGlow"));
    if (!Metal)
        Metal = ActorMaterial;
    if (!Glow)
        Glow = ActorMaterial;
    ActorMaterial = Glow;
    BodyMaterial = UMaterialInstanceDynamic::Create(Metal, this);
    BodyMaterial->SetVectorParameterValue(TEXT("Tint"), IdentityColor);
    AccentMaterial = UMaterialInstanceDynamic::Create(Metal, this);
    AccentMaterial->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.018f, 0.043f, 0.060f));
    TracerMaterial = UMaterialInstanceDynamic::Create(Glow, this);
    TracerMaterial->SetVectorParameterValue(TEXT("Tint"), Friend ? FLinearColor(0.04f, 1.0f, 0.70f)
                                                                 : FLinearColor(1.0f, 0.18f, 0.03f));
    Body->SetStaticMesh(CubeMesh);
    Body->SetMaterial(0, BodyMaterial);
    Body->SetRelativeLocation(FVector(0, 0, Companion ? 3 : 0));
    Body->SetRelativeScale3D(
        FVector(Heavy ? 0.82f : 0.60f, Heavy ? 0.90f : 0.57f, Companion ? 0.38f : 0.53f));
    Body->SetRelativeRotation(FRotator(0, 0, 0));
    const float FootZ = -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() + 4;
    for (int Side : {-1, 1})
    {
        if (Companion)
        {
            AddDecoration(CylinderMesh, FVector(0, Side * 48, -12), FVector(0.34f, 0.34f, 0.16f),
                          FRotator(0, 0, 90), AccentMaterial);
            PortfolioLegs.Add(AddDecoration(CubeMesh, FVector(-4, Side * 44, 4), FVector(0.64f, 0.16f, 0.20f),
                                            FRotator(0, 0, Side * 12), BodyMaterial));
            AddDecoration(CubeMesh, FVector(-29, Side * 42, -6), FVector(0.14f, 0.12f, 0.12f),
                          FRotator::ZeroRotator, TracerMaterial);
        }
        else
        {
            PortfolioLegs.Add(AddDecoration(CubeMesh, FVector(-3, Side * 21, -48),
                                            FVector(0.27f, 0.22f, 0.56f), FRotator::ZeroRotator,
                                            AccentMaterial));
            AddDecoration(CubeMesh, FVector(4, Side * 21, FootZ + 8), FVector(0.44f, 0.28f, 0.18f),
                          FRotator::ZeroRotator, BodyMaterial);
            AddDecoration(CubeMesh, FVector(0, Side * (Heavy ? 52 : 38), 10),
                          FVector(Heavy ? 0.56f : 0.36f, 0.30f, 0.35f), FRotator(0, 0, Side * 12),
                          BodyMaterial);
            AddDecoration(CubeMesh, FVector(6, Side * (Heavy ? 54 : 38), -13), FVector(0.28f, 0.20f, 0.36f),
                          FRotator(0, 0, Side * 8), AccentMaterial);
        }
    }
    AddDecoration(CubeMesh, FVector(0, 0, 40), FVector(0.40f, 0.38f, 0.30f), FRotator::ZeroRotator,
                  AccentMaterial);
    AddDecoration(CubeMesh, FVector(22, 0, 42), FVector(0.035f, 0.29f, 0.085f), FRotator::ZeroRotator,
                  TracerMaterial);
    AddDecoration(CubeMesh, FVector(-32, 0, 6), FVector(0.18f, 0.42f, 0.56f), FRotator::ZeroRotator,
                  AccentMaterial);
    AddDecoration(CubeMesh, FVector(40, Heavy ? -24 : -17, 4), FVector(0.78f, 0.19f, 0.21f),
                  FRotator::ZeroRotator, AccentMaterial);
    AddDecoration(CubeMesh, FVector(79, Heavy ? -24 : -17, 4), FVector(0.09f, 0.21f, 0.10f),
                  FRotator::ZeroRotator, TracerMaterial);
    AddDecoration(CubeMesh, FVector(31, 0, 0), FVector(0.04f, 0.15f, 0.28f), FRotator::ZeroRotator,
                  TracerMaterial);
    if (Heavy)
        AddDecoration(CubeMesh, FVector(40, 24, 4), FVector(0.78f, 0.19f, 0.21f), FRotator::ZeroRotator,
                      AccentMaterial);
    if (Flank)
        AddDecoration(ConeMesh, FVector(-28, 0, 24), FVector(0.36f, 0.46f, 0.80f), FRotator(-25, 0, 0),
                      BodyMaterial);
    if (Elite)
        for (int Side : {-1, 1})
            AddDecoration(ConeMesh, FVector(-12, Side * 44, 55), FVector(0.24f, 0.24f, 0.70f),
                          FRotator(0, 0, Side * 18), TracerMaterial);
    // Thin broken identity ring leaves the feet and ground contact visible.
    for (int I = 0; I < 12; ++I)
    {
        float A = I * 2 * PI / 12;
        AddDecoration(CubeMesh, FVector(FMath::Cos(A) * 59, FMath::Sin(A) * 59, FootZ),
                      FVector(0.17f, 0.035f, 0.025f), FRotator(0, FMath::RadiansToDegrees(A) + 90, 0),
                      TracerMaterial);
    }
    TracerTimers.SetNum(3);
    for (int I = 0; I < 3; ++I)
    {
        auto* T = AddDecoration(CubeMesh, FVector::ZeroVector, FVector::OneVector, FRotator::ZeroRotator,
                                TracerMaterial);
        T->SetAbsolute(true, true, true);
        T->SetVisibility(false);
        Tracers.Add(T);
    }
    GetWorldTimerManager().SetTimer(PortfolioPoseTimer, this, &AAegisCharacter::UpdatePortfolioPose, 0.033f,
                                    true);
}
void AAegisCharacter::UpdatePortfolioPose()
{
    if (!Health->IsAlive())
    {
        for (const auto& Decoration : Decorations)
            if (Decoration)
                Decoration->SetVisibility(false);
        Body->SetRelativeRotation(FRotator(0, 0, 72));
        Body->SetRelativeLocation(FVector(0, 0, -52));
        GetWorldTimerManager().ClearTimer(PortfolioPoseTimer);
        return;
    }
    const auto* Bot = Cast<AAegisAICharacter>(this);
    const bool Companion = Bot && Bot->bCompanion;
    const float T = GetWorld()->GetTimeSeconds(),
                Speed = FMath::Clamp(GetVelocity().Size2D() / 420.f, 0.f, 1.f);
    if (AegisPortfolioPresentation::V2Enabled())
    {
        const auto* Player = Cast<AAegisPlayerCharacter>(this);
        const float Charge = Player && Player->IsCharging() ? Player->GetChargeFraction() : 0.f;
        bool ShotVisible = false;
        for (const auto& Tracer : Tracers) if (Tracer && Tracer->IsVisible()) { ShotVisible = true; break; }
        const float Recoil = ShotVisible ? 1.f : 0.f;
        const float Ready = GetShotWindupRemaining() > 0 ? 1.f : Charge;
        const FVector LocalVelocity = GetActorTransform().InverseTransformVectorNoScale(GetVelocity());
        Body->SetRelativeLocation(FVector(-2 * Recoil, 0, Companion ? 5 + FMath::Sin(T * 3) * 3 : FMath::Abs(FMath::Sin(T * 12)) * 2 * Speed));
        Body->SetRelativeRotation(FRotator(-FMath::Clamp(LocalVelocity.X / 70.f, -5.f, 5.f) - Ready * 3 + Recoil * 4,
            0, FMath::Clamp(LocalVelocity.Y / 95.f, -4.f, 4.f)));
        for (int I = 0; I < PortfolioLegs.Num(); ++I)
            if (PortfolioLegs[I])
                PortfolioLegs[I]->SetRelativeRotation(Companion
                    ? FRotator(0, (I ? 1 : -1) * -12, (I ? 1 : -1) * (18 + FMath::Sin(T * 3) * 3 + Speed * 6))
                    : FRotator(FMath::Sin(T * 12 + I * PI) * 25 * Speed, 0, 0));
        for (const auto& Part : Decorations)
        {
            if (!Part) continue;
            if (Part->ComponentHasTag(TEXT("V2Gun")))
            {
                const bool Heavy = Bot && Bot->EnemyRole == EAegisEnemyRole::Suppressor && Team != EAegisTeam::Player;
                Part->SetRelativeLocation(FVector(43 - Recoil * 6, Heavy ? -25 : -18, 4 + Ready * 3));
                Part->SetRelativeRotation(FRotator(Ready * -3 + Recoil * 5, 0, 0));
            }
            else if (Part->ComponentHasTag(TEXT("V2Core")))
                Part->SetRelativeScale3D(FVector(.14f, .14f, .27f) * (1 + Charge * .40f));
        }
        return;
    }
    Body->SetRelativeLocation(
        FVector(0, 0, Companion ? 6 + FMath::Sin(T * 3) * 4 : FMath::Abs(FMath::Sin(T * 12)) * 3 * Speed));
    for (int I = 0; I < PortfolioLegs.Num(); ++I)
        if (PortfolioLegs[I])
            PortfolioLegs[I]->SetRelativeRotation(
                Companion ? FRotator(0, 0, (I ? 1 : -1) * (12 + FMath::Sin(T * 3) * 4))
                          : FRotator(FMath::Sin(T * 12 + I * PI) * 24 * Speed, 0, 0));
}
