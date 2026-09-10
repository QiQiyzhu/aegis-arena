"""Reweight existing native episode endpoints; never compile or execute Unreal.

Output is deterministic JSON. Hashes use canonical parsed JSON so Git's Windows
line-ending conversion does not change the evidence identity.
"""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "evidence/unreal/evaluation"
EVIDENCE_REF = "v1.0.0-native"
POLICIES = ("priority", "utility")
SEEDS = tuple(range(1001, 1031))
METRICS = (
    {"id": "companionAliveAtEnd", "label": "结束时同伴仍存活", "direction": 1, "scale": 1,
     "unit": "0 / 1", "definition": "1 - companionDeaths；只是在本轮终止时的状态，非存活时间。"},
    {"id": "alliedDamage", "label": "累计己方输出", "direction": 1, "scale": 100,
     "unit": "damage", "definition": "脚本玩家与同伴造成的实际累计伤害；暴露时长不同。"},
    {"id": "playerDamageTaken", "label": "累计玩家承伤", "direction": -1, "scale": 100,
     "unit": "damage", "definition": "治疗后可再次受伤，故可能超过初始生命；较高值不等于更早死亡。"},
    {"id": "episodeSeconds", "label": "本轮持续时间", "direction": 1, "scale": 60,
     "unit": "game seconds", "definition": "玩家死亡、胜利或时限即结束；不是同伴生存时间。"},
)
PRESETS = (
    {"id": "endpoint", "label": "优先结束时同伴存活", "weights": [0.65, 0.15, 0.15, 0.05]},
    {"id": "pressure", "label": "优先输出与坚持", "weights": [0.10, 0.45, 0.25, 0.20]},
    {"id": "exposure", "label": "优先降低累计玩家承伤", "weights": [0.10, 0.15, 0.65, 0.10]},
)


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"),
                      allow_nan=False).encode("utf-8")


def read_json(path):
    def reject_constant(value):
        raise ValueError(f"Nonfinite {value}")
    return json.loads(path.read_text(encoding="utf-8-sig"), parse_constant=reject_constant)


def endpoint(row):
    return {"companionAliveAtEnd": 1 - row["companionDeaths"],
            "alliedDamage": row["damageDealt"], "playerDamageTaken": row["damageTaken"],
            "episodeSeconds": row["survivalSeconds"]}


def preference_score(values, weights):
    """Descriptive endpoint index; not reward optimization or a new experiment."""
    if len(weights) != len(METRICS) or any(type(w) not in (int, float) or
            not math.isfinite(w) or w < 0 for w in weights) or not math.isfinite(sum(weights)) or sum(weights) <= 0:
        raise ValueError("Four finite nonnegative weights with positive total required")
    total = sum(weights)
    return sum((w / total) * m["direction"] * values[m["id"]] / m["scale"]
               for m, w in zip(METRICS, weights))


def sign(value, epsilon=1e-4):
    return 1 if value > epsilon else -1 if value < -epsilon else 0


def validate_rows(rows, policy):
    if len(rows) != 30:
        raise ValueError("This case requires all 30 native episodes for each policy")
    by_seed = {}
    expected = dict(arena="AegisArena", playerConfig="scripted", companionPolicy=policy,
                    enemyPolicy="behavior_tree", enemyCount=4, duration=60,
                    directorEnabled=False, performanceMode=False)
    for row in rows:
        scenario = row.get("scenario", {})
        seed = scenario.get("seed")
        if type(seed) is not int or seed not in SEEDS or seed in by_seed:
            raise ValueError("Duplicate, invalid or unpaired seed")
        if scenario != dict(expected, seed=seed):
            raise ValueError("Scenario differs from the frozen native protocol")
        if row.get("engine") != "unreal-runtime" or row.get("renderingEnabled") is not False:
            raise ValueError("Only non-rendered native policy evidence is allowed")
        if type(row.get("win")) is not bool or type(row.get("companionDeaths")) is not int or row["companionDeaths"] not in (0, 1):
            raise ValueError("Invalid outcome types")
        for key, value in row.items():
            if key in ("scenario", "engine", "win", "renderingEnabled"):
                continue
            if type(value) not in (float, int) or not math.isfinite(value):
                raise ValueError(f"Invalid finite metric {key}")
        for key in ("damageDealt", "damageTaken", "survivalSeconds", "completedQueries", "failedQueries"):
            if row[key] < 0:
                raise ValueError(f"Negative {key}")
        if not 0 < row["survivalSeconds"] <= 60 or row["failedQueries"] > row["completedQueries"]:
            raise ValueError("Invalid duration/query bounds")
        by_seed[seed] = row
    return by_seed


