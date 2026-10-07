"""Run a fresh native Decision Lab batch; preserve failures and exact evidence scope."""
import argparse
from collections import Counter
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import time

from run_unreal_functional import snapshot_inputs
from run_unreal_input_probe import inspect_png
from run_unreal_trial import OFFLINE_ARGUMENT

ROOT = Path(__file__).resolve().parents[1]
SCENARIO = "decision_lab_v1"
POLICIES = ("baseline", "improved", "priority")
METRICS = (
    "playerDamageTaken", "companionDamageTaken", "playerDamageDealt", "companionDamageDealt",
    "playerHealingReceived", "companionHealingReceived", "companionAliveSeconds",
    "playerDeathSeconds", "companionDeathSeconds", "companionDecisions",
    "companionActionSwitches", "companionStuckSeconds", "companionShortReversals",
    "companionSupportHeals", "companionShots",
)
COUNTS = ("companionDecisions", "companionActionSwitches", "companionShortReversals", "companionShots")
DWELL = ("companionActionSeconds", "companionSelectedSeconds")
DIRECT = re.compile(r"^(?:\[[^\]\r\n]*\])*LogTemp: Display: (AEGIS_DECISION_LAB_[^\r\n]+)$")
BAD_ENGINE_LINE = re.compile(r"^(?:\[[^\]\r\n]*\])*Log[^\r\n:]*: (?:Warning|Error|Fatal):")
KNOWN_RENDER_WARNING = ("LogConsoleManager: Warning: Console variable 'r.MotionVectorSimulation' used in the render thread. "
                        "Rendering artifacts could happen. Use ECVF_RenderThreadSafe or don't use in render thread.")
KNOWN_RENDER_WARNING_LINE = re.compile(r"^(?:\[[^\]\r\n]*\])*" + re.escape(KNOWN_RENDER_WARNING) + r"$")


def known_engine_warnings(log, rendered):
    """Keep complete original lines; this exact UE metadata warning is rendered-only."""
    return [line for line in log.splitlines() if rendered and KNOWN_RENDER_WARNING_LINE.fullmatch(line)]


def validate_engine_messages(log, rendered=False):
    allowed = known_engine_warnings(log, rendered)
    if len(allowed) > 1:
        raise ValueError("Diagnosed engine metadata warning occurred more than once")
    if any(BAD_ENGINE_LINE.match(line) and line not in allowed for line in log.splitlines()):
        raise ValueError("Engine log reported an unaccepted warning, error or fatal message")
    return allowed


def rendering_arguments(rendered):
    return ["-RenderOffscreen", "-d3d11", "-ResX=1280", "-ResY=720", "-windowed"] if rendered else ["-NullRHI"]


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def strict_json(text):
    def pairs(items):
        value = {}
        for key, child in items:
            if key in value:
                raise ValueError("Duplicate JSON key")
            value[key] = child
        return value
    def constant(_):
        raise ValueError("Nonfinite JSON constant")
    value = json.loads(text, object_pairs_hook=pairs, parse_constant=constant)
    def finite(item):
        if isinstance(item, float) and not math.isfinite(item):
            raise ValueError("Nonfinite JSON number")
        if isinstance(item, dict):
            for child in item.values():
                finite(child)
        if isinstance(item, list):
            for child in item:
                finite(child)
    finite(value)
    return value


def read_json(path):
    return strict_json(Path(path).read_text(encoding="utf-8-sig"))


def number(value, name, low=0, high=math.inf, integer=False):
    if (type(value) not in (int, float) or not math.isfinite(value) or not low <= value <= high or
            (integer and value != int(value))):
        raise ValueError(f"Invalid numeric field: {name}")
    return value


def same_fields(value, expected, label):
    if not isinstance(value, dict):
        raise ValueError(f"Missing {label} object")
    for key, target in expected.items():
        actual = value.get(key)
        if type(target) is bool or isinstance(target, str):
            valid = type(actual) is type(target) and actual == target
        else:
            valid = type(actual) in (int, float) and actual == target
        if not valid:
            raise ValueError(f"Unexpected {label}.{key}")


