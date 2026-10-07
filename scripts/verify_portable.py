"""One-command portable verification; never invokes Unreal.

By default: strict C++ build, core assertions, the current Python test suite, and a
two-episode smoke scenario. --full additionally runs the defined 60-episode set.
The driver uses the standard library; the full test suite requires installing
the pinned dependencies with `python -m pip install -r requirements-media.txt`.
"""
from __future__ import annotations
import argparse
import datetime as dt
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", help="g++, clang++, or absolute path to zig executable")
    parser.add_argument("--full", action="store_true", help="also run scenarios/evaluation.json (60 episodes)")
    parser.add_argument("--output", type=pathlib.Path, help="new output directory; default is a timestamped outputs/verify directory")
    args = parser.parse_args()
    output = args.output or ROOT / "outputs" / ("verify-" + dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S%fZ"))
    output = output.resolve()
    if output.exists():
        parser.exit(2, "Verification output already exists; choose a new directory to preserve evidence.\n")
    output.mkdir(parents=True)
    commands = [
        ("build", [sys.executable, "scripts/build_portable.py"] + (["--compiler", args.compiler] if args.compiler else [])),
        ("python-tests", [sys.executable, "-m", "unittest", "discover", "-s", "scripts", "-p", "test_*.py", "-v"]),
        ("smoke", [sys.executable, "scripts/run_benchmark.py", "--scenario", "scenarios/smoke.json", "--output", str(output / "smoke")]),
    ]
    if args.full:
        commands.append(("evaluation", [sys.executable, "scripts/run_benchmark.py", "--scenario", "scenarios/evaluation.json", "--output", str(output / "evaluation")]))
    manifest = {"engine": "portable-cpp-model", "unreal_invoked": False, "status": "running", "steps": []}
    manifest_path = output / "verification.json"
    try:
        for name, command in commands:
            print(f"[{name}]", flush=True)
            completed = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
            (output / f"{name}.log").write_text(completed.stdout, encoding="utf-8")
            print(completed.stdout, end="", flush=True)
            manifest["steps"].append({"name": name, "exit_code": completed.returncode})
            if completed.returncode:
                manifest["status"] = "failed"
                manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
                return completed.returncode
        manifest["status"] = "passed"
        manifest["build"] = json.loads((ROOT / "build/build-report.json").read_text(encoding="utf-8"))
    except (OSError, subprocess.TimeoutExpired, ValueError) as exc:
        manifest["status"] = "failed"
        manifest["error"] = str(exc)
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        print(f"Verification failed: {exc}", file=sys.stderr)
        return 2
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Portable verification passed. Evidence: {output}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
