"""Summarize strictly validated native Decision Lab batches without pooling revisions.

The analysis reports paired endpoints, not causal survival or learned intelligence.
Holdout requires the exact protocol saved before each invocation.
"""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import statistics
from types import SimpleNamespace

from run_benchmark import wilson
from run_decision_lab import METRICS, POLICIES, protocol_cells, read_json, sha, validate_directory


def describe(values):
    return {"mean": statistics.mean(values), "median": statistics.median(values),
            "minimum": min(values), "maximum": max(values)}


def summarize(inputs, policies, phase, protocol_path=None):
    if len(policies) < 2 or len(set(policies)) != len(policies) or any(p not in POLICIES for p in policies):
        raise ValueError("Choose distinct comparison policies")
    if phase == "holdout" and protocol_path is None:
        raise ValueError("Holdout analysis requires its frozen protocol")
    protocol = read_json(protocol_path) if protocol_path else None
    if protocol:
        protocol_cells(protocol)
        if phase != "holdout" or set(protocol["policies"]) != set(policies):
            raise ValueError("Analysis policies/phase differ from frozen protocol")
    groups = defaultdict(lambda: defaultdict(dict))
    revisions, binary_sets, wrapper_versions, engine_versions = set(), set(), set(), set()
    runs, records = [], []
    for directory in map(Path, inputs):
        meta = read_json(directory / "provenance.json")
        if meta.get("passed") is not True or meta.get("engine") != "unreal-runtime" or meta.get("phase") != phase or meta.get("exitCode") != 0:
            raise ValueError("Cannot aggregate incomplete, failed or different-phase evidence")
        if meta.get("inputsBefore") != meta.get("inputsAfter") or meta.get("binariesBefore") != meta.get("binariesAfter") or not meta.get("binariesBefore"):
            raise ValueError("Input or compiled-module evidence is unbound")
        manifest = meta.get("files")
        if not isinstance(manifest, dict) or not manifest:
            raise ValueError("Missing evidence file manifest")
        actual = {p.relative_to(directory).as_posix() for p in directory.rglob("*")
                  if p.is_file() and p != directory / "provenance.json"}
        if actual != set(manifest):
            raise ValueError("Evidence directory file set changed after validation")
        if any(sha(directory / name) != digest for name, digest in manifest.items()):
            raise ValueError("Raw evidence changed after validation")
        args = SimpleNamespace(**meta["requested"])
        rows, _ = validate_directory(directory, args, (directory / "engine.log").read_text(encoding="utf-8-sig", errors="replace"))
        if meta.get("episodeCount") != len(rows) or args.policy not in policies:
            raise ValueError("Unexpected count or comparison policy")
        revision = tuple(meta["inputsBefore"][key] for key in ("sourceSha256", "contentSha256"))
        revisions.add(revision)
        binary_sets.add(tuple(sorted((key, value["sha256"]) for key, value in meta["binariesBefore"].items())))
        wrapper_versions.add(meta["wrapperSha256"])
        engine_versions.add(json.dumps(meta["engineBuild"], sort_keys=True))
        if protocol and (meta.get("protocolSha256") != sha(protocol_path) or
                         revision != (protocol["sourceSha256"], protocol["contentSha256"])):
            raise ValueError("A holdout batch did not run under this frozen protocol")
        runs.append({"directory": str(directory.resolve()), "provenanceSha256": sha(directory / "provenance.json"),
                     "episodeCount": len(rows), "phase": phase})
        for row in rows:
            scenario = row["scenario"]
            condition = tuple(scenario[key] for key in ("scenarioId", "layout", "enemyCount", "duration")) + (meta["timeMode"], args.rendered)
            policy, seed = scenario["policy"], scenario["seed"]
            if seed in groups[condition][policy]:
                raise ValueError("Duplicate seed inside one policy/condition")
            groups[condition][policy][seed] = row
            records.append({"condition": condition, "policy": policy, "seed": seed, "episode": row,
                            "source": str((directory / f"episode-{seed - args.seed:03d}.json").resolve())})
    if not groups or len(revisions) != 1 or len(binary_sets) != 1 or len(wrapper_versions) != 1 or len(engine_versions) != 1:
        raise ValueError("Missing data or mixed source/assets, compiled binaries, wrapper or engine versions")
    if protocol:
        wanted = {(cell["scenarioId"], cell["layout"], cell["enemyCount"], cell["duration"]): set(cell["seeds"])
                  for cell in protocol["cells"]}
        if len(groups) != len(wanted) or {key[:4] for key in groups} != set(wanted):
            raise ValueError("Holdout conditions differ from frozen cells")
        for condition, by_policy in groups.items():
            if condition[4:] != ("fixed_game_step_1_60", False):
                raise ValueError("Frozen holdout is the declared fixed-step NullRHI experiment")
            if any(set(by_policy.get(policy, {})) != wanted[condition[:4]] for policy in policies):
                raise ValueError("Holdout is incomplete or contains undeclared seeds")
    summaries, comparisons = [], []
    metric_names = [name for name in METRICS if not name.endswith("DeathSeconds")]
    metric_names += ["elapsedGameSeconds", "companionAliveAtEnd", "enemiesAlive"]
    def metric(row, key):
        if key == "companionAliveAtEnd":
            return int(row["companionAlive"])
        return row[key] if key in row else row["metrics"][key]
    for condition, by_policy in sorted(groups.items()):
        if set(by_policy) != set(policies):
            raise ValueError("A condition is missing a requested comparison arm")
        seed_sets = [set(by_policy[policy]) for policy in policies]
        if any(seeds != seed_sets[0] for seeds in seed_sets[1:]):
            raise ValueError("Comparison arms do not contain exactly paired seeds")
        seeds = sorted(seed_sets[0])
        for policy in policies:
            rows = [by_policy[policy][seed] for seed in seeds]
            wins = sum(row["win"] for row in rows)
            summaries.append({"condition": condition, "policy": policy, "seeds": seeds,
                              "episodes": len(rows), "wins": wins, "winRateWilson95": wilson(wins, len(rows)),
                              "terminationCounts": dict(Counter(row["terminationReason"] for row in rows)),
                              "companionDeathsAtEnd": sum(not row["companionAlive"] for row in rows),
                              "metrics": {name: describe([metric(row, name) for row in rows]) for name in metric_names},
                              "meanActionSeconds": {field: {action: statistics.mean(row["metrics"][field].get(action, 0) for row in rows)
                                                            for action in sorted({action for row in rows for action in row["metrics"][field]})}
                                                    for field in ("companionActionSeconds", "companionSelectedSeconds")},
                              "deathTimes": [{"seed": row["scenario"]["seed"], "player": row["metrics"]["playerDeathSeconds"],
                                              "companion": row["metrics"]["companionDeathSeconds"],
                                              "terminationReason": row["terminationReason"]} for row in rows]})
        reference = "baseline" if "baseline" in policies else policies[0]
        for policy in policies:
            if policy == reference:
                continue
            pairs = []
            for seed in seeds:
                before, after = by_policy[reference][seed], by_policy[policy][seed]
                pairs.append({"seed": seed, "referenceWin": before["win"], "candidateWin": after["win"],
                              "delta": {name: metric(after, name) - metric(before, name) for name in metric_names}})
            comparisons.append({"condition": condition, "reference": reference, "candidate": policy,
                                "pairs": pairs, "candidateOnlyWins": sum(p["candidateWin"] and not p["referenceWin"] for p in pairs),
                                "referenceOnlyWins": sum(p["referenceWin"] and not p["candidateWin"] for p in pairs),
                                "pairedDeltas": {name: describe([p["delta"][name] for p in pairs]) for name in metric_names}})
    return {"schemaVersion": 1, "engine": "unreal-runtime", "phase": phase,
            "sourceSha256": next(iter(revisions))[0], "contentSha256": next(iter(revisions))[1],
            "protocolSha256": sha(protocol_path) if protocol_path else None,
            "analyzerSha256": sha(Path(__file__)), "episodeCount": len(records),
            "summaries": summaries, "pairedComparisons": comparisons, "episodes": records, "runs": runs,
            "limits": ["Legacy BT/EQS scripted encounter, not Copilot or Uplink gameplay", "One map; alternate spawn layout is not cross-map generalization",
                       "Project seeds do not certify bit-deterministic UE trajectories", "Companion alive seconds are observed exposure ending at episode termination, not survival free of competing risks",
                       "Fixed-step throughput is not rendered gameplay FPS", "Small paired samples; descriptive endpoints do not establish general policy superiority",
                       "Historical v1.0 0/30 results remain separate and are not pooled here"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, action="append", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--policies", nargs="+", choices=POLICIES, default=list(POLICIES))
    parser.add_argument("--phase", choices=("development", "holdout", "diagnostic"), required=True)
    parser.add_argument("--protocol", type=Path)
    args = parser.parse_args()
    report = summarize(args.input, args.policies, args.phase, args.protocol)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2, ensure_ascii=False, allow_nan=False)
        stream.write("\n")
    print(f"Decision Lab analysis: {report['episodeCount']} validated native episodes; {args.output}")


if __name__ == "__main__":
    main()
