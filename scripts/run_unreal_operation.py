"""Run the isolated PIE objective fixture with exact native evidence requirements."""
import argparse
from collections import Counter
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import time

from run_unreal_functional import snapshot_inputs
from run_unreal_trial import OFFLINE_ARGUMENT, OFFLINE_MARKER

ROOT = Path(__file__).resolve().parents[1]
MAP = "/Game/Aegis/Maps/AegisOperationFunctional"
TEST_PATH = "Project.Functional Tests.Aegis.Maps.AegisOperationFunctional"
ASSERTION_PREFIX = "AEGIS_OPERATION_ASSERT_PASS "
ASSERTIONS = Counter({name: 1 for name in (
    "begun_pie_world",
    "one_objective_runner",
    "briefing_objectives_reset",
    "briefing_cannot_choose_upgrade",
    "opening_deploy_has_objective_and_hostiles",
    "living_hostile_contests_actual_ring",
    "clearing_enemies_does_not_complete_empty_ring",
    "companion_alone_charges_actual_ring",
    "dual_occupancy_applies_one_point_five_rate",
    "opening_completion_pauses_for_upgrade",
    "upgrade_pause_freezes_game_clock",
    "invalid_upgrade_keeps_selection_paused",
    "piercing_upgrade_applies_and_resumes",
    "second_choice_cannot_apply_without_pending_upgrade",
    "middle_wave_starts_two_fresh_relays",
    "first_middle_relay_activates_distinct_second_ring",
    "occupying_old_ring_cannot_charge_new_relay",
    "two_relays_complete_but_living_enemies_block_transition",
    "middle_clear_opens_second_paused_upgrade",
    "owned_upgrade_cannot_be_selected_again",
    "restorative_upgrade_applies_and_resumes",
    "extraction_requires_enemies_cleared_before_charging",
    "living_companion_cannot_extract_without_player",
    "authored_damage_creates_real_companion_death",
    "dead_companion_inside_cleared_ring_cannot_charge",
    "dead_companion_adds_no_capture_bonus",
    "living_player_finishes_extraction_and_wins",
    "completed_operation_stays_terminal",
    "restart_replaces_pawns_and_resets_objective",
    "restart_clears_upgrades_contest_and_pause",
    "redeploy_begins_fresh_opening_objective",
)})
NATIVE_ASSERTIONS = sum(ASSERTIONS.values())
MARKER = ("AEGIS_OPERATION_FUNCTIONAL_PASS | Assertions=31 | WorldType=3 | BegunPlay=1 | "
          "FixtureDamage=1 | AIStopped=1 | PlacedOccupants=1")
DIRECT_EVENT = re.compile(r"^(?:\[[^\]\r\n]*\])*LogTemp: Display: (AEGIS_OPERATION_[^\r\n]+)$")


