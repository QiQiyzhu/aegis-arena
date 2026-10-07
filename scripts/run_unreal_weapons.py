"""Run isolated native weapon rules, requiring exact original PIE assertions."""
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
MAP = "/Game/Aegis/Maps/AegisWeaponsFunctional"
TEST_PATH = "Project.Functional Tests.Aegis.Maps.AegisWeaponsFunctional"
FULL_TEST_PATH = TEST_PATH + ".Weapons rules - fixed geometry and seeded windup"
ASSERTION_PREFIX = "AEGIS_WEAPONS_ASSERT_PASS "
ASSERTIONS = (
    "Weapons fixture runs in begun play game world",
    "Weapons fixture has isolated offscreen input context",
    "Weapons fixture owns a local player controller",
    "Fresh player preserves baseline combat and disabled upgrades",
    "Pulse requires explicit trial opt in",
    "Pulse fixture contains a pending seeded shot warning",
    "Enabled pulse activation uses real gameplay entry",
    "Pulse applies exactly thirty two damage to visible hostile",
    "World cover blocks pulse damage",
    "Pulse cannot hit beyond radius",
    "Normal pulse neither damages allies nor restores health",
    "Normal pulse cannot revive dead companion",
    "Pulse staggers hostile and immediately cancels controller warning",
    "Stagger blocks both ranged and melee combat",
    "Pulse cooldown rejects duplicate without additional damage",
    "Stagger expires and permits combat again",
    "Restorative upgrade uses the same pulse activation",
    "Restorative pulse heals self by twelve",
    "Restorative pulse heals visible companion by twenty",
    "World cover blocks companion restoration",
    "Restorative pulse cannot revive a dead companion",
    "Baseline shot stops at first hostile for fourteen damage",
    "Piercing round damages two distinct hostiles once each",
    "Piercing round stops after two hostiles from one attack",
    "Cover after first hostile blocks piercing continuation",
    "Ally after first hostile blocks piercing and receives no damage",
    "Ally at muzzle blocks entire piercing shot",
    "Cover at muzzle blocks entire piercing shot",
    "Piercing continuation retains original total range",
    "Pulse cooldown tuning controls actual ability and HUD duration",
    "Fresh pawn clears upgrades activation counters and cooldown tuning",
)
NATIVE_ASSERTIONS = len(ASSERTIONS)
MARKER = (f"AEGIS_WEAPONS_FUNCTIONAL_PASS | Assertions={NATIVE_ASSERTIONS} | "
          "WorldType=3 | BegunPlay=1 | FixtureDamage=1")
FIXTURE_MARKER = ("AEGIS_WEAPONS_FIXTURE | WorldType=3 | BegunPlay=1 | "
                  "FixtureDamage=1 | FrozenMovement=1 | SeededWindup=1")
DIRECT_EVENT = re.compile(r"^(?:\[[^\]\r\n]*\])*LogTemp: Display: (AEGIS_WEAPONS_[^\r\n]+)$")


