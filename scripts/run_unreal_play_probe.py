"""Observe normal arena combat through simulated input and retain native evidence.

The observation budget is 60 seconds; a natural win or loss may finish earlier.
This is automated gameplay observation, not a human playtest or a win-rate study.
"""
from collections import Counter
import argparse
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import subprocess
import time
import zlib

from run_unreal_input_probe import inspect_png, snapshot_inputs, hash_executable
from run_unreal_trial import OFFLINE_ARGUMENT

ROOT = Path(__file__).resolve().parents[1]
MAP = "/Game/Aegis/Maps/AegisArena"
SCREENSHOTS = ("play-briefing.png", "play-02.png", "play-04.png", "play-06.png", "play-08.png", "play-final.png")
REASON = "Observed normal gameplay through simulated player input"
BEGIN = "AEGIS_PLAY_PROBE_BEGIN synthetic=1 offscreen=1 fixture_damage=0 frozen_ai=0"
DIRECT_EVENT = re.compile(r"^(?:\[[^\]\r\n]*\])*LogTemp: Display: (AEGIS_PLAY_[^\r\n]+)$")
PHASES = {"briefing", "active", "intermission", "won", "lost"}
COUNT_TOTALS = ("playerShots", "companionShots", "enemyShots", "enemyDeaths", "companionDeaths",
                "playerDeaths", "enemyWindups", "companionWindups", "pulseActivations")
TOTALS = COUNT_TOTALS + ("playerDistanceCm", "companionDistanceCm", "enemyDistanceCm", "playerDamageDealt",
                       "enemyDamageDealt", "playerDamageTaken", "companionDamageTaken")
POSITIVE_TOTALS = ("playerShots", "enemyShots", "playerDistanceCm", "enemyDistanceCm",
                   "playerDamageDealt", "enemyDamageDealt", "playerDamageTaken")


def number(value, name, minimum=0, maximum=None):
    if (type(value) not in (int, float) or not math.isfinite(value) or value < minimum or
            (maximum is not None and value > maximum)):
        raise ValueError(f"Invalid finite numeric field: {name}")
    return value


def totals(value):
    if not isinstance(value, dict):
        raise ValueError("Missing cumulative gameplay totals")
    for name in TOTALS:
        metric = number(value.get(name), name)
        if name in COUNT_TOTALS and metric != int(metric):
            raise ValueError(f"Expected integral event count: {name}")
    return value


def vector(value, name):
    if (not isinstance(value, list) or len(value) != 3 or
            any(type(item) not in (int, float) or not math.isfinite(item) for item in value)):
        raise ValueError(f"Expected a finite three-dimensional position: {name}")


