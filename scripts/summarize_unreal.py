"""Aggregate native UE reports while retaining runtime, timing and policy boundaries."""
import argparse
import collections
import datetime
import json
from pathlib import Path
import statistics

from run_benchmark import wilson


def summarize(inputs):
    groups = collections.defaultdict(list)
    provenance = []
    hashes = set()
    for directory in inputs:
        meta = json.loads((directory / "provenance.json").read_text(encoding="utf-8-sig"))
        if meta["engine"] != "unreal-runtime" or meta.get("passed") is not True:
            raise ValueError("Only validated native runs can be aggregated")
        hashes.add((meta["sourceSha256"], meta["contentSha256"]))
        provenance.append(meta)
        rows = [json.loads(path.read_text(encoding="utf-8-sig")) for path in sorted((directory / "raw").glob("episode-*.json"))]
        if len(rows) != meta["episodeCount"]:
            raise ValueError("Missing or unexpected raw episodes")
        for row in rows:
            scenario = row["scenario"]
            if row["engine"] != "unreal-runtime":
                raise ValueError("Mixed portable/UE records")
            key = (scenario["companionPolicy"], scenario["enemyCount"], scenario["performanceMode"],
                   row["renderingEnabled"], meta["timeMode"], scenario["directorEnabled"], scenario["duration"])
            groups[key].append(row)
    if len(hashes) != 1:
        raise ValueError("Source/content changed between runs; do not silently pool revisions")
    output = []
    for key, rows in sorted(groups.items()):
        policy, enemies, performance, rendered, timing, director, duration = key
        seeds = sorted(row["scenario"]["seed"] for row in rows)
        if len(set(seeds)) != len(seeds):
            raise ValueError("Duplicate seeds within the same experimental condition")
        decisions = sum(row["decisionCounts"] for row in rows)
        queries = sum(row["profiledQueries"] for row in rows)
        wins = sum(row["win"] for row in rows)
        item = {"policy": policy, "enemies": enemies, "totalInitialAI": enemies + 2,
                "performanceMode": performance, "renderingEnabled": rendered, "timeMode": timing,
                "directorEnabled": director, "durationLimitGameSeconds": duration,
                "episodes": len(rows), "seeds": seeds, "wins": None if performance else wins,
                "winRateWilson95": None if performance else wilson(wins, len(rows)),
                "companionDeaths": sum(row["companionDeaths"] for row in rows),
                "decisionCalls": decisions, "profiledQueries": queries,
                "completedQueries": sum(row["completedQueries"] for row in rows),
                "failedQueries": sum(row["failedQueries"] for row in rows),
                "decisionMicrosecondsPerCall": 1000 * sum(row["decisionCpuMilliseconds"] for row in rows) / decisions if decisions else None,
                "eqsMicrosecondsPerProfiledQuery": 1000 * sum(row["eqsCpuMilliseconds"] for row in rows) / queries if queries else None}
        for field in ("survivalSeconds", "damageDealt", "damageTaken", "enemyDeaths", "stuckEvents", "distanceTravelled", "coverUsage"):
            item["mean_" + field] = statistics.mean(row[field] for row in rows)
        item["meanEpisodeFrameMilliseconds"] = statistics.mean(row["frameMeanMilliseconds"] for row in rows) if performance else None
        item["meanEpisodeFrameP95Milliseconds"] = statistics.mean(row["frameP95Milliseconds"] for row in rows) if performance else None
        item["meanEpisodeGameThreadMilliseconds"] = statistics.mean(row["gameThreadMeanMilliseconds"] for row in rows) if performance else None
        item["peakProcessResidentMiB"] = max(row["peakResidentMiB"] for row in rows) if performance else None
        output.append(item)
    # Pairing is reported as an explicit condition, not assumed from equal episode counts.
    pairing = []
    for item in output:
        if item["policy"] == "priority" and not item["performanceMode"]:
            peers = [peer for peer in output if peer["policy"] == "utility" and
                     all(peer[field] == item[field] for field in ("enemies", "performanceMode", "renderingEnabled", "timeMode", "directorEnabled", "durationLimitGameSeconds"))]
            if peers:
                if peers[0]["seeds"] != item["seeds"]:
                    raise ValueError("Priority/utility conditions have different seed lists")
                pairing.append({"enemies": item["enemies"], "pairedSeeds": len(item["seeds"])})
    return {"engine": "unreal-runtime", "createdAtUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "sourceSha256": next(iter(hashes))[0], "contentSha256": next(iter(hashes))[1],
            "episodeCount": sum(len(rows) for rows in groups.values()), "aggregate": output,
            "pairedConditions": pairing, "runs": provenance,
            "limitations": ["One arena and one shared desktop host; not generalization evidence",
                            "Fixed-step policy throughput is not real-time frame performance",
                            "Frame P95 aggregation is mean of episode P95 values, not pooled frame P95",
                            "Performance mode disables damage and excludes only the first game second",
                            "Engine scheduling is not certified bit deterministic; seeds control project randomness",
                            "No learning, RL training or online-service result"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = summarize(args.input)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as target:
        json.dump(report, target, indent=2)
        target.write("\n")
    print(f"Native aggregate: {report['episodeCount']} episodes; {args.output}")


if __name__ == "__main__":
    main()
