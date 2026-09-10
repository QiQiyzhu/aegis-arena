"""Validate a cooked Development package without launching an Unreal Editor."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import time
from types import SimpleNamespace

from run_unreal_scenario import validate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package-root", type=Path, required=True, help="Archived Windows directory")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    package = args.package_root.resolve()
    executable = package / "AegisArena/Binaries/Win64/AegisArena.exe"
    if not executable.is_file():
        parser.error("Cooked Development game executable missing; Shipping excludes the batch entry point")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    user_dir = output / "user"
    command = [str(executable), "-AegisBatch", "-AegisQuit", "-AegisPolicy=utility",
               "-AegisEnemies=2", "-AegisEpisodes=2", "-AegisSeed=2001", "-AegisDuration=15",
               "-UseFixedTimeStep", "-FPS=60", "-NullRHI", "-unattended", "-nosound",
               "-stdout", f"-UserDir={user_dir}", f"-abslog={output / 'engine.log'}"]
    digest = hashlib.sha256()
    with executable.open("rb") as binary:
        for chunk in iter(lambda: binary.read(1024 * 1024), b""):
            digest.update(chunk)
    report = {"engine": "unreal-packaged-development", "passed": False,
              "startedAtUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "command": command, "binarySha256": digest.hexdigest(),
              "limitation": "Cooked game-world smoke with NullRHI; separate from UI/render/performance acceptance"}
    start = time.monotonic()
    try:
        with (output / "stdout.log").open("w", encoding="utf-8") as log:
            process = subprocess.run(command, cwd=package, stdout=log, stderr=subprocess.STDOUT,
                                     timeout=120, check=False)
        report["exitCode"] = process.returncode
        if process.returncode != 0:
            raise RuntimeError("Packaged game returned failure")
        log_text = (output / "engine.log").read_text(encoding="utf-8-sig", errors="replace")
        markers = re.findall(r"AEGIS_REPORT Complete: ([^\r\n]+)", log_text)
        if len(markers) != 1:
            raise RuntimeError("A process exit alone cannot pass: successful report marker missing")
        source = Path(markers[0].strip()).resolve()
        source.relative_to((user_dir / "Saved/AegisReports").resolve())
        files = sorted(source.glob("episode-*.json"))
        rows = [json.loads(path.read_text(encoding="utf-8-sig")) for path in files]
        validate(rows, SimpleNamespace(episodes=2, seed=2001, policy="utility", enemies=2,
                                      performance=False, director=False, duration=15, rendered=False))
        shutil.copytree(source, output / "raw")
        report.update(passed=True, episodes=len(rows))
        print(f"Cooked Development PASS: two game-world episodes; {output}")
    finally:
        report["wallSeconds"] = time.monotonic() - start
        (output / "provenance.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
