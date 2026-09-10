# Acceptance status

Recorded 2026-09-10. Compiled source, NullRHI game-world execution, rendered measurement and packaged-window operation are distinct evidence levels.

## Complete and actually run

- Portable C++17 strict build: **325 assertions**, **16 Python cases**, **60 model evaluation episodes**, **20 model CPU samples**. The earlier [hosted Ubuntu GCC / Python CI](https://github.com/QiQiyzhu/aegis-arena/actions/runs/34440960072) passed; it runs no Unreal.
- **UE 5.8.2 CL 56702186 / MSVC 14.50.35738 / Windows SDK 26100**: Editor compiled and linked; Game Development and Shipping also have actual successful build logs. The Game packaging builds refresh those separate targets.
- **Ten native assets** generated and saved by the real Editor: two maps, BB, BT, three EQS graphs and three materials. Saved graph structure, keys, positive query scoring, references, camera tag/rotation, movable lights and physical navigation bounds passed [reload inspection](../evidence/unreal/asset-inspection.json).
- **Five Core Automation tests passed**, zero failures/notRun/inProcess. The separate **one native World Functional Test passed**, executing twelve named assertions for physical ranged hit, cooldown, team filtering, death/healing, real possession, a moved Pawn’s EQS Querier and missing-query fallback. The runner requires PIE/begun-play and rejects JSON-only success. [Raw Core](../evidence/unreal/automation/index.json) / [Functional](../evidence/unreal/functional/index.json).
- **60 native policy episodes** with four enemies, paired seeds 1001–1030, NullRHI and 1/60 fixed game step. Actual BT/Perception/EQS/navigation, damage and JSON/CSV capture ran. Both policies won 0/30; utility companion deaths at episode termination 6 vs priority 25, with less allied damage output and greater cumulative player damage taken. Utility episodes also ended earlier; unequal observation windows prevent a causal protection claim. [Protocol and limits](native-evaluation.md).
- **12 rendered performance episodes**: 1/10/25/50 enemies plus two allies, three seeds each, 15 seconds sustained damage-disabled load, RTX 4060 Laptop / D3D12 / 1280×720 offscreen. [Actual results and limits](performance.md).
- **Actual native PNG/GIF**, viewed to verify the scene, camera and lights. The 13-frame GIF is sampled at 2 fps, not a game-framerate measurement. The old portable GIF remains explicitly labelled non-Unreal.
- Nine developer command/overlay/query-debug markers were present in the final Development binary and absent from the final Shipping binary. It is separate from runtime acceptance.
- Complete [A–T interview dossier](interview-dossier.md), including actual failures and unfavorable outcomes; no invented RL, RAG, commercial deployment or optimization percentage.

## Packaged runtime acceptance

The final Development and Shipping revisions were built, cooked, staged and archived in fresh directories. UAT BuildCookRun completed in 153.34s / 128.22s respectively; these are build wall times under concurrent desktop load, not performance benchmarks. The standalone Development executable completed two validated game-world episodes with no Editor dependency. The actual Shipping window rendered the map and running AI; its diagnostics HUD was absent and the grave key did not open the developer console. Nine compiled developer markers were present in Development and absent from Shipping. [Package record and download](release.md).

A late Development GUI run encountered a Windows firewall permission dialog; no security control was automated or changed. The offline batch execution passed, and the final Shipping window ran without that dialog. Basic input bindings were exercised during earlier Development acceptance, but exhaustive mouse/controller/player usability testing is outside this technical AI lab's evidence. The Shipping inspection proves launch/presentation and the diagnostic boundary, not every input combination.

## P1 — explicit remaining scope limits

- The actual runtime overlay and raw EQS candidate scores are captured, with the recent BT action and query IDs. A recording of the Editor’s own live BT/EQS graph debugger remains optional follow-up; the runtime image is not labelled as that debugger.
- Add targeted native tests for perception-loss memory expiry, repeated-batch Actor/Controller count invariants and distinct cancellation/no-candidate categories. Current scenario completion is useful but does not prove every lifecycle edge.
- More arenas and independent holdout seeds. The four-enemy stress case has a win-rate floor; define follow-up experiments before tuning.
- Longer profiling warmup, longer independent sessions and Unreal Insights CPU/GPU/memory traces. Short invulnerable runs have a limited tactical-query count and do not prove worst-case or commercial performance.
- Primitive visuals and basic controls are appropriate to a technical AI lab. This is not a commercial action game or an animation-production portfolio.

## P2 — optional, not implemented

StateTree is not duplicated alongside BT without a specific need. Learning Agents, imitation datasets, learned policies and training curves are absent. Experimental plugin availability is not a training result.
