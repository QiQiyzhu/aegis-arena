# Decision Lab presentation audit

Date: 2026-09-17. Scope: source inspection; no Unreal run, API request, or new gameplay feature. Existing v1.3 work and historical evidence remain intact. This records the pre-lab behavior and the interfaces selected for the separate `-AegisDecisionLab` entry point.

## The important mismatch

The normal objective trial sets `bTacticalTrial=true` when spawning bots. `RefreshDecision()` still computes four Utility scores and writes the Blackboard, but `ExecuteAction()` immediately delegates every BT leaf to `ExecuteTacticalTrial()` in this mode. That routine implements commands, formation, navigation, windup and firing itself. It does not run an EQS query or a health-driven Retreat branch. Therefore a panel showing its Utility scores beside its movement must not imply that Utility and EQS caused those actions.

Sources: [spawn configuration](../Source/AegisArena/Private/AegisLab.cpp), [observation and action dispatch](../Source/AegisArena/Private/AegisAIController.cpp), [policy rules](../core/include/aegis/rules.hpp).

There is a second distinction inside the historical BT. The companion branches are Recover, Retreat, Support, Attack, Chase and Follow. Its Retreat requests EQS; its Attack/Chase branches do not request the attack-position query. FindCover and AttackPosition are enemy branches. An idle companion EQS panel can be correct while the companion attacks. Source: [asset builder](../Source/AegisArenaEditor/Private/AegisEditorLibrary.cpp), `BuildAIAssets()` and `Query()`.

## Existing useful feedback and its limits

| Surface | Already real and reusable | Limit to preserve in presentation |
| --- | --- | --- |
| Character | Shipping-safe shot tracers, hit flash, team/role silhouettes, fixed-point enemy windup, pulse ring | A tracer proves a shot, not damage. Windup is a visible-target snapshot, not tracking hidden actors. Healing currently has no dedicated character effect. |
| Main Canvas HUD | HP, cooldowns, companion state, objective progress, pause/restart/quit | Companion state is a short string; automatic tactical healing can be overwritten by the next movement/fire state. A command acknowledgement is not execution completion. |
| F1 HUD | Actual controller state, scores, selected query point and move status | It overlays the normal HUD, uses unscaled positions, mixes all controllers, and does not label scores as inactive in Tactical/Priority modes. |
| Perception | Actual Sight set, expiring location memory, explicit tactical player link | HUD must consume the evaluated observation. Reading hidden actors afresh would create a different, more informed observation. |
| BT | Conditions and leaf Success/Failure | Existing `DebugState` is assigned before validation. Last attempted leaf is not necessarily the accepted action, damage, arrival, or healing. |
| EQS diagnostics | Engine-generated scores, validity, failed-test details, query ID in `AEGIS_EQS` logs | Debug retention is opt-in, bounded, and absent in Shipping. `SingleResult` item count is not generated-candidate count. Old spheres can coexist for 1.2 seconds. Query success, accepted navigation and arrival are separate facts. |
| Planner panel | Independent Slate overlay and clear manual controls | A model's plan label is not a Utility switch reason. No cloud request is needed for this lab. |

The current F1 binding cannot be toggled while paused. The new lab should permit inspecting a frozen snapshot without advancing simulation. Unknown target distance/location must stay unknown; a remembered target permits only the stored, expiring location used by gameplay.

## Small, truthful separation

The accepted direction is an independent, opt-in Decision Lab. It runs the real Utility/priority BT path, uses a compact encounter and the existing player controls, and keeps v1.3's objective trial available separately.

**Demo view:** player and companion HP, a plain-language companion action, visible applied healing feedback, enemies cleared and a 60-second limit. Briefing explains four choices, three policies, Enter, same-seed R reset, pause and quit. Do not expand the controls with unrelated abilities or Copilot configuration.

**Debug view:** one companion, one right-side panel, toggled with F1. It shows the evaluated observation and its simulation-time age; four real scores; raw winner, selected action and switch/hold reason; last attempted and accepted BT leaf with their own sequence/time; query ID/status/submitting leaf, result age and a valid accepted point. Priority scores are labelled reference-only. Tactical scores are labelled not controlling if that mode is ever inspected.

The normal companion verb must come from accepted execution, not merely the largest score. Support selection can mean moving toward an ally; actual `+HP` comes from the applied heal amount. A successful Retreat task can mean waiting for an EQS result; an accepted movement request does not mean the destination was reached.

## Passive telemetry contract

The controller owns a read-only snapshot; the HUD performs no policy evaluation, AI perception query, path request, or enemy scan.

1. **Decision:** the exact `aegis::Observation` used by Evaluate, scores, sequence, game time, previous selection, raw winner, selected action, policy mode, and reason (`priority`, `hysteresis`, `raw_winner`). A switch count is a measured count, not a quality score.
2. **Execution:** last attempted BT leaf and result, last accepted leaf, execution and decision sequences, respective timestamps, plus actual applied support-heal count/amount/time. Keep decision selection and execution outcome separately labelled.
3. **EQS:** query ID/name, submitting leaf and decision sequence, submitted/completed times, state (including cancelled/stale), returned items, retained generated/valid counts or an explicit unavailable sentinel, accepted point plus acceptance flag. Invalidate the previous result when a new request or cancellation changes identity.
4. **Optional retained candidates:** bounded real positions/scores/validity, tied to that same query ID. Never substitute a custom navigation candidate list for EQS. Only draw retained candidates, and clear stale world annotations when the selected query changes. If candidate data was not retained, say so.

`FAegisDecisionTelemetry` / `GetDecisionTelemetry()` is the implementation boundary agreed with the controller owner. A Canvas-only `AAegisDebugHUD::DrawDecisionLab()` in its own source file can consume it; the existing HUD routes to that method only for the lab actor.