def validate(report, log, output):
    if not isinstance(report, dict):
        raise ValueError("Play report must be an object")
    required = {"schemaVersion": 1, "engine": "unreal-runtime", "syntheticInput": True,
                "renderOffscreen": True, "renderingEnabled": True, "humanUsabilityTest": False,
                "fixtureDamage": False, "frozenAI": False, "teleportedActors": False,
                "forcedWaves": False, "worldType": 1, "begunPlay": True, "passed": True}
    if any(type(report.get(key)) is not type(value) or report[key] != value for key, value in required.items()):
        raise ValueError("Unexpected gameplay observation schema, interventions, world or outcome")
    seconds = number(report.get("wallSeconds"), "wallSeconds", maximum=65)
    if seconds < 8 or number(report.get("observationBudgetSeconds"), "observationBudgetSeconds") != 60:
        raise ValueError("Observation must reach the eight-second screenshot and declare a 60-second budget")
    outcome, phase = report.get("observedOutcome"), report.get("finalPhase")
    if ((outcome in ("won", "lost") and phase != outcome) or
            (outcome == "budget_exhausted" and (phase not in ("active", "intermission") or seconds < 60)) or
            outcome not in ("won", "lost", "budget_exhausted")):
        raise ValueError("Observation outcome, final phase and completed budget disagree")
    if report.get("reason") != REASON:
        raise ValueError("Unexpected completion reason")
    final = totals(report.get("totals"))
    if any(final[name] <= 0 for name in POSITIVE_TOTALS):
        raise ValueError("Evidence must contain player and enemy firing, movement and actual mutual damage")
    for name in ("screenshotRequests", "screenshotProcessed"):
        if type(report.get(name)) is not int or report[name] != 6:
            raise ValueError("All six screenshot requests must finish")

    samples = report.get("samples")
    if not isinstance(samples, list) or len(samples) < 2:
        raise ValueError("Gameplay observation needs a sequence of runtime samples")
    previous_wall, previous_game = -1, -1
    previous_totals = {name: 0 for name in TOTALS}
    active_seen = False
    for sample in samples:
        if not isinstance(sample, dict):
            raise ValueError("Invalid gameplay sample")
        wall = number(sample.get("wallSeconds"), "sample.wallSeconds", maximum=seconds + 0.001)
        game = number(sample.get("gameSeconds"), "sample.gameSeconds")
        if wall <= previous_wall or game < previous_game:
            raise ValueError("Sample wall clock must advance and game clock must not decrease")
        if sample.get("phase") not in PHASES:
            raise ValueError("Unknown sampled trial phase")
        for name, maximum in (("wave", 3), ("relay", 2)):
            value = number(sample.get(name), name, maximum=maximum)
            if value != int(value):
                raise ValueError("Wave and relay must be integral")
        number(sample.get("charge"), "charge", maximum=4.001)
        number(sample.get("playerHealth"), "playerHealth")
        alive = number(sample.get("enemyAlive"), "enemyAlive")
        if alive != int(alive):
            raise ValueError("Enemy population must be integral")
        if any(type(sample.get(name)) is not bool for name in ("objectiveComplete", "objectiveContested")):
            raise ValueError("Missing sampled objective state")
        vector(sample.get("playerPosition"), "playerPosition")
        vector(sample.get("objectivePosition"), "objectivePosition")
        if sample.get("companionCommand") not in ("guard", "focus", "rally", "unavailable"):
            raise ValueError("Missing companion command sample")
        current = totals(sample.get("totals"))
        if any(current[name] < previous_totals[name] or current[name] > final[name] + 0.001 for name in TOTALS):
            raise ValueError("Cumulative sample metrics regress or exceed final metrics")
        active_seen |= sample["phase"] == "active" and alive > 0
        previous_wall, previous_game, previous_totals = wall, game, current
    if not active_seen or samples[-1]["gameSeconds"] <= samples[0]["gameSeconds"]:
        raise ValueError("Samples do not show game time advancing during an encounter with living enemies")
    if samples[-1]["phase"] != phase or any(
            not math.isclose(samples[-1]["totals"][name], final[name], rel_tol=1e-6, abs_tol=1e-4)
            for name in TOTALS):
        raise ValueError("Final runtime sample must match the reported final phase and cumulative metrics")

    events = report.get("events")
    if not isinstance(events, list) or not events:
        raise ValueError("Missing original gameplay event sequence")
    previous_wall, previous_game = -1, -1
    kinds = Counter()
    damage_sums = {name: 0.0 for name in ("playerDamageDealt", "enemyDamageDealt", "playerDamageTaken", "companionDamageTaken")}
    pressed = set()
    held = set()
    for event in events:
        if not isinstance(event, dict) or not isinstance(event.get("kind"), str):
            raise ValueError("Malformed gameplay event")
        wall = number(event.get("wallSeconds"), "event.wallSeconds", maximum=seconds + 0.001)
        game = number(event.get("gameSeconds"), "event.gameSeconds")
        if wall < previous_wall or game < previous_game:
            raise ValueError("Gameplay event clocks cannot decrease")
        kinds[event["kind"]] += 1
        if event["kind"] == "damage":
            amount = number(event.get("amount"), "damage.amount")
            if amount <= 0:
                raise ValueError("Damage event must record positive applied damage")
            if any(not isinstance(event.get(key), str) or not event[key] for key in ("source", "victim")):
                raise ValueError("Damage must identify its actual source and victim")
            source, victim = event.get("sourceTeam"), event.get("victimTeam")
            if any(role not in ("player", "companion", "enemy", "unknown") for role in (source, victim)):
                raise ValueError("Unknown damage source or victim role")
            if source == "player": damage_sums["playerDamageDealt"] += amount
            if source == "enemy": damage_sums["enemyDamageDealt"] += amount
            if victim == "player": damage_sums["playerDamageTaken"] += amount
            if victim == "companion": damage_sums["companionDamageTaken"] += amount
        elif event["kind"] == "input":
            if any(not isinstance(event.get(key), str) or not event[key] for key in ("key", "state")):
                raise ValueError("Input event must identify key and state")
            if event["state"] not in ("pressed", "released"):
                raise ValueError("Input state must identify a press or release")
            if event["state"] == "pressed":
                if event["key"] in held:
                    raise ValueError("Input history repeats a held press without release")
                held.add(event["key"])
                pressed.add(event["key"])
            else:
                if event["key"] not in held:
                    raise ValueError("Input history releases an unheld key")
                held.remove(event["key"])
        elif event["kind"] == "transition":
            if event.get("phase") not in PHASES:
                raise ValueError("Unknown transition phase")
            for name, maximum in (("wave", 3), ("relay", 2)):
                value = number(event.get(name), name, maximum=maximum)
                if value != int(value):
                    raise ValueError("Invalid transition wave or relay")
        previous_wall, previous_game = wall, game
    if any(kinds[name] == 0 for name in ("input", "damage", "transition")):
        raise ValueError("Observation must include real input, damage and progression events")
    if held or not {"Enter", "LeftMouseButton"}.issubset(pressed) or not pressed.intersection({"W", "A", "S", "D"}):
        raise ValueError("Input history must deploy, move, fire and release all held controls")
    if any(not math.isclose(damage_sums[name], final[name], rel_tol=1e-6, abs_tol=1e-4) for name in damage_sums):
        raise ValueError("Cumulative damage is not corroborated by the actual source/victim event sequence")

    original = [match.group(1).strip() for line in log.splitlines()
                if (match := DIRECT_EVENT.fullmatch(line))]
    begins = [event for event in original if event.startswith("AEGIS_PLAY_PROBE_BEGIN")]
    completions = [event for event in original if event.startswith(("AEGIS_PLAY_PROBE_PASS", "AEGIS_PLAY_PROBE_FAIL"))]
    if begins != [BEGIN] or len(completions) != 1:
        raise ValueError("Missing unique original observation begin and completion markers")
    completion = re.fullmatch(r"AEGIS_PLAY_PROBE_PASS screenshots=6 wall=([0-9]+(?:\.[0-9]+)?) outcome=(won|lost|budget_exhausted)", completions[0])
    if not completion or abs(float(completion.group(1)) - seconds) > 0.001 or completion.group(2) != outcome:
        raise ValueError("Native completion duration or outcome disagrees with JSON")
    if any(failure in log for failure in ("Handled ensure", "Ensure condition failed", "Fatal error:",
                                         "Assertion failed", "AEGIS_PLAY_PROBE_FAIL")):
        raise ValueError("Gameplay observation log contains an engine failure or ensure")
    paths = report.get("screenshots")
    output = Path(output).resolve()
    expected = {output / name for name in SCREENSHOTS}
    if (not isinstance(paths, list) or len(paths) != 6 or
            any(not isinstance(path, str) or not Path(path).is_absolute() for path in paths) or
            {Path(path).resolve() for path in paths} != expected):
        raise ValueError("Screenshot paths must identify this run's six exact native files")
    return [inspect_png(output / name) for name in SCREENSHOTS]


