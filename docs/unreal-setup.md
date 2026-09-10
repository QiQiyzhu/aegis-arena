# Unreal 5.7 setup and acceptance

This is a reproducible integration procedure, not evidence that the steps have already run. Follow the gates in [status.md](status.md).

## Installation

Install the latest **5.7 hotfix** offered by Epic Games Launcher to a drive with space, for example `D:/Epic/UE_5.7`. The development machine had only about 20 GiB free on C: when checked, so engine/toolchain/cache installation should use D:. Do not assume the example path exists.

Use **Visual Studio 2022 17.14**, the C++ game development and desktop C++ workloads, MSVC v143, and Windows SDK 10.0.22621 or newer. For UE 5.7, Epic lists VS2022 17.8+ and recommends 17.14; VS2026 is marked experimental for that engine version. [Official compatibility table](https://dev.epicgames.com/documentation/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine).

For this Windows primitive arena, mobile/console platforms and debug symbol downloads are unnecessary initially. Keep core editor components. [Official engine installation](https://dev.epicgames.com/documentation/unreal-engine/install-unreal-engine?lang=en-US).

## Build and core automation

```powershell
./scripts/build_unreal.ps1 -EngineRoot 'D:/Epic/UE_5.7' -Automation
```

The script checks actual engine binaries and `Build.version`, invokes UBT for `AegisArenaEditor Win64 Development`, and writes logs under `outputs/unreal`. It can then invoke five `Aegis.Core.*` Automation tests. Review report contents and test counts, not merely an editor process exit code. Keep raw logs/reports in a dated evidence folder only after successful execution.

## Generate primitive arena

Open the compiled project. Enable the bundled Python plugin if prompted, restart, and run `scripts/unreal/create_arena.py` through **Tools → Execute Python Script**. The script saves dirty levels before creating a new one and refuses to overwrite `/Game/Aegis/Maps/AegisArena`. It creates floor, four walls, four cover primitives, lights, player start, scenario runner and a native functional-test actor.

The generator uses documented [EditorActorSubsystem](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/EditorActorSubsystem?application_version=5.7) and [LevelEditorSubsystem](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/LevelEditorSubsystem?application_version=5.7) APIs. Python editor scripting is experimental; the script itself still needs engine execution validation.

Add a **NavMeshBoundsVolume using the editor's Place Actors tool**, enclosing the floor (about 4000 × 3000 × 500 cm). Build navigation and press P to inspect the walkable region. Merely spawning an empty volume Actor in Python does not reliably author a valid brush, so the script intentionally does not pretend to create one.

Set this map as Editor/Game startup map after it exists. Save it. Run the native `AAegisCombatFunctionalTest` from the Functional Testing workflow: it spawns two characters, verifies a physical ranged trace, cooldown, team rejection and terminal death, then cleans up.

## Author and wire AI assets

Create `BB_Aegis`, `BT_Aegis`, `EQ_Cover`, `EQ_Attack`, and `EQ_Retreat` under `/Game/Aegis/AI` following [ai-design.md](ai-design.md). The UE editor is the source of truth for graph serialization. There are no fabricated `.uasset` bytes in the repository.

Assign the BT/three EQS references on the Scenario Runner actor. For interactive play, place one `AAegisAICharacter` with `bCompanion=true` and two enemies, one `bElite=true`; give each the same asset references. Use a separate saved testing map or remove interactive actors before batch evaluation to avoid interference.

In Play mode: WASD movement, mouse X rotates aim, left click ranged, right click melee. The setup intentionally uses primitives and simple controls; no animation montage, marketplace art or commercial gameplay polish is claimed.

## Run a scenario and inspect behavior

In PIE, select the runner's runtime instance, choose `CompanionPolicy`, `EnemyCount`, `Seed`, `Duration`, `EpisodeCount` and press **Run Batch**. Alternatively use `aegis.RunScenario`. `Arena` must match the current map name. Results go to `Saved/AegisReports/<timestamp>`. Do not call a batch from an editor-only world; it fails explicitly.

Developer commands (compiled out of Shipping):

| Command | Action |
|---|---|
| `aegis.PauseAI 1` / `0` | Pause/resume brain logic and movement |
| `aegis.StepDecision` | Recompute one observation/utility decision for paused bots; does not step world physics |
| `aegis.Policy utility` / `priority` | Switch companion scoring policy |
| `aegis.SpawnBot` | Clone one configured enemy's asset wiring into a new enemy |
| `aegis.KillBot` | Kill first alive enemy through hostile damage path |
| `aegis.Visualize perception` | Five-second observation-radius/last-known-position snapshot |
| `aegis.Visualize eqs` | Five-second selected-point snapshot |
| `aegis.Visualize clear` | Clear persistent debug lines |

Use Unreal's built-in AI Debugger/EQS testing tools for candidate scores, real perception cones and BT execution. The project snapshots do not replace those tools.

Finally capture genuine gameplay/BT/EQS/runner media, profile with Insights, and build Shipping to verify debug entry removal. Add evidence links to README only after those artifacts exist.