def validate_episode(row, args, index):
    same_fields(row, {"schemaVersion": 1, "engine": "unreal-runtime", "automated": True}, "episode")
    same_fields(row.get("scenario"), {
        "policy": args.policy, "seed": args.seed + index, "enemyCount": args.enemies,
        "duration": args.duration, "scenarioId": SCENARIO, "layout": args.layout,
    }, "scenario")
    for name in ("win", "playerAlive", "companionAlive"):
        if type(row.get(name)) is not bool:
            raise ValueError(f"Missing boolean outcome: {name}")
    elapsed = number(row.get("elapsedGameSeconds"), "elapsedGameSeconds", 0.000001, args.duration + 0.2)
    enemies = number(row.get("enemiesAlive"), "enemiesAlive", 0, args.enemies, integer=True)
    reason = row.get("terminationReason")
    if reason not in ("enemy_clear", "player_dead", "timeout") or row["win"] != (reason == "enemy_clear"):
        raise ValueError("Contradictory termination reason and win")
    if reason == "enemy_clear" and (not row["playerAlive"] or enemies != 0):
        raise ValueError("Victory requires a living player and no enemies")
    if reason == "player_dead" and row["playerAlive"]:
        raise ValueError("Player-death outcome has a living player")
    if reason == "timeout" and (not row["playerAlive"] or enemies == 0 or elapsed < args.duration - 0.11):
        raise ValueError("Timeout did not reach the live, unresolved time budget")
    if not row["playerAlive"] and reason != "player_dead":
        raise ValueError("Player death must be the terminal reason")
    metrics = row.get("metrics")
    if not isinstance(metrics, dict):
        raise ValueError("Missing metrics")
    for name in METRICS:
        number(metrics.get(name), name, -1 if name.endswith("DeathSeconds") else 0,
               integer=name in COUNTS)
    if metrics["companionDecisions"] <= 0:
        raise ValueError("No companion decision was observed")
    for role in ("player", "companion"):
        death = metrics[role + "DeathSeconds"]
        alive = row[role + "Alive"]
        if (alive and death != -1) or (not alive and not 0 <= death <= elapsed + 0.11):
            raise ValueError("Death timestamp disagrees with role endpoint")
    exposure = metrics["companionAliveSeconds"]
    expected_exposure = elapsed if row["companionAlive"] else metrics["companionDeathSeconds"]
    if abs(exposure - expected_exposure) > 0.11:
        raise ValueError("Companion exposure is not bounded by death or episode end")
    if metrics["companionStuckSeconds"] > exposure + 0.2:
        raise ValueError("Stuck time exceeds observed companion lifetime")
    if metrics["companionActionSwitches"] > metrics["companionDecisions"] or metrics["companionShortReversals"] > metrics["companionActionSwitches"]:
        raise ValueError("Action transition counts are impossible")
    for field in DWELL:
        mapping = metrics.get(field)
        if not isinstance(mapping, dict) or any(not isinstance(k, str) or not k for k in mapping):
            raise ValueError(f"Missing action dwell object: {field}")
        for value in mapping.values():
            number(value, field)
        if sum(mapping.values()) > exposure + 0.25:
            raise ValueError("Action dwell exceeds observed companion lifetime")
    return row


