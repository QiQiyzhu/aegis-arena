"""Gate real Decision Lab input and untouched PNGs; never claim human usability.

The actor has a 30-second wall budget after initialization. The process gets 120
seconds including renderer startup. Every run requires a fresh output directory;
failure logs, native JSON, images and before/after hashes remain available.
"""
import argparse
import datetime
import hashlib
import math
import os
from pathlib import Path
import re
import subprocess
import time
import json

from run_decision_lab import strict_json, known_engine_warnings, validate_engine_messages
from run_unreal_input_probe import inspect_png, snapshot_inputs, hash_executable
from run_unreal_trial import OFFLINE_ARGUMENT

ROOT = Path(__file__).resolve().parents[1]
MAP = "/Game/Aegis/Maps/AegisArena"
SCREENSHOTS = ("lab-briefing.png", "lab-active.png", "lab-debug.png", "lab-paused.png")
ASSERTIONS = (
    "game_world_begun", "manual_lab_briefing", "policy_one_baseline", "policy_two_improved",
    "policy_three_priority", "policy_one_restores_baseline", "enter_starts_manual_encounter",
    "companion_uses_utility_bt", "position_link_rejects_hostile", "position_link_rejects_enemy_controller",
    "position_link_accepts_player", "baseline_does_not_sample_registered_link", "w_moves_player",
    "space_starts_dash_cooldown", "dash_moves_player",
    "dash_cooldown_blocks_repeat", "ground_aim_projects_outside_hud", "cursor_matches_ground_point",
    "held_lmb_fires_repeatedly", "released_lmb_stops_fire", "f1_opens_debug", "f1_preserves_lit_view_mode",
    "debug_snapshot_matches_controller", "escape_pauses", "pause_freezes_world",
    "f1_toggles_while_paused", "paused_f1_preserves_lit_view_mode", "escape_resumes", "restart_begins_with_held_inputs",
    "r_restores_briefing_same_seed", "restart_flushes_held_inputs", "restart_retires_old_companion",
    "enter_restarts_fresh_pawn", "restart_resets_combat_state", "four_completed_screenshots",
)
BEGIN = "AEGIS_LAB_INPUT_BEGIN synthetic=1 offscreen=1 human_usability=0"
REASON = "Synthetic manual-input lifecycle completed; no policy-quality or human-usability claim"
DIRECT = re.compile(r"^(?:\[[^\]\r\n]*\])*LogTemp: Display: (AEGIS_LAB_INPUT_[^\r\n]+)$")


def finite_number(value, label, low=0, high=math.inf):
    if type(value) not in (int, float) or not math.isfinite(value) or not low <= value <= high:
        raise ValueError(f"Invalid finite numeric field: {label}")
    return value


def validate(report, log, output):
    required = dict(schemaVersion=1, engine="unreal-runtime", syntheticInput=True,
                    humanUsabilityTest=False, policyQualityTest=False, fixtureDamage=False,
                    renderOffscreen=True, renderingEnabled=True, passed=True, worldType=1,
                    begunPlay=True, screenshotRequests=4, screenshotProcessed=4, reason=REASON)
    if not isinstance(report, dict) or any(type(report.get(k)) is not type(v) or report[k] != v
                                           for k, v in required.items()):
        raise ValueError("Unexpected native schema, scope, world or outcome")
    finite_number(report.get("wallSeconds"), "wallSeconds", 0.001, 30)
    if type(report.get("seed")) is not int or not 0 <= report["seed"] <= 2147483647:
        raise ValueError("Invalid seed")
    if finite_number(report.get("walkDistanceCm"), "walkDistanceCm") <= 40:
        raise ValueError("W did not produce the required movement")
    if finite_number(report.get("dashDistanceCm"), "dashDistanceCm") <= 180:
        raise ValueError("Space did not produce the required dash movement")
    if type(report.get("heldShots")) is not int or report["heldShots"] < 2:
        raise ValueError("Held LMB did not fire through its cooldown")
    assertions = report.get("assertions")
    if (not isinstance(assertions, list) or len(assertions) != len(ASSERTIONS) or
            any(not isinstance(row, dict) for row in assertions) or
            tuple(row.get("name") for row in assertions) != ASSERTIONS or
            any(row.get("passed") is not True or not isinstance(row.get("detail"), str) or
                not row["detail"] or "\n" in row["detail"] or "\r" in row["detail"] for row in assertions)):
        raise ValueError(f"Native checks must match all {len(ASSERTIONS)} names, order and successful outcomes")
    validate_engine_messages(log, rendered=True)
    if any(marker in log for marker in (
            "Fatal error:", "Assertion failed", "Ensure condition failed", "Handled ensure",
            "AEGIS_LAB_INPUT_FAIL", "AEGIS_DECISION_LAB_INVALID_CONFIG", "AEGIS_DECISION_LAB_SPAWN_FAILED")):
        raise ValueError("Engine log contains a warning, error, failed check or ensure")
    events = [m.group(1) for line in log.splitlines() if (m := DIRECT.fullmatch(line))]
    expected = [BEGIN] + [f"AEGIS_LAB_INPUT_CHECK {row['name']} pass=1 {row['detail']}"
                          for row in assertions] + [f"AEGIS_LAB_INPUT_COMPLETE checks={len(ASSERTIONS)}"]
    if events != expected:
        raise ValueError("Unique original begin/check/completion logs must match the native JSON exactly")
    output = Path(output).resolve()
    paths = report.get("screenshots")
    if (not isinstance(paths, list) or len(paths) != 4 or
            any(not isinstance(path, str) or not Path(path).is_absolute() for path in paths) or
            [Path(path).resolve() for path in paths] != [output / name for name in SCREENSHOTS]):
        raise ValueError("Expected exactly four ordered native screenshots inside this output directory")
    return [inspect_png(output / name) for name in SCREENSHOTS]


