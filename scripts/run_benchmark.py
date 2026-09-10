"""Run real compiled model episodes; provenance never labels these as Unreal runs."""
from __future__ import annotations
import argparse
import csv
import datetime as dt
import hashlib
import json
import pathlib
import platform
import statistics
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

def validate(config: dict) -> dict:
    required = {"schema_version", "arena", "player_config", "companion_policies", "enemy_policy", "enemy_counts", "seed_start", "episodes", "duration", "director"}
    if set(config) != required or config["schema_version"] != 1:
        raise ValueError("Unknown keys or invalid schema_version")
    for field, low, high in (("seed_start", 0, 2**32 - 1), ("episodes", 1, 1000), ("duration", 1, 300)):
        if type(config[field]) is not int or not low <= config[field] <= high:
            raise ValueError(f"{field} out of range")
    if config["seed_start"] + config["episodes"] - 1 > 2**32 - 1:
        raise ValueError("Seed range overflows uint32")
    if config["arena"] not in ("pillars", "open") or config["player_config"] != "scripted" or config["enemy_policy"] != "priority":
        raise ValueError("Unsupported arena / player / enemy policy")
    policies, counts = config["companion_policies"], config["enemy_counts"]
    if not isinstance(policies, list) or not policies or len(set(policies)) != len(policies) or any(p not in ("utility", "priority") for p in policies):
        raise ValueError("Invalid or duplicate companion policies")
    if not isinstance(counts, list) or not counts or len(set(counts)) != len(counts) or any(type(n) is not int or not 1 <= n <= 50 for n in counts):
        raise ValueError("Invalid or duplicate enemy counts")
    if type(config["director"]) is not bool:
        raise ValueError("director must be boolean")
    return config

def source_hash() -> str:
    digest = hashlib.sha256()
    for path in sorted((ROOT / "core").rglob("*")):
        if path.is_file():
            digest.update(path.relative_to(ROOT).as_posix().encode())
            digest.update(path.read_bytes())
    return digest.hexdigest()

def wilson(wins: int, n: int) -> list[float]:
    z = 1.96
    p = wins / n
    d = 1 + z*z/n
    center = (p + z*z/(2*n))/d
    radius = z*((p*(1-p)/n + z*z/(4*n*n))**0.5)/d
    return [max(0, center-radius), min(1, center+radius)]

def run(config: dict, directory: pathlib.Path, binary: pathlib.Path) -> dict:
    validate(config)
    directory.mkdir(parents=True, exist_ok=True)
    if (directory / "report.json").exists():
        raise ValueError("Output report already exists; choose a new directory to preserve evidence")
    records = []
    for count in config["enemy_counts"]:
        for policy in config["companion_policies"]:
            for seed in range(config["seed_start"], config["seed_start"] + config["episodes"]):
                command = [str(binary), "--seed", str(seed), "--enemies", str(count), "--duration", str(config["duration"]), "--policy", policy, "--arena", config["arena"], "--director", "on" if config["director"] else "off"]
                completed = subprocess.run(command, check=True, capture_output=True, text=True, timeout=60)
                record = json.loads(completed.stdout)
                if record["engine"] != "portable-cpp-model":
                    raise ValueError("Unexpected engine provenance")
                records.append(record)
    aggregate = []
    for count in config["enemy_counts"]:
        for policy in config["companion_policies"]:
            rows = [r for r in records if r["enemies"] == count and r["policy"] == policy]
            wins = sum(r["win"] for r in rows)
            aggregate.append({"enemy_count": count, "policy": policy, "episodes": len(rows), "wins": wins, "win_rate": wins/len(rows), "win_rate_95ci_wilson": wilson(wins, len(rows)),
                **{f"mean_{field}": statistics.mean(r[field] for r in rows) for field in ("survival_seconds", "damage_dealt", "damage_taken", "companion_deaths", "enemy_deaths", "stuck_events", "distance_travelled", "cover_usage", "decision_counts")},
                "mean_decision_us": statistics.mean(r["cpu"]["decision_mean_us"] for r in rows), "mean_geometry_query_us": statistics.mean(r["cpu"]["geometry_query_mean_us"] for r in rows), "mean_step_p95_us": statistics.mean(r["cpu"]["step_p95_us"] for r in rows)})
    build = json.loads((ROOT / "build/build-report.json").read_text()) if (ROOT / "build/build-report.json").exists() else {"compiler": "unrecorded"}
    report = {"schema_version": 1, "engine": "portable-cpp-model", "created_utc": dt.datetime.now(dt.timezone.utc).isoformat(), "source_sha256": source_hash(), "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(), "host": {"platform": platform.platform(), "processor": platform.processor()}, "build": build, "config": config, "episode_count": len(records), "aggregate": aggregate,
        "limitations": ["Not Unreal runtime or rendered frame measurements", "CPU microbenchmarks share a busy desktop host", "Simple axis-slide geometry can become stuck; no NavMesh", "Priority policy is not execution of Unreal Behavior Tree", "No RL or learning results"]}
    (directory / "episodes.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
    (directory / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    fields = [k for k in records[0] if k not in ("cpu", "companion_actions")]
    with (directory / "episodes.csv").open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=fields)
        writer.writeheader()
        writer.writerows({k: r[k] for k in fields} for r in records)
    return report

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenario", type=pathlib.Path, default=ROOT / "scenarios/evaluation.json")
    parser.add_argument("--output", type=pathlib.Path, default=ROOT / "outputs/benchmark")
    parser.add_argument("--binary", type=pathlib.Path, default=ROOT / ("build/aegis_sim.exe" if sys.platform == "win32" else "build/aegis_sim"))
    args = parser.parse_args()
    try:
        report = run(json.loads(args.scenario.read_text(encoding="utf-8")), args.output, args.binary)
        print(json.dumps({"output": str(args.output), "episodes": report["episode_count"], "aggregate": report["aggregate"]}, indent=2))
    except (ValueError, OSError, subprocess.SubprocessError) as exc:
        parser.exit(2, f"Benchmark failed: {exc}\n")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
