#include "aegis/simulation.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
int checks = 0;
void check(bool success, const char* message)
{
    ++checks;
    if (!success)
        throw std::runtime_error(message);
}
int main()
{
    try
    {
        using namespace aegis;
        Health h;
        check(h.damage(40, Team::Enemy, Team::Player) == 40, "hostile damage");
        check(h.damage(20, Team::Player, Team::Player) == 0, "friendly fire blocked");
        check(h.damage(20, Team::Neutral, Team::Player) == 0, "neutral damage blocked");
        check(h.damage(-20, Team::Enemy, Team::Player) == 0, "negative damage blocked");
        check(h.damage(std::numeric_limits<double>::quiet_NaN(), Team::Enemy, Team::Player) == 0,
              "NaN damage blocked");
        check(h.heal(100) == 40 && h.current == 100, "healing clamp");
        h.damage(1000, Team::Enemy, Team::Player);
        check(!h.alive() && h.heal(10) == 0 && h.damage(10, Team::Enemy, Team::Player) == 0,
              "death is terminal");
        Cooldown cd;
        check(cd.consume(1, 2) && !cd.consume(2, 2) && cd.consume(3, 2), "cooldown boundary");
        check(!cd.consume(std::numeric_limits<double>::infinity(), 1), "invalid cooldown time");
        Random a(17), b(17), c(18);
        bool different = false;
        for (int i = 0; i < 100; ++i)
        {
            auto n = a.next();
            check(n == b.next(), "reproducible PRNG");
            different |= n != c.next();
        }
        check(different, "independent seeds");
        Observation o;
        auto scores = utility(o);
        check(scores[1] == 0 && scores[2] == 0, "hidden target and ally unavailable");
        o.targetVisible = true;
        check(choose(utility(o), Action::Follow) == Action::Attack, "healthy attack");
        o.health = 0.05;
        check(choose(utility(o), Action::Attack) == Action::Retreat, "critical retreat");
        o.health = 1;
        o.allyKnown = true;
        o.allyHealth = 0.1;
        o.supportReady = true;
        check(choose(utility(o), Action::Attack) == Action::Support, "ally support");
        check(choose({0.5, 0.54, 0, 0}, Action::Follow) == Action::Follow, "selection hysteresis");
        o.allyKnown = false;
        check(utility(o)[2] == 0, "unauthorized ally telemetry ignored");
        Director d;
        DirectorInput input;
        auto output = d.update(input, 0, 0.1);
        const int budget = output.spawnBudget;
        input.performance = 100;
        check(d.update(input, 1, 0.1).spawnBudget == budget, "director cooldown");
        input.enemyCount = 12;
        check(d.update(input, 1.1, 0.1).spawnBudget == 0, "population cap overrides director cooldown");
        input.playerHealth = 0;
        input.recentDamage = 100;
        input.enemyCount = 50;
        for (int i = 2; i < 100; ++i)
        {
            output = d.update(input, i * 0.2, 0.2);
            check(output.spawnBudget >= 0 && output.spawnBudget <= 3, "director spawn bound");
            check(output.eliteProbability >= 0 && output.eliteProbability <= 0.25, "director elite bound");
        }
        check(output.recovery && output.spawnBudget == 0, "director recovery hysteresis enter");
        input = {};
        for (int i = 100; i < 250; ++i)
            output = d.update(input, i * 0.2, 0.2);
        check(!output.recovery, "director recovery exit");
        std::vector<Circle> pillars = {{{0, 0}, 1}};
        check(!lineOfSight({-3, 0}, {3, 0}, pillars) && lineOfSight({-3, 2}, {3, 2}, pillars),
              "LOS obstacle");
        check(!navigable({0, 0}, pillars) && !navigable({30, 0}, pillars), "collision and arena bounds");
        auto cover = coverPoint({-4, 2}, {4, 0}, pillars, true);
        check(navigable(cover, pillars) && !lineOfSight(cover, {4, 0}, pillars), "cover sampling occludes");
        Scenario scenario;
        check(scenario.valid(), "default scenario valid");
        scenario.enemies = 51;
        check(!scenario.valid(), "oversized scenario rejected");
        scenario.enemies = 4;
        auto m1 = Simulation(scenario).run(), m2 = Simulation(scenario).run();
        check(m1.win == m2.win && m1.taken == m2.taken && m1.enemyDeaths == m2.enemyDeaths &&
                  m1.distance == m2.distance && m1.decisions == m2.decisions,
              "deterministic episode");
        check(m1.dealt > 0 && m1.firstEngage >= 0 && m1.decisions > 0, "actual combat metrics populated");
        check(m1.enemyDeaths <= m1.spawned && m1.companionDeaths <= 1, "death metrics not duplicated");
        std::ostringstream json;
        writeJson(json, scenario, m1);
        check(json.str().find("portable-cpp-model") != std::string::npos, "engine provenance serialized");
        std::cout << "{\"suite\":\"portable_cpp\",\"assertions\":" << checks << ",\"failed\":0}\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "assertion " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
