"""Run one real UE batch and retain fresh reports; never substitutes the portable model."""
import argparse
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import time


ROOT = Path(__file__).resolve().parents[1]


def validate(rows, args):
    if len(rows) != args.episodes:
        raise ValueError(f"Expected {args.episodes} complete episodes, got {len(rows)}")
    for index, row in enumerate(rows):
        scenario = row["scenario"]
        if row["engine"] != "unreal-runtime":
            raise ValueError("Wrong runtime evidence source")
        expected = {"seed": args.seed + index, "companionPolicy": args.policy,
                    "enemyCount": args.enemies, "performanceMode": args.performance,
                    "directorEnabled": args.director, "duration": args.duration}
        if any(scenario[key] != value for key, value in expected.items()):
            raise ValueError(f"Episode {index} configuration differs from requested inputs")
        if row["decisionCounts"] <= 0 or row["survivalSeconds"] <= 0:
            raise ValueError(f"Episode {index} did not execute AI decisions")
        if row["renderingEnabled"] != args.rendered:
            raise ValueError("Reported rendering mode differs from command")
        if args.performance and (row["frameSamples"] <= 0 or row["damageDealt"] != 0 or
                                 row["damageTaken"] != 0 or row["win"]):
            raise ValueError("Performance mode did not preserve a measured damage-free workload")
        if args.performance and row["survivalSeconds"] < args.duration - 0.11:
            raise ValueError("Performance episode terminated before requested sustained duration")
        if any(isinstance(value, float) and not math.isfinite(value) for value in row.values()):
            raise ValueError("Nonfinite measurement")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cache-root", type=Path)
    parser.add_argument("--policy", choices=("utility", "priority"), default="utility")
    parser.add_argument("--enemies", type=int, default=4)
    parser.add_argument("--episodes", type=int, default=2)
    parser.add_argument("--seed", type=int, default=1001)
    parser.add_argument("--duration", type=float, default=15)
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--performance", action="store_true")
    parser.add_argument("--director", action="store_true")
    parser.add_argument("--rendered", action="store_true")
    parser.add_argument("--capture", action="store_true", help="Capture native still frames, separate from performance runs")
    parser.add_argument("--query-diagnostics", action="store_true", help="Retain actual EQS debug candidates; excluded from performance evidence")
    parser.add_argument("--fixed-step", action="store_true", help="UE fixed 1/60 game step for policy evaluation; not real-time performance")
    args = parser.parse_args()
    if not 1 <= args.enemies <= 50 or not 1 <= args.episodes <= 100 or not 1 <= args.duration <= 300:
        parser.error("Expected enemies 1..50, episodes 1..100, finite duration 1..300")
    if not -2147483648 <= args.seed <= 2147483647 - args.episodes or not 1 <= args.timeout <= 86400:
        parser.error("Seed range overflows or timeout outside 1..86400 seconds")
    if args.performance and args.duration <= 1:
        parser.error("Performance duration must exceed the one-second warm-up")
    if args.performance and args.fixed_step:
        parser.error("Fixed game-time policy evaluation cannot be used as real-time performance evidence")
    if args.performance and args.query_diagnostics:
        parser.error("Candidate retention/drawing cannot be used in performance measurement")
    if args.capture and (not args.rendered or args.performance or args.fixed_step):
        parser.error("Capture requires rendered real-time policy mode, separate from performance")
    editor = args.engine_root.resolve() / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
    if not editor.is_file():
        parser.error("UnrealEditor-Cmd.exe missing")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    command = [str(editor), str(ROOT / "AegisArena.uproject"), "/Game/Aegis/Maps/AegisArena",
               "-game", "-AegisBatch", "-AegisQuit", f"-AegisPolicy={args.policy}",
               f"-AegisEnemies={args.enemies}", f"-AegisEpisodes={args.episodes}",
               f"-AegisSeed={args.seed}", f"-AegisDuration={args.duration}",
               "-unattended", "-nop4", "-nosound", "-stdout", "-FullStdOutLogOutput",
               f"-abslog={output / 'engine.log'}"]
    if not args.rendered:
        command.append("-NullRHI")
    else:
        command += ["-RenderOffScreen", "-ResX=1280", "-ResY=720", "-windowed"]
    if args.performance:
        command.append("-AegisPerformance")
    if args.director:
        command.append("-AegisDirector")
    if args.capture:
        command.append("-AegisCapture")
    if args.query_diagnostics:
        command.append("-AegisQueryDiagnostics")
    if args.fixed_step:
        command += ["-UseFixedTimeStep", "-FPS=60"]
    environment = os.environ.copy()
    if args.cache_root:
        cache = args.cache_root.resolve()
        (cache / "Temp").mkdir(parents=True, exist_ok=True)
        environment.update(TEMP=str(cache / "Temp"), TMP=str(cache / "Temp"))
        environment["UE-LocalDataCachePath"] = str(cache / "DerivedDataCache")
    start = time.monotonic()
    provenance = {"engine": "unreal-runtime", "startedAtUtc": datetime.datetime.now(
        datetime.timezone.utc).isoformat(), "command": command, "host": platform.platform(),
        "engineBuild": json.loads((args.engine_root / "Engine/Build/Build.version").read_text())}
    provenance["configuredGameStepSeconds"] = 1 / 60 if args.fixed_step else None
    provenance["timeMode"] = "fixed game timestep; wall throughput is not gameplay FPS" if args.fixed_step else "real-time engine clock"
    source_hash = hashlib.sha256()
    source_files = [path for directory in ("Source", "core/include", "Config")
                    for path in (ROOT / directory).rglob("*") if path.is_file()]
    source_files.append(ROOT / "AegisArena.uproject")
    for path in sorted(source_files):
        if path.is_file():
            source_hash.update(path.relative_to(ROOT).as_posix().encode())
            source_hash.update(path.read_bytes())
    provenance["sourceSha256"] = source_hash.hexdigest()
    provenance["sourceDigestScope"] = "Source, core/include, Config, AegisArena.uproject; sorted relative path + bytes"
    content_hash = hashlib.sha256()
    for path in sorted((ROOT / "Content/Aegis").rglob("*")):
        if path.is_file():
            content_hash.update(path.relative_to(ROOT).as_posix().encode())
            content_hash.update(path.read_bytes())
    provenance["contentSha256"] = content_hash.hexdigest()
    try:
        with (output / "stdout.log").open("w", encoding="utf-8") as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                    env=environment, timeout=args.timeout, check=False)
        provenance["exitCode"] = result.returncode
        if result.returncode:
            raise RuntimeError(f"Unreal process failed ({result.returncode}); inspect {output}")
        log_text = (output / "engine.log").read_text(encoding="utf-8-sig", errors="replace")
        # Preserve actual RHI selection; Automation's hardware inventory can name
        # an integrated adapter even when D3D12 renders on the discrete GPU.
        provenance["rhiLogEvidence"] = [line.split("]", 2)[-1].strip()
            for line in log_text.splitlines() if any(marker in line for marker in (
                "Found D3D12 adapter", "Chosen D3D12 Adapter", "Driver Version:",
                "RHI Selected:", "RHIName:"))]
        provenance["requestedResolution"] = [1280, 720] if args.rendered else None
        provenance["captureIsPerformanceMeasurement"] = False
        provenance["queryDiagnosticsEnabled"] = args.query_diagnostics
        if args.query_diagnostics:
            records = [json.loads(value) for value in re.findall(r"AEGIS_EQS ([^\r\n]+)", log_text)]
            if not records:
                raise RuntimeError("No actual EQS diagnostic records were produced")
            (output / "query-diagnostics.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
        matches = re.findall(r"AEGIS_REPORT Complete: ([^\r\n]+)", log_text)
        if len(matches) != 1:
            raise RuntimeError("Expected one fresh successful AEGIS_REPORT marker")
        reports = Path(matches[0].strip())
        if not reports.is_absolute():
            reports = editor.parent / reports
        reports = reports.resolve()
        reports.relative_to((ROOT / "Saved/AegisReports").resolve())
        sources = sorted(reports.glob("episode-*.json"))
        rows = [json.loads(path.read_text(encoding="utf-8-sig")) for path in sources]
        validate(rows, args)
        raw = output / "raw"
        raw.mkdir()
        for path in sources + [reports / "episodes.csv"]:
            shutil.copy2(path, raw / path.name)
        if args.capture:
            capture_markers = re.findall(r"AEGIS_CAPTURE ([^\r\n]+)", log_text)
            if len(capture_markers) != 1:
                raise RuntimeError("Expected one native capture directory")
            captures = Path(capture_markers[0].strip())
            if not captures.is_absolute():
                captures = editor.parent / captures
            captures = captures.resolve()
            captures.relative_to((ROOT / "Saved/AegisCaptures").resolve())
            frames = sorted(captures.glob("frame-*.png"))
            if len(frames) < 3:
                raise RuntimeError("Fewer than three actual PNG frames were captured")
            media = output / "captures"
            media.mkdir()
            for path in frames:
                with path.open("rb") as source:
                    if source.read(8) != b"\x89PNG\r\n\x1a\n":
                        raise RuntimeError(f"Invalid PNG capture: {path.name}")
                shutil.copy2(path, media / path.name)
            shutil.copy2(captures / "frames.csv", media / "frames.csv")
            provenance["capturedNativeFrames"] = len(frames)
        provenance.update(passed=True, episodeCount=len(rows))
        print(f"UE native batch PASS: {len(rows)} episodes; {output}")
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.TimeoutExpired) as error:
        provenance.update(passed=False, failure=str(error))
        raise
    finally:
        provenance["wallSeconds"] = time.monotonic() - start
        (output / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