def build_case(base=BASE):
    records, manifest, hashes = {}, [], set()
    combined_digest = hashlib.sha256()
    for policy in POLICIES:
        meta = read_json(base / policy / "provenance.json")
        if meta.get("engine") != "unreal-runtime" or meta.get("passed") is not True or meta.get("episodeCount") != 30:
            raise ValueError("Native provenance is incomplete or failed")
        if meta.get("exitCode") != 0 or meta.get("queryDiagnosticsEnabled") is not False or not math.isclose(meta.get("configuredGameStepSeconds", 0), 1 / 60):
            raise ValueError("Wrong native timing/diagnostics protocol")
        hashes.add((meta["sourceSha256"], meta["contentSha256"]))
        files = sorted((base / policy / "raw").glob("episode-*.json"))
        records[policy] = validate_rows([read_json(path) for path in files], policy)
        if any(path.name != f"episode-{read_json(path)['scenario']['seed'] - 1001:03d}.json" for path in files):
            raise ValueError("Episode filename does not match its seed; source links would be wrong")
        for path in [base / policy / "provenance.json", *files]:
            relative = path.relative_to(base).as_posix()
            data = canonical(read_json(path))
            combined_digest.update(relative.encode("utf-8") + b"\0" + data + b"\0")
            manifest.append({"path": "evidence/unreal/evaluation/" + relative,
                             "canonicalJsonSha256": hashlib.sha256(data).hexdigest()})
    if len(hashes) != 1:
        raise ValueError("Do not pool native revisions")
    summary = {}
    for policy in POLICIES:
        rows = list(records[policy].values())
        summary[policy] = {
            "episodes": 30, "wins": sum(row["win"] for row in rows),
            "companionDeathsAtEnd": sum(row["companionDeaths"] for row in rows),
            "reachedDurationLimit": sum(row["survivalSeconds"] >= 60 for row in rows),
            "completedQueries": sum(row["completedQueries"] for row in rows),
            "failedQueries": sum(row["failedQueries"] for row in rows),
            "mean": {m["id"]: statistics.mean(endpoint(row)[m["id"]] for row in rows) for m in METRICS},
        }
    # The committed aggregate is an independent consistency check, not our input summary.
    aggregate = read_json(base / "report.json")
    source_hash, content_hash = next(iter(hashes))
    if aggregate["sourceSha256"] != source_hash or aggregate["contentSha256"] != content_hash or aggregate["episodeCount"] != 60:
        raise ValueError("Aggregate revision/count disagrees with raw evidence")
    for policy in POLICIES:
        matches = [item for item in aggregate["aggregate"] if item["policy"] == policy]
        if len(matches) != 1 or matches[0]["seeds"] != list(SEEDS):
            raise ValueError("Aggregate policy/seed mismatch")
        prior, current = matches[0], summary[policy]
        for key, expected_value in (("wins", current["wins"]), ("companionDeaths", current["companionDeathsAtEnd"]),
                ("completedQueries", current["completedQueries"]), ("failedQueries", current["failedQueries"]),
                ("mean_damageDealt", current["mean"]["alliedDamage"]),
                ("mean_damageTaken", current["mean"]["playerDamageTaken"]),
                ("mean_survivalSeconds", current["mean"]["episodeSeconds"])):
            if not math.isclose(prior[key], expected_value, rel_tol=1e-10, abs_tol=1e-8):
                raise ValueError(f"Aggregate mismatch: {policy}/{key}")
    pairs = []
    for seed in SEEDS:
        pair = {"seed": seed}
        for policy in POLICIES:
            row = records[policy][seed]
            pair[policy] = dict(endpoint(row), win=row["win"], companionDeaths=row["companionDeaths"],
                                completedQueries=row["completedQueries"], failedQueries=row["failedQueries"],
                                source=f"evidence/unreal/evaluation/{policy}/raw/episode-{seed - 1001:03d}.json")
        pair["deltaUtilityMinusPriority"] = {m["id"]: pair["utility"][m["id"]] - pair["priority"][m["id"]] for m in METRICS}
        signs = [sign(pair["deltaUtilityMinusPriority"][m["id"]] * m["direction"]) for m in METRICS]
        pair["endpointComparison"] = ("equal" if not any(signs) else
            "utility_dominates" if min(signs) >= 0 else "priority_dominates" if max(signs) <= 0 else "tradeoff")
        pairs.append(pair)
    deltas = {}
    for metric in METRICS:
        key = metric["id"]
        values = [pair["deltaUtilityMinusPriority"][key] for pair in pairs]
        signs = [sign(value * metric["direction"]) for value in values]
        deltas[key] = {"mean": statistics.mean(values), "median": statistics.median(values),
                       "minimum": min(values), "maximum": max(values), "utilityHigherPreference": signs.count(1),
                       "priorityHigherPreference": signs.count(-1), "equalWithinTolerance": signs.count(0)}
    presets = []
    for preset in PRESETS:
        entry = copy.deepcopy(preset)
        entry["scores"] = {policy: preference_score(summary[policy]["mean"], entry["weights"]) for policy in POLICIES}
        delta = entry["scores"]["utility"] - entry["scores"]["priority"]
        entry["deltaUtilityMinusPriority"] = delta
        entry["ranking"] = list(reversed(POLICIES)) if delta > 1e-6 else list(POLICIES) if delta < -1e-6 else []
        presets.append(entry)
    return {
        "schemaVersion": 1, "project": "aegis-arena", "title": "同伴活着，队伍却更好吗？",
        "subtitle": "30 对原生实验的策略取舍审查：先审查指标，再选择偏好。",
        "evidenceKind": "reanalysis-of-existing-native-endpoints",
        "notANewExperiment": True, "pairCount": 30, "episodeCount": 60,
        "provenance": {"nativeEvidenceRef": EVIDENCE_REF, "sourceSha256": source_hash,
            "contentSha256": content_hash, "inputSetCanonicalSha256": combined_digest.hexdigest(),
            "hashMethod": "SHA256 of sorted-within-policy relative path + NUL + canonical parsed JSON + NUL; priority then utility",
            "sourceBaseUrl": f"https://github.com/QiQiyzhu/aegis-arena/blob/{EVIDENCE_REF}/",
            "inputs": manifest},
        "protocol": {"engine": "UE 5.8.2", "seeds": list(SEEDS), "arenaCount": 1,
            "enemies": 4, "directorEnabled": False, "player": "scripted", "maximumGameSeconds": 60,
            "rendering": "NullRHI", "fixedGameStepSeconds": 1 / 60,
            "pairingMeaning": "同项目seed与配置；不保证相同导航/感知时序或反事实轨迹。"},
        "metrics": copy.deepcopy(METRICS), "summary": summary, "pairedDeltas": deltas,
        "endpointComparisonCounts": {label: sum(pair["endpointComparison"] == label for pair in pairs)
            for label in ("equal", "utility_dominates", "priority_dominates", "tradeoff")},
        "pairs": pairs, "presets": presets,
        "scoring": {"formula": "sum(weight[i] * direction[i] * endpoint[i] / scale[i]) / sum(weight)",
            "metricOrder": [m["id"] for m in METRICS], "weightDomain": "finite, nonnegative; sum > 0",
            "scoreTieTolerance": 1e-6, "pairComparisonToleranceInMetricUnits": 1e-4,
            "interpretation": "无量纲偏好指数，允许负值；不是胜率、AI策略重训练或新实验。量纲参考1/100/100/60预先在公式中公开，非数据min-max。",
            "warning": "均值排序只重加权已观测终点；改变滑块不会改变游戏行为。即使端点支配也不证明因果或外推。"},
        "counterexamples": [
            {"seed": 1027, "title": "反例：Utility 同伴反而死亡", "detail": "Priority 同伴在11.5秒结束时仍活着；Utility 在17.8秒结束时已死，同时输出高28。观察窗口不同，不能据此声称长时生存更差。"},
            {"seed": 1024, "title": "保住同伴的输出代价", "detail": "同伴端点由死亡变存活，但输出少128、玩家累计承伤多22，本轮早7秒结束。"},
            {"seed": 1003, "title": "更高承伤也可能伴随更长战斗", "detail": "Utility 结束时同伴活着，战斗长17.6秒，累计玩家承伤多127.5；治疗/暴露时长使累计伤害不能直接等同保护能力。"},
            {"seed": 1021, "title": "时限与死亡不可混为一谈", "detail": "Priority 达60秒时限，Utility 为40.8秒；两边同伴均死。raw未单独记录终止原因，不把时限行当作精确死亡时间。"},
        ],
        "limitations": [
            "同伴死亡是结束时状态，没有同伴死亡时刻、逐时风险集或匹配观察窗口；不能计算生存曲线或每存活秒死亡率。",
            "Utility 本轮平均更短；25对6的同伴死亡数不能单独证明保护能力提高。",
            "累计玩家承伤受治疗和暴露时长影响；累计己方输出也受终止时刻影响。",
            "双方0/30胜，有地板效应；不宣称总体更优、不进行外推或配对显著性宣传。",
            "这是一次政策各一轮/seed的单地图结果；UE调度并非位级确定，不能当作同一轨迹的两个反事实。",
        ],
        "acceptanceLesson": {"headline": "Automation JSON 成功不等于测试在真实世界执行",
            "falsePositive": "错误地图没有测试Actor，JSON仍Success；显式加载fixture又揭示Editor World问题。",
            "currentGate": "1个干净结果 + PIE WorldType=3/BegunPlay=1 marker + 12条具名断言 + 无handled ensure。",
            "evidencePaths": ["evidence/unreal/before-eqs-fix/functional-false-positive/index.json",
                "evidence/unreal/before-eqs-fix/functional-false-positive/engine.log",
                "evidence/unreal/functional/index.json", "evidence/unreal/functional/engine.log"],
            "codePath": "scripts/run_unreal_functional.py"},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true", help="Fail if committed export differs")
    args = parser.parse_args()
    result = build_case()
    data = json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False) + "\n"
    if args.check:
        if args.output.read_text(encoding="utf-8-sig") != data:
            raise SystemExit("Decision-case export is stale")
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(data, encoding="utf-8", newline="\n")
    print(f"Verified 30 pairs / 60 existing native rows; {'checked' if args.check else 'wrote'} {args.output}")


if __name__ == "__main__":
    main()
