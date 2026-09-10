# Architecture and ownership

## Two execution environments, one decision core

`core/include/aegis/rules.hpp` is a C++17 header-only value layer. No `UObject`, world pointer, global actor lookup or rendering dependency enters its API. The UE module includes this same file for health/team decisions, companion scores and the director. Unreal code targets C++20; the shared core remains C++17 compatible.

`Simulation` is a separate reference environment used to exercise rules and the reporting pipeline without an engine installation. Its circular cover, axis sliding, 50 ms steps and local sensors are simplifying assumptions. It is not a replacement implementation of UE BT, NavMesh, physics or EQS.

## Unreal responsibility boundaries

| Type | Ownership and responsibility |
|---|---|
| `AAegisCharacter` | World-owned Actor/Pawn. Owns health/combat components through `CreateDefaultSubobject` and reflected `TObjectPtr` members. Capsule handles collision; primitive mesh is visual only. |
| `AAegisPlayerCharacter` | Input bindings, top-down camera and movement intentions. Combat still uses the common component. |
| `AAegisAICharacter` | Enemy/companion/elite configuration and reflected BT/EQS asset references. |
| `AAegisAIController` | Possession, built-in AI Perception, BB updates, transient target memory, navigation and EQS lifecycle. Observed actors are `TWeakObjectPtr`, not ownership claims. |
| `UAegisDecisionComponent` | No Tick. Converts observations into scores and a selected action. State includes only previous action, scores and decision count. |
| `UBTService_AegisObserve` | Refreshes an observation at 0.2 s interval with zero random deviation. The service stores no per-bot state. |
| `UBTTask_AegisAction` | Immutable action configuration, requests controller commands and returns success/failure. A Wait task paces branch repetition. It does not store a Pawn pointer in a shared BT node. |
| `AAegisEncounterDirector` | Designer-authorized global telemetry sampled at 0.25 s; bounded recommendations. It does not secretly give a companion global knowledge. |
| `AAegisScenarioRunner` | Owns the actors it spawns, timers and results. Records actual damage events, positions and controller counters. It destroys only its own episode actors. |
| `AAegisDebugHUD` | Observational presentation. The omniscient developer display is not a policy input. |

The existing `UAIPerceptionComponent` is used directly. A thin `UAegisPerceptionComponent` wrapper was not added solely to satisfy a class-name checklist.

## Lifetime and asynchronous work

Reflected strong references keep components and asset templates discoverable by Unreal GC. Weak target references become invalid when a target is destroyed; decisions check validity before access. Pure C++ state uses ordinary values and RAII. `UObject` instances are never manually `delete`d or held in `std::unique_ptr`.

The controller permits one EQS request at a time and no more than one submission per second. Its callback checks the result query ID and current Pawn. Unpossess/death aborts the active query and stops navigation/brain logic. Request delegates are bound to the controller as a UObject. Scenario/director timers are cleared in `EndPlay`; the director unregisters its damage listener.

## Decision flow

1. Sight/damage events update last-known location and time. A damage stimulus does not grant live visual tracking.
2. The BT service reads currently perceived sight actors and filters alive hostile targets by distance.
3. TargetActor is cleared when hidden; LastKnownLocation remains for 2.5 s. A companion sees allied health only for a currently perceived ally.
4. The pure utility scorer receives a value observation; it cannot fetch an unseen actor's current transform or health.
5. Immutable Blackboard comparison decorators select a branch during paced traversal. EQS scores spatial candidates; action tasks submit navigation/attack/support commands.
6. At execution, physical traces and team filtering validate damage. Perception authorization is not a bypass for collision.
7. Runner callbacks capture applied damage, not attempted damage or score estimates.

## Runtime budgets

No Actor/component decision Tick is enabled. The runner enables lightweight per-frame timing only for dedicated performance episodes. Engine CharacterMovement and BT machinery still have their own legitimate updates. The controller service is 5 Hz, director 4 Hz, scenario sampling 10 Hz, EQS submission cap 1 Hz/controller with one outstanding request. These are configuration budgets, not demonstrated engine timing improvements.

The total EQS demand still scales with bot count. A global query scheduler or significance manager is a future response to measured contention, not an unimplemented feature claimed here.

## Failure behavior

Missing BT/EQS configuration logs an explicit failure and prevents scenario execution; it does not silently switch to a different policy. The runner checks map identity, navigation and all asset references. Invalid scenario input fails before spawning. External deletion of an episode actor aborts without manufacturing a result.

The portable runner refuses unknown fields, invalid types, seed overflow, duplicate policies/counts and existing output report directories. Results record binary/source SHA256, compiler, flags and host platform. [See current integration gaps](status.md).
