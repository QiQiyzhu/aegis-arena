#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

namespace aegis
{
// Occupancy is supplied by the game world and includes only living allies.
// This objective model does not change the historical Trial/evaluation rules.
struct Operation
{
    int wave = 0, relay = 0;
    double charge = 0;
    double relaySeconds = 4.0, extractionSeconds = 3.0;
    bool complete = false;

    void reset(int nextWave, double nextRelaySeconds = 4.0, double nextExtractionSeconds = 3.0)
    {
        if (nextWave < 1 || nextWave > 3 || !std::isfinite(nextRelaySeconds) ||
            !std::isfinite(nextExtractionSeconds) || nextRelaySeconds <= 0 || nextExtractionSeconds <= 0) return;
        *this = {};
        wave = nextWave;
        relaySeconds = nextRelaySeconds;
        extractionSeconds = nextExtractionSeconds;
    }
    int requiredRelays() const { return wave == 2 ? 2 : 1; }
    double requiredSeconds() const { return wave == 3 ? extractionSeconds : relaySeconds; }
    void observe(double dt, bool playerIn, bool companionIn, bool contested, bool enemiesCleared)
    {
        if (wave < 1 || wave > 3 || complete || !std::isfinite(dt) || dt <= 0 || dt > 1 ||
            contested || (!playerIn && !companionIn) || (wave == 3 && !enemiesCleared))
            return;
        const double required = requiredSeconds();
        charge = std::min(required, charge + dt * (playerIn && companionIn ? 1.5 : 1.0));
        // Repeated decimal timesteps can finish a few ULPs below the threshold.
        const double tolerance = required * std::numeric_limits<double>::epsilon() * 8;
        if (charge + tolerance < required) return;
        ++relay;
        complete = relay >= requiredRelays();
        // A newly activated relay must be occupied in a later observation.
        charge = complete ? required : 0.0;
    }
};
}
