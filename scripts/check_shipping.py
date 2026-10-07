"""Check actual target binaries for development registration markers.

This static binary check does not claim cook, launch or interactive acceptance.
"""
import argparse
import datetime
import hashlib
import json
import mmap
from pathlib import Path


MARKERS = (
    "aegis.PauseAI", "aegis.StepDecision", "aegis.Policy", "aegis.SpawnBot",
    "aegis.KillBot", "aegis.Visualize", "aegis.RunScenario",
    "AEGIS / development diagnostics",
    "AEGIS_EQS",
    "AEGIS_INPUT_PROBE_PASS", "AegisProbeOutput=",
    "AEGIS_PLAY_PROBE_PASS", "AegisPlayOutput=",
    "AEGIS_COPILOT_PROBE_BEGIN", "AegisCopilotOutput=",
    "AEGIS_COPILOT_FAULT_BEGIN", "AegisCopilotFaultOutput=",
    "AEGIS_RELEASE_PROBE_CHECK", "AegisReleaseProbeOutput=",
)


def inspect(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1048576), b""):
            digest.update(chunk)
        with mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as binary:
            markers = {text: binary.find(text.encode("utf-16-le")) >= 0 or
                       binary.find(text.encode("ascii")) >= 0 for text in MARKERS}
    return {"file": path.name, "bytes": path.stat().st_size,
            "sha256": digest.hexdigest(), "markers": markers}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binaries", type=Path, default=Path("Binaries/Win64"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    development = inspect(args.binaries / "AegisArena.exe")
    shipping = inspect(args.binaries / "AegisArena-Win64-Shipping.exe")
    passed = all(development["markers"].values()) and not any(shipping["markers"].values())
    report = {
        "checkedAtUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "check": "Development vs Shipping registration strings; not execution or packaging acceptance",
        "targets": {"Development": development, "Shipping": shipping}, "passed": passed,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Static Shipping marker check: {'PASS' if passed else 'FAIL'}; {args.output}")
    raise SystemExit(0 if passed else 1)


if __name__ == "__main__":
    main()
