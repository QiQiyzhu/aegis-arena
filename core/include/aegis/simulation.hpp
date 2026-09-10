#pragma once
#include "geometry.hpp"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

namespace aegis
{
using Clock = std::chrono::steady_clock;
inline double micros(Clock::duration elapsed)
{
    return std::chrono::duration<double, std::micro>(elapsed).count();
}
struct Scenario
{
    std::uint32_t seed = 1001;
    int enemies = 4;
    double duration = 60, step = 0.05;
    bool utilityCompanion = true, director = false;
    std::string arena = "pillars", player = "scripted", enemyPolicy = "priority";
    bool valid() const
    {
        return enemies >= 1 && enemies <= 50 && duration >= 1 && duration <= 300 && step == 0.05 &&
               (arena == "pillars" || arena == "open") && player == "scripted" && enemyPolicy == "priority";
    }
};
enum class State
{
    Patrol,
    Investigate,
    AcquireTarget,
    Chase,
    Attack,
    FindCover,
    Retreat,
    Recover,
    Follow,
    Support,
    Dead
};
inline const char* stateName(State s)
{
    constexpr const char* names[] = {"patrol",     "investigate", "acquire_target", "chase",  "attack",
                                     "find_cover", "retreat",     "recover",        "follow", "support",
                                     "dead"};
    return names[static_cast<unsigned>(s)];
}
struct Bot
{
    int id = 0;
    Team team = Team::Enemy;
    bool companion = false, elite = false;
    Vec position, goal, remembered;
    Health health;
    Cooldown shot, support;
    double seenAt = -1000, nextDecision = 0, stuckSince = -1, lastHit = -1000;
    int target = -1;
    bool visible = false;
    Action action = Action::Follow;
    State state = State::Patrol;
    Scores scores{};
    double travelled = 0;
};
struct Metrics
{
    bool win = false;
    double survival = 0, dealt = 0, taken = 0, firstEngage = -1, distance = 0;
    int companionDeaths = 0, enemyDeaths = 0, stuck = 0, coverUses = 0, decisions = 0, spawned = 0;
    std::array<int, 4> actions{};
    double decisionMicros = 0, queryMicros = 0, queryCount = 0, wallMicros = 0;
    std::vector<double> stepMicros;
};
class Simulation
{
    Scenario cfg_;
    Random rng_;
    std::vector<Bot> bots_;
    std::vector<Circle> cover_;
    Metrics m_;
    Director director_;
    double now_ = 0, nextSpawn_ = 10, recentDamage_ = 0;
    std::ofstream trace_;
    bool live(int i) const
    {
        return i >= 0 && i < static_cast<int>(bots_.size()) && bots_[i].health.alive();
    }
    int countEnemies() const
    {
        int n = 0;
        for (const auto& b : bots_)
            if (b.team == Team::Enemy && b.health.alive())
                ++n;
        return n;
    }
    void spawnEnemy(bool elite = false)
    {
        Bot b;
        b.id = static_cast<int>(bots_.size());
        b.elite = elite;
        b.position = {12 + rng_.unit() * 5, -12 + rng_.unit() * 24};
        b.goal = {-10 + rng_.unit() * 20, -10 + rng_.unit() * 20};
        b.health = {elite ? 140.0 : 65.0, elite ? 140.0 : 65.0};
        b.nextDecision = rng_.unit() * 0.2;
        bots_.push_back(b);
        ++m_.spawned;
    }
    void sense(Bot& b)
    {
        int nearest = -1;
        double distance = 15;
        for (const auto& other : bots_)
        {
            if (!other.health.alive() || !hostile(b.team, other.team))
                continue;
            const double d = length(other.position - b.position);
            if (d < distance && lineOfSight(b.position, other.position, cover_))
            {
                distance = d;
                nearest = other.id;
            }
        }
        b.visible = nearest >= 0;
        if (b.visible)
        {
            b.target = nearest;
            b.remembered = bots_[nearest].position;
            b.seenAt = now_;
        }
        else if (now_ - b.seenAt > 2.5)
            b.target = -1;
    }
    Vec queryCover(Bot& b, bool retreat)
    {
        const auto start = Clock::now();
        const Vec p = coverPoint(b.position, b.remembered, cover_, retreat);
        m_.queryMicros += micros(Clock::now() - start);
        ++m_.queryCount;
        if (length(p - b.position) > 0.2)
            ++m_.coverUses;
        return p;
    }
    void decide(Bot& b)
    {
        const auto start = Clock::now();
        sense(b);
        ++m_.decisions;
        const double fraction = b.health.current / b.health.maximum;
        const double distance = length(b.remembered - b.position);
        b.goal = b.position;
        if (b.companion)
        {
            // Allied telemetry is explicitly authorized only within local range and line of sight.
            const auto& ally = bots_[0];
            const double allyDistance = length(ally.position - b.position);
            const bool allyKnown =
                ally.health.alive() && allyDistance < 18 && lineOfSight(b.position, ally.position, cover_);
            Observation o;
            o.health = fraction;
            o.allyKnown = allyKnown;
            o.allyHealth = allyKnown ? ally.health.current / ally.health.maximum : 1;
            o.allyDistance = allyKnown ? allyDistance : 0;
            o.targetDistance = distance;
            o.targetVisible = b.visible;
            o.targetRemembered = b.target >= 0;
            o.supportReady = b.support.ready(now_);
            b.scores = utility(o);
            if (cfg_.utilityCompanion)
                b.action = choose(b.scores, b.action);
            else
                b.action = b.visible ? Action::Attack : Action::Follow;
            ++m_.actions[static_cast<unsigned>(b.action)];
            switch (b.action)
            {
            case Action::Support:
                b.state = State::Support;
                if (allyKnown)
                {
                    b.goal = ally.position;
                    if (allyDistance < 3 && b.support.consume(now_, 5))
                        bots_[0].health.heal(22);
                }
                break;
            case Action::Retreat:
                b.state = State::Retreat;
                b.goal = queryCover(b, true);
                break;
            case Action::Attack:
                b.state = b.visible ? State::Attack : State::Investigate;
                if (distance > 7 || !b.visible)
                    b.goal = b.remembered;
                break;
            default:
                b.state = State::Follow;
                if (allyKnown && allyDistance > 3)
                    b.goal = ally.position;
                break;
            }
        }
        else if (b.id == 0)
        {
            // Player baseline uses exactly the same local sensor path, never a global nearest actor.
            b.state = b.visible ? State::Attack : State::Patrol;
            if (b.target >= 0 && (!b.visible || distance > 9))
                b.goal = b.remembered;
            else if (b.visible && distance < 5)
                b.goal = b.position + normalized(b.position - b.remembered) * 3;
            else if (b.target < 0)
                b.goal = {0, 5 * std::sin(now_ * 0.3)};
        }
        else
        {
            // Deterministic priority baseline. This is not an Unreal Behavior Tree execution.
            if (fraction < 0.28 && b.target >= 0)
            {
                b.state = State::Retreat;
                b.goal = queryCover(b, true);
            }
            else if (!b.visible && now_ - b.lastHit > 4 && fraction < 0.65)
            {
                b.state = State::Recover;
                b.health.heal(0.7);
            }
            else if (b.visible && distance < 9 && b.shot.ready(now_))
                b.state = State::Attack;
            else if (b.visible && fraction < 0.65 && now_ - b.lastHit < 2)
            {
                b.state = State::FindCover;
                b.goal = queryCover(b, false);
            }
            else if (b.visible)
            {
                b.state = State::Chase;
                b.goal = b.remembered;
            }
            else if (b.target >= 0)
            {
                b.state = State::Investigate;
                b.goal = b.remembered;
            }
            else
            {
                b.state = State::Patrol;
                b.goal = {8 + 4 * std::sin(now_ * 0.2 + b.id), 10 * std::cos(now_ * 0.2 + b.id)};
            }
        }
        m_.decisionMicros += micros(Clock::now() - start);
    }
    void move(Bot& b)
    {
        Vec delta = b.goal - b.position;
        const double remaining = length(delta);
        if (remaining < 0.15)
        {
            b.stuckSince = -1;
            return;
        }
        double speed = b.team == Team::Player ? 4.2 : (b.elite ? 3.3 : 2.8);
        Vec step = normalized(delta) * std::min(remaining, speed * cfg_.step), before = b.position;
        Vec proposed = b.position + step;
        if (navigable(proposed, cover_))
            b.position = proposed;
        else if (navigable(b.position + Vec{step.x, 0}, cover_))
            b.position.x += step.x;
        else if (navigable(b.position + Vec{0, step.y}, cover_))
            b.position.y += step.y;
        const double d = length(b.position - before);
        b.travelled += d;
        if (d < 0.01)
        {
            if (b.stuckSince < 0)
                b.stuckSince = now_;
            else if (now_ - b.stuckSince >= 2)
            {
                ++m_.stuck;
                b.stuckSince = now_;
            }
        }
        else
            b.stuckSince = -1;
    }
    void attack(Bot& b)
    {
        if (!b.visible || !live(b.target) || b.state != State::Attack)
            return;
        auto& target = bots_[b.target];
        const double distance = length(target.position - b.position);
        // Perception authorizes intent; collision/LOS validates the physical attack at execution time.
        if (distance > 11 || !lineOfSight(b.position, target.position, cover_))
            return;
        if (!b.shot.consume(now_, b.team == Team::Player ? 0.7 : (b.elite ? 0.9 : 1.2)))
            return;
        if (m_.firstEngage < 0)
            m_.firstEngage = now_;
        const double amount = distance < 1.8 ? 20 : (b.team == Team::Player ? 14 : (b.elite ? 13 : 8));
        // Inaccuracy is explicit seeded spread, not randomized decision logic.
        if (rng_.unit() > 0.85)
            return;
        const double damage = target.health.damage(amount, b.team, target.team);
        target.lastHit = now_;
        if (target.id == 0)
        {
            m_.taken += damage;
            recentDamage_ += damage;
        }
        if (b.team == Team::Player)
            m_.dealt += damage;
        if (!target.health.alive())
        {
            target.state = State::Dead;
            if (target.companion)
                ++m_.companionDeaths;
            else if (target.team == Team::Enemy)
                ++m_.enemyDeaths;
        }
    }
    void trace()
    {
        if (!trace_)
            return;
        for (const auto& b : bots_)
            trace_ << now_ << ',' << b.id << ',' << static_cast<int>(b.team) << ',' << b.position.x << ','
                   << b.position.y << ',' << b.health.current << ',' << stateName(b.state) << ',' << b.target
                   << ',' << b.visible << '\n';
    }

