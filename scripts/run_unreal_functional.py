"""Run the native PIE fixture, rejecting UE's observed JSON-only false success."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
MARKER = "AEGIS_FUNCTIONAL_PASS CombatAndTactical | WorldType=3 | BegunPlay=1"
MEMORY_MARKER = "AEGIS_MEMORY_FIXTURE | Seconds=2.500 | Seed=1001 | WorldType=3 | BegunPlay=1"
ASSERTIONS = (
    "Ranged fire accepted", "Real trace applies 14 damage", "Cooldown rejects duplicate fire",
    "Team filter rejects friendly damage", "Dead pawn cannot heal",
    "Real default controller possesses pawn", "Pawn moved away from spawn",
    "EQS Querier context resolves", "EQS Querier follows moved pawn",
    "Missing attack query permits fallback", "Missing cover query permits fallback",
    "Missing retreat query permits fallback",
    "Memory fixture acquires target through real sight",
    "Configured target memory lasts 2.5 seconds",
    "Sight loss clears live target and blackboard", "Sight loss clears actor focus",
    "Lost target remains remembered before deadline",
    "Unseen motion cannot update remembered location", "Hidden target cannot be attacked",
    "Memory expiry clears target and blackboard location",
    "Expired memory rejects investigation", "Expired memory rejects tactical query",
    "Sight reacquisition restores target and memory", "Reacquisition records newly perceived location",
    "Real EQS request becomes pending", "Shutdown cancels pending EQS and clears blackboard",
    "Shutdown cannot be repopulated by perception", "Cancelled EQS cannot publish a late tactical point",
)


def snapshot_inputs(root=ROOT):
    """Use the native scenario runner's sorted relative path + raw bytes digest."""
    source_files = [path for directory in ("Source", "core/include", "Config")
                    for path in (root / directory).rglob("*") if path.is_file()]
    source_files.append(root / "AegisArena.uproject")
    digests = {}
    for name, files in (("sourceSha256", source_files),
                        ("contentSha256", (root / "Content/Aegis").rglob("*"))):
        digest = hashlib.sha256()
        for path in sorted(files):
            if path.is_file():
                digest.update(path.relative_to(root).as_posix().encode())
                digest.update(path.read_bytes())
        digests[name] = digest.hexdigest()
    return digests


def validate(report, log):
    if (report.get("succeeded") != 1 or report.get("failed") != 0 or
            report.get("notRun") != 0 or report.get("inProcess") != 0 or
            report.get("succeededWithWarnings", 0) != 0):
        raise ValueError("Expected one completed clean native Functional Test")
    if (MARKER not in log or MEMORY_MARKER not in log or
            any(f"Assertion passed ({name})" not in log for name in ASSERTIONS)):
        raise ValueError("No proof that the PIE fixture executed every required assertion")
    if any(marker in log for marker in ("Handled ensure", "No functional testing script",
                                        "TestResult=Failed", "Fatal error:")):
        raise ValueError("Engine log contains a functional failure or handled ensure")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cache-root", type=Path)
    args = parser.parse_args()
    editor = args.engine_root.resolve() / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
    if not editor.is_file():
        parser.error("UnrealEditor-Cmd.exe is missing")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    command = [str(editor), str(ROOT / "AegisArena.uproject"), "/Game/Aegis/Maps/AegisFunctional",
               "-unattended", "-nop4", "-NullRHI", "-stdout", "-FullStdOutLogOutput",
               "-ForceDPCVars=HomeScreen.EnableHomeScreen=0",
               "-ExecCmds=Automation RunTests Project.Functional Tests.Aegis.Maps.AegisFunctional",
               "-TestExit=Automation Test Queue Empty", f"-ReportExportPath={output}",
               f"-abslog={output / 'engine.log'}"]
    environment = os.environ.copy()
    if args.cache_root:
        cache = args.cache_root.resolve()
        (cache / "Temp").mkdir(parents=True, exist_ok=True)
        environment.update(TEMP=str(cache / "Temp"), TMP=str(cache / "Temp"))
        environment["UE-LocalDataCachePath"] = str(cache / "DerivedDataCache")
    provenance = {"engine": "unreal-runtime", "world": "PIE", "rendering": "NullRHI",
                  "command": command, "startedAtUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  "host": platform.platform(), "passed": False,
                  "fixtureSeed": 1001, "targetMemorySeconds": 2.5,
                  "sourceDigestScope": "Source, core/include, Config, AegisArena.uproject; sorted relative path + bytes",
                  "contentDigestScope": "Content/Aegis; sorted relative path + bytes"}
    start = time.monotonic()
    try:
        provenance["engineBuild"] = json.loads((args.engine_root / "Engine/Build/Build.version").read_text())
        provenance["gitHead"] = subprocess.check_output(
            ["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip()
        provenance["gitStatus"] = subprocess.check_output(
            ["git", "-C", str(ROOT), "status", "--porcelain"], text=True).splitlines()
        inputs = snapshot_inputs()
        provenance.update(inputs)
        with (output / "stdout.log").open("w", encoding="utf-8") as target:
            result = subprocess.run(command, stdout=target, stderr=subprocess.STDOUT,
                                    env=environment, timeout=120, check=False)
        provenance["exitCode"] = result.returncode
        if result.returncode:
            raise RuntimeError(f"Unreal process exited {result.returncode}")
        report = json.loads((output / "index.json").read_text(encoding="utf-8-sig"))
        log = (output / "engine.log").read_text(encoding="utf-8-sig", errors="replace")
        validate(report, log)
        if snapshot_inputs() != inputs:
            raise RuntimeError("Source or content changed during the native fixture run")
        provenance.update(passed=True, nativeAssertions=len(ASSERTIONS))
        print(f"Native PIE Functional Test PASS: {len(ASSERTIONS)} real world assertions; {output}")
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        provenance["failure"] = str(error)
        raise
    finally:
        provenance["wallSeconds"] = time.monotonic() - start
        (output / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