## Capture and validation reuse

Existing [input probe](../Source/AegisArena/Private/AegisInputProbe.cpp) supplies real `PC::InputKey`, offscreen virtual cursor, pause-aware real-time driving and completed screenshot callbacks. Its old assertions depend on the original trial population and buttons, so preserve that gate rather than relabel its success as lab coverage.

Existing [play probe](../Source/AegisArena/Private/AegisPlayProbe.cpp) records real player/enemy movement, shots and damage, with six PNGs. It excludes actual HUD hitboxes before holding fire. It does not record Observation/Utility/BT/EQS causality or prove all four companion behaviors. Reuse the mechanics and PNG/provenance validation; add lab-specific assertions separately.

A bounded lab observation should capture the normal view and the debug view, then verify that a displayed decision sequence matches its input and scores, that BT results retain their source decision sequence, and that any drawn EQS point belongs to the displayed query. Four-state coverage needs accepted actions and real effects, not four text labels. Automated control remains synthetic input evidence, not human usability or a balance verdict.

## Minimal correction for the legacy F1 display

Without changing old gameplay, label each controller's active path (Tactical, Utility BT, Priority BT). Mark Tactical scores `not controlling` and EQS `unused by tactical movement`; mark Priority scores `reference only`. Rename an unqualified state to `last task/state` until execution telemetry is used. Render a query point only when the corresponding validity flag says it exists. Historical benchmark and EQS evidence should retain its original scope.

## Rendered QA follow-up, 2026-09-17

The independent input fixture now has 35 native checks. It uses actual PlayerController input for deployment, movement, dash, held fire, F1, pause and same-seed reset. Four additional checks call the position-link registration API directly: reject a hostile target, reject an enemy controller linking another enemy on its own team, restore the allowed player link, and verify that baseline registers but never samples the link. These four checks are permission checks, not simulated key presses. No fixture health, damage, actor position or enemy AI override is applied.

The first rendered run exposed two real presentation issues despite successful input assertions: two paused buttons shared the `Menu` hitbox name, and Unreal's default F1 debug binding switched the viewport away from Lit. The duplicate hitbox was removed; the engine F1 binding was removed by the integration owner. The probe now explicitly requires `ViewModeIndex == VMI_Lit` both after opening diagnostics and after toggling it while paused. The second run exposed an incorrect probe assumption that a possessed Pawn still has the lab as its owner; the fixture now identifies the two living enemy actors by team and records their actual count.

Original captures inspected: `D:/AegisWork/Reports/decision-lab-20260917/input-editor-v3/lab-{briefing,active,debug,paused}.png`. At 1280×720 the briefing and controls fit, the live scene remains visible behind the right-side diagnostics, and the paused view has a single Resume button plus a noninteractive footer hint. The prior debug/paused black scene and shader-preparation message are absent. The baseline panel correctly distinguishes `ally sight` from `position link registered / inactive`; accepted Follow names `sight` as its source. This visual inspection covers these four frames, not combat balance, all action states or human usability.

### One documented engine warning, retained verbatim

The D3D11 v3 run completed all 35 native checks but the original strict wrapper rejected its sole warning:

```text
LogConsoleManager: Warning: Console variable 'r.MotionVectorSimulation' used in the render thread. Rendering artifacts could happen. Use ECVF_RenderThreadSafe or don't use in render thread.
```

The installed UE 5.8 source at `D:/Program Files/UE_5.8/Engine/Source/Runtime/Engine/Private/Rendering/MotionVectorSimulation.cpp:8` initializes `GMotionVectorSimulation` to zero and registers `CVarEnabled` without a render-thread-safe flag. This is the engine registration identified during diagnosis; the game has not patched the engine or silenced its logging. D3D11 avoids the separate NVIDIA/TSR fallback warning observed with the previous renderer, but does not remove this metadata warning.

The rendered input and rendered batch gates now accept **at most one exact full warning message**, including its category and severity, while preserving the complete original timestamped line in `provenance.json` under `knownEngineWarnings`. A different console variable, added suffix, changed severity, repeated warning, other warning/error, or any ensure still fails. NullRHI batch validation has no exception. Rendered launch commands explicitly select `-d3d11`.

The v3 failure provenance remains unchanged; revising the gate does not rewrite a failed historical run as a new successful run. The next formal packaged run must use a fresh output directory and record its own binaries, inputs, logs and PNG hashes. Offline gate tests cover the exact exception, rejected near matches, duplicates and unchanged NullRHI strictness.

### Final packaged acceptance

The four original files in `D:/AegisWork/Reports/decision-lab-20260917/input-packaged-final/` were inspected separately from the Editor captures: `lab-briefing.png`, `lab-active.png`, `lab-debug.png`, and `lab-paused.png`. The 1280×720 packaged images show complete briefing/policy/start controls, live player and companion HP, an intact right-side inspector, and a single set of pause actions. Text is not clipped; the debug and paused scenes remain rendered, with no black-screen regression. Briefing health cards are empty until the characters spawn on Enter; the active cards contain the real HP and companion action.

The packaged provenance reports `passed=true`, `launch=packaged-development`, 35 native assertions, four verified images, and exit code 0. Both the bootstrap executable and actual game payload are hashed. `knownEngineWarnings` retains exactly the original timestamped `r.MotionVectorSimulation` warning at `2026.09.17-08.05.06:009`; the engine log contains that same warning and the unique `AEGIS_LAB_INPUT_COMPLETE checks=35` marker. No runtime source, binary, wrapper or original evidence file was changed during this final read-only inspection. Acceptance remains limited to these captured UI states and the declared synthetic checks.