def validate(report, log):
    expected = dict(succeeded=1, succeededWithWarnings=0, failed=0, notRun=0, inProcess=0)
    if any(type(report.get(key)) is not int or report[key] != value for key, value in expected.items()):
        raise ValueError("Expected one completed clean weapons fixture")
    tests = report.get("tests", [])
    if (len(tests) != 1 or tests[0].get("state") != "Success" or
            tests[0].get("fullTestPath") != FULL_TEST_PATH):
        raise ValueError("Report does not name the exact isolated weapons test")
    if OFFLINE_MARKER not in log:
        raise ValueError("Missing offline editor startup evidence")
    events = [match.group(1).strip() for line in log.splitlines()
              if (match := DIRECT_EVENT.fullmatch(line))]
    if [event for event in events if event.startswith("AEGIS_WEAPONS_FUNCTIONAL_PASS")] != [MARKER]:
        raise ValueError("Missing unique completed weapons PIE marker")
    if [event for event in events if event.startswith("AEGIS_WEAPONS_FIXTURE ")] != [FIXTURE_MARKER]:
        raise ValueError("Missing unique fixed-geometry seeded-windup disclosure")
    observed = Counter(event[len(ASSERTION_PREFIX):] for event in events if event.startswith(ASSERTION_PREFIX))
    if observed != Counter(ASSERTIONS):
        raise ValueError("Missing, duplicated or unexpected original weapons assertion")
    if any(failure in log for failure in (
            "Handled ensure", "Ensure condition failed", "No functional testing script",
            "TestResult=Failed", "Fatal error:", "Assertion failed", "AEGIS_WEAPONS_FUNCTIONAL_FAIL")):
        raise ValueError("Engine log contains a weapons fixture failure or ensure")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cache-root", type=Path)
    args = parser.parse_args()
    engine_root = args.engine_root.resolve()
    editor = engine_root / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
    if not editor.is_file():
        parser.error("UnrealEditor-Cmd.exe missing")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    # No screenshots or rendering claim: NullRHI measures rules. RenderOffscreen
    # provides a virtual cursor, while the opt-in permits the real ability entry
    # under that virtual input context. This map contains no scenario runner or probe.
    command = [str(editor), str(ROOT / "AegisArena.uproject"), MAP,
               "-unattended", "-nop4", "-nosound", "-NullRHI", "-RenderOffscreen", "-AegisInputProbe",
               "-stdout", "-FullStdOutLogOutput", OFFLINE_ARGUMENT,
               f"-ExecCmds=Automation RunTests {TEST_PATH}", "-TestExit=Automation Test Queue Empty",
               f"-ReportExportPath={output}", f"-abslog={output / 'engine.log'}"]
    environment = os.environ.copy()
    if args.cache_root:
        cache = args.cache_root.resolve()
        (cache / "Temp").mkdir(parents=True, exist_ok=True)
        environment.update(TEMP=str(cache / "Temp"), TMP=str(cache / "Temp"))
        environment["UE-LocalDataCachePath"] = str(cache / "DerivedDataCache")
    provenance = {
        "engine": "unreal-runtime", "world": "PIE", "rendering": "NullRHI",
        "command": command, "testPath": FULL_TEST_PATH, "passed": False,
        "startedAtUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "host": platform.platform(), "fixtureDamage": True, "fixedGeometry": True,
        "frozenMovement": True, "seededWindup": True, "humanUsabilityTest": False,
        "abilityInvocation": "real TryPulse entry with offscreen local-controller context",
        "expectedNativeAssertions": NATIVE_ASSERTIONS,
        "editorHomeScreenEnabled": False,
        "sourceDigestScope": "Source, core/include, Config, AegisArena.uproject; sorted relative path + bytes",
        "contentDigestScope": "Content/Aegis; sorted relative path + bytes",
        "gateSha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    }
    started = time.monotonic()
    try:
        provenance["engineBuild"] = json.loads((engine_root / "Engine/Build/Build.version").read_text())
        provenance["gitHead"] = subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip()
        provenance["gitStatus"] = subprocess.check_output(["git", "-C", str(ROOT), "status", "--porcelain"], text=True).splitlines()
        inputs = snapshot_inputs()
        provenance.update(inputs)
        with (output / "stdout.log").open("w", encoding="utf-8") as target:
            result = subprocess.run(command, stdout=target, stderr=subprocess.STDOUT, env=environment,
                                    timeout=120, check=False)
        provenance["exitCode"] = result.returncode
        if result.returncode:
            raise RuntimeError(f"Unreal exited {result.returncode}")
        report = json.loads((output / "index.json").read_text(encoding="utf-8-sig"))
        validate(report, (output / "engine.log").read_text(encoding="utf-8-sig", errors="replace"))
        if snapshot_inputs() != inputs:
            raise RuntimeError("Source or content changed during weapons fixture")
        provenance.update(passed=True, nativeAssertions=NATIVE_ASSERTIONS)
        print(f"Native weapons Functional Test PASS: {NATIVE_ASSERTIONS} world assertions; {output}")
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
