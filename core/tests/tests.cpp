#include "aegis/simulation.hpp"
#include "aegis/trial.hpp"
#include "aegis/operation.hpp"
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
void testOperation()
{
    using aegis::Operation;
    Operation operation;
    operation.observe(1, true, true, false, true);
    check(operation.wave == 0 && operation.relay == 0 && operation.charge == 0 && !operation.complete,
          "operation remains inert until a valid wave is configured");
    operation.reset(1);
    check(operation.wave == 1 && operation.requiredRelays() == 1 && operation.requiredSeconds() == 4,
          "opening operation requires one four-second relay");
    operation.observe(1, true, false, false, false);
    check(operation.charge == 1 && !operation.complete,
          "player alone advances opening objective while enemies remain elsewhere");
    operation.observe(1, true, true, true, false);
    check(operation.charge == 1, "enemy presence freezes even two allied occupants");
    operation.observe(1, false, false, false, false);
    check(operation.charge == 1, "empty objective retains progress without advancing");
    operation.observe(1, false, true, false, false);
    check(operation.charge == 2, "living companion can charge alone when player is absent or dead");
    operation.observe(1, true, true, false, false);
    check(operation.charge == 3.5, "two living occupants charge at one-and-a-half times the solo rate");
    operation.observe(1, true, true, false, false);
    check(operation.complete && operation.relay == 1 && operation.charge == 4,
          "opening completion clamps charge when dual occupancy overshoots");
    operation.observe(1, true, true, false, true);
    operation.observe(1, false, false, true, false);
    check(operation.complete && operation.wave == 1 && operation.relay == 1 && operation.charge == 4,
          "completed objective is terminal after further occupancy or contest changes");

    operation.reset(2);
    check(operation.wave == 2 && operation.requiredRelays() == 2 && operation.requiredSeconds() == 4 &&
              operation.relay == 0 && operation.charge == 0 && !operation.complete,
          "middle wave reset clears prior completion and requires two relays");
    operation.observe(1, true, true, false, false);
    operation.observe(1, true, true, false, false);
    operation.observe(0.5, true, false, false, false);
    operation.observe(1, true, true, false, false);
    check(operation.relay == 1 && operation.charge == 0 && !operation.complete,
          "first middle relay activates the second without carrying surplus charge");
    operation.observe(1, false, false, false, true);
    check(operation.relay == 1 && operation.charge == 0 && !operation.complete,
          "clearing all enemies does not automatically charge an empty second relay");
    for (int i = 0; i < 3; ++i) operation.observe(1, true, false, false, false);
    check(operation.relay == 1 && operation.charge == 3 && !operation.complete,
          "second relay requires its own complete occupancy interval");
    operation.observe(1, true, false, false, false);
    check(operation.relay == 2 && operation.charge == 4 && operation.complete,
          "middle operation finishes only after both relays complete");

    operation.reset(3);
    check(operation.wave == 3 && operation.requiredRelays() == 1 && operation.requiredSeconds() == 3 &&
              operation.relay == 0 && operation.charge == 0 && !operation.complete,
          "extraction reset requires one three-second relay");
    operation.observe(1, true, true, false, false);
    check(operation.charge == 0, "extraction cannot charge before enemies are cleared");
    operation.observe(1, true, true, true, true);
    check(operation.charge == 0, "extraction still respects contest flag after clear");
    operation.observe(0.5, true, true, false, true);
    check(operation.charge == 0.75, "cleared extraction receives the same dual occupancy benefit");
    operation.observe(1, true, false, false, true);
    check(operation.charge == 1.75, "companion death represented by absent flag removes its bonus");
    operation.observe(1, false, false, false, true);
    operation.observe(1, true, true, false, false);
    check(operation.charge == 1.75, "empty extraction or renewed enemies freeze existing progress");
    operation.observe(0.5, true, true, false, true);
    operation.observe(0.5, false, true, false, true);
    check(operation.complete && operation.relay == 1 && operation.charge == 3,
          "extraction completes exactly at its shorter threshold");

    operation.reset(1);
    for (int i = 0; i < 40; ++i) operation.observe(0.1, true, false, false, false);
    check(operation.complete && operation.charge == 4 && operation.relay == 1,
          "forty decimal timesteps finish four seconds without an extra frame");
    operation.reset(2);
    operation.observe(0.5, true, true, false, false);
    const double invalidDt[] = {0, -0.1, 1.000001, std::numeric_limits<double>::infinity(),
                                -std::numeric_limits<double>::infinity(),
                                std::numeric_limits<double>::quiet_NaN()};
    for (double dt : invalidDt)
    {
        operation.observe(dt, true, true, false, true);
        check(operation.wave == 2 && operation.relay == 0 && operation.charge == 0.75 && !operation.complete,
              "invalid timestep leaves the entire operation unchanged");
    }
    for (int invalidWave : {-1, 0, 4})
    {
        operation.reset(invalidWave);
        check(operation.wave == 2 && operation.relay == 0 && operation.charge == 0.75 && !operation.complete,
              "invalid reset wave cannot erase active objective progress");
        Operation invalid = operation;
        invalid.wave = invalidWave;
        invalid.observe(1, true, true, false, true);
        check(invalid.wave == invalidWave && invalid.relay == 0 && invalid.charge == 0.75 && !invalid.complete,
              "observation with invalid public wave cannot advance objective");
    }
}
int main()
{
    try
    {
        using namespace aegis;
        testOperation();
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
        Trial trial;
        trial.observe(50, false, 0);
        check(trial.phase == TrialPhase::Briefing && trial.wave == 0 && trial.enemyCount() == 0,
              "briefing cannot advance or lose before deployment");
        trial.start(std::numeric_limits<double>::quiet_NaN(), true);
        check(trial.phase == TrialPhase::Briefing, "non-finite deployment is ignored");
        trial.start(100, false);
        check(trial.phase == TrialPhase::Active && trial.wave == 1 && trial.enemyCount() == 2,
              "guided deployment starts the two-enemy opening");
        trial.observe(99, false, 0);
        trial.observe(std::numeric_limits<double>::infinity(), false, 0);
        check(trial.phase == TrialPhase::Active && trial.elapsed == 0,
              "invalid observation time cannot cause loss or clear");
        trial.observe(102, true, 1);
        check(trial.phase == TrialPhase::Active && trial.elapsed == 2,
              "surviving enemy prevents recovery");
        trial.observe(101, true, 1);
        check(trial.elapsed == 2, "elapsed time cannot decrease");
        trial.observe(103, true, 0);
        check(trial.phase == TrialPhase::Intermission && trial.transitionAt == 108 && trial.wave == 1,
              "first clear schedules a five-second recovery");
        trial.observe(104, true, 0);
        check(trial.phase == TrialPhase::Intermission && trial.transitionAt == 108,
              "repeated empty observations do not postpone recovery");
        trial.observe(107.999, true, 0);
        check(trial.phase == TrialPhase::Intermission && trial.wave == 1,
              "next wave does not start before recovery deadline");
        trial.observe(108, true, 0);
        check(trial.phase == TrialPhase::Active && trial.wave == 2 && trial.enemyCount() == 3,
              "recovery boundary advances exactly one wave");
        trial.observe(109, true, 0);
        trial.observe(114, true, 0);
        check(trial.phase == TrialPhase::Active && trial.wave == 3 && trial.enemyCount() == 4,
              "guided final wave has four enemies");
        trial.observe(115, true, 0);
        check(trial.phase == TrialPhase::Won && trial.finished() && trial.elapsed == 15,
              "surviving the final clear wins");
        trial.observe(1000, false, 10);
        check(trial.phase == TrialPhase::Won && trial.elapsed == 15 && trial.wave == 3,
              "won result is terminal despite later death or timeout observations");
        trial.start(200, true);
        check(trial.phase == TrialPhase::Active && trial.wave == 1 && trial.enemyCount() == 3 &&
                  trial.pressure && trial.elapsed == 0 && trial.transitionAt == 0,
              "restart clears prior outcome and timings and selects pressure opening");
        trial.observe(201, true, 0);
        trial.observe(206, true, 0);
        check(trial.wave == 2 && trial.enemyCount() == 4, "pressure middle wave has four enemies");
        trial.observe(207, true, 0);
        trial.observe(212, true, 0);
        check(trial.wave == 3 && trial.enemyCount() == 5, "pressure final wave has five enemies");
        trial.observe(213, false, 0);
        check(trial.phase == TrialPhase::Lost && trial.finished(),
              "simultaneous player death and final clear is loss");
        trial.observe(214, true, 0);
        check(trial.phase == TrialPhase::Lost && trial.elapsed == 13,
              "lost result cannot resurrect on later observations");
        trial.start(300, false);
        trial.observe(479.999, true, 1);
        check(trial.phase == TrialPhase::Active, "trial remains active just before timeout");
        trial.observe(480, true, 0);
        check(trial.phase == TrialPhase::Lost, "exact timeout takes priority over a clear");
        trial.start(500, false);
        trial.observe(501, true, 0);
        trial.observe(502, false, 0);
        check(trial.phase == TrialPhase::Lost && trial.wave == 1,
              "player death during recovery ends the trial without another wave");
        trial.start(600, false);
        trial.observe(601, true, -1);
        check(trial.phase == TrialPhase::Active, "invalid negative population is not a completed wave");
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
