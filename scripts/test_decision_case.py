"""Reanalysis regression tests; these never invoke Unreal or fabricate episodes."""
import copy
import json
import math
from pathlib import Path
import shutil
import tempfile
import unittest

from export_decision_case import BASE, build_case, canonical, preference_score, read_json, validate_rows


class DecisionCaseTests(unittest.TestCase):
    def test_actual_frozen_rows_and_counterexamples(self):
        case = build_case()
        self.assertEqual(len(case["pairs"]), 30)
        self.assertEqual(case["summary"]["priority"]["companionDeathsAtEnd"], 25)
        self.assertEqual(case["summary"]["utility"]["companionDeathsAtEnd"], 6)
        self.assertEqual([case["summary"][p]["wins"] for p in ("priority", "utility")], [0, 0])
        pairs = {pair["seed"]: pair for pair in case["pairs"]}
        self.assertEqual(pairs[1027]["priority"]["companionAliveAtEnd"], 1)
        self.assertEqual(pairs[1027]["utility"]["companionAliveAtEnd"], 0)
        self.assertEqual(pairs[1024]["deltaUtilityMinusPriority"]["alliedDamage"], -128)
        self.assertEqual(pairs[1021]["priority"]["episodeSeconds"], 60)
        self.assertAlmostEqual(pairs[1003]["deltaUtilityMinusPriority"]["playerDamageTaken"], 127.5, places=3)
        self.assertEqual(sum(case["endpointComparisonCounts"].values()), 30)
        for path in case["acceptanceLesson"]["evidencePaths"]:
            self.assertTrue((BASE.parents[2] / path).is_file(), path)

    def test_preference_flip_is_only_endpoint_reweighting(self):
        case = build_case()
        self.assertEqual(case["presets"][0]["ranking"][0], "utility")
        self.assertEqual(case["presets"][1]["ranking"][0], "priority")
        for preset in case["presets"]:
            for policy in ("priority", "utility"):
                mean_pair_score = sum(preference_score(pair[policy], preset["weights"])
                                      for pair in case["pairs"]) / 30
                self.assertAlmostEqual(mean_pair_score, preset["scores"][policy], places=12)
        before = canonical(case["pairs"])
        preference_score(case["pairs"][0]["utility"], [99, 1, 0, 0])
        self.assertEqual(before, canonical(case["pairs"]))

    def test_invalid_weights_rejected(self):
        values = {"companionAliveAtEnd": 1, "alliedDamage": 120, "playerDamageTaken": 110, "episodeSeconds": 20}
        for weights in ([0, 0, 0, 0], [1, -1, 0, 0], [math.nan, 1, 0, 0], [math.inf, 0, 0, 0], [1e308, 1e308, 0, 0], [True, 1, 0, 0], [1, 0, 0]):
            with self.subTest(weights=weights), self.assertRaises(ValueError):
                preference_score(values, weights)
        self.assertAlmostEqual(preference_score(values, [0, 1e308, 0, 0]), 1.2)

    def test_missing_duplicate_mixed_or_nonfinite_rows_rejected(self):
        rows = [read_json(path) for path in sorted((BASE / "priority/raw").glob("episode-*.json"))]
        invalid = [rows[:-1], rows[:-1] + [rows[0]]]
        for field, value in (("engine", "portable-cpp-model"), ("renderingEnabled", True),
                             ("damageDealt", math.nan), ("win", 1), ("companionDeaths", True)):
            changed = copy.deepcopy(rows)
            changed[0][field] = value
            invalid.append(changed)
        changed = copy.deepcopy(rows)
        changed[0]["scenario"]["directorEnabled"] = True
        invalid.append(changed)
        for index, changed in enumerate(invalid):
            with self.subTest(case=index), self.assertRaises(ValueError):
                validate_rows(changed, "priority")

    def test_canonical_json_ignores_checkout_line_endings_and_key_order(self):
        self.assertEqual(canonical(json.loads('{"b":2,\r\n"a":1}')),
                         canonical(json.loads('{"a":1,\n"b":2}')))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "invalid.json"
            path.write_text('{"metric": NaN}', encoding="utf-8")
            with self.assertRaises(ValueError):
                read_json(path)

    def test_committed_export_is_reproducible(self):
        export = BASE.parent / "decision-case.json"
        self.assertEqual(canonical(read_json(export)), canonical(build_case()))

    def test_failed_provenance_mixed_revision_and_changed_aggregate_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory) / "evaluation"
            shutil.copytree(BASE, base)
            meta_path = base / "utility/provenance.json"
            original_meta = read_json(meta_path)
            for field, value in (("passed", False), ("sourceSha256", "different-revision")):
                changed = dict(original_meta, **{field: value})
                meta_path.write_bytes(canonical(changed))
                with self.subTest(field=field), self.assertRaises(ValueError):
                    build_case(base)
            meta_path.write_bytes(canonical(original_meta))
            aggregate_path = base / "report.json"
            aggregate = read_json(aggregate_path)
            aggregate["aggregate"][0]["mean_damageDealt"] += 1
            aggregate_path.write_bytes(canonical(aggregate))
            with self.assertRaises(ValueError):
                build_case(base)
