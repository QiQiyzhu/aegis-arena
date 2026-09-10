# Acceptance status

Recorded 2026-09-10. This document deliberately separates compiled evidence from Unreal integration work.

## Complete and run

- Portable C++17: health, hostile team filtering, cooldown, fixed PRNG, authorized observation DTO, companion utility + score hysteresis, bounded director + smoothing/cooldown/hysteresis, LOS/collision/cover sampler.
- Standalone fixed-step model with actual combat, movement, support, death, seeded spread, state transitions, metrics, and CSV trace. It does not use Unreal physics, AI Perception, EQS, NavMesh, or BT.
- 325 core assertions, 7 Python tests, 60 evaluation episodes and 20 CPU sampling episodes. Raw per-episode files, compiler flags, source/binary digests, platform and timestamp are retained.
- Trace GIF rendered directly from an actual episode CSV. Every image identifies the portable model.

## P0 — must pass before saying “Unreal project completed”

1. Install UE 5.7 and VS2022 C++ toolchain on a drive with space. Detection found no usable UE or MSVC installation; the old registry path was stale.
2. Compile both runtime and editor modules. Resolve UHT/UBT/compiler integration errors against the installed engine. Source review is not compilation.
3. Run five `Aegis.Core.*` Unreal Automation tests and inspect the exported report; the 325 portable assertions do not imply these passed.
4. Execute `scripts/unreal/create_arena.py` inside the editor. Add a real NavMesh bounds brush and build navigation. Save `/Game/Aegis/Maps/AegisArena` and run `AAegisCombatFunctionalTest`. No `.umap` currently exists in this repository.
5. Author the BT/BB and three EQS assets according to `ai-design.md`; wire them to the runner and placed AI. Their C++ services/tasks/context exist; the actual engine graph assets are not yet authored.
6. Play the actual arena, confirm player controls, enemy/companion state transitions, loss-of-sight memory expiry, support authorization, cover queries, melee/trace damage, and cleanup across repeated runs.

## P1 — must pass before portfolio media / engine benchmarks

1. Execute two policies on the same fixed seed list in `AAegisScenarioRunner`. Verify damage callbacks, alive/dead transitions, navigation distance, and result writes. UE seeds currently control spawn layout; engine navigation/perception scheduling is not claimed bit deterministic.
2. Capture a genuine BT debug view, EQS candidate/score visualization, gameplay video, companion utility HUD and runner operation. The portable GIF cannot replace any of these.
3. Profile Development builds at 1 / 10 / 25 / 50 bots with Unreal Insights. Capture Game Thread, AI service, EQS, frame distribution and process memory under controlled duration. Portable CPU microseconds are not these measurements.
4. Build Shipping and verify that `aegis.*` console registrations are absent and no overlay is drawn. These paths are compile-gated in source; Shipping has not been built.

## P2 — optional, not a dependency of the lab

- StateTree: not implemented because BT execution + utility selection are sufficient for this scope; document a future experiment rather than presenting an empty third policy.
- Learning Agents / imitation learning: not implemented or trained. Only attempt after a supported installed experimental plugin, functioning baseline, reproducible environment and independent evaluation are available.
- Improve the portable model's simple navigation only if it serves a specific evaluation hypothesis. Stuck counts are reported, not hidden.

## Milestone ledger

| User milestone | Current status |
|---|---|
| C++ gameplay foundation | Adapter implemented; pure rules compiled; Unreal behavior pending |
| Enemy BT / BB / Perception / EQS | Source nodes and graph specification; assets and engine run pending |
| Companion utility | Pure scorer + model verified; UE wiring/run pending |
| Director | Pure rules verified; UE observer emits recommendations; encounter spawner wiring pending |
| Scenario runner | Portable batch tested; UE runner actor implemented, unrun |
| Editor/development evaluation panel | Runner actor editable properties + Run Batch button/console, unrun |
| RL / imitation | Intentionally absent |
| Performance | Model CPU evidence only; all engine metrics pending |
| Automated tests | Portable passed; UE Automation/functional test source unrun |
| Debug overlay / tools | Source implemented; UI and Shipping acceptance pending |
| Portfolio | Honest current-stage README, architecture, raw evidence, replay, interview documentation |