def build_command(engine_root, output, packaged_exe=None):
    engine_root, output = Path(engine_root).resolve(), Path(output).resolve()
    if packaged_exe is not None:
        prefix = [str(Path(packaged_exe).resolve()), MAP]
    else:
        prefix = [str(engine_root / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"),
                  str(ROOT / "AegisArena.uproject"), MAP, "-game"]
    return prefix + ["-RenderOffscreen", "-AegisPlayProbe", "-AegisInputProbe", "-AegisQuit",
                     f"-AegisPlayOutput={output}", "-ResX=1280", "-ResY=720", "-windowed",
                     "-unattended", "-nop4", OFFLINE_ARGUMENT, "-stdout", "-FullStdOutLogOutput",
                     f"-abslog={output / 'engine.log'}"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cache-root", type=Path)
    parser.add_argument("--packaged-exe", type=Path,
                        help="Standalone Development game; engine-root supplies build metadata only")
    args = parser.parse_args()
    engine_root = args.engine_root.resolve()
    executable = (args.packaged_exe.resolve() if args.packaged_exe else
                  engine_root / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe")
    if not executable.is_file():
        parser.error("Requested game or editor executable is missing")
    if args.packaged_exe and executable.name.casefold().startswith("unrealeditor"):
        parser.error("--packaged-exe must name the standalone Development game")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    command = build_command(engine_root, output, args.packaged_exe)
    environment = os.environ.copy()
    if args.cache_root:
        cache = args.cache_root.resolve()
        (cache / "Temp").mkdir(parents=True, exist_ok=True)
        environment.update(TEMP=str(cache / "Temp"), TMP=str(cache / "Temp"))
        environment["UE-LocalDataCachePath"] = str(cache / "DerivedDataCache")
    provenance = {
        "engine": "unreal-runtime", "world": "Game", "rendering": "RenderOffscreen",
        "launch": "packaged-development" if args.packaged_exe else "UnrealEditor-Cmd -game",
        "unrealEditorLaunched": args.packaged_exe is None, "executable": str(executable),
        "engineRootRole": "build-metadata-only" if args.packaged_exe else "editor-runtime-and-build-metadata",
        "command": command, "resolution": [1280, 720], "passed": False,
        "startedAtUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "host": platform.platform(),
        "syntheticInEngineInput": True, "osInputUsed": False, "humanUsabilityTest": False,
        "fixtureDamage": False, "frozenAI": False, "teleportedActors": False, "forcedWaves": False,
        "scope": "Normal gameplay observed until a natural outcome or the 60-second observation budget",
        "observationBudgetSeconds": 60, "actorDeadlineSeconds": 65, "processTimeoutSeconds": 120,
        "requiredPositiveMetrics": list(POSITIVE_TOTALS), "editorHomeScreenOverride": OFFLINE_ARGUMENT,
        "sourceDigestScope": "Source, core/include, Config, AegisArena.uproject; sorted relative path + bytes",
        "contentDigestScope": "Content/Aegis; sorted relative path + bytes",
        "gateSha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    }
    started = time.monotonic()
    try:
        executable_hash = hash_executable(executable)
        provenance.update(executableSha256=executable_hash, executableBytes=executable.stat().st_size)
        provenance["engineBuild"] = json.loads((engine_root / "Engine/Build/Build.version").read_text())
        provenance["gitHead"] = subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip()
        provenance["gitStatus"] = subprocess.check_output(["git", "-C", str(ROOT), "status", "--porcelain"], text=True).splitlines()
        inputs = snapshot_inputs()
        provenance.update(inputs)
        with (output / "stdout.log").open("w", encoding="utf-8") as target:
            result = subprocess.run(command, stdout=target, stderr=subprocess.STDOUT, env=environment,
                                    timeout=120, check=False, cwd=executable.parent if args.packaged_exe else None)
        provenance["exitCode"] = result.returncode
        if result.returncode:
            raise RuntimeError(f"Unreal gameplay observation exited {result.returncode}")
        report = json.loads((output / "play-probe.json").read_text(encoding="utf-8-sig"))
        log = (output / "engine.log").read_text(encoding="utf-8-sig", errors="replace")
        images = validate(report, log, output)
        provenance["inputsAfter"] = snapshot_inputs()
        provenance["executableSha256After"] = hash_executable(executable)
        if provenance["inputsAfter"] != inputs or provenance["executableSha256After"] != executable_hash:
            raise RuntimeError("Source, content or executable changed during gameplay observation")
        provenance.update(passed=True, screenshots=images, observedWallSeconds=report["wallSeconds"],
                          observedOutcome=report["observedOutcome"], totals=report["totals"])
        print(f"Gameplay observation PASS: {report['observedOutcome']} after {report['wallSeconds']:.2f}s; {output}")
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError, zlib.error) as error:
        provenance["failure"] = str(error)
        raise
    finally:
        provenance["wallSeconds"] = time.monotonic() - started
        if "inputsAfter" not in provenance:
            try:
                provenance["inputsAfter"] = snapshot_inputs()
                provenance["executableSha256After"] = hash_executable(executable)
            except OSError as error:
                provenance["afterSnapshotFailure"] = str(error)
        for name in ("play-probe.json", "engine.log", "stdout.log") + SCREENSHOTS:
            path = output / name
            if path.is_file():
                provenance[name + "Sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
        (output / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