def validate(report, log):
    expected = dict(succeeded=1, succeededWithWarnings=0, failed=0, notRun=0, inProcess=0)
    if any(type(report.get(key)) is not int or report[key] != value for key, value in expected.items()):
        raise ValueError("Expected one completed operation test with zero warnings or failures")
    tests = report.get("tests", [])
    if (len(tests) != 1 or tests[0].get("state") != "Success" or
            not tests[0].get("fullTestPath", "").startswith(TEST_PATH + ".")):
        raise ValueError("Report does not identify the requested operation fixture")
    if OFFLINE_MARKER not in log:
        raise ValueError("Missing executed editor Home Screen connectivity opt-out")
    # AutomationController may replay entries. Only the fixture's original
    # LogTemp emissions count, but failure checks inspect the entire log.
    events = [match.group(1).strip() for line in log.splitlines()
              if (match := DIRECT_EVENT.fullmatch(line))]
    markers = [event for event in events if event.startswith("AEGIS_OPERATION_FUNCTIONAL_PASS")]
    if markers != [MARKER]:
        raise ValueError("Missing unique 31-assertion PIE marker with disclosed fixture interventions")
    observed = Counter(event[len(ASSERTION_PREFIX):].strip() for event in events
                       if event.startswith(ASSERTION_PREFIX))
    if observed != ASSERTIONS or sum(observed.values()) != NATIVE_ASSERTIONS:
        raise ValueError("Operation log lacks exact required assertion labels and multiplicities")
    if any(failure in log for failure in (
            "AEGIS_OPERATION_FUNCTIONAL_FAIL", "Handled ensure", "Ensure condition failed",
            "No functional testing script", "TestResult=Failed", "Fatal error:", "Assertion failed")):
        raise ValueError("Operation log contains a native failure, fatal error, or ensure")


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
    if not (ROOT / "Content/Aegis/Maps/AegisOperationFunctional.umap").is_file():
        parser.error("Operation map is missing; run scripts/unreal/create_operation_fixture.py in the built editor")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    command = [str(editor), str(ROOT / "AegisArena.uproject"), MAP,
               "-unattended", "-nop4", "-NullRHI", "-stdout", "-FullStdOutLogOutput", OFFLINE_ARGUMENT,
               f"-ExecCmds=Automation RunTests {TEST_PATH}",
               "-TestExit=Automation Test Queue Empty", f"-ReportExportPath={output}",
               f"-abslog={output / 'engine.log'}"]
    environment = os.environ.copy()
    if args.cache_root:
        cache = args.cache_root.resolve()
        (cache / "Temp").mkdir(parents=True, exist_ok=True)
        environment.update(TEMP=str(cache / "Temp"), TMP=str(cache / "Temp"))
        environment["UE-LocalDataCachePath"] = str(cache / "DerivedDataCache")
    provenance = {
        "engine": "unreal-runtime", "world": "PIE", "rendering": "NullRHI",
        "evidenceScope": "world-objective-integration", "humanUsabilityTest": False,
        "fixtureDamage": True, "aiStoppedDuringInterventions": True, "placedOccupants": True,
        "command": command, "testPath": TEST_PATH, "passed": False,
        "startedAtUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "host": platform.platform(), "expectedNativeAssertions": NATIVE_ASSERTIONS,
        "fixtureStartupDeadlineSeconds": 35, "fixtureRunDeadlineSeconds": 45,
        "processTimeoutSeconds": 180, "editorHomeScreenEnabled": False,
        "editorHomeScreenOverride": OFFLINE_ARGUMENT,
        "sourceDigestScope": "Source, core/include, Config, AegisArena.uproject; sorted relative path + bytes",
        "contentDigestScope": "Content/Aegis; sorted relative path + bytes",
        "gateSha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "fixtureGeneratorSha256": hashlib.sha256(
            (ROOT / "scripts/unreal/create_operation_fixture.py").read_bytes()).hexdigest(),
    }
    started = time.monotonic()
    try:
        provenance["engineBuild"] = json.loads((engine_root / "Engine/Build/Build.version").read_text())
        provenance["gitHead"] = subprocess.check_output(
            ["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip()
        provenance["gitStatus"] = subprocess.check_output(
            ["git", "-C", str(ROOT), "status", "--porcelain"], text=True).splitlines()
        inputs = snapshot_inputs()
        provenance.update(inputs)
        with (output / "stdout.log").open("w", encoding="utf-8") as target:
            result = subprocess.run(command, stdout=target, stderr=subprocess.STDOUT,
                                    env=environment, timeout=180, check=False)
        provenance["exitCode"] = result.returncode
        if result.returncode:
            raise RuntimeError(f"Unreal process exited {result.returncode}")
        report = json.loads((output / "index.json").read_text(encoding="utf-8-sig"))
        log = (output / "engine.log").read_text(encoding="utf-8-sig", errors="replace")
        validate(report, log)
        after = snapshot_inputs()
        provenance["inputsAfter"] = after
        if after != inputs:
            raise RuntimeError("Source or content changed during the native operation run")
        provenance.update(passed=True, nativeAssertions=NATIVE_ASSERTIONS, assertionCounts=dict(ASSERTIONS))
        print(f"Native operation Functional Test PASS: {NATIVE_ASSERTIONS} world assertions; {output}")
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        provenance["failure"] = str(error)
        raise
    finally:
        provenance["wallSeconds"] = time.monotonic() - started
        if "inputsAfter" not in provenance:
            try:
                provenance["inputsAfter"] = snapshot_inputs()
            except OSError as error:
                provenance["inputsAfterFailure"] = str(error)
        for name in ("index.json", "engine.log", "stdout.log"):
            path = output / name
            if path.is_file():
                provenance[name + "Sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
        (output / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
