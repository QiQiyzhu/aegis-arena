#include "aegis/survey.hpp"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAegisSurveyCacheTest, "Aegis.Core.SurveyCache",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAegisSurveyCacheTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    aegis::SurveyCache Cache;
    TestFalse(TEXT("Invalid node cannot be scanned"), Cache.begin(2));
    TestTrue(TEXT("First node starts a key scan"), Cache.begin(0));
    TestFalse(TEXT("Reward choice is fixed throughout a scan"), Cache.selectSupply(true));
    TestFalse(TEXT("Partial scan cannot claim a reward"), Cache.claim(true));
    for (int Index = 0; Index < 9; ++Index) Cache.advance(.25);
    TestFalse(TEXT("Nine quarters of a second do not complete the scan"), Cache.claim(true));
    TestFalse(TEXT("Invalid delta does not advance a scan"), Cache.advance(std::numeric_limits<double>::quiet_NaN()));
    TestFalse(TEXT("A long hitch is not credited as channel time"), Cache.advance(1));
    TestEqual(TEXT("Invalid deltas preserve earned progress"), Cache.progress, 2.25);
    Cache.cancel();
    TestEqual(TEXT("Interrupting a scan discards progress"), Cache.progress, 0.0);
    TestTrue(TEXT("Cancelled cache remains available"), Cache.begin(0));
    for (int Index = 0; Index < 150; ++Index) Cache.advance(1.0 / 60.0);
    TestFalse(TEXT("World eligibility must still hold when committing"), Cache.claim(false));
    TestTrue(TEXT("Exactly 2.5 seconds can yield one key"), Cache.claim(true));
    TestTrue(TEXT("Key is retained until real objective progression"), Cache.keyPending);
    TestFalse(TEXT("Claimed node never grants twice"), Cache.begin(0));
    TestTrue(TEXT("Other cache can be inspected independently"), Cache.begin(1));
    for (int Index = 0; Index < 10; ++Index) Cache.advance(.25);
    TestFalse(TEXT("Stored key capacity rejects a second key without consuming cache"), Cache.claim(true));
    TestFalse(TEXT("Failed capacity claim leaves second cache available"), Cache.claimed(1));
    Cache.cancel();
    TestFalse(TEXT("Extraction cannot consume a data relay key"), Cache.bind(30));
    TestTrue(TEXT("First productive data relay binds the key"), Cache.bind(10));
    TestFalse(TEXT("One key cannot bind another relay"), Cache.bind(20));
    TestEqual(TEXT("Bound relay gains declared multiplier"), Cache.multiplier(10), 1.25);
    TestEqual(TEXT("Other relay has no free multiplier"), Cache.multiplier(20), 1.0);
    Cache.finishBoost();
    Cache.finishBoost();
    TestEqual(TEXT("Ending a relay is idempotent"), Cache.multiplier(10), 1.0);
    TestTrue(TEXT("Supply can be selected for the other node"), Cache.selectSupply(true));
    TestTrue(TEXT("Second cache remains independent"), Cache.begin(1));
    for (int Index = 0; Index < 10; ++Index) Cache.advance(.25);
    TestFalse(TEXT("Full health never consumes supply"), Cache.claim(false));
    TestTrue(TEXT("Actual healing eligibility consumes supply once"), Cache.claim(true));
    TestEqual(TEXT("Both unique nodes are accounted for"), Cache.claimedMask, 3u);
    TestFalse(TEXT("Supply never grants an extra key"), Cache.keyPending);
    Cache = {};
    TestEqual(TEXT("Restart clears all node identities"), Cache.claimedMask, 0u);
    TestEqual(TEXT("Restart clears bound relay"), Cache.boostRelayKey, -1);
    TestFalse(TEXT("Restart clears stored reward"), Cache.keyPending);
    Cache.begin(0);
    for (int Index = 0; Index < 10; ++Index) Cache.advance(.25);
    Cache.claim(true);
    Cache.bind(20);
    Cache.begin(1);
    for (int Index = 0; Index < 10; ++Index) Cache.advance(.25);
    TestTrue(TEXT("A second key can wait behind an already active boost"), Cache.claim(true));
    TestFalse(TEXT("Pending key cannot replace an active relay boost"), Cache.bind(21));
    Cache.finishBoost();
    TestTrue(TEXT("Pending key activates on the next actual relay"), Cache.bind(21));
    TestEqual(TEXT("Old relay no longer inherits any boost"), Cache.multiplier(20), 1.0);
    return true;
}
#endif