def build_command(engine_root, output, packaged_exe=None):
    engine_root, output = Path(engine_root).resolve(), Path(output).resolve()
    command = ([str(Path(packaged_exe).resolve()), MAP] if packaged_exe else
               [str(engine_root / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"),
                str(ROOT / "AegisArena.uproject"), MAP, "-game"])
    return command + [
        "-AegisDecisionLab", "-AegisLabInputProbe", "-AegisInputProbe", "-RenderOffscreen", "-d3d11",
        "-AegisLabPolicy=baseline", "-AegisLabSeed=2001", "-AegisLabEnemies=2", "-AegisLabLayout=0",
        f"-AegisLabProbeOutput={output}", "-ResX=1280", "-ResY=720", "-windowed",
        "-unattended", "-nop4", "-nosound", OFFLINE_ARGUMENT, "-stdout", "-FullStdOutLogOutput",
        f"-abslog={output / 'engine.log'}",
    ]


def binary_snapshot(executable, packaged=False):
    """Bind Editor's manifest-selected project DLL, or the actual monolithic game."""
    executable = Path(executable).resolve(strict=True)
    files = {"launcher": executable}
    if packaged:
        if executable.parent.name.casefold() == "win64":
            files["gamePayload"] = executable
        else:
            files["gamePayload"] = executable.parent / "AegisArena/Binaries/Win64/AegisArena.exe"
    else:
        folder = ROOT / "Binaries/Win64"
        manifest_path = folder / "UnrealEditor.modules"
        manifest = strict_json(manifest_path.read_text(encoding="utf-8-sig"))
        modules = manifest.get("Modules") if isinstance(manifest, dict) else None
        if not isinstance(modules, dict) or "AegisArena" not in modules:
            raise ValueError("Editor module manifest does not identify AegisArena")
        files["moduleManifest"] = manifest_path
        for name in ("AegisArena", "AegisArenaEditor"):
            if name not in modules:
                continue
            filename = modules[name]
            if not isinstance(filename, str) or Path(filename).name != filename or not filename.lower().endswith(".dll"):
                raise ValueError("Unexpected project DLL location in Editor module manifest")
            module = (folder / filename).resolve(strict=True)
            if module.parent != folder.resolve():
                raise ValueError("Editor project module resolves outside Binaries/Win64")
            files[name] = module
    return {name: {"path": str(path.resolve(strict=True)), "bytes": path.stat().st_size,
                   "sha256": hash_executable(path)} for name, path in files.items()}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cache-root", type=Path)
    parser.add_argument("--packaged-exe", type=Path, help="Development game executable, never Shipping or an editor")
    args = parser.parse_args(argv)
    engine = args.engine_root.resolve()
    executable = args.packaged_exe.resolve() if args.packaged_exe else engine / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
    if not executable.is_file():
        parser.error("Launch executable does not exist")
    if args.packaged_exe and (executable.name.casefold().startswith("unrealeditor") or "shipping" in executable.name.casefold()):
        parser.error("--packaged-exe must be the Development game; probes do not execute in Shipping")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    record = dict(schemaVersion=1, engine="unreal-runtime", passed=False,
                  startedAtUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  launch="packaged-development" if args.packaged_exe else "UnrealEditor-Cmd -game",
                  syntheticInput=True, osInputUsed=False, humanUsabilityTest=False,
                  policyQualityTest=False, fixtureDamage=False, expectedAssertions=list(ASSERTIONS),
                  rendering="RenderOffscreen", resolution=[1280, 720],
                  actorTimeoutSeconds=30, processTimeoutSeconds=120,
                  sourceDigestScope="Source, core/include, Config, AegisArena.uproject; sorted relative path + bytes",
                  contentDigestScope="checkout Content/Aegis; sorted relative path + bytes",
                  scope="Actual PC input, four direct link-permission API checks, virtual cursor, four untouched PNGs; no human playtest or policy-quality claim")
    try:
        record["engineBuild"] = strict_json((engine / "Engine/Build/Build.version").read_text(encoding="utf-8-sig"))
        record["gitHead"] = subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip()
        record["gitStatus"] = subprocess.check_output(["git", "-C", str(ROOT), "status", "--porcelain"], text=True).splitlines()
        record["inputsBefore"] = snapshot_inputs()
        record["binariesBefore"] = binary_snapshot(executable, bool(args.packaged_exe))
        record["gateSha256"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
        command = build_command(engine, output, args.packaged_exe)
        record["command"] = command
        environment = os.environ.copy()
        # No inference is part of this test, and no configured provider credentials
        # need to be inherited by the renderer or recorded in evidence.
        for name in list(environment):
            if name.startswith(("AEGIS_AI_", "AEGIS_LLM_")):
                environment.pop(name)
        if args.cache_root:
            cache = args.cache_root.resolve()
            (cache / "Temp").mkdir(parents=True, exist_ok=True)
            environment.update(TEMP=str(cache / "Temp"), TMP=str(cache / "Temp"))
            environment["UE-LocalDataCachePath"] = str(cache / "DerivedDataCache")
        with (output / "stdout.log").open("w", encoding="utf-8") as stream:
            result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, env=environment,
                                    cwd=executable.parent if args.packaged_exe else ROOT, timeout=120, check=False)
        record["exitCode"] = result.returncode
        if result.returncode != 0:
            raise RuntimeError(f"Unreal input probe exited {result.returncode}")
        report = strict_json((output / "lab-input.json").read_text(encoding="utf-8-sig"))
        log = (output / "engine.log").read_text(encoding="utf-8-sig", errors="replace")
        record["screenshots"] = validate(report, log, output)
        if report["seed"] != 2001:
            raise ValueError("Native report seed differs from the launched seed")
        record["nativeAssertions"] = len(ASSERTIONS)
    except Exception as error:
        record["failure"] = f"{type(error).__name__}: {error}"
    finally:
        log_path = output / "engine.log"
        record["knownEngineWarnings"] = known_engine_warnings(
            log_path.read_text(encoding="utf-8-sig", errors="replace") if log_path.is_file() else "", rendered=True)
        try:
            record["inputsAfter"] = snapshot_inputs()
            record["binariesAfter"] = binary_snapshot(executable, bool(args.packaged_exe))
            if record.get("inputsBefore") != record["inputsAfter"] or record.get("binariesBefore") != record["binariesAfter"]:
                raise ValueError("Source, content, executable or manifest-selected project DLL changed during the run")
        except Exception as error:
            record["failure"] = record.get("failure", "") + f" | Final snapshot: {type(error).__name__}: {error}"
        record["wallSeconds"] = time.monotonic() - started
        record["passed"] = "failure" not in record and record.get("nativeAssertions") == len(ASSERTIONS)
        record["files"] = {path.name: hash_executable(path) for path in output.iterdir()
                           if path.is_file() and path.name != "provenance.json"}
        (output / "provenance.json").write_text(json.dumps(record, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    if not record["passed"]:
        raise RuntimeError(f"Decision Lab input failed; raw evidence preserved in {output}: {record.get('failure')}")
    print(f"Decision Lab input PASS: {len(ASSERTIONS)} native checks, four untouched PNGs; {output}")


if __name__ == "__main__":
    main()
