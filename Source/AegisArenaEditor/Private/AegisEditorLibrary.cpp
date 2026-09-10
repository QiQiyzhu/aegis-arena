#include "AegisEditorLibrary.h"
#include "AegisAIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "BehaviorTreeGraph.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/Contexts/EnvQueryContext_Querier.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_SimpleGrid.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Distance.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Trace.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Pathfinding.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraph/EdGraphSchema.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationSystem.h"
#include "ActorFactories/ActorFactory.h"
#include "Builders/CubeBuilder.h"
#include "EngineUtils.h"
#include "Engine/EngineTypes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace
{
template <typename T> T* NewAsset(const TCHAR* Name)
{
    const FString PackageName = FString(TEXT("/Game/Aegis/AI/")) + Name;
    if (FPackageName::DoesPackageExist(PackageName))
        return nullptr;
    return NewObject<T>(CreatePackage(*PackageName), Name, RF_Public | RF_Standalone);
}
bool SaveAsset(UObject* Asset)
{
    if (!Asset)
        return false;
    FAssetRegistryModule::AssetCreated(Asset);
    Asset->MarkPackageDirty();
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    return UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args);
}
template <typename T> void Key(UBlackboardData* BB, const TCHAR* Name)
{
    FBlackboardEntry Entry;
    Entry.EntryName = Name;
    Entry.KeyType = NewObject<T>(BB);
    BB->Keys.Add(Entry);
}
UBTDecorator_AegisCondition* Condition(UBehaviorTree* BT, const TCHAR* Name, int32 Expected,
                                       bool Integer = false)
{
    auto* D = NewObject<UBTDecorator_AegisCondition>(BT);
    D->Key = Name;
    D->Expected = Expected;
    D->bInteger = Integer;
    return D;
}
void Branch(UBehaviorTree* BT, UBTCompositeNode* Parent, const TCHAR* Action,
            std::initializer_list<UBTDecorator*> Conditions)
{
    FBTCompositeChild Child;
    auto* Task = NewObject<UBTTask_AegisAction>(BT);
    Task->Action = Action;
    Task->NodeName = Action;
    Child.ChildTask = Task;
    for (auto* D : Conditions)
        Child.Decorators.Add(D);
    Parent->Children.Add(Child);
}
UEnvQuery* Query(const TCHAR* Name, int32 Mode)
{
    auto* Q = NewAsset<UEnvQuery>(Name);
    if (!Q)
        return nullptr;
    auto* Option = NewObject<UEnvQueryOption>(Q);
    auto* Grid = NewObject<UEnvQueryGenerator_SimpleGrid>(Option);
    Grid->GenerateAround = UEnvQueryContext_Querier::StaticClass();
    Grid->GridSize.DefaultValue = 700;
    Grid->SpaceBetween.DefaultValue = 140;
    Grid->ProjectionData.TraceMode = EEnvQueryTrace::Navigation;
    Grid->UpdateNodeVersion();
    Option->Generator = Grid;
    Q->GetOptionsMutable().Add(Option);

    auto* Path = NewObject<UEnvQueryTest_Pathfinding>(Option);
    Path->TestMode = EEnvTestPathfinding::PathExist;
    Path->Context = UEnvQueryContext_Querier::StaticClass();
    Path->TestPurpose = EEnvTestPurpose::Filter;
    Path->BoolValue.DefaultValue = true;
    Option->Tests.Add(Path);

    auto* Trace = NewObject<UEnvQueryTest_Trace>(Option);
    Trace->Context = UEnvQueryContext_AegisThreat::StaticClass();
    Trace->TraceData.TraceMode = EEnvQueryTrace::GeometryByChannel;
    Trace->TraceData.TraceShape = EEnvTraceShape::Line;
    Trace->TraceData.TraceChannel = UEngineTypes::ConvertToTraceType(ECC_GameTraceChannel1);
    Trace->TraceData.SetGeometryOnly();
    Trace->ItemHeightOffset.DefaultValue = 90;
    Trace->ContextHeightOffset.DefaultValue = 30;
    Trace->TestPurpose = Mode == 2 ? EEnvTestPurpose::Score : EEnvTestPurpose::Filter;
    Trace->BoolValue.DefaultValue = Mode != 1; // cover needs occlusion, attack position needs clear LOS
    Option->Tests.Add(Trace);

    auto* Distance = NewObject<UEnvQueryTest_Distance>(Option);
    Distance->DistanceTo = UEnvQueryContext_AegisThreat::StaticClass();
    Distance->TestMode = EEnvTestDistance::Distance2D;
    Distance->TestPurpose = Mode == 1 ? EEnvTestPurpose::FilterAndScore : EEnvTestPurpose::Score;
    Distance->FilterType = EEnvTestFilterType::Range;
    Distance->FloatValueMin.DefaultValue = 300;
    Distance->FloatValueMax.DefaultValue = 900;
    Distance->bDefineReferenceValue = Mode == 1;
    Distance->ReferenceValue.DefaultValue = 650;
    Distance->ScoringFactor.DefaultValue = 1;
    Option->Tests.Add(Distance);
    for (int32 I = 0; I < Option->Tests.Num(); ++I)
    {
        Option->Tests[I]->TestOrder = I;
        // NewObject bypasses the EQS editor's new-node version initialization.
        // Without this, first load applies legacy migrations (including inverting reference scores).
        Option->Tests[I]->UpdateNodeVersion();
    }

    // EQS editor is a plugin in 5.8; use reflected graph class + exported UAIGraph interface.
    UClass* GraphClass =
        LoadClass<UAIGraph>(nullptr, TEXT("/Script/EnvironmentQueryEditor.EnvironmentQueryGraph"));
    if (!GraphClass)
        return nullptr;
    auto* Defaults = GraphClass->GetDefaultObject<UAIGraph>();
    Q->EdGraph = FBlueprintEditorUtils::CreateNewGraph(Q, TEXT("EQSGraph"), GraphClass, Defaults->Schema);
    auto* Graph = CastChecked<UAIGraph>(Q->EdGraph);
    Graph->GetSchema()->CreateDefaultNodesForGraph(*Graph);
    Graph->OnCreated();
    Graph->Initialize();
    Graph->UpdateAsset();
    return Q;
}
} // namespace

