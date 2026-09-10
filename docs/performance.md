# Performance evidence and limits

**No Unreal Game Thread, engine EQS, rendered frame time or process memory result exists yet.** The current executable measures portable-model CPU work with `std::chrono::steady_clock`. These are microbenchmarks on a shared desktop, not a game FPS claim.

Source: [per-episode measurements](../evidence/portable/performance/episodes.json), [aggregate/provenance](../evidence/portable/performance/report.json). C++17, Clang 20.1.2 via verified Zig 0.15.2, `-O2 -Wall -Wextra -Werror -pedantic`.

| Initial enemies | Episodes | Mean decision cost | Mean geometry query cost | Mean of episode step P95 |
|---:|---:|---:|---:|---:|
| 1 | 5 | 0.348 µs | 2.544 µs | 2.32 µs |
| 10 | 5 | 0.205 µs | 2.101 µs | 2.98 µs |
| 25 | 5 | 0.116 µs | 1.973 µs | 3.38 µs |
| 50 | 5 | 0.142 µs | 3.941 µs | 4.90 µs |

The P95 column averages five per-episode percentiles; it is not the pooled percentile of all steps. Initial population is not held constant. Large encounters end rapidly when the player dies (mean 4.82 s with 50 enemies), so these are **not sustained-load tests**. Per-decision cost also changes with the mix of cheap patrol and expensive cover decisions; the decreasing mean does not imply that more bots are cheaper overall.

Memory is explicitly `null`, not zero. Unreal Game Thread and EQS fields are `null`. Engine CharacterMovement, collision, perception scheduling, nav queries, rendering and UObject memory are absent from this program.

## Engine profiling plan: required before a performance resume claim

1. Use a Development build and a fixed map/render configuration. Record CPU/GPU, RAM, OS, resolution, engine hotfix, compiler, build SHA and background load.
2. Spawn 1 / 10 / 25 / 50 bots. Sustain the population for 60 s after 10 s warmup using a dedicated non-scoring load scenario; do not use early victory/death episodes as throughput tests.
3. Record Unreal Insights CPU trace and memory trace, plus frame-time CSV. Report median/P95/P99 Game Thread and total frame time, AI service work, actual EQS query cost, and peak working set.
4. Inspect service counts, query counts, queue delay and failed path counts. First compare 5 Hz decisions/1 Hz EQS against a controlled alternate frequency; change one variable at a time.
5. Keep correctness checks: lower sensing frequency must not break lost-target memory, attack response or support authorization.

Current source budgets: observation services 0.2 s ±0.02 s, director 0.25 s, scenario sampling 0.1 s, at most one outstanding EQS query and at most one new query per second per controller. No project decision Actor uses Tick. Engine subsystems still tick normally.

A global EQS queue budget, significance-based sensing and StateTree comparison should be added only if engine traces justify them. No measured optimization percentage is claimed now.

Official reference: [Unreal Insights](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-insights-in-unreal-engine).
