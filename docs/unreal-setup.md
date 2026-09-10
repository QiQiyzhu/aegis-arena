# Unreal 5.8 setup and acceptance

Target the installed **UE 5.8.2**. Editor and Game targets, real maps, native scenarios and render sampling have run on Windows; the precise acceptance boundaries are recorded in [status.md](status.md).

## Toolchain and disk placement

The validated engine is `D:/Program Files/UE_5.8` (5.8.2, changelist 56702186). Its own `Engine/Config/Windows/Windows_SDK.json` lists preferred MSVC families 14.50 / 14.44 and rejects known-bad patch levels. The build used **MSVC 14.50.35738** and **Windows SDK 10.0.26100.0**. Do not install an older engine simply to match an earlier draft of this project.

UE Editor's `SwarmInterface.Build.cs` additionally requires a .NET Framework SDK >=4.6. The small official Visual Studio component `Microsoft.Net.Component.4.8.SDK` supplies the missing SDK; it is different from UE's bundled .NET 10 SDK. [Microsoft component reference](https://learn.microsoft.com/en-us/visualstudio/install/workload-component-id-vs-build-tools).

Keep engine, toolchain downloads, DDC, UBA and generated project outputs on a drive with room. `-CacheRoot` routes process TEMP/TMP, DDC, UBA and reports there, then restores the prior process environment. It does not relocate an existing Windows SDK or system runtime. Project `Binaries`, `Intermediate`, `Saved` may be relocated with verified local junctions; such machine configuration is not part of Git.

This machine also had an unrelated **existing VC++ x86 14.44.35211 runtime with missing MSI cache**. Upgrading it failed with MSI 1612/1714. A hash/signature-verified official repair package was prepared for the user to run locally. No registry deletion or silent privilege workaround is part of this repository.

## Build, generate assets and run core tests

For a fresh clone containing the checked-in native maps:

```powershell
./scripts/build_unreal.ps1 -EngineRoot 'D:/Program Files/UE_5.8' -CacheRoot 'D:/AegisWork' -Automation
```

Use `-GenerateAssets` only for an intentionally asset-free checkout. Generation deliberately refuses to overwrite authored levels or a partial AI asset set. The command builds `AegisArenaEditor Win64 Development`, generates real assets inside UE when requested, and checks a new Automation report when requested. Logs live in a unique directory under `<CacheRoot>/Reports`; no stale report can satisfy the gate.

The asset authoring library uses UE 5.8 source APIs: `UBehaviorTreeGraph` creates an editable graph from runtime nodes; BB keys, native decorators/services/tasks and Wait are saved as real assets. EQS has path-existence, geometry-only occlusion and distance tests. Its editor graph class is loaded from the 5.8 EnvironmentQueryEditor plugin. `UActorFactory::CreateBrushForVolumeActor` creates a real NavMeshBoundsVolume brush with finite physical bounds. The runner builds the small dynamic navigation once before gameplay begins: commandlet-saved Recast actors have no baked tiles, and UE 5.8 does not automatically treat a loaded empty actor as newly spawned.

The Python script creates:

- `/Game/Aegis/Maps/AegisArena`: floor, walls, four cover primitives, lights, PlayerStart, overview camera, configured runner and navigation bounds.
- `/Game/Aegis/Maps/AegisFunctional`: an isolated combat Functional Test scene.
- `BB_Aegis`, `BT_Aegis`, `EQS_Cover`, `EQS_Attack`, `EQS_Retreat`.
- Three simple authored materials; the actor material uses a `Tint` parameter. No external art is downloaded.

Core Automation must have at least five completed successful `Aegis.Core.*` tests and zero failures. Actual functional-map execution and perception/navigation tests remain distinct from pure rules Automation. Never infer a test passed because the editor process returned 0.

Run the separate twelve-assertion game-world fixture:

```powershell
python scripts/run_unreal_functional.py --engine-root 'D:/Program Files/UE_5.8' --cache-root 'D:/AegisWork' --output 'D:/AegisWork/Reports/my-functional'
```

The actor remains in the Editor module to exclude it from the packaged game, but explicitly requests a PIE world. The wrapper loads the Functional map first and requires `WorldType=3`, begun-play, all twelve named assertion messages, a clean report and no handled ensure. This catches an observed UE 5.8 false positive: its report could say Success after failing to find the actor in the default map. A test count and exit code are insufficient.

## Run scenarios

Interactive mode starts the small encounter when navigation is ready. WASD moves, mouse X rotates aim, left click fires and right click performs melee. The camera keeps a fixed orientation. The companion is green, the player cyan, normal enemies orange, and the elite magenta. These are primitive diagnostic visuals, not a claim of commercial animation polish.

In PIE select the runner's **runtime** instance, configure `Definition` and `EpisodeCount`, then **Run Batch**, or enter `aegis.RunScenario`. It removes the interactive actors and player pawn before spawning benchmark actors, so user input does not contaminate scripted episodes. `Arena` must match the loaded map. Results go to `Saved/AegisReports/<UTC>-<GUID>`.

A Development editor game process can run a batch without manual input:

```powershell
& 'D:/Program Files/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' `
  'C:/path/to/AegisArena/AegisArena.uproject' /Game/Aegis/Maps/AegisArena -game `
  -AegisBatch -AegisPolicy=utility -AegisEnemies=4 -AegisEpisodes=10 `
  -AegisSeed=1001 -AegisDuration=60 -AegisQuit -unattended -nop4 -NullRHI
```

`-AegisDirector` enables bounded extra spawns. `-AegisPerformance` disables damage for a sustained-load measurement; it must not be mixed into policy win-rate evaluation. Omit `-NullRHI` for rendered frame measurements. Batch failure requests an error exit; Unreal can still return process status 0 during a graceful shutdown, so the wrapper also requires the unique successful `AEGIS_REPORT` marker and validates every raw episode. The same seed controls project spawn and patrol randomness; engine scheduling and physics are not claimed bit deterministic.

The checked wrapper creates a fresh output directory, limits process duration, copies raw engine JSON/CSV and verifies the requested seed/configuration, actual nonzero decisions and rendering mode:

```powershell
python scripts/run_unreal_scenario.py --engine-root 'D:/Program Files/UE_5.8' --cache-root 'D:/AegisWork' --output 'D:/AegisWork/Reports/native-smoke-01' --episodes 2 --duration 15
```

Add `--rendered --performance --enemies 25 --duration 30` for a damage-disabled sustained render workload. The wrapper does not build or install the engine, and it cannot turn a missing or stale report into success.

## Debug controls and acceptance

| Command | Action |
| --- | --- |
| `aegis.PauseAI 1` / `0` | Pause/resume brain and movement |
| `aegis.StepDecision` | Evaluate one observation/utility decision; does not step physics |
| `aegis.Policy utility` / `priority` | Change companion policy |
| `aegis.SpawnBot` | Spawn a configured enemy |
| `aegis.KillBot` | Kill through the regular hostile damage path |
| `aegis.Visualize perception` | Five-second observation snapshot |
| `aegis.Visualize eqs` | Five-second selected-point snapshot |
| `aegis.Visualize clear` | Clear persistent debug lines |

`--query-diagnostics` on the scenario wrapper records actual engine candidate scores and failure reasons and draws a bounded runtime sample. This is explicitly a runtime diagnostic overlay, not the Editor BT/EQS debugger. See [the measured spatial-context bug](eqs-debugging.md). Compile Shipping and verify commands/overlay are unavailable; source gates alone are not that acceptance test. Keep raw native reports separate from portable model evidence.
