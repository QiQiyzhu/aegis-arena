"""Execute the predeclared packaged holdout once, retaining every failed run."""
import argparse
import datetime
import json
from pathlib import Path
import subprocess
import sys

from run_decision_lab import binary_snapshot, protocol_cells, read_json, sha, snapshot_inputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-root", required=True, type=Path)
    parser.add_argument("--packaged-exe", required=True, type=Path)
    parser.add_argument("--protocol", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--cache-root", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    protocol = read_json(args.protocol)
    cells = protocol_cells(protocol)
    expected = {(cell["layout"], seed, policy) for cell in cells
                for seed in cell["seeds"] for policy in protocol["policies"]}
    order = protocol["executionOrder"]
    actual = [(item["layout"], item["seed"], item["policy"]) for item in order]
    if len(actual) != len(expected) or set(actual) != expected:
        raise ValueError("Frozen order must include each declared comparison exactly once")
    if snapshot_inputs() != {k: protocol[k] for k in ("sourceSha256", "contentSha256")}:
        raise ValueError("Source or assets differ from the frozen protocol")
    binaries = binary_snapshot(args.packaged_exe, True)
    if {k: v["sha256"] for k, v in binaries.items()} != {k: v["sha256"] for k, v in protocol["binaries"].items()}:
        raise ValueError("Package executable differs from the frozen binary")
    for name, field in (("run_decision_lab.py", "wrapperSha256"), ("analyze_decision_lab.py", "analyzerSha256")):
        if sha(root / "scripts" / name) != protocol[field]:
            raise ValueError("Evaluation code changed after freezing")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    package = args.packaged_exe.resolve().parent
    cooked = {str(p.relative_to(package)): sha(p) for p in sorted(package.rglob("*"))
              if p.is_file() and p.suffix.lower() in (".pak", ".ucas", ".utoc")}
    if not cooked:
        raise ValueError("Expected a packaged cooked content container")
    manifest = {"startedAtUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                "protocolSha256": sha(args.protocol), "orchestratorSha256": sha(Path(__file__)),
                "binaries": binaries, "cookedContainers": cooked, "completed": False, "runs": []}

    def save():
        (output / "execution.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    save()
    try:
        for index, item in enumerate(order):
            cell = next(c for c in cells if c["layout"] == item["layout"])
            directory = output / f"L{item['layout']}-{item['seed']}-{item['policy']}"
            command = [sys.executable, str(root / "scripts/run_decision_lab.py"),
                       "--engine-root", str(args.engine_root), "--packaged-exe", str(args.packaged_exe),
                       "--cache-root", str(args.cache_root), "--protocol", str(args.protocol),
                       "--output", str(directory), "--phase", "holdout", "--policy", item["policy"],
                       "--enemies", str(cell["enemyCount"]), "--duration", str(cell["duration"]),
                       "--seed", str(item["seed"]), "--layout", str(item["layout"]), "--episodes", "1"]
            run = {"index": index, **item, "output": str(directory), "command": command}
            manifest["runs"].append(run)
            save()
            result = subprocess.run(command, cwd=root, check=False)
            run["exitCode"] = result.returncode
            save()
            if result.returncode:
                raise RuntimeError("Holdout stopped at a retained infrastructure failure; no replacement")
        if any(sha(package / name) != digest for name, digest in cooked.items()):
            raise ValueError("Cooked package changed during holdout")
        manifest["completed"] = True
    except Exception as error:
        manifest["failure"] = str(error)
        raise
    finally:
        manifest["finishedAtUtc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        save()


if __name__ == "__main__":
    main()