def validate_trace(path, row):
    events = [strict_json(line) for line in Path(path).read_text(encoding="utf-8-sig").splitlines() if line.strip()]
    if len(events) < 3 or any(not isinstance(item, dict) for item in events):
        raise ValueError("Missing native decision/event trace")
    kinds = [item.get("event") for item in events]
    if kinds[0] != "episode_start" or kinds[-1] != "episode_end" or kinds.count("episode_start") != 1 or kinds.count("episode_end") != 1:
        raise ValueError("Trace must have one enclosing episode start/end")
    if kinds.count("decision") < 1:
        raise ValueError("Trace has no decisions")
    same_fields(events[-1], {"terminationReason": row["terminationReason"], "win": row["win"]}, "episode_end")
    elapsed, previous = row["elapsedGameSeconds"], -1
    damage = Counter()
    healing = Counter()
    deaths = {role: [] for role in ("player", "companion", "enemy")}
    for event in events:
        stamp = number(event.get("timeSeconds"), "trace.timeSeconds", 0, elapsed + 0.11)
        if stamp < previous:
            raise ValueError("Event timestamps moved backwards")
        previous = stamp
        if not isinstance(event.get("event"), str) or not event["event"]:
            raise ValueError("Missing event kind")
        if event["event"] in ("damage", "heal"):
            allowed_sources = set(deaths) | ({"health_accounting"} if event["event"] == "heal" else set())
            if event.get("source") not in allowed_sources or event.get("victim") not in deaths:
                raise ValueError("Damage/heal event has an unknown role")
            amount = number(event.get("amount"), "event.amount", 0.000001)
            if event["event"] == "damage":
                damage[(event["source"], "dealt")] += amount
                damage[(event["victim"], "taken")] += amount
            else:
                healing[event["victim"]] += amount
        elif event["event"] == "death":
            if event.get("actor") not in deaths:
                raise ValueError("Death event has an unknown role")
            deaths[event["actor"]].append(stamp)
    if abs(events[-1]["timeSeconds"] - elapsed) > 0.11:
        raise ValueError("Trace end time differs from episode")
    for role in ("player", "companion"):
        for suffix, kind in (("Dealt", "dealt"), ("Taken", "taken")):
            if not math.isclose(damage[(role, kind)], row["metrics"][role + "Damage" + suffix], abs_tol=0.01, rel_tol=1e-5):
                raise ValueError("Damage trace disagrees with role totals")
        if not math.isclose(healing[role], row["metrics"][role + "HealingReceived"], abs_tol=0.01, rel_tol=1e-5):
            raise ValueError("Heal trace disagrees with applied healing")
        expected_deaths = 0 if row[role + "Alive"] else 1
        if len(deaths[role]) != expected_deaths:
            raise ValueError("Death event count disagrees with role endpoint")
        if deaths[role] and abs(deaths[role][0] - row["metrics"][role + "DeathSeconds"]) > 0.11:
            raise ValueError("Death event time disagrees with reported death time")
    if len(deaths["enemy"]) != row["scenario"]["enemyCount"] - row["enemiesAlive"]:
        raise ValueError("Enemy death events disagree with endpoint population")
    return events


def validate_directory(output, args, log):
    output = Path(output)
    validate_engine_messages(log, args.rendered)
    if any(
            marker in log for marker in ("Fatal error:", "Assertion failed", "Ensure condition failed", "Handled ensure", "AEGIS_DECISION_LAB_FAILED", "AEGIS_DECISION_LAB_FAIL ")):
        raise ValueError("Engine log reported warning, error, failure or ensure")
    markers = [match.group(1) for line in log.splitlines() if (match := DIRECT.fullmatch(line))]
    if any(item.startswith("AEGIS_DECISION_LAB_FAIL") for item in markers):
        raise ValueError("Native Decision Lab reported a failure")
    complete = [item for item in markers if item.startswith("AEGIS_DECISION_LAB_COMPLETE")]
    if complete != [f"AEGIS_DECISION_LAB_COMPLETE episodes={args.episodes}"]:
        raise ValueError("Missing unique direct native completion marker")
    report = read_json(output / "results.json")
    same_fields(report, {"schemaVersion": 1, "engine": "unreal-runtime", "completed": True,
                        "episodes": args.episodes, "policy": args.policy, "firstSeed": args.seed,
                        "enemyCount": args.enemies, "duration": args.duration,
                        "scenarioId": SCENARIO, "layout": args.layout}, "results")
    expected = [f"episode-{index:03d}.json" for index in range(args.episodes)]
    expected_traces = [f"decision-{index:03d}.jsonl" for index in range(args.episodes)]
    if sorted(p.name for p in output.glob("episode-*.json")) != expected or sorted(p.name for p in output.glob("decision-*.jsonl")) != expected_traces:
        raise ValueError("Native episodes or traces are missing, repeated or unexpected")
    rows = []
    for index, name in enumerate(expected):
        row = validate_episode(read_json(output / name), args, index)
        validate_trace(output / expected_traces[index], row)
        rows.append(row)
    pictures = sorted(output.rglob("*.png"))
    if args.capture and not pictures:
        raise ValueError("Capture requested but no native PNG was saved")
    image_info = [inspect_png(path) for path in pictures]
    return rows, image_info


def binary_snapshot(executable, packaged):
    files = {"launcher": Path(executable).resolve(strict=True)}
    if packaged:
        if Path(executable).parent.name.lower() != "win64":
            nested = Path(executable).parent / "AegisArena/Binaries/Win64"
            candidates = [nested / name for name in ("AegisArena.exe", "AegisArena-Win64-Shipping.exe")]
            present = [path for path in candidates if path.is_file()]
            if len(present) != 1:
                raise ValueError("Cannot identify exactly one packaged game payload")
            files["gamePayload"] = present[0]
    else:
        files["projectModule"] = ROOT / "Binaries/Win64/UnrealEditor-AegisArena.dll"
    return {name: {"path": str(path), "sha256": sha(path)} for name, path in files.items()}


