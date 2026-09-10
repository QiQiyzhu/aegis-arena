# Performance evidence and limits

## Actual rendered Unreal measurements

Twelve native episodes ran on UE **5.8.2 CL 56702186**, Win64 Development **editor game**, Windows 11, i9-13900HX, 16 GiB RAM, **NVIDIA RTX 4060 Laptop GPU / D3D12 / driver 577.00**, requested 1280×720 offscreen rendering. The actual RHI log selects adapter 0; the Intel GPU name in the separate NullRHI Automation inventory is not the rendered device. MSVC 14.50.35738 / Windows SDK 26100 compiled the project.

Protocol: 1 / 10 / 25 / 50 enemies, **plus two allied scripted AI**, three seeds 5001–5003 per count, 15 real-time game seconds each, damage disabled to sustain the population, Director off, utility companion, one arena. The first second of each episode is excluded. Shaders were warmed by the separate capture run; no screenshots were requested during timing. Other project browser tests/builds were paused for the measurement window. This is a short desktop sample, not a controlled hardware lab or a shipping-build FPS guarantee.

[Raw episodes and provenance](../evidence/unreal/performance/) · [Aggregate](../evidence/unreal/performance/report.json)

| Enemies | Mean frame ms | Mean episode frame P95 ms | Mean Game Thread busy ms | Decision µs/call | EQS µs/profiled query | Peak process MiB |
|---|---:|---:|---:|---:|---:|---:|
| 1 (+2 allies) | 3.304 | 4.042 | 1.205 | 8.11 | 1278.55 | 1877.2 |
| 10 (+2 allies) | 3.369 | 4.171 | 1.723 | 3.79 | 353.26 | 1876.8 |
| 25 (+2 allies) | 3.400 | 4.261 | 2.134 | 4.16 | 304.51 | 1872.7 |
| 50 (+2 allies) | 3.422 | 4.325 | 2.559 | 4.10 | 250.63 | 1887.6 |

The percentile column is the **mean of three episode P95 values**, not a pooled P95. Frame wall time, Game Thread busy time and narrow decision/EQS CPU scopes are different measurements. Process residency includes the editor-game runtime, engine content and drivers; it is not project-only memory. No GPU timing, Unreal Insights trace, P99, memory leak proof or optimization percentage is claimed.

The workload has important limits: invulnerability keeps actors alive and removes low-health support/cover behavior; only 2 / 14 / 44 / 45 queries completed across the three episodes at the four loads, with failed-query counts 0 / 0 / 0 / 0 in this sample. These are the post-spatial-context-fix results; the earlier high-failure workload is retained separately in `evidence/unreal/before-eqs-fix/performance/`. The small number and mix of queries still do not form a worst-case sustained-EQS workload. A future study should sustain 60 seconds after a longer warmup, exercise dense tactical queries, record Insights traces and repeat independent sessions. Do not infer global AI scalability from these short samples.

Budget controls: 200ms observation services, one outstanding EQS request per Controller, at least one second between query submissions, 100ms scenario sampling, 250ms Director observations and finite population/corpse limits. They are implementation constraints, not evidence that a prior version was slower.

Reproduce each count in a fresh output directory:

```powershell
python scripts/run_unreal_scenario.py --engine-root 'D:/Program Files/UE_5.8' --cache-root 'D:/AegisWork' --output 'D:/AegisWork/Reports/my-rendered-50' --enemies 50 --episodes 3 --seed 5001 --duration 15 --performance --rendered
```

Do not add `--fixed-step`: accelerated policy evaluation is a different experiment. The wrapper rejects that combination.

## Separate portable CPU model

The following earlier 20 samples measure the portable C++ model with `std::chrono::steady_clock`; they exclude UE movement, NavMesh, Perception, collision, rendering and UObject costs. They cannot be compared numerically to the table above.

Source: [per-episode measurements](../evidence/portable/performance/episodes.json), [aggregate/provenance](../evidence/portable/performance/report.json). C++17, Clang 20.1.2 via verified Zig 0.15.2, `-O2 -Wall -Wextra -Werror -pedantic`.

| Initial enemies | Episodes | Mean decision cost | Mean geometry query cost | Mean of episode step P95 |
|---:|---:|---:|---:|---:|
| 1 | 5 | 0.348 µs | 2.544 µs | 2.32 µs |
| 10 | 5 | 0.205 µs | 2.101 µs | 2.98 µs |
| 25 | 5 | 0.116 µs | 1.973 µs | 3.38 µs |
| 50 | 5 | 0.142 µs | 3.941 µs | 4.90 µs |

The P95 column averages five per-episode percentiles; it is not the pooled percentile of all steps. Initial population is not held constant. Large encounters end rapidly when the player dies (mean 4.82 s with 50 enemies), so these are **not sustained-load tests**. Per-decision cost also changes with the mix of cheap patrol and expensive cover decisions; the decreasing mean does not imply that more bots are cheaper overall.

Memory is explicitly `null`, not zero. Unreal Game Thread and EQS fields are `null`. Engine CharacterMovement, collision, perception scheduling, nav queries, rendering and UObject memory are absent from this program.


Official profiling reference: [Unreal Insights](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-insights-in-unreal-engine).