bool UAegisEditorLibrary::BuildAIAssets()
{
    // Preflight the whole set before mutation; a partial run is an explicit failure.
    for (const TCHAR* Name :
         {TEXT("BB_Aegis"), TEXT("BT_Aegis"), TEXT("EQS_Cover"), TEXT("EQS_Attack"), TEXT("EQS_Retreat")})
        if (FPackageName::DoesPackageExist(FString(TEXT("/Game/Aegis/AI/")) + Name))
        {
            UE_LOG(LogTemp, Error, TEXT("Aegis asset generation refuses to overwrite %s"), Name);
            return false;
        }
    auto* BB = NewAsset<UBlackboardData>(TEXT("BB_Aegis"));
    for (const TCHAR* Name :
         {TEXT("HasLOS"), TEXT("HasMemory"), TEXT("CriticalHealth"), TEXT("NeedsRecovery"),
          TEXT("NeedsCover"), TEXT("InRange"), TEXT("IsCompanion"), TEXT("QueryPending")})
        Key<UBlackboardKeyType_Bool>(BB, Name);
    Key<UBlackboardKeyType_Int>(BB, TEXT("UtilityAction"));
    Key<UBlackboardKeyType_Object>(BB, TEXT("TargetActor"));
    Key<UBlackboardKeyType_Vector>(BB, TEXT("LastKnownLocation"));
    Key<UBlackboardKeyType_Vector>(BB, TEXT("TacticalPoint"));
    auto* BT = NewAsset<UBehaviorTree>(TEXT("BT_Aegis"));
    BT->BlackboardAsset = BB;
    auto* Root = NewObject<UBTComposite_Sequence>(BT);
    Root->NodeName = TEXT("Observe / choose / wait 200ms");
    Root->Services.Add(NewObject<UBTService_AegisObserve>(BT));
    BT->RootNode = Root;
    auto* Selector = NewObject<UBTComposite_Selector>(BT);
    Selector->NodeName = TEXT("Companion utility / enemy priority");
    FBTCompositeChild SelectChild;
    SelectChild.ChildComposite = Selector;
    Root->Children.Add(SelectChild);
    auto C = [BT](const TCHAR* N, int32 V = 1) { return Condition(BT, N, V); };
    auto U = [BT](int32 V) { return Condition(BT, TEXT("UtilityAction"), V, true); };
    auto* Companion = NewObject<UBTComposite_Selector>(BT);
    Companion->NodeName = TEXT("Companion: utility-authorized actions");
    FBTCompositeChild CompanionChild;
    CompanionChild.ChildComposite = Companion;
    CompanionChild.Decorators.Add(C(TEXT("IsCompanion")));
    Selector->Children.Add(CompanionChild);
    Branch(BT, Companion, TEXT("Recover"), {U(3), C(TEXT("NeedsRecovery"))});
    Branch(BT, Companion, TEXT("Retreat"), {U(3), C(TEXT("HasMemory"))});
    Branch(BT, Companion, TEXT("Support"), {U(2)});
    Branch(BT, Companion, TEXT("Attack"), {U(1), C(TEXT("InRange"))});
    Branch(BT, Companion, TEXT("Chase"), {U(1), C(TEXT("HasMemory"))});
    Branch(BT, Companion, TEXT("Follow"), {});
    FBTCompositeChild CompanionWaitChild;
    auto* CompanionWait = NewObject<UBTTask_Wait>(BT);
    CompanionWait->WaitTime = FValueOrBBKey_Float(0.2f);
    CompanionWait->RandomDeviation = FValueOrBBKey_Float(0.f);
    CompanionWaitChild.ChildTask = CompanionWait;
    Companion->Children.Add(CompanionWaitChild);
    auto* Enemy = NewObject<UBTComposite_Selector>(BT);
    Enemy->NodeName = TEXT("Enemy: priority baseline");
    FBTCompositeChild EnemyChild;
    EnemyChild.ChildComposite = Enemy;
    EnemyChild.Decorators.Add(C(TEXT("IsCompanion"), 0));
    Selector->Children.Add(EnemyChild);
    Branch(BT, Enemy, TEXT("Retreat"), {C(TEXT("CriticalHealth")), C(TEXT("HasMemory"))});
    Branch(BT, Enemy, TEXT("Recover"), {C(TEXT("NeedsRecovery"))});
    Branch(BT, Enemy, TEXT("FindCover"), {C(TEXT("NeedsCover"))});
    Branch(BT, Enemy, TEXT("Attack"), {C(TEXT("InRange"))});
    Branch(BT, Enemy, TEXT("AttackPosition"), {C(TEXT("HasLOS"))});
    Branch(BT, Enemy, TEXT("Investigate"), {C(TEXT("HasMemory"))});
    Branch(BT, Enemy, TEXT("Patrol"), {});
    FBTCompositeChild EnemyWaitChild;
    auto* EnemyWait = NewObject<UBTTask_Wait>(BT);
    EnemyWait->WaitTime = FValueOrBBKey_Float(0.2f);
    EnemyWait->RandomDeviation = FValueOrBBKey_Float(0.f);
    EnemyWaitChild.ChildTask = EnemyWait;
    Enemy->Children.Add(EnemyWaitChild);
    FBTCompositeChild WaitChild;
    auto* Wait = NewObject<UBTTask_Wait>(BT);
    Wait->WaitTime = FValueOrBBKey_Float(0.2f);
    Wait->RandomDeviation = FValueOrBBKey_Float(0.f);
    WaitChild.ChildTask = Wait;
    Root->Children.Add(WaitChild);
    const auto* Defaults = GetDefault<UBehaviorTreeGraph>();
    BT->BTGraph = FBlueprintEditorUtils::CreateNewGraph(BT, TEXT("BTGraph"),
                                                        UBehaviorTreeGraph::StaticClass(), Defaults->Schema);
    auto* Graph = CastChecked<UBehaviorTreeGraph>(BT->BTGraph);
    // AddSubNode and pin linking trigger UpdateAsset. Freeze until the full graph exists;
    // otherwise a partial graph can clear BT->RootNode and orphan not-yet-linked nodes.
    Graph->LockUpdates();
    Graph->GetSchema()->CreateDefaultNodesForGraph(*Graph);
    Graph->OnCreated();
    Graph->Initialize();
    Graph->UnlockUpdates();
    auto* Cover = Query(TEXT("EQS_Cover"), 0);
    auto* Attack = Query(TEXT("EQS_Attack"), 1);
    auto* Retreat = Query(TEXT("EQS_Retreat"), 2);
    return SaveAsset(BB) && SaveAsset(BT) && SaveAsset(Cover) && SaveAsset(Attack) && SaveAsset(Retreat);
}

