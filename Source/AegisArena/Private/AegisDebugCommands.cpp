#include "AegisAIController.h"
#include "AegisCharacter.h"
#include "AegisLab.h"
#include "BrainComponent.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "DrawDebugHelpers.h"

// No console registrations or developer mutation paths are compiled into Shipping.
#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorldAndArgs
    PauseAI(TEXT("aegis.PauseAI"), TEXT("aegis.PauseAI 1|0"),
            FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
                [](const TArray<FString>& Args, UWorld* World)
                {
                    if (!World || Args.Num() != 1 || (Args[0] != TEXT("0") && Args[0] != TEXT("1")))
                        return;
                    const bool Pause = Args[0] == TEXT("1");
                    for (TActorIterator<AAegisAIController> It(World); It; ++It)
                    {
                        It->bPaused = Pause;
                        if (Pause)
                        {
                            It->StopMovement();
                            if (It->GetBrainComponent())
                                It->GetBrainComponent()->PauseLogic(TEXT("Developer pause"));
                        }
                        else if (It->GetBrainComponent())
                            It->GetBrainComponent()->ResumeLogic(TEXT("Developer resume"));
                    }
                }));
static FAutoConsoleCommandWithWorldAndArgs
    StepAI(TEXT("aegis.StepDecision"),
           TEXT("Evaluate one authorized observation per paused AI; does not advance physics"),
           FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
               [](const TArray<FString>& Args, UWorld* World)
               {
                   (void)Args;
                   if (!World)
                       return;
                   for (TActorIterator<AAegisAIController> It(World); It; ++It)
                       if (It->bPaused)
                       {
                           It->bPaused = false;
                           It->RefreshDecision();
                           It->bPaused = true;
                       }
               }));
static FAutoConsoleCommandWithWorldAndArgs
    ChangePolicy(TEXT("aegis.Policy"), TEXT("aegis.Policy utility|priority"),
                 FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
                     [](const TArray<FString>& Args, UWorld* World)
                     {
                         if (!World || Args.Num() != 1 ||
                             (Args[0] != TEXT("utility") && Args[0] != TEXT("priority")))
                             return;
                         for (TActorIterator<AAegisAIController> It(World); It; ++It)
                             if (auto* Bot = Cast<AAegisAICharacter>(It->GetPawn()))
                                 if (Bot->bCompanion)
                                     It->Decision->bUtilityPolicy = Args[0] == TEXT("utility");
                     }));
static FAutoConsoleCommandWithWorldAndArgs
    SpawnBot(TEXT("aegis.SpawnBot"), TEXT("Spawn one enemy using an existing enemy's configured assets"),
             FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
                 [](const TArray<FString>& Args, UWorld* World)
                 {
                     (void)Args;
                     if (!World)
                         return;
                     for (TActorIterator<AAegisAICharacter> It(World); It; ++It)
                         if (It->Team == EAegisTeam::Enemy && It->Behavior)
                         {
                             const FTransform Transform(It->GetActorRotation(),
                                                        It->GetActorLocation() + FVector(200, 0, 100));
                             auto* Bot = World->SpawnActorDeferred<AAegisAICharacter>(
                                 AAegisAICharacter::StaticClass(), Transform);
                             if (Bot)
                             {
                                 Bot->Behavior = It->Behavior;
                                 Bot->CoverQuery = It->CoverQuery;
                                 Bot->AttackQuery = It->AttackQuery;
                                 Bot->RetreatQuery = It->RetreatQuery;
                                 Bot->FinishSpawning(Transform);
                             }
                             break;
                         }
                 }));
static FAutoConsoleCommandWithWorldAndArgs
    KillBot(TEXT("aegis.KillBot"), TEXT("Kill first alive enemy through normal hostile damage path"),
            FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
                [](const TArray<FString>& Args, UWorld* World)
                {
                    (void)Args;
                    if (!World)
                        return;
                    AAegisCharacter* Source = nullptr;
                    for (TActorIterator<AAegisCharacter> It(World); It; ++It)
                        if (It->Team == EAegisTeam::Player)
                        {
                            Source = *It;
                            break;
                        }
                    if (Source)
                        for (TActorIterator<AAegisAICharacter> It(World); It; ++It)
                            if (It->Team == EAegisTeam::Enemy && It->Health->IsAlive())
                            {
                                It->Health->ApplyDamage(It->Health->Maximum, Source);
                                break;
                            }
                }));
static FAutoConsoleCommandWithWorldAndArgs Visualize(
    TEXT("aegis.Visualize"), TEXT("aegis.Visualize perception|eqs|clear : draws a five-second snapshot"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World)
        {
            if (!World || Args.Num() != 1)
                return;
            if (Args[0] == TEXT("clear"))
            {
                FlushPersistentDebugLines(World);
                return;
            }
            for (TActorIterator<AAegisAIController> It(World); It; ++It)
                if (It->GetPawn())
                {
                    const FVector Origin = It->GetPawn()->GetActorLocation();
                    if (Args[0] == TEXT("perception"))
                    {
                        DrawDebugSphere(World, Origin, 1500, 32, FColor::Cyan, false, 5);
                        DrawDebugLine(World, Origin, It->LastKnown,
                                      It->bTargetVisible ? FColor::Green : FColor::Orange, false, 5, 0, 3);
                    }
                    if (Args[0] == TEXT("eqs") && It->bHasTacticalPoint)
                    {
                        DrawDebugSphere(World, It->SelectedPoint, 40, 12, FColor::Purple, false, 5);
                        DrawDebugLine(World, Origin, It->SelectedPoint, FColor::Purple, false, 5, 0, 3);
                    }
                }
        }));
static FAutoConsoleCommandWithWorldAndArgs
    RunScenario(TEXT("aegis.RunScenario"),
                TEXT("Run the configured ScenarioRunner in this development world"),
                FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
                    [](const TArray<FString>& Args, UWorld* World)
                    {
                        (void)Args;
                        if (World)
                            for (TActorIterator<AAegisScenarioRunner> It(World); It; ++It)
                            {
                                It->RunBatch();
                                break;
                            }
                    }));
#endif
