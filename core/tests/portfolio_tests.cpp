#include "aegis/portfolio.hpp"
#include <iostream>
#include <limits>
#include <string>

namespace
{
int checks = 0, failures = 0;
void check(const char* name, bool ok)
{
    ++checks;
    if (!ok) { ++failures; std::cerr << "FAIL " << name << '\n'; }
}
bool balanced(const aegis::PortfolioEconomy& e)
{
    return e.energy >= 0 && e.energy <= 100 && e.energy == 60 + e.earned - e.spent &&
        e.spent == e.pulses * 35 + e.repairs * 40 &&
        e.earned + e.overflow == e.kills * 12 + e.stages * 25;
}
}
int main()
{
    using aegis::PortfolioEconomy;
    PortfolioEconomy e;
    check("initial_budget", e.energy == 60 && balanced(e));
    check("inactive_pulse_rejected", !e.tryPulse(false) && e.energy == 60);
    check("first_pulse_cost", e.tryPulse(true) && e.energy == 25 && e.pulses == 1);
    check("pulse_overspend_rejected", !e.tryPulse(true) && e.energy == 25 && e.spent == 35);
    check("kill_reward", e.rewardEnemy(1, true) && e.energy == 37);
    check("duplicate_kill_rejected", !e.rewardEnemy(1, true) && e.energy == 37 && e.kills == 1);
    check("zero_identity_rejected", !e.rewardEnemy(0, true) && e.kills == 1);
    check("dead_kill_not_consumed", !e.rewardEnemy(2, false) && e.kills == 1);
    check("rejected_identity_can_be_valid_later", e.rewardEnemy(2, true) && e.energy == 49);
    check("earned_pulse_available", e.tryPulse(true) && e.energy == 14 && balanced(e));
    e = {};
    check("restart_resets_balance_and_ids", e.energy == 60 && e.rewardEnemy(1, true) && e.energy == 72);
    e = {};
    check("no_recipient_no_charge", !e.tryRepair(0, false, true) && e.energy == 60 && e.repairReadyAt == 0);
    check("inactive_repair_no_charge", !e.tryRepair(0, true, false) && e.energy == 60);
    check("negative_time_no_charge", !e.tryRepair(-1, true, true) && e.energy == 60);
    check("nan_time_no_charge", !e.tryRepair(std::numeric_limits<double>::quiet_NaN(), true, true) && e.energy == 60);
    check("infinite_time_no_charge", !e.tryRepair(std::numeric_limits<double>::infinity(), true, true) && e.energy == 60);
    check("repair_commits_cost_and_cooldown", e.tryRepair(0, true, true) && e.energy == 20 && e.repairReadyAt == 8);
    check("repair_regain_budget", e.rewardEnemy(1, true) && e.rewardEnemy(2, true) && e.energy == 44);
    check("repair_cooldown_rejects", !e.tryRepair(7.999, true, true) && e.energy == 44 && e.repairs == 1);
    check("paused_clock_preserves_cooldown", e.repairRemaining(2) == 6 && e.repairRemaining(2) == 6);
    check("repair_exact_deadline_accepts", e.tryRepair(8, true, true) && e.energy == 4 && e.repairs == 2);
    check("repair_overspend_rejected", !e.tryRepair(16, true, true) && e.energy == 4 && balanced(e));
    e = {};
    for (std::uint64_t id = 1; id <= 5; ++id) e.rewardEnemy(id, true);
    check("cap_records_actual_income_and_overflow", e.energy == 100 && e.earned == 40 && e.overflow == 20);
    check("capped_reward_still_consumes_identity", !e.rewardEnemy(5, true) && e.kills == 5);
    check("stage_reward_can_overflow", e.rewardStage(1, true) && e.stages == 1 && e.overflow == 45);
    check("duplicate_stage_no_new_reward", !e.rewardStage(1, true) && e.overflow == 45);
    check("invalid_stage_rejected", !e.rewardStage(0, true) && !e.rewardStage(4, true) && e.stages == 1);
    check("dead_stage_does_not_pay", !e.rewardStage(2, false) && e.stages == 1);
    check("spend_then_stage_refills", e.tryPulse(true) && e.rewardStage(2, true) && e.energy == 90);
    check("final_stage_awarded_once", e.rewardStage(3, true) && !e.rewardStage(3, true) && e.stages == 3 && balanced(e));
    e = {};
    bool invariant = true;
    for (int step = 0; step < 10000; ++step)
    {
        const bool eligible = step % 7 != 0;
        switch (step % 4)
        {
        case 0: e.rewardEnemy(static_cast<std::uint64_t>(step % 101), eligible); break;
        case 1: e.tryPulse(eligible); break;
        case 2: e.tryRepair(step * 0.2, step % 5 != 0, eligible); break;
        case 3: e.rewardStage(step % 5, eligible); break;
        }
        invariant = invariant && balanced(e);
    }
    check("mixed_transactions_preserve_conservation", invariant);
    e = {};
    for (std::uint64_t id = 1; id <= 4096; ++id) e.rewardEnemy(id, true);
    check("bounded_identity_storage", !e.rewardEnemy(4097, true) && e.kills == 4096 && balanced(e));
    std::cout << "{\"suite\":\"portfolio-economy\",\"checks\":" << checks
              << ",\"failures\":" << failures << ",\"unrealRuntime\":false}\n";
    return failures == 0 ? 0 : 1;
}
