# Native Windows release

[Download the verified Win64 packages](https://github.com/QiQiyzhu/aegis-arena/releases/tag/v1.0.0-native). The release separates **Shipping** (clean standalone arena) from **Development** (AI diagnostics and scenario commands). Extract the whole ZIP into a writable folder and run `Windows/AegisArena.exe`. Do not copy the executable out of its runtime folders. The Editor and a source build are not required to run the package.

This is a small native AI systems lab with primitive geometry, not a full commercial game. It starts the encounter directly. WASD moves along world axes, horizontal mouse motion rotates aim, left click fires and right click uses melee. Cyan is the player, green the companion, orange enemies and magenta elites. Grey actors are dead. Exit with Alt+F4; reopening starts a new encounter. Shipping intentionally removes the development HUD and console; use Development or the captured runtime diagnostic to inspect decisions.

The final builds use UE 5.8.2 CL 56702186, MSVC 14.50.35738 and Windows SDK 26100. Both targets actually completed BuildCookRun in fresh output directories. The packaged Development executable, launched without the Editor, completed two validated episodes. The Shipping executable was launched and its real map/AI rendering, absent debug HUD and inactive grave-key console were observed. The native Functional Test separately verifies twelve world assertions for combat, possession, moving EQS Querier and missing-query fallback. These checks do not constitute exhaustive player-input or hardware compatibility testing.

The packaged application is an offline experiment. A Development launch on the authoring machine displayed a Windows firewall prompt; no firewall permission or security setting was automated or changed. The final Shipping inspection ran without that prompt. The included UE prerequisites may be needed on a clean Windows machine. This unsigned portfolio build has not been tested on every Windows/GPU configuration.

Source code is MIT. The packaged executable also contains Unreal Engine runtime, cooked Engine primitive content and its third-party dependencies; those retain their own licenses. `NOTICES.txt` in each archive explains this distinction. Local toolchains, Editor executables, debug symbols, Saved logs and build/DDC caches are excluded. [Asset provenance](../ASSETS.md).

## Rebuild and verify

Use fresh output folders:

```powershell
./scripts/package_unreal.ps1 -EngineRoot 'D:/Program Files/UE_5.8' -CacheRoot 'D:/AegisWork' -OutputRoot 'D:/AegisWork/Packages/my-development' -Configuration Development
./scripts/package_unreal.ps1 -EngineRoot 'D:/Program Files/UE_5.8' -CacheRoot 'D:/AegisWork' -OutputRoot 'D:/AegisWork/Packages/my-shipping' -Configuration Shipping
python scripts/verify_packaged.py --package-root 'D:/AegisWork/Packages/my-development/package/Windows' --output 'D:/AegisWork/Reports/my-packaged-smoke'
python scripts/check_shipping.py --output 'D:/AegisWork/Reports/my-shipping-gate.json'
```

The archive checksums and binary identities are in [the release manifest](../evidence/unreal/release-manifest.json). The [static nine-marker gate](../evidence/unreal/environment/shipping-string-gate.json), [packaged raw reports](../evidence/unreal/packaged-development/) and [actual Shipping window](media/shipping-window.png) provide distinct evidence. Rendered performance was measured in a separate Editor Game workload, not in these Shipping windows.
