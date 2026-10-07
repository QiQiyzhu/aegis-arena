#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_set>

namespace aegis
{
// A transaction ledger, not a simulation of combat or healing. The native world
// supplies eligibility and unique death/stage identities after observing them.
struct PortfolioEconomy
{
    static constexpr int initialEnergy = 60, maximumEnergy = 100;
    static constexpr int pulseCost = 35, repairCost = 40;
    static constexpr int killReward = 12, stageReward = 25;
    static constexpr double repairCooldown = 8.0;

    int energy = initialEnergy, earned = 0, spent = 0, overflow = 0;
    int kills = 0, stages = 0, pulses = 0, repairs = 0;
    double repairReadyAt = 0;

    bool rewardEnemy(std::uint64_t identity, bool eligible)
    {
        if (!eligible || identity == 0 || rewardedEnemies.size() >= 4096 ||
            !rewardedEnemies.insert(identity).second)
            return false;
        ++kills;
        grant(killReward);
        return true;
    }
    bool rewardStage(int stage, bool eligible)
    {
        if (!eligible || stage < 1 || stage > 3 || (rewardedStages & (1u << (stage - 1))))
            return false;
        rewardedStages |= 1u << (stage - 1);
        ++stages;
        grant(stageReward);
        return true;
    }
    bool tryPulse(bool eligible)
    {
        if (!eligible || energy < pulseCost) return false;
        energy -= pulseCost;
        spent += pulseCost;
        ++pulses;
        return true;
    }
    bool trySpend(int cost, bool eligible)
    {
        if (!eligible || cost <= 0 || cost > maximumEnergy || energy < cost) return false;
        energy -= cost;
        spent += cost;
        return true;
    }
    bool tryRepair(double now, bool hasRecipient, bool eligible, int cost = repairCost)
    {
        if (!eligible || !hasRecipient || !std::isfinite(now) || now < 0 ||
            now < repairReadyAt || cost <= 0 || cost > repairCost || energy < cost)
            return false;
        energy -= cost;
        spent += cost;
        ++repairs;
        repairReadyAt = now + repairCooldown;
        return true;
    }
    double repairRemaining(double now) const
    {
        return std::isfinite(now) ? std::max(0.0, repairReadyAt - now) : repairCooldown;
    }

  private:
    std::unordered_set<std::uint64_t> rewardedEnemies;
    unsigned rewardedStages = 0;
    void grant(int amount)
    {
        const int accepted = std::min(amount, maximumEnergy - energy);
        energy += accepted;
        earned += accepted;
        overflow += amount - accepted;
    }
};
}