bool UAegisEditorLibrary::AddNavigationBounds(UWorld* World)
{
    if (!World || World->IsGameWorld())
        return false;
    for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
        return false;
    auto* Volume = World->SpawnActor<ANavMeshBoundsVolume>();
    if (!Volume)
        return false;
    Volume->SetActorLabel(TEXT("Aegis NavMesh Bounds"));
    auto* Builder = NewObject<UCubeBuilder>();
    Builder->X = 4100;
    Builder->Y = 3100;
    Builder->Z = 700;
    UActorFactory::CreateBrushForVolumeActor(Volume, Builder);
    Volume->SetActorLocation(FVector(0, 0, 200));
    if (auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
    {
        Nav->OnNavigationBoundsUpdated(Volume);
        // Dynamic Recast builds when the loaded game world releases its navigation lock.
        // A synchronous Python commandlet can still hold the editor AsyncLoadLock here.
    }
    World->MarkPackageDirty();
    return true;
}

FString UAegisEditorLibrary::DescribeQuery(UEnvQuery* Query)
{
    auto Summary = MakeShared<FJsonObject>();
    Summary->SetNumberField(TEXT("optionCount"), Query ? Query->GetOptions().Num() : 0);
    if (Query && Query->GetOptions().Num() == 1)
    {
        const UEnvQueryOption* Option = Query->GetOptions()[0];
        Summary->SetStringField(TEXT("generator"), GetNameSafe(Option->Generator ? Option->Generator->GetClass() : nullptr));
        TArray<TSharedPtr<FJsonValue>> Tests;
        for (const UEnvQueryTest* Test : Option->Tests)
        {
            Tests.Add(MakeShared<FJsonValueString>(GetNameSafe(Test ? Test->GetClass() : nullptr)));
            if (const auto* Distance = Cast<UEnvQueryTest_Distance>(Test))
                Summary->SetNumberField(TEXT("distanceFactor"), Distance->ScoringFactor.DefaultValue);
        }
        Summary->SetArrayField(TEXT("tests"), Tests);
    }
    FString Json;
    FJsonSerializer::Serialize(Summary, TJsonWriterFactory<>::Create(&Json));
    return Json;
}
