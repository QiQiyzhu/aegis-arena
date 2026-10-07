#include "AegisSquadPlanner.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AegisLab.h"
#include "Misc/AutomationTest.h"

// Exercise the exact runtime decoder without a model, network call or spawned
// Actor. The temporary CDO pointer supplies only the default objective state.
struct FAegisPlannerProtocolAccess
{
    static bool Decode(const FString& Text, int32& Count)
    {
        auto* Planner = GetMutableDefault<AAegisSquadPlanner>();
        const auto OriginalRunner = Planner->Runner;
        Planner->Runner = GetMutableDefault<AAegisScenarioRunner>();
        TArray<AAegisSquadPlanner::FStep> Steps;
        FString Label, Reason;
        const bool Valid = Planner->DecodePlan(Text, Steps, Label, Reason);
        Planner->Runner = OriginalRunner;
        Count = Steps.Num();
        return Valid;
    }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAegisPlannerProtocolTest, "Aegis.Core.PlannerProtocol",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAegisPlannerProtocolTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    int32 Count = 0;
    TestTrue(TEXT("Single guard step is a valid minimal model plan"), FAegisPlannerProtocolAccess::Decode(
        TEXT("{\"label\":\"Protect player\",\"steps\":[{\"skill\":\"guard\",\"target\":\"none\"}]}"), Count));
    TestEqual(TEXT("Single guard remains one step"), Count, 1);
    TestTrue(TEXT("Three grounded non-target skills use the exact runtime decoder"), FAegisPlannerProtocolAccess::Decode(
        TEXT("{\"label\":\"Regroup capture guard\",\"steps\":[{\"skill\":\"regroup\",\"target\":\"none\"},{\"skill\":\"capture_relay\",\"target\":\"none\"},{\"skill\":\"guard\",\"target\":\"none\"}]}"), Count));
    TestEqual(TEXT("Three steps remain bounded"), Count, 3);
    const TCHAR* Invalid[] = {
        TEXT("{\"label\":\"Empty\",\"steps\":[]}"),
        TEXT("{\"label\":\"Four\",\"steps\":[{\"skill\":\"guard\",\"target\":\"none\"},{\"skill\":\"guard\",\"target\":\"none\"},{\"skill\":\"guard\",\"target\":\"none\"},{\"skill\":\"guard\",\"target\":\"none\"}]}"),
        TEXT("{\"label\":\"Unknown\",\"steps\":[{\"skill\":\"teleport\",\"target\":\"none\"}]}"),
        TEXT("{\"label\":\"Extra\",\"steps\":[{\"skill\":\"guard\",\"target\":\"none\"}],\"location\":[0,0,0]}"),
        TEXT("{\"label\":\"Extra step field\",\"steps\":[{\"skill\":\"guard\",\"target\":\"none\",\"x\":0}]}"),
        TEXT("{\"label\":\"Target misuse\",\"steps\":[{\"skill\":\"capture_relay\",\"target\":\"t0\"}]}"),
        TEXT("{\"label\":\"Missing field\",\"steps\":[{\"skill\":\"guard\"}]}"),
        TEXT("{\"label\":\"Hidden target\",\"steps\":[{\"skill\":\"focus_visible\",\"target\":\"t0\"}]}"),
        TEXT("{\"label\":\"No target\",\"steps\":[{\"skill\":\"focus_visible\",\"target\":\"none\"}]}"),
        TEXT("{\"label\":\"\\u4e2d\\u6587\",\"steps\":[{\"skill\":\"guard\",\"target\":\"none\"}]}"),
        TEXT("{\"label\":\"Bad\\nlabel\",\"steps\":[{\"skill\":\"guard\",\"target\":\"none\"}]}"),
        TEXT("{\"label\":\"\",\"steps\":[{\"skill\":\"guard\",\"target\":\"none\"}]}"),
        TEXT("{\"label\":\"Duplicate\",\"label\":\"Replacement\",\"steps\":[{\"skill\":\"guard\",\"target\":\"none\"}]}"),
        TEXT("{\"label\":\"Duplicate skill\",\"steps\":[{\"skill\":\"focus_visible\",\"skill\":\"guard\",\"target\":\"none\"}]}"),
        TEXT("```json\n{\"label\":\"Wrapped\",\"steps\":[{\"skill\":\"guard\",\"target\":\"none\"}]}\n```"),
    };
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Invalid); ++Index)
        TestFalse(FString::Printf(TEXT("Strict runtime parser rejects invalid contract %d"), Index),
                  FAegisPlannerProtocolAccess::Decode(Invalid[Index], Count));
    const FString LongLabel = FString::ChrN(81, TEXT('x'));
    TestFalse(TEXT("Label byte budget is enforced"), FAegisPlannerProtocolAccess::Decode(
        TEXT("{\"label\":\"") + LongLabel + TEXT("\",\"steps\":[{\"skill\":\"guard\",\"target\":\"none\"}]}"), Count));
    return true;
}
#endif
