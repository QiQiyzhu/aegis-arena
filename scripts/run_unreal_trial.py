"""Run the isolated PIE trial fixture and reject report-only or partial success."""
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

ROOT = Path(__file__).resolve().parents[1]
MAP = "/Game/Aegis/Maps/AegisTrialFunctional"
TEST_PATH = "Project.Functional Tests.Aegis.Maps.AegisTrialFunctional"
# UE 5.8 primary source: MainFrameModule.cpp registers this read-only CVar;
# TabManager.h gates Home Screen creation on it; SHomeScreen.cpp:956 otherwise
# sends the external generate_204 probe. DeviceProfileManager.cpp:739-766 applies
# ForceDPCVars at startup with command-line priority, before the panel is built.
OFFLINE_ARGUMENT = "-ForceDPCVars=HomeScreen.EnableHomeScreen=0"
OFFLINE_MARKER = "Setting CommandLine Device Profile CVar: [[HomeScreen.EnableHomeScreen:0]]"
DIRECT_EVENT = re.compile(r"^(?:\[[^\]\r\n]*\])*LogTemp: Display: (AEGIS_TRIAL_[^\r\n]+)$")
ASSERTION_PREFIX = "AEGIS_TRIAL_ASSERT_PASS "
MARKER = ("AEGIS_TRIAL_FUNCTIONAL_PASS | Assertions=54 | WorldType=3 | "
          "BegunPlay=1 | FixtureDamage=1")

# Multiplicities matter: five fresh briefings, all four wave deployments, and
# three retry checks must execute. A repeated unrelated success is insufficient.
ASSERTIONS = Counter({
    "Exactly one configured trial runner": 1,
    "Player and companion exist": 5,
    "Briefing contains no enemies or active wave": 5,
    "Briefing blocks player combat input": 5,
    "Fresh player health movement collision and cooldowns restored": 5,
    "Trial damage counters reset": 5,
    "Exact bot controller and possessed-player population": 9,
    "Guided deploy creates two enemies and enables combat": 1,
    "Duplicate deploy cannot change mode or add enemies": 1,
    "Authored hostile damage reaches real allied health components": 1,
    "Damage delegates record allied output and player intake once": 1,
    "First elimination enters recovery without spawning early": 1,
    "Guided recovery heals player thirty and companion forty": 1,
    "Repeated recovery updates neither spawn nor postpone transition": 1,
    "Recovery healing happens exactly once": 1,
    "Second wave starts after real five-second recovery with three enemies": 1,
    "Second clear enters second recovery": 1,
    "Final guided wave contains four enemies after recovery": 1,
    "Final wave contains exactly one elite": 1,
    "Final clear wins and blocks player combat": 1,
    "Finished trial cannot redeploy without restart": 1,
    "Pressure opening creates three enemies": 1,
    "Player death produces loss and stops combat movement collision": 1,
    "Retry destroys prior pawn and possesses a new one": 3,
})
NATIVE_ASSERTIONS = sum(ASSERTIONS.values())


def validate(report, log):
    expected = dict(succeeded=1, succeededWithWarnings=0, failed=0, notRun=0, inProcess=0)
    if any(type(report.get(key)) is not int or report[key] != value
           for key, value in expected.items()):
        raise ValueError("Expected exactly one completed native trial test and zero warnings or failures")
    tests = report.get("tests", [])
    if (len(tests) != 1 or tests[0].get("state") != "Success" or
            not tests[0].get("fullTestPath", "").startswith(TEST_PATH + ".")):
        raise ValueError("Report does not identify the requested trial fixture")
    if OFFLINE_MARKER not in log:
        raise ValueError("No startup evidence that the editor Home Screen connectivity probe was disabled")
    # Failed Automation tests replay their recorded log entries through
    # LogAutomationController. Count only original fixture emissions; the full
    # report and entire log still participate in failure/ensure checks below.
    events = [match.group(1).strip() for line in log.splitlines()
              if (match := DIRECT_EVENT.fullmatch(line))]
    markers = [event for event in events if event.startswith("AEGIS_TRIAL_FUNCTIONAL_PASS")]
    if markers != [MARKER]:
        raise ValueError("Missing unique completed 54-assertion PIE trial marker")
    observed = Counter(event.partition(ASSERTION_PREFIX)[2].strip()
                       for event in events if event.startswith(ASSERTION_PREFIX))
    if observed != ASSERTIONS or sum(observed.values()) != 54:
        raise ValueError("Trial log lacks the exact required assertion labels and multiplicities")
    if any(marker in log for marker in (
            "Handled ensure", "Ensure condition failed", "No functional testing script",
            "TestResult=Failed", "Fatal error:", "Assertion failed")):
        raise ValueError("Engine log contains a functional failure, fatal error, or ensure")


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
        "command": command, "testPath": TEST_PATH,
        "startedAtUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "host": platform.platform(), "passed": False,
        "fixtureDamage": True, "aiStoppedDuringInterventions": True,
        "editorHomeScreenEnabled": False,
        "editorHomeScreenOverride": OFFLINE_ARGUMENT,
        "expectedNativeAssertions": NATIVE_ASSERTIONS,
        "sourceDigestScope": "Source, core/include, Config, AegisArena.uproject; sorted relative path + bytes",
        "contentDigestScope": "Content/Aegis; sorted relative path + bytes",
        "gateSha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
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
                                    env=environment, timeout=150, check=False)
        provenance["exitCode"] = result.returncode
        if result.returncode:
            raise RuntimeError(f"Unreal process exited {result.returncode}")
        report = json.loads((output / "index.json").read_text(encoding="utf-8-sig"))
        log = (output / "engine.log").read_text(encoding="utf-8-sig", errors="replace")
        validate(report, log)
        if snapshot_inputs() != inputs:
            raise RuntimeError("Source or content changed during the native trial run")
        provenance.update(passed=True, nativeAssertions=NATIVE_ASSERTIONS,
                          assertionCounts=dict(ASSERTIONS))
        print(f"Native trial Functional Test PASS: {NATIVE_ASSERTIONS} world assertions; {output}")
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
