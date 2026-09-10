#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AegisLab.h"
#include "aegis/rules.hpp"
#include "JsonObjectConverter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAegisDamageTest, "Aegis.Core.DamageAndTeams",
                                 EAutomationTestFlags_ApplicationContextMask |
                                     EAutomationTestFlags::EngineFilter)
bool FAegisDamageTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    aegis::Health H;
    TestEqual(TEXT("friendly fire"), H.damage(20, aegis::Team::Player, aegis::Team::Player), 0.0);
    TestEqual(TEXT("enemy damage"), H.damage(20, aegis::Team::Enemy, aegis::Team::Player), 20.0);
    TestEqual(TEXT("health"), H.current, 80.0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAegisUtilityTest, "Aegis.Core.UtilityAuthorization",
                                 EAutomationTestFlags_ApplicationContextMask |
                                     EAutomationTestFlags::EngineFilter)
bool FAegisUtilityTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    aegis::Observation O;
    O.allyHealth = 0.1;
    O.supportReady = true;
    TestEqual(TEXT("unknown ally grants no support"), aegis::utility(O)[2], 0.0);
    O.allyKnown = true;
    TestTrue(TEXT("known injured ally enables support"), aegis::utility(O)[2] > 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAegisDirectorTest, "Aegis.Core.DirectorBounds",
                                 EAutomationTestFlags_ApplicationContextMask |
                                     EAutomationTestFlags::EngineFilter)
bool FAegisDirectorTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    aegis::Director D;
    aegis::DirectorInput I;
    I.performance = 1e6;
    I.combatSeconds = 1e6;
    for (int N = 0; N < 1000; ++N)
    {
        const auto O = D.update(I, N * 0.25, 0.25);
        TestTrue(TEXT("budget clamped"), O.spawnBudget >= 0 && O.spawnBudget <= 3);
        TestTrue(TEXT("elite chance clamped"), O.eliteProbability <= 0.25);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAegisScenarioTest, "Aegis.Core.ScenarioSerialization",
                                 EAutomationTestFlags_ApplicationContextMask |
                                     EAutomationTestFlags::EngineFilter)
bool FAegisScenarioTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    FAegisScenarioDefinition Before;
    Before.Seed = 1234;
    FString Json;
    TestTrue(TEXT("encode"), FJsonObjectConverter::UStructToJsonObjectString(Before, Json));
    FAegisScenarioDefinition After;
    TestTrue(TEXT("decode"), FJsonObjectConverter::JsonObjectStringToUStruct(Json, &After));
    TestEqual(TEXT("seed"), After.Seed, 1234);
    TestTrue(TEXT("valid"), After.IsValid());
    After.EnemyCount = 51;
    TestFalse(TEXT("reject 51 enemies"), After.IsValid());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAegisSeedTest, "Aegis.Core.FixedSeed",
                                 EAutomationTestFlags_ApplicationContextMask |
                                     EAutomationTestFlags::EngineFilter)
bool FAegisSeedTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    aegis::Random A(91), B(91);
    for (int I = 0; I < 100; ++I)
        TestEqual(TEXT("PRNG sequence"), A.next(), B.next());
    return true;
}
#endif
