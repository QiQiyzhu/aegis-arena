"""Run the isolated tactical PIE fixture with exact, original assertion evidence."""
import argparse
from collections import Counter
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

from run_unreal_functional import snapshot_inputs
from run_unreal_trial import OFFLINE_ARGUMENT, OFFLINE_MARKER

ROOT = Path(__file__).resolve().parents[1]
MAP = "/Game/Aegis/Maps/AegisTacticalFunctional"
TEST_PATH = "Project.Functional Tests.Aegis.Maps.AegisTacticalFunctional"
MARKER = "AEGIS_TACTICAL_FUNCTIONAL_PASS | Assertions=32 | WorldType=3 | BegunPlay=1 | DamageDisabled=1"
ASSERTIONS = Counter({name: 1 for name in (
    "natural_spawn_order_sees_existing_player_and_companion_without_sense_toggle",
    "natural_spawn_order_winds_up_at_player_without_sense_toggle",
    "natural_spawn_order_fires_at_player_without_sense_toggle",
    "tactical_companion_enabled", "explicit_ally_link_survives_no_sight",
    "guard_moves_without_visual_player", "guard_avoids_player_fire_lane",
    "rally_command_moves_to_signal", "invalid_rally_does_not_teleport_or_accept_partial_path",
    "focus_nomination_does_not_grant_sight_or_fire", "hidden_focus_uses_command_snapshot",
    "focus_selects_genuinely_seen_nominee", "companion_emits_windup_before_fire",
    "occlusion_cancels_windup_without_firing",
)})
for enemy_role in range(3):
    for suffix in ("rejects_live_player_link", "advances_to_objective_without_target",
                   "moves_before_aiming", "reaction_warning_precedes_shot",
                   "fires_with_real_los", "repositions_after_shot"):
        ASSERTIONS[f"role_{enemy_role}_{suffix}"] = 1
DIRECT_EVENT = re.compile(r"^(?:\[[^\]\r\n]*\])*LogTemp: Display: (AEGIS_TACTICAL_[^\r\n]+)$")


def validate(report, log):
    expected = dict(succeeded=1, succeededWithWarnings=0, failed=0, notRun=0, inProcess=0)
    if any(type(report.get(key)) is not int or report[key] != value for key, value in expected.items()):
        raise ValueError("Expected one clean completed tactical test")
    tests = report.get("tests", [])
    if (len(tests) != 1 or tests[0].get("state") != "Success" or
            not tests[0].get("fullTestPath", "").startswith(TEST_PATH + ".")):
        raise ValueError("Wrong tactical fixture report")
    if OFFLINE_MARKER not in log:
        raise ValueError("Missing offline editor startup evidence")
    events = [match.group(1) for line in log.splitlines() if (match := DIRECT_EVENT.fullmatch(line))]
    if [event for event in events if event.startswith("AEGIS_TACTICAL_FUNCTIONAL_PASS")] != [MARKER]:
        raise ValueError("Missing unique 32-assertion PIE tactical marker")
    prefix = "AEGIS_TACTICAL_ASSERT_PASS "
    seen = Counter(event[len(prefix):] for event in events if event.startswith(prefix))
    if seen != ASSERTIONS:
        raise ValueError("Missing, duplicate or unexpected tactical assertion")
    if any(fragment in log for fragment in (
            "Handled ensure", "Ensure condition failed", "Assertion failed", "Fatal error:",
            "No functional testing script", "TestResult=Failed")):
        raise ValueError("Tactical run includes native failure or ensure")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cache-root", type=Path)
    args = parser.parse_args()
    engine_root = args.engine_root.resolve()
    editor = engine_root / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
    if not editor.is_file():
        parser.error("UnrealEditor-Cmd.exe is missing")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    command = [str(editor), str(ROOT / "AegisArena.uproject"), MAP, "-unattended", "-nop4", "-NullRHI",
               "-stdout", "-FullStdOutLogOutput", OFFLINE_ARGUMENT,
               f"-ExecCmds=Automation RunTests {TEST_PATH}", "-TestExit=Automation Test Queue Empty",
               f"-ReportExportPath={output}", f"-abslog={output / 'engine.log'}"]
    environment = os.environ.copy()
    if args.cache_root:
        cache = args.cache_root.resolve()
        (cache / "Temp").mkdir(parents=True, exist_ok=True)
        environment.update(TEMP=str(cache / "Temp"), TMP=str(cache / "Temp"))
        environment["UE-LocalDataCachePath"] = str(cache / "DerivedDataCache")
    provenance = dict(engine="unreal-runtime", world="PIE", rendering="NullRHI", command=command,
                      startedAtUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(), passed=False,
                      expectedNativeAssertions=32, damageDisabled=True,
                      fixtureInterventions="Natural spawn-order enemy uses untouched senses against stationary existing player/companion; later sense toggles isolate allied link/objective advance; real Sight plus spawned occluder verify focus and windup cancellation",
                      testPath=TEST_PATH, enemyRoles=["Striker", "Flanker", "Suppressor"],
                      gateSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    started = time.monotonic()
    try:
        provenance["engineBuild"] = json.loads((engine_root / "Engine/Build/Build.version").read_text())
        provenance["gitHead"] = subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip()
        inputs = snapshot_inputs()
        provenance.update(inputs)
        with (output / "stdout.log").open("w", encoding="utf-8") as target:
            result = subprocess.run(command, stdout=target, stderr=subprocess.STDOUT, env=environment,
                                    timeout=150, check=False)
        provenance["exitCode"] = result.returncode
        if result.returncode:
            raise RuntimeError(f"Unreal process exited {result.returncode}")
        report = json.loads((output / "index.json").read_text(encoding="utf-8-sig"))
        log = (output / "engine.log").read_text(encoding="utf-8-sig", errors="replace")
        validate(report, log)
        if snapshot_inputs() != inputs:
            raise RuntimeError("Source or content changed during tactical test")
        provenance.update(passed=True, nativeAssertions=32, assertionCounts=dict(ASSERTIONS))
        print(f"Native tactical test PASS: 32 real world assertions; {output}")
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        provenance["failure"] = str(error)
        raise
    finally:
        provenance["wallSeconds"] = time.monotonic() - started
        for name in ("index.json", "engine.log", "stdout.log"):
            path = output / name
            if path.is_file():
                provenance[name + "Sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
        (output / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
