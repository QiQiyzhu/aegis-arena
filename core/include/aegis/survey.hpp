#pragma once
#include <algorithm>
#include <cmath>

namespace aegis
{
// A small, independent cache ledger. The world supplies proximity, uninterrupted
// input and actual healing eligibility; this model never grants combat energy.
struct SurveyCache
{
    static constexpr int nodeCount = 2;
    static constexpr double scanSeconds = 2.5;
    static constexpr double radius = 180.0;
    static constexpr double relayMultiplier = 1.25;

    unsigned claimedMask = 0;
    int node = -1, boostRelayKey = -1;
    double progress = 0;
    bool supplySelected = false, keyPending = false;

    bool claimed(int index) const
    {
        return index >= 0 && index < nodeCount && (claimedMask & (1u << index));
    }
    bool selectSupply(bool supply)
    {
        if (node != -1 || supply == supplySelected) return false;
        supplySelected = supply;
        return true;
    }
    bool begin(int index)
    {
        if (node != -1 || index < 0 || index >= nodeCount || claimed(index)) return false;
        node = index;
        progress = 0;
        return true;
    }
    bool advance(double dt)
    {
        // Long hitches cannot silently complete a channel in a single frame.
        if (node < 0 || !std::isfinite(dt) || dt <= 0 || dt > .25) return false;
        progress = std::min(scanSeconds, progress + dt);
        if (scanSeconds - progress < 1e-8) progress = scanSeconds;
        return progress >= scanSeconds;
    }
    void cancel() { node = -1; progress = 0; }
    bool claim(bool rewardEligible)
    {
        if (node < 0 || claimed(node) || progress < scanSeconds || !rewardEligible ||
            (!supplySelected && keyPending)) return false;
        claimedMask |= 1u << node;
        if (!supplySelected) keyPending = true;
        cancel();
        return true;
    }
    bool bind(int relayKey)
    {
        if (!keyPending || boostRelayKey != -1 ||
            (relayKey != 10 && relayKey != 20 && relayKey != 21)) return false;
        keyPending = false;
        boostRelayKey = relayKey;
        return true;
    }
    void finishBoost() { boostRelayKey = -1; }
    double multiplier(int relayKey) const
    {
        return boostRelayKey >= 0 && boostRelayKey == relayKey ? relayMultiplier : 1.0;
    }
};
}
