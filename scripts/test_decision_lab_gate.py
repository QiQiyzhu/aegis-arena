"""Offline adversarial tests; synthetic files never represent a native experiment."""
import copy
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_decision_lab as gate
import analyze_decision_lab as analysis


def direct(text):
    return "[2026.09.17-10.00.00:000][ 22]LogTemp: Display: " + text


class DecisionLabGateTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="aegis-decision-gate-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.output = self.root / "batch"
        self.output.mkdir()
        self.args = SimpleNamespace(policy="baseline", enemies=1, seed=2001, episodes=1,
                                    duration=2, layout=0, fixed_step=True, rendered=False, capture=False)
        self.log = direct("AEGIS_DECISION_LAB_COMPLETE episodes=1")
        self.row = {"schemaVersion": 1, "engine": "unreal-runtime", "automated": True,
                    "scenario": {"policy": "baseline", "enemyCount": 1, "seed": 2001, "duration": 2,
                                 "layout": 0, "scenarioId": gate.SCENARIO},
                    "win": False, "terminationReason": "timeout", "elapsedGameSeconds": 2,
                    "playerAlive": True, "companionAlive": True, "enemiesAlive": 1,
                    "metrics": {name: 0 for name in gate.METRICS}}
        self.row["metrics"].update(playerDeathSeconds=-1, companionDeathSeconds=-1, companionAliveSeconds=2,
                                   companionDecisions=2, companionActionSeconds={"Follow": 2},
                                   companionSelectedSeconds={"Follow": 2})
        self.events = [{"event": "episode_start", "timeSeconds": 0},
                       {"event": "decision", "timeSeconds": 0.1, "observer": "companion", "scores": [0, 1, 0, 0]},
                       {"event": "episode_end", "timeSeconds": 2, "terminationReason": "timeout", "win": False}]
        self.report = {"schemaVersion": 1, "engine": "unreal-runtime", "completed": True, "episodes": 1,
                       "policy": "baseline", "firstSeed": 2001, "enemyCount": 1, "duration": 2,
                       "layout": 0, "scenarioId": gate.SCENARIO}

    def write(self, directory=None):
        directory = directory or self.output
        (directory / "results.json").write_text(json.dumps(self.report), encoding="utf-8")
        (directory / "episode-000.json").write_text(json.dumps(self.row), encoding="utf-8")
        (directory / "decision-000.jsonl").write_text("\n".join(map(json.dumps, self.events)) + "\n", encoding="utf-8")
        (directory / "engine.log").write_text(self.log, encoding="utf-8")

    def validate(self):
        self.write()
        return gate.validate_directory(self.output, self.args, self.log)

    def reject(self):
        with self.assertRaises((ValueError, OSError)):
            self.validate()

    def test_valid_timeout_is_a_completed_experiment_not_a_win(self):
        rows, pictures = self.validate()
        self.assertEqual(len(rows), 1)
        self.assertFalse(rows[0]["win"])
        self.assertEqual(pictures, [])

    def test_exact_engine_metadata_warning_is_rendered_only_and_not_silenced(self):
        warning = "[2026.09.17-07.58.46:955][  0]" + gate.KNOWN_RENDER_WARNING
        self.log += "\n" + warning
        self.reject()  # NullRHI remains strict, even for this known warning.
        self.args.rendered = True
        self.validate()
        self.assertEqual(gate.known_engine_warnings(self.log, True), [warning])
        for extra in (warning, "LogNet: Warning: unrelated", warning.replace("MotionVectorSimulation", "OtherCVar"),
                      warning + " different suffix", warning.replace("Warning:", "Error:")):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                gate.validate_engine_messages(self.log + "\n" + extra, True)

    def test_rendered_uses_d3d11_without_changing_nullrhi_runs(self):
        self.assertIn("-d3d11", gate.rendering_arguments(True))
        self.assertIn("-RenderOffscreen", gate.rendering_arguments(True))
        self.assertEqual(gate.rendering_arguments(False), ["-NullRHI"])

    def test_valid_clear_requires_and_matches_actual_enemy_death_event(self):
        self.row.update(win=True, terminationReason="enemy_clear", enemiesAlive=0)
        self.row["metrics"]["playerDamageDealt"] = 100
        self.events[-1].update(win=True, terminationReason="enemy_clear")
        self.events[2:2] = [{"event": "damage", "timeSeconds": 1, "source": "player", "victim": "enemy", "amount": 100},
                            {"event": "death", "timeSeconds": 1, "actor": "enemy"}]
        self.assertTrue(self.validate()[0][0]["win"])

    def test_valid_player_death_preserves_alive_companion_competing_endpoint(self):
        self.row.update(playerAlive=False, terminationReason="player_dead")
        self.row["metrics"].update(playerDeathSeconds=1, playerDamageTaken=100)
        self.events[-1]["terminationReason"] = "player_dead"
        self.events[2:2] = [{"event": "damage", "timeSeconds": 1, "source": "enemy", "victim": "player", "amount": 100},
                            {"event": "death", "timeSeconds": 1, "actor": "player"}]
        self.assertTrue(self.validate()[0][0]["companionAlive"])

    def test_missing_native_marker_rejects_json_only_success(self):
        self.log = ""
        self.reject()

    def test_console_replay_is_not_a_direct_native_marker(self):
        self.log = "LogAutomationController: Display: [log] " + self.log
        self.reject()

    def test_duplicate_or_conflicting_completion_marker_is_rejected(self):
        for extra in (direct("AEGIS_DECISION_LAB_COMPLETE episodes=1"), direct("AEGIS_DECISION_LAB_COMPLETE episodes=2")):
            with self.subTest(extra=extra):
                self.log = direct("AEGIS_DECISION_LAB_COMPLETE episodes=1") + "\n" + extra
                self.reject()

    def test_warning_error_fatal_ensure_and_native_failure_reject(self):
        for text in ("LogNet: Warning: network error", "LogTemp: Error: failure", "Fatal error: crash",
                     "Ensure condition failed: x", "Handled ensure", "LogTemp: Display: AEGIS_DECISION_LAB_FAIL: x"):
            with self.subTest(text=text):
                self.log = direct("AEGIS_DECISION_LAB_COMPLETE episodes=1") + "\n[2026.09.17][22]" + text
                self.reject()

    def test_wrong_policy_seed_layout_or_native_scope_rejects(self):
        original = copy.deepcopy(self.row)
        for key, value in (("policy", "priority"), ("seed", 2002), ("layout", 1), ("scenarioId", "legacy")):
            with self.subTest(key=key):
                self.row = copy.deepcopy(original)
                self.row["scenario"][key] = value
                self.reject()
        self.row = original
        self.row["engine"] = "portable-cpp-model"
        self.reject()

    def test_numeric_booleans_and_non_automated_scope_reject(self):
        for field, value in (("automated", 1), ("automated", False), ("win", 0), ("companionAlive", 1), ("playerAlive", "true")):
            with self.subTest(field=field, value=value):
                previous = self.row[field]
                self.row[field] = value
                self.reject()
                self.row[field] = previous

    def test_false_or_mismatched_batch_result_rejects(self):
        for field, value in (("completed", False), ("completed", 1), ("episodes", 2), ("firstSeed", 9)):
            with self.subTest(field=field):
                previous = self.report[field]
                self.report[field] = value
                self.reject()
                self.report[field] = previous

    def test_missing_or_extra_episode_and_trace_files_reject(self):
        self.write()
        (self.output / "episode-001.json").write_text("{}")
        with self.assertRaises(ValueError):
            gate.validate_directory(self.output, self.args, self.log)
        (self.output / "episode-001.json").unlink()
        (self.output / "decision-000.jsonl").unlink()
        with self.assertRaises(ValueError):
            gate.validate_directory(self.output, self.args, self.log)

    def test_nested_nonfinite_numbers_and_duplicate_keys_reject(self):
        for value in ('{"a":1,"a":2}', '{"scores":[NaN]}', '{"scores":[1e999]}', '{"a":Infinity}'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                gate.strict_json(value)

    def test_impossible_win_or_short_timeout_rejects(self):
        for edits in ({"win": True}, {"enemiesAlive": 0}, {"elapsedGameSeconds": 0.5}, {"playerAlive": False}):
            with self.subTest(edits=edits):
                original = copy.deepcopy(self.row)
                self.row.update(edits)
                self.reject()
                self.row = original

    def test_lifetime_does_not_masquerade_as_fixed_window_survival(self):
        self.row["metrics"]["companionAliveSeconds"] = 60
        self.reject()

    def test_alive_role_cannot_have_death_timestamp(self):
        self.row["metrics"]["companionDeathSeconds"] = 1
        self.reject()

    def test_impossible_action_counts_or_dwell_rejects(self):
        for field, value in (("companionActionSwitches", 3), ("companionShortReversals", 1),
                             ("companionShots", 0.5), ("companionSelectedSeconds", {"Attack": 3}),
                             ("companionActionSeconds", {"Follow": -1})):
            with self.subTest(field=field):
                previous = self.row["metrics"][field]
                self.row["metrics"][field] = value
                self.reject()
                self.row["metrics"][field] = previous

    def test_missing_zero_or_negative_decisions_rejects(self):
        self.row["metrics"]["companionDecisions"] = 0
        self.reject()

    def test_damage_and_healing_totals_must_match_events(self):
        for field in ("playerDamageTaken", "companionDamageTaken", "playerDamageDealt", "companionDamageDealt", "playerHealingReceived"):
            with self.subTest(field=field):
                self.row["metrics"][field] = 10
                self.reject()
                self.row["metrics"][field] = 0

    def test_health_accounting_is_disclosed_healing_not_damage_attribution(self):
        self.events.insert(2, {"event": "heal", "timeSeconds": 1, "source": "health_accounting", "victim": "player", "amount": 5})
        self.row["metrics"]["playerHealingReceived"] = 5
        self.assertEqual(len(self.validate()[0]), 1)
        self.events[2]["event"] = "damage"
        self.row["metrics"]["playerHealingReceived"] = 0
        self.row["metrics"]["playerDamageTaken"] = 5
        self.reject()

    def test_trace_requires_enclosing_markers_and_real_decision_rows(self):
        original = copy.deepcopy(self.events)
        for events in (original[1:], original[:-1], [original[0], original[-1]], [original[0], *original]):
            with self.subTest(events=events):
                self.events = events
                self.reject()

    def test_backward_or_out_of_budget_event_timestamps_reject(self):
        self.events.insert(2, {"event": "decision", "timeSeconds": 0.05})
        self.reject()
        self.events[2]["timeSeconds"] = 10
        self.reject()

    def test_terminal_event_must_agree_with_result(self):
        self.events[-1]["terminationReason"] = "player_dead"
        self.reject()

    def test_missing_png_rejects_capture_request(self):
        self.args.capture = True
        self.reject()

    def test_png_is_structurally_inspected_without_editing(self):
        self.args.capture = self.args.rendered = True
        (self.output / "native.png").write_bytes(b"synthetic-unit-test")
        with mock.patch.object(gate, "inspect_png", return_value={"width": 1280, "height": 720}) as inspect:
            self.assertEqual(self.validate()[1][0]["width"], 1280)
            inspect.assert_called_once_with(self.output / "native.png")

    def test_native_frames_subdirectory_is_checked(self):
        self.args.capture = self.args.rendered = True
        folder = self.output / "frames"
        folder.mkdir()
        (folder / "frame-0000.png").write_bytes(b"synthetic-unit-test")
        with mock.patch.object(gate, "inspect_png", return_value={"width": 1280, "height": 720}) as inspect:
            self.assertEqual(len(self.validate()[1]), 1)
            inspect.assert_called_once_with(folder / "frame-0000.png")

    def make_batch(self, name, policy, phase="development", protocol=None):
        directory = self.root / name
        directory.mkdir()
        self.args.policy = self.report["policy"] = self.row["scenario"]["policy"] = policy
        self.write(directory)
        if protocol:
            (directory / "frozen-protocol.json").write_bytes(protocol.read_bytes())
        meta = {"passed": True, "engine": "unreal-runtime", "phase": phase, "exitCode": 0, "episodeCount": 1,
                "inputsBefore": {"sourceSha256": "a" * 64, "contentSha256": "b" * 64},
                "binariesBefore": {"projectModule": {"path": "synthetic.dll", "sha256": "c" * 64}},
                "wrapperSha256": "d" * 64, "engineBuild": {"MajorVersion": 5, "MinorVersion": 8},
                "requested": vars(self.args).copy(), "timeMode": "fixed_game_step_1_60"}
        meta.update(inputsAfter=meta["inputsBefore"], binariesAfter=meta["binariesBefore"])
        if protocol:
            meta["protocolSha256"] = gate.sha(protocol)
        meta["files"] = {p.name: gate.sha(p) for p in directory.iterdir()}
        (directory / "provenance.json").write_text(json.dumps(meta))
        return directory

    def test_analyzer_keeps_paired_endpoints_and_terminal_reasons(self):
        first, second = self.make_batch("base", "baseline"), self.make_batch("prio", "priority")
        report = analysis.summarize([first, second], ["baseline", "priority"], "development")
        self.assertEqual(report["episodeCount"], 2)
        self.assertEqual(report["summaries"][0]["terminationCounts"], {"timeout": 1})
        self.assertEqual(len(report["pairedComparisons"][0]["pairs"]), 1)

    def test_analyzer_rejects_tampered_raw_data(self):
        first, second = self.make_batch("base", "baseline"), self.make_batch("prio", "priority")
        with (first / "episode-000.json").open("a") as stream:
            stream.write(" ")
        with self.assertRaises(ValueError):
            analysis.summarize([first, second], ["baseline", "priority"], "development")

    def test_analyzer_rejects_duplicate_seed_and_missing_arm(self):
        first, second = self.make_batch("base", "baseline"), self.make_batch("prio", "priority")
        for inputs in ([first, second, first], [first]):
            with self.subTest(inputs=inputs), self.assertRaises(ValueError):
                analysis.summarize(inputs, ["baseline", "priority"], "development")

    def test_analyzer_rejects_changed_binary_or_source_revision(self):
        first, second = self.make_batch("base", "baseline"), self.make_batch("prio", "priority")
        original = json.loads((second / "provenance.json").read_text())
        for key in ("source", "binary"):
            meta = copy.deepcopy(original)
            if key == "source":
                meta["inputsBefore"]["sourceSha256"] = meta["inputsAfter"]["sourceSha256"] = "f" * 64
            else:
                meta["binariesBefore"]["projectModule"]["sha256"] = meta["binariesAfter"]["projectModule"]["sha256"] = "f" * 64
            (second / "provenance.json").write_text(json.dumps(meta))
            with self.subTest(key=key), self.assertRaises(ValueError):
                analysis.summarize([first, second], ["baseline", "priority"], "development")

    def test_holdout_requires_all_frozen_seeds_and_exact_protocol(self):
        protocol = self.root / "protocol.json"
        spec = {"schemaVersion": 1, "phase": "holdout", "policies": ["baseline", "priority"],
                "sourceSha256": "a" * 64, "contentSha256": "b" * 64,
                "cells": [{"scenarioId": gate.SCENARIO, "layout": 0, "enemyCount": 1, "duration": 2, "seeds": [2001]}]}
        protocol.write_text(json.dumps(spec))
        first, second = self.make_batch("base", "baseline", "holdout", protocol), self.make_batch("prio", "priority", "holdout", protocol)
        self.assertEqual(analysis.summarize([first, second], spec["policies"], "holdout", protocol)["episodeCount"], 2)
        spec["cells"][0]["seeds"].append(2002)
        protocol.write_text(json.dumps(spec))
        with self.assertRaises(ValueError):
            analysis.summarize([first, second], spec["policies"], "holdout", protocol)

    def test_preflight_failure_still_records_failed_provenance_without_launch(self):
        output = self.root / "failed"
        with mock.patch.object(gate, "snapshot_inputs", return_value={"sourceSha256": "a", "contentSha256": "b"}), mock.patch.object(gate.subprocess, "run") as process:
            with self.assertRaises(RuntimeError):
                gate.main(["--engine-root", str(self.root / "missing-engine"), "--output", str(output), "--policy", "baseline", "--enemies", "1", "--seed", "2001"])
            process.assert_not_called()
        meta = json.loads((output / "provenance.json").read_text())
        self.assertIs(meta["passed"], False)
        self.assertIn("failure", meta)


if __name__ == "__main__":
    unittest.main()
