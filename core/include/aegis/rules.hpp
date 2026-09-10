#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace aegis
{
inline double bounded(double value, double low, double high)
{
    return std::isfinite(value) ? std::clamp(value, low, high) : low;
}
enum class Team : std::uint8_t
{
    Neutral,
    Player,
    Enemy
};
inline bool hostile(Team a, Team b)
{
    return a != Team::Neutral && b != Team::Neutral && a != b;
}
struct Health
{
    double maximum = 100, current = 100;
    bool alive() const
    {
        return current > 0;
    }
    double damage(double amount, Team source, Team owner)
    {
        if (!alive() || !hostile(source, owner))
            return 0;
        const double applied = std::min(current, bounded(amount, 0, maximum));
        current -= applied;
        return applied;
    }
    double heal(double amount)
    {
        if (!alive())
            return 0;
        const double applied = std::min(maximum - current, bounded(amount, 0, maximum));
        current += applied;
        return applied;
    }
};
struct Cooldown
{
    double readyAt = 0;
    bool ready(double now) const
    {
        return std::isfinite(now) && now >= readyAt;
    }
    bool consume(double now, double seconds)
    {
        if (!ready(now) || !std::isfinite(seconds) || seconds < 0)
            return false;
        readyAt = now + seconds;
        return true;
    }
};
// Fully specified PRNG: identical sequence across standard-library implementations.
struct Random
{
    std::uint32_t state;
    explicit Random(std::uint32_t seed) : state(seed ? seed : 0x9e3779b9u) {}
    std::uint32_t next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    double unit()
    {
        return static_cast<double>(next()) / 4294967296.0;
    }
};
enum class Action : std::uint8_t
{
    Follow,
    Attack,
    Support,
    Retreat,
    Count
};
inline const char* actionName(Action a)
{
    constexpr const char* names[] = {"follow", "attack", "support", "retreat"};
    return names[static_cast<unsigned>(a) < 4 ? static_cast<unsigned>(a) : 0];
}
// A policy cannot reach actors, engine state, or the world. Only this observation crosses the boundary.
struct Observation
{
    double health = 1, allyHealth = 1, allyDistance = 0, targetDistance = 1000;
    bool targetVisible = false, targetRemembered = false, supportReady = false, allyKnown = false;
};
using Scores = std::array<double, 4>;
inline Scores utility(const Observation& o)
{
    const double hp = bounded(o.health, 0, 1), ally = bounded(o.allyHealth, 0, 1);
    return {o.allyKnown ? 0.15 + 0.55 * bounded(o.allyDistance / 12, 0, 1) : 0.1,
            o.targetVisible ? 0.45 + 0.3 * hp : (o.targetRemembered ? 0.25 : 0),
            o.allyKnown && o.supportReady && ally < 0.8 ? 0.3 + (1 - ally) * 0.85 : 0,
            (1 - hp) * (o.targetVisible ? 1.25 : 0.85)};
}
inline Action choose(const Scores& scores, Action previous, double hysteresis = 0.08)
{
    auto best =
        static_cast<unsigned>(std::distance(scores.begin(), std::max_element(scores.begin(), scores.end())));
    const auto old = static_cast<unsigned>(previous);
    if (old < scores.size() && scores[best] < scores[old] + std::max(0.0, hysteresis))
        return previous;
    return static_cast<Action>(best);
}
struct DirectorInput
{
    double playerHealth = 1, recentDamage = 0, combatSeconds = 0, performance = 0;
    int enemyCount = 0;
};
struct DirectorOutput
{
    int spawnBudget = 0;
    double eliteProbability = 0, recoverySeconds = 0, pressure = 0;
    bool recovery = false;
};
class Director
{
    double smoothed_ = 0, nextChange_ = 0, lastNow_ = -1;
    bool recovering_ = false;
    DirectorOutput output_;

  public:
    DirectorOutput update(const DirectorInput& input, double now, double dt)
    {
        if (!std::isfinite(now) || now < lastNow_ || !std::isfinite(dt) || dt <= 0)
            return output_;
        lastNow_ = now;
        const double pressure = bounded((1 - bounded(input.playerHealth, 0, 1)) * 0.65 +
                                            bounded(input.recentDamage / 40, 0, 1) * 0.25 +
                                            bounded(input.enemyCount / 12.0, 0, 1) * 0.10,
                                        0, 1);
        smoothed_ += (pressure - smoothed_) * (1 - std::exp(-std::min(dt, 1.0) / 2.0));
        output_.pressure = smoothed_;
        // The decision cooldown must never defer a hard population safety cap.
        const int capacity = std::max(0, 12 - std::max(0, input.enemyCount));
        output_.spawnBudget = std::min(output_.spawnBudget, capacity);
        if (now < nextChange_)
            return output_;
        if (!recovering_ && smoothed_ > 0.58)
            recovering_ = true;
        else if (recovering_ && smoothed_ < 0.32)
            recovering_ = false;
        output_.spawnBudget =
            recovering_ ? 0 : std::min(capacity, static_cast<int>(bounded(1 + input.performance * 2, 1, 3)));
        output_.eliteProbability = recovering_ ? 0 : bounded(0.05 + input.combatSeconds / 600.0, 0.05, 0.25);
        output_.recovery = recovering_;
        output_.recoverySeconds = recovering_ ? 8 : 0;
        nextChange_ = now + 5;
        return output_;
    }
};
} // namespace aegis
