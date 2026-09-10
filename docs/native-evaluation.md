# Native Unreal evaluation

These are actual **UE 5.8.2 game-world episodes**, not the portable simulation. The Editor compiled, saved BB/BT/EQS assets were reloaded and structurally checked, five Core Automation tests passed and a separate PIE Functional Test passed all twelve real-world assertions, including moving Querier and missing-query fallback regressions. The runner creates characters/controllers, uses real AI Perception, navigation, BT tasks and EQS, records actual damage and exports JSON/CSV. [Raw native reports](../evidence/unreal/evaluation/) · [Aggregate and exact provenance](../evidence/unreal/evaluation/report.json).

## Protocol

One primitive arena, scripted allied player plus companion, four enemies including one elite, maximum 60 game seconds, Director off. Seeds **1001–1030** are paired across priority and utility companion policies: **60 episodes**. UE used `-NullRHI -UseFixedTimeStep -FPS=60`; the fixed game-time interval is 1/60 second. It accelerates evaluation and is **not rendered performance**. Engine scheduling, perception ordering and navigation are not certified bit deterministic; equal project seeds do not establish identical physics trajectories.

The final source/content digests match the separately rendered performance report. Early failed navigation/camera runs and prior revisions are not pooled into these 60 rows. No weights were tuned to produce a desired winner.

| Metric | Priority companion | Utility companion |
|---|---:|---:|
| Wins / 30 | 0 | 0 |
| Companion deaths / 30 | 25 | 6 |
| Mean player damage taken | 106.68 | 126.06 |
| Mean allied damage dealt | 170.46 | 139.40 |
| Mean episode duration, game seconds | 25.38 | 21.76 |
| Completed EQS queries | 878 | 892 |
| Failed EQS queries | 16 | 8 |
| Failed query rate | 1.82% | 0.90% |

Both policies won **0/30**. Each win-rate Wilson 95% interval is 0–11.35%; these separate intervals are not a paired significance test. This difficult encounter has a win-rate floor, so it does not discriminate policies by wins. Utility reduced companion deaths in this sample while allied offense was lower and player damage taken was higher. It is not an overall improvement claim. A follow-up should predefine an easier encounter and independent seeds, retain both results, and measure the intended team objective rather than tune against this table.

Damage taken can exceed starting health because support heals. Duration ends at death, victory or timeout; it is not always time-to-death. Native time-to-engage is first applied damage, native distances are centimetres, and cover usage counts accepted cover-query destinations rather than occupancy time. The portable model has different geometry, timing and some metric definitions; do not compare those values as if they came from one environment.

The earlier high failure rate exposed an actual spatial-context and BT fallback defect; [the diagnosis, raw pre-fix records and correction](eqs-debugging.md) are preserved. Final failed EQS queries are reported rather than replaced with invented valid points. They include candidates filtered by navigation/trace/range and requests canceled at shutdown. A query completion count is not a successful path-arrival count. More maps, longer episodes, explicit cancellation-vs-no-candidate categories and targeted perception-loss tests remain useful follow-up work.

## Reproduction

Build the checked-in assets with the Editor command in [setup](unreal-setup.md), then use fresh output directories:

```powershell
python scripts/run_unreal_scenario.py --engine-root 'D:/Program Files/UE_5.8' --cache-root 'D:/AegisWork' --output 'D:/AegisWork/Reports/reproduce-priority' --policy priority --enemies 4 --episodes 30 --seed 1001 --duration 60 --fixed-step
python scripts/run_unreal_scenario.py --engine-root 'D:/Program Files/UE_5.8' --cache-root 'D:/AegisWork' --output 'D:/AegisWork/Reports/reproduce-utility' --policy utility --enemies 4 --episodes 30 --seed 1001 --duration 60 --fixed-step
python scripts/summarize_unreal.py --input 'D:/AegisWork/Reports/reproduce-priority' --input 'D:/AegisWork/Reports/reproduce-utility' --output 'D:/AegisWork/Reports/reproduced-policy.json'
```

The wrapper checks a fresh successful completion marker, exact episode count, requested seed/configuration, finite metrics and nonzero decisions. It rejects stale directories and mismatched rendering modes. Aggregation refuses mixed source/content revisions or duplicate seeds within a condition. A graceful Unreal exit code 0 alone cannot pass the gate.

The [actual native screenshot](media/unreal-arena.png) and [sampled GIF](media/unreal-arena.gif) come from a separate rendered capture. The GIF uses thirteen actual engine frames sampled every 0.5 game seconds and resized to 960×540; the run ended on player death after 13 frames; its 2 fps sampling rate is not the game framerate. Dynamic lights are used; no baked lightmap or external artwork is claimed.
