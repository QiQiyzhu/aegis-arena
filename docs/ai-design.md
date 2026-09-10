# AI design and engine asset specification

The real UE 5.8.2 editor generated and saved the specified BB/BT/EQS assets. Native structural inspection verifies their saved nodes, role guards and query tests; the runtime scenario uses them. The diagrams below describe architecture, not screenshots of a running graph. Full BT/EQS editor-debugger captures remain a distinct acceptance item.

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

The chosen action changes only when the winner exceeds the previous action by at least 0.08. This suppresses small score oscillations; an overly large threshold can delay useful reactions and requires evaluation. The scorer does not assert tactical quality. The fixed evaluation actually showed a win-rate tradeoff: [results](evaluation.md).

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

A root Sequence holds the observation service, a role Selector, then Wait 0.2 s. The role Selector has two guarded subtrees: Companion when `IsCompanion=true`, enemy otherwise. A failed companion action cannot fall into enemy logic. Immutable native Blackboard comparison decorators are re-evaluated on each paced traversal; this implementation does not claim event-driven observer aborts.

Companion branches: recover when retreat is selected and threat memory has expired; retreat with remembered threat; support; attack in range; chase a remembered target; follow a perceived ally; final Wait. Enemy branches: critical retreat; recover; find cover; attack; seek attack position; investigate; patrol; final Wait. Both subtrees have a bounded wait fallback. Attack cooldown retains the attack branch instead of accidentally triggering patrol.

## Three native EQS assets

The generator uses a 700 cm grid half-size, 140 cm spacing (121 initial candidates), navigation projection and a path-existence filter. The authorized threat context is a remembered location, not an omniscient actor lookup.

| Query | Filtering | Scoring |
| --- | --- | --- |
| EQS_Cover | Reachable and geometry trace occluded | Prefer distance from threat within the local search region |
| EQS_Attack | Reachable, clear geometry trace, 300–900 cm threat distance | Prefer reference distance 650 cm |
| EQS_Retreat | Reachable | Prefer occlusion and greater threat distance |

The `AegisCoverTrace` collision channel ignores character capsules while level geometry blocks it. This avoids a target capsule at the context endpoint falsely marking all candidates as cover. Candidate height offset is 90 cm; threat context offset 30 cm. Actual path/occlusion/range tests ran in native scenarios, with raw engine candidate diagnostics retained in the EQS debugging report. A dedicated EQS Testing Pawn debugger recording remains an optional additional view. A portable geometry sampler is not an EQS capture.

## Director

Pressure is a normalized combination of low player health (0.65), recent damage (0.25) and enemy count (0.10), smoothed by an exponential moving average with 2 s time constant. Recovery enters above 0.58 and exits below 0.32. Policy changes have a 5 s cooldown. Outputs: spawn budget 0–3 with hard living-enemy cap 12, elite probability 0–0.25 and 8 s recovery recommendation. Population safety clamping applies even during cooldown.

The UE director emits recommendations; the Scenario Runner / interactive encounter owner now consumes them at a five-second interval, honors recovery and rechecks the live-enemy cap before spawning. The portable development scenario contains an actual bounded spawner, evaluated separately from the fixed policy comparison. No LLM participates.

## Why these techniques

Unreal BT provides explicit priorities, aborts and inspectable execution history. Blackboard is typed working memory, not a source of omniscient truth. EQS ranks **where** to act after policy selects **what** to do. Utility naturally weighs a companion's conflicting support/survival/offense goals. StateTree fits persistent phases with explicit transitions, but adding it here would duplicate the current execution layer without a measured need.

Official references: [event-driven BT](https://dev.epicgames.com/documentation/en-us/unreal-engine/behavior-tree-in-unreal-engine---overview), [shared node instancing](https://dev.epicgames.com/documentation/en-us/unreal-engine/behavior-tree-node-reference-in-unreal-engine), [StateTree semantics](https://dev.epicgames.com/documentation/unreal-engine/overview-of-state-tree-in-unreal-engine?lang=en-US), [EQS request API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/AIModule/FEnvQueryRequest), [damage perception API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/Perception/UAISense_Damage?application_version=5.5).
