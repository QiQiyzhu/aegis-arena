#pragma once
#include <algorithm>
#include <cmath>

namespace aegis
{
enum class TrialPhase { Briefing, Active, Intermission, Won, Lost };

// The playable trial is separate from the frozen scripted evaluation protocol.
struct Trial
{
    TrialPhase phase = TrialPhase::Briefing;
    int wave = 0;
    bool pressure = false;
    double startedAt = 0, transitionAt = 0, elapsed = 0;
    static constexpr double duration = 180;
    static constexpr double recovery = 5;

    void start(double now, bool hard)
    {
        if (!std::isfinite(now)) return;
        *this = {};
        pressure = hard;
        phase = TrialPhase::Active;
        wave = 1;
        startedAt = now;
    }
    int enemyCount() const { return wave > 0 ? wave + (pressure ? 2 : 1) : 0; }
    bool finished() const { return phase == TrialPhase::Won || phase == TrialPhase::Lost; }
    void observe(double now, bool playerAlive, int enemiesAlive)
    {
        if (phase == TrialPhase::Briefing || finished() || !std::isfinite(now) || now < startedAt)
            return;
        elapsed = std::max(elapsed, now - startedAt);
        // Death and timeout take priority over a simultaneous final elimination.
        if (!playerAlive || elapsed >= duration)
        {
            phase = TrialPhase::Lost;
            return;
        }
        if (phase == TrialPhase::Active && enemiesAlive == 0)
        {
            phase = wave == 3 ? TrialPhase::Won : TrialPhase::Intermission;
            transitionAt = now + recovery;
        }
        else if (phase == TrialPhase::Intermission && now >= transitionAt)
        {
            ++wave;
            phase = TrialPhase::Active;
        }
    }
};
}