  public:
    explicit Simulation(const Scenario& cfg, const std::string& tracePath = "") : cfg_(cfg), rng_(cfg.seed)
    {
        bots_.reserve(256);
        if (cfg.arena == "pillars")
            cover_ = {{{-3, 4}, 1.8}, {{3, -4}, 1.8}, {{6, 6}, 1.4}, {{-7, -6}, 1.4}};
        Bot player;
        player.id = 0;
        player.team = Team::Player;
        player.position = {-13, 0};
        player.health = {130, 130};
        bots_.push_back(player);
        Bot companion;
        companion.id = 1;
        companion.team = Team::Player;
        companion.companion = true;
        companion.position = {-15, 2};
        bots_.push_back(companion);
        for (int i = 0; i < cfg.enemies; ++i)
            spawnEnemy(i == cfg.enemies - 1 && cfg.enemies >= 4);
        if (!tracePath.empty())
        {
            trace_.open(tracePath);
            if (!trace_)
                throw std::runtime_error("cannot open trace");
            trace_ << "time,id,team,x,y,health,state,target,visible\n";
        }
    }
    Metrics run()
    {
        const auto start = Clock::now();
        const int steps = static_cast<int>(std::ceil(cfg_.duration / cfg_.step));
        for (int step = 0; step < steps; ++step)
        {
            now_ = step * cfg_.step;
            const auto frame = Clock::now();
            for (auto& b : bots_)
                if (b.health.alive())
                {
                    if (now_ + 1e-6 >= b.nextDecision)
                    {
                        decide(b);
                        b.nextDecision = now_ + 0.2;
                    }
                    move(b);
                    attack(b);
                }
            recentDamage_ *= std::exp(-cfg_.step / 5);
            if (cfg_.director)
            {
                DirectorInput input;
                input.playerHealth = bots_[0].health.current / 130;
                input.recentDamage = recentDamage_;
                input.enemyCount = countEnemies();
                input.combatSeconds = now_;
                input.performance = m_.enemyDeaths / std::max(1.0, now_ / 10);
                const auto out = director_.update(input, now_, cfg_.step);
                if (now_ >= nextSpawn_ && bots_.size() < 250)
                {
                    for (int j = 0; j < out.spawnBudget; ++j)
                        spawnEnemy(rng_.unit() < out.eliteProbability);
                    nextSpawn_ = now_ + 10 + out.recoverySeconds;
                }
            }
            if (step % 4 == 0)
                trace();
            m_.survival = std::min(cfg_.duration, now_ + cfg_.step);
            m_.stepMicros.push_back(micros(Clock::now() - frame));
            if (!bots_[0].health.alive())
                break;
            if (countEnemies() == 0 && !cfg_.director)
            {
                m_.win = true;
                break;
            }
        }
        if (cfg_.director)
            m_.win = bots_[0].health.alive();
        for (const auto& b : bots_)
            m_.distance += b.travelled;
        m_.wallMicros = micros(Clock::now() - start);
        trace();
        return m_;
    }
};
inline double percentile(std::vector<double> values, double p)
{
    if (values.empty())
        return 0;
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>((values.size() - 1) * p)];
}
inline void writeJson(std::ostream& out, const Scenario& c, const Metrics& m)
{
    out << std::fixed << std::setprecision(6)
        << "{\"schema_version\":1,\"engine\":\"portable-cpp-model\",\"seed\":" << c.seed << ",\"arena\":\""
        << c.arena << "\",\"policy\":\"" << (c.utilityCompanion ? "utility" : "priority")
        << "\",\"enemies\":" << c.enemies << ",\"duration_limit\":" << c.duration
        << ",\"director\":" << (c.director ? "true" : "false") << ",\"win\":" << (m.win ? "true" : "false")
        << ",\"survival_seconds\":" << m.survival << ",\"damage_dealt\":" << m.dealt
        << ",\"damage_taken\":" << m.taken << ",\"companion_deaths\":" << m.companionDeaths
        << ",\"enemy_deaths\":" << m.enemyDeaths << ",\"spawned\":" << m.spawned
        << ",\"time_to_engage\":" << (m.firstEngage < 0 ? "null" : std::to_string(m.firstEngage))
        << ",\"stuck_events\":" << m.stuck << ",\"distance_travelled\":" << m.distance
        << ",\"cover_usage\":" << m.coverUses << ",\"decision_counts\":" << m.decisions
        << ",\"companion_actions\":{\"follow\":" << m.actions[0] << ",\"attack\":" << m.actions[1]
        << ",\"support\":" << m.actions[2] << ",\"retreat\":" << m.actions[3] << "}"
        << ",\"cpu\":{\"wall_ms\":" << m.wallMicros / 1000
        << ",\"decision_mean_us\":" << (m.decisions ? m.decisionMicros / m.decisions : 0)
        << ",\"geometry_query_mean_us\":" << (m.queryCount ? m.queryMicros / m.queryCount : 0)
        << ",\"step_p50_us\":" << percentile(m.stepMicros, 0.5)
        << ",\"step_p95_us\":" << percentile(m.stepMicros, 0.95)
        << ",\"memory_bytes\":null,\"unreal_game_thread_ms\":null,\"unreal_eqs_ms\":null}}\n";
}
} // namespace aegis
