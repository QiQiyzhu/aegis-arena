# AI design and engine asset specification

These are implementation/authoring specifications. They are **not screenshots of executed Unreal assets**. UE graph authoring and validation remain pending.

## Knowledge contract

The companion receives self health, perceived target visibility/last-known memory, observed ally health/distance and support cooldown. It receives no global actor list or unseen target transform. Developer HUD and director telemetry are separate privileged consumers.

An enemy remembers the most recent stimulus position for 2.5 s. Sight makes a target attackable; damage provides an investigate location. A lost target's live Actor reference is removed from Blackboard. Recover is allowed after threat memory expires; retreat is considered below 28% health.

## Utility scores

| Action | Score |
|---|---|
| Follow | Known ally: `0.15 + 0.55 * clamp(distance / 12m)`; otherwise 0.1 |
| Attack | Visible: `0.45 + 0.30 * self_health`; remembered: 0.25; absent: 0 |
| Support | Known ally, cooldown ready, ally health below 0.8: `0.30 + 0.85 * (1 - ally_health)`; otherwise 0 |
| Retreat | `(1 - self_health) * (visible_threat ? 1.25 : 0.85)` |

The chosen action changes only when the winner exceeds the previous action by at least 0.08. This suppresses small score oscillations; it does not prevent urgent safety selection. The scorer does not assert tactical quality. The fixed evaluation actually showed a win-rate tradeoff: [results](evaluation.md).

## Blackboard: `BB_Aegis`

| Key | Type | Meaning |
|---|---|---|
| TargetActor | Object / Actor | Current visually observed enemy only |
| LastKnownLocation | Vector | Last authorized sight/damage position |
| HasLOS / HasMemory | Bool | Live visual contact / memory younger than 2.5 s |
| CriticalHealth / NeedsRecovery / NeedsCover | Bool | Service-derived state guards |
| InRange | Bool | Visible target within 900 cm |
| IsCompanion | Bool | Role configuration |
| UtilityAction | Int | Follow=0, Attack=1, Support=2, Retreat=3 |
| TacticalPoint | Vector | Completed EQS location |
| QueryPending | Bool | Outstanding query; prevents repeated scheduling |

## Behavior Tree: `BT_Aegis`

Root → Selector, with `BTService_AegisObserve`. Create these branches in priority order. Use Blackboard decorators with **Observer Aborts = Both** on role/action/visibility transitions. Every action sequence ends in **Wait = 0.2 s**; failed EQS branches must also yield through a fallback wait rather than tight retries.

| Priority | Guards | Sequence actions |
|---|---|---|
| 1 | IsCompanion, UtilityAction=3 | AegisAction(Retreat), Wait |
| 2 | IsCompanion, UtilityAction=2 | AegisAction(Support), Wait |
| 3 | IsCompanion, UtilityAction=1, HasLOS, InRange | AegisAction(Attack), Wait |
| 4 | IsCompanion, UtilityAction=1, HasMemory | AegisAction(Chase), Wait |
| 5 | IsCompanion | AegisAction(Follow), Wait |
| 6 | not IsCompanion, CriticalHealth, HasMemory | AegisAction(Retreat), Wait |
| 7 | not IsCompanion, NeedsRecovery | AegisAction(Recover), Wait |
| 8 | not IsCompanion, NeedsCover | AegisAction(FindCover), Wait |
| 9 | not IsCompanion, HasLOS, InRange | AegisAction(Attack), Wait |
| 10 | not IsCompanion, HasLOS | AegisAction(AttackPosition), Wait; fallback Chase |
| 11 | not IsCompanion, HasMemory | AegisAction(Investigate), Wait |
| 12 | otherwise | AegisAction(Patrol), Wait; fallback Wait |

The companion branches need a role-constrained parent selector: if a companion action fails, it must reach a Wait fallback inside that subtree rather than accidentally enter enemy branches. All enemy rows explicitly exclude companions. Root's final Wait prevents rapid failure loops when no move/query succeeds.

## EQS: three actual assets to author

Use **Points: Grid**, centered on Querier, 1600 cm extent, 200 cm spacing, projected to navigation. Threat context is `EnvQueryContext_AegisThreat`, whose location originates from authorized memory. Add a Pathfinding test to discard unreachable items. Resolve querier to its controlled Pawn in EQS as required by the installed engine's generator/test.

| Query | Filters | Scoring |
|---|---|---|
| EQ_Cover | Path exists; trace to Threat blocked | Prefer short travel distance; avoid being closer than 350 cm to threat |
| EQ_Attack | Path exists; trace to Threat clear; distance 500–1100 cm | Prefer about 800 cm combat distance and short travel |
| EQ_Retreat | Path exists; trace to Threat blocked; threat distance at least 600 cm | Prefer farther from threat with a bounded travel penalty |

Trace direction, eye-height offset and collision channel must be verified in the **EQS Testing Pawn** with actual arena walls. Use Visibility and a 30 cm offset consistent with attack traces. Save debug screenshots only after items, scores and selected points are visible in a real run. A geometric portable sampler does not count as an EQS implementation or capture.

## Director

Pressure is a normalized combination of low player health (0.65), recent damage (0.25) and enemy count (0.10), smoothed by an exponential moving average with 2 s time constant. Recovery enters above 0.58 and exits below 0.32. Policy changes have a 5 s cooldown. Outputs: spawn budget 0–3 with hard living-enemy cap 12, elite probability 0–0.25 and 8 s recovery recommendation. Population safety clamping applies even during cooldown.

The UE actor emits these recommendations; an encounter owner must consume them to spawn appropriate enemies. The portable development scenario contains an actual bounded spawner, evaluated separately from the fixed policy comparison. No LLM participates.

## Why these techniques

Unreal BT provides explicit priorities, aborts and inspectable execution history. Blackboard is typed working memory, not a source of omniscient truth. EQS ranks **where** to act after policy selects **what** to do. Utility naturally weighs a companion's conflicting support/survival/offense goals. StateTree fits persistent phases with explicit transitions, but adding it here would duplicate the current execution layer without a measured need.

Official references: [event-driven BT](https://dev.epicgames.com/documentation/en-us/unreal-engine/behavior-tree-in-unreal-engine---overview), [shared node instancing](https://dev.epicgames.com/documentation/en-us/unreal-engine/behavior-tree-node-reference-in-unreal-engine), [StateTree semantics](https://dev.epicgames.com/documentation/unreal-engine/overview-of-state-tree-in-unreal-engine?lang=en-US), [EQS request API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/AIModule/FEnvQueryRequest), [damage perception API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/Perception/UAISense_Damage?application_version=5.5).
