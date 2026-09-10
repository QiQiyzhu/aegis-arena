# Evaluation: evidence, not policy labels

All results below are from the compiled **portable C++ model**, not Unreal. No engine evaluation has been run yet. Raw data and exact configuration are in [the report](../evidence/portable/evaluation/report.json), [JSON episodes](../evidence/portable/evaluation/episodes.json), and [CSV](../evidence/portable/evaluation/episodes.csv).

## Protocol

- Fixed pillar arena, scripted player, priority enemy policy, four initial enemies (one elite), 60 s maximum duration, 50 ms simulation step, 200 ms decisions.
- Companion comparison: priority Attack/Follow versus utility Follow/Attack/Support/Retreat.
- Development seed reservation: 1–10. Evaluation: **1001–1030**, paired by policy. Performance reservation: 5001–5005.
- Weights were not tuned to obtain a desired evaluation winner. The observed unfavorable win-rate result is retained.
- Same seed gives identical model gameplay metrics within the verified build; CPU timings are deliberately excluded from deterministic equality assertions. Explicit xorshift32 avoids standard-library distribution differences.
- Win = player alive and every initial enemy dead before timeout. Timeout is not a win. Survival terminates on player death, victory or duration limit. In the separate director-enabled development scenario, win means surviving the duration, so those rows must not be mixed with the fixed encounter comparison.

## Observed results

| Metric | Priority companion | Utility companion |
|---|---:|---:|
| Episodes | 30 | 30 |
| Wins | 20 | 19 |
| Win rate | 66.7% | 63.3% |
| 95% Wilson interval | 48.8–80.8% | 45.5–78.1% |
| Companion deaths | 16 | 6 |
| Mean player damage taken | 86.53 | 104.50 |
| Mean allied damage dealt | 359.96 | 344.22 |
| Mean observed episode duration | 33.91 s | 35.47 s |
| Mean stuck events | 4.17 | 5.17 |

The utility policy protected itself more often while delivering less offensive pressure. Support can also increase cumulative damage absorbed because healing extends survival. These outcomes do not establish causal superiority of one action weight or statistical significance of the death difference. The binomial intervals are per-policy uncertainty estimates, not a paired significance test. More independent arenas/seeds and ablations are required before generalizing.

## Metric definitions

| Output | Definition |
|---|---|
| survival_seconds | Elapsed simulated time until termination; victory time is not time-to-death |
| damage_dealt | Sum of applied hostile damage from player **and companion**, after health clamp |
| damage_taken | Applied damage to player only; may exceed initial maximum because of healing |
| companion_deaths / enemy_deaths | Count alive→dead transitions, counted once per actor |
| time_to_engage | First valid attack attempt in the model; null if none. UE adapter currently records first applied damage instead, so these definitions must be aligned before cross-environment comparison. |
| stuck_events | Movement requested but displacement < 0.01 m for 2 s; every additional 2 s counts another event |
| distance_travelled | Sum of displacement across all agents, metres in model; UE exports centimetres |
| cover_usage | Accepted nontrivial cover destination selections; **not** occupancy time or successful path arrivals |
| decision_counts | Number of actual observation + decision invocations across alive actors |
| companion_actions | Selection counts per companion action; these are not execution-success counts |

The reference model's axis-slide navigation can become stuck behind a pillar. This is intentionally visible in the report and replay. It is not presented as a NavMesh benchmark.

## Reproduce without changing evidence

```bash
python scripts/build_portable.py --compiler g++
python scripts/run_benchmark.py --scenario scenarios/evaluation.json --output outputs/reproduction-01
```

The runner validates inputs before invoking a binary and refuses to overwrite an existing report. Outputs include UTC timestamp, source/binary SHA256, compiler/version/flags, host platform, full scenario and per-episode rows. CI reruns the same contract on Linux; a configured workflow is not itself proof of a successful hosted CI run.

## Unreal runner acceptance

The `AAegisScenarioRunner` source provides editable map name, policy, count, starting seed, duration and episode count. In PIE, its **Run Batch** button or `aegis.RunScenario` starts capture. It validates map, navigation and BT/EQS assets; creates only its own actors; subscribes to actual damage callbacks; samples positions and health at 10 Hz; and writes JSON/CSV to `Saved/AegisReports/<UTC timestamp>`.

It currently requires manual BT/EQS asset wiring and does not produce verified results until run in the engine. Engine seeds set spawn placement; engine scheduling and default navigation sampling are not certified deterministic. The director is kept out of fixed policy comparisons. A real UE evaluation must also align the metric differences above and confirm there are no pre-existing combat actors contaminating the encounter.