def protocol_cells(protocol):
    same_fields(protocol, {"schemaVersion": 1, "phase": "holdout"}, "protocol")
    policies = protocol.get("policies")
    if (not isinstance(policies, list) or len(policies) < 2 or
            any(p not in POLICIES for p in policies) or len(set(policies)) != len(policies)):
        raise ValueError("Frozen protocol requires distinct declared policies")
    cells = protocol.get("cells")
    if not isinstance(cells, list) or not cells:
        raise ValueError("Frozen protocol has no conditions")
    seen = set()
    for cell in cells:
        if not isinstance(cell, dict) or cell.get("scenarioId") != SCENARIO:
            raise ValueError("Frozen protocol has an unknown scenario")
        number(cell.get("layout"), "protocol.layout", 0, 1, integer=True)
        number(cell.get("enemyCount"), "protocol.enemyCount", 1, 8, integer=True)
        number(cell.get("duration"), "protocol.duration", 1, 180)
        key = tuple(cell[k] for k in ("scenarioId", "layout", "enemyCount", "duration"))
        if key in seen:
            raise ValueError("Repeated frozen condition")
        seen.add(key)
        seeds = cell.get("seeds")
        if not isinstance(seeds, list) or not seeds or any(type(s) is not int or not 0 <= s < 2147483647 for s in seeds) or len(set(seeds)) != len(seeds):
            raise ValueError("Invalid frozen seed list")
    for field in ("sourceSha256", "contentSha256"):
        if not isinstance(protocol.get(field), str) or not re.fullmatch(r"[0-9a-f]{64}", protocol[field]):
            raise ValueError("Frozen protocol must identify source and content")
    return cells


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--cache-root", type=Path)
    parser.add_argument("--packaged-exe", type=Path)
    parser.add_argument("--policy", choices=POLICIES, required=True)
    parser.add_argument("--enemies", type=int, required=True)
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--episodes", type=int, default=1)
    parser.add_argument("--duration", type=float, default=60)
    parser.add_argument("--layout", type=int, choices=(0, 1), default=0)
    parser.add_argument("--phase", choices=("development", "holdout", "diagnostic"), default="development")
    parser.add_argument("--protocol", type=Path, help="Required predeclared protocol JSON for holdout")
    parser.add_argument("--fixed-step", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--rendered", action="store_true")
    parser.add_argument("--capture", action="store_true")
    parser.add_argument("--timeout", type=int, default=900)
    args = parser.parse_args(argv)
    if not (1 <= args.enemies <= 8 and 1 <= args.episodes <= 100 and math.isfinite(args.duration) and
            1 <= args.duration <= 180 and 0 <= args.seed <= 2147483647 - args.episodes and 1 <= args.timeout <= 86400):
        parser.error("Invalid enemy, episode, seed, duration or process timeout bounds")
    if args.capture and not args.rendered:
        parser.error("Native capture requires --rendered")
    if args.capture and args.episodes != 1:
        parser.error("Native capture uses one episode per directory to preserve frame filenames")
    if args.phase == "holdout" and args.protocol is None:
        parser.error("Holdout requires a frozen --protocol before execution")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    record = {"schemaVersion": 1, "engine": "unreal-runtime", "passed": False,
              "startedAtUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "phase": args.phase, "automated": True, "humanUsabilityTest": False,
              "requested": {key: getattr(args, key) for key in ("policy", "enemies", "episodes", "seed", "duration", "layout", "fixed_step", "rendered", "capture")},
              "timeMode": "fixed_game_step_1_60" if args.fixed_step else "real_time",
              "scope": "Legacy BT/EQS encounter; scripted player; no Copilot inference or Uplink objective evaluation"}
    executable = None
    try:
        engine = args.engine_root.resolve()
        executable = args.packaged_exe.resolve() if args.packaged_exe else engine / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
        record["launch"] = "packaged" if args.packaged_exe else "editor-game"
        record["engineBuild"] = read_json(engine / "Engine/Build/Build.version")
        record["inputsBefore"] = snapshot_inputs()
        if args.protocol:
            protocol = read_json(args.protocol)
            cells = protocol_cells(protocol)
            if args.phase != "holdout" or args.policy not in protocol["policies"]:
                raise ValueError("Invocation is outside the frozen phase/policies")
            matching = [cell for cell in cells if cell["layout"] == args.layout and cell["enemyCount"] == args.enemies and cell["duration"] == args.duration]
            if len(matching) != 1 or not set(range(args.seed, args.seed + args.episodes)) <= set(matching[0]["seeds"]):
                raise ValueError("Invocation is outside frozen conditions/seeds")
            if any(protocol[field] != record["inputsBefore"][field] for field in ("sourceSha256", "contentSha256")):
                raise ValueError("Frozen protocol source/assets differ from this checkout")
            record["protocolSha256"] = sha(args.protocol)
            (output / "frozen-protocol.json").write_bytes(args.protocol.read_bytes())
        record["binariesBefore"] = binary_snapshot(executable, bool(args.packaged_exe))
        record["wrapperSha256"] = sha(Path(__file__))
        command = [str(executable)]
        if not args.packaged_exe:
            command += [str(ROOT / "AegisArena.uproject")]
        command += ["/Game/Aegis/Maps/AegisArena"]
        if not args.packaged_exe:
            command += ["-game"]
        command += ["-AegisDecisionLab", "-AegisLabAutomated", "-AegisQuit",
                    f"-AegisLabPolicy={args.policy}", f"-AegisLabEnemies={args.enemies}",
                    f"-AegisLabSeed={args.seed}", f"-AegisLabEpisodes={args.episodes}",
                    f"-AegisLabDuration={args.duration}", f"-AegisLabLayout={args.layout}",
                    f"-AegisLabOutput={output}", "-unattended", "-nop4", "-nosound", OFFLINE_ARGUMENT,
                    "-stdout", "-FullStdOutLogOutput", f"-abslog={output / 'engine.log'}"]
        command += rendering_arguments(args.rendered)
        if args.fixed_step:
            command += ["-UseFixedTimeStep", "-FPS=60"]
        if args.capture:
            command += ["-AegisLabCapture"]
        record["command"] = command
        environment = os.environ.copy()
        if args.cache_root:
            cache = args.cache_root.resolve()
            (cache / "Temp").mkdir(parents=True, exist_ok=True)
            environment.update(TEMP=str(cache / "Temp"), TMP=str(cache / "Temp"))
            environment["UE-LocalDataCachePath"] = str(cache / "DerivedDataCache")
        with (output / "stdout.log").open("w", encoding="utf-8") as stream:
            result = subprocess.run(command, env=environment, cwd=executable.parent if args.packaged_exe else ROOT,
                                    stdout=stream, stderr=subprocess.STDOUT, timeout=args.timeout)
        record["exitCode"] = result.returncode
        if result.returncode != 0:
            raise RuntimeError("Native Decision Lab exited unsuccessfully")
        rows, pictures = validate_directory(output, args, (output / "engine.log").read_text(encoding="utf-8-sig", errors="replace"))
        record["episodeCount"] = len(rows)
        record["screenshots"] = pictures
    except Exception as error:
        record["failure"] = f"{type(error).__name__}: {error}"
    finally:
        log_path = output / "engine.log"
        record["knownEngineWarnings"] = known_engine_warnings(
            log_path.read_text(encoding="utf-8-sig", errors="replace") if log_path.is_file() else "", args.rendered)
        try:
            record["inputsAfter"] = snapshot_inputs()
            if executable is not None and "binariesBefore" in record:
                record["binariesAfter"] = binary_snapshot(executable, bool(args.packaged_exe))
            if record.get("inputsBefore") != record.get("inputsAfter") or record.get("binariesBefore") != record.get("binariesAfter"):
                record["failure"] = record.get("failure", "") + " | Source, assets or project executable/module changed or could not be bound"
            if args.protocol and record.get("protocolSha256") != sha(args.protocol):
                record["failure"] = record.get("failure", "") + " | Frozen protocol changed during execution"
        except Exception as error:
            record["failure"] = record.get("failure", "") + f" | Final evidence snapshot failed: {type(error).__name__}"
        record["passed"] = "failure" not in record and record.get("episodeCount") == args.episodes
        record["wallSeconds"] = time.monotonic() - started
        record["files"] = {path.relative_to(output).as_posix(): sha(path) for path in output.rglob("*")
                           if path.is_file() and path != output / "provenance.json"}
        (output / "provenance.json").write_text(json.dumps(record, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    if not record["passed"]:
        raise RuntimeError(f"Decision Lab failed; preserved evidence in {output}: {record.get('failure')}")
    print(f"Decision Lab PASS: {args.episodes} native episodes; {args.phase}; {output}")


if __name__ == "__main__":
    main()
