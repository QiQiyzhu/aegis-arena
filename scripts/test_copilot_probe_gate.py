"""Adversarial offline checks for the Copilot probe evidence gate."""
import copy
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_unreal_copilot_probe as gate


def direct(marker):
    return "[2026.09.11-09.00.00:000][ 12]LogTemp: Display: " + marker


class CopilotProbeGateTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="aegis-copilot-gate-")
        self.addCleanup(self.directory.cleanup)
        self.output = Path(self.directory.name).resolve()
        self.report = {
            "passed": True, "engine": "unreal-runtime", "worldType": 1,
            "begunPlay": True, "syntheticSlateInput": True, "humanUsabilityTest": False,
            "fixtureService": False, "wallSeconds": 14.5,
            "provider": "Cloud / synthetic-test-model", "plan": "capture_relay > guard",
            "tracePath": str(self.output / "AegisPlans" / ("a" * 32 + ".jsonl")),
            "assertions": [{"name": name, "passed": True} for name in gate.ASSERTIONS],
            "screenshots": [str(self.output / name) for name in gate.SCREENSHOTS],
        }
        self.log = self.make_log(False)
        self.png = mock.patch.object(gate, "inspect_png", side_effect=lambda path: {
            "path": str(path), "width": 1280, "height": 720, "sha256": "synthetic-unit-fixture"
        }).start()
        self.addCleanup(mock.patch.stopall)

    @staticmethod
    def make_log(fixture):
        markers = [f"AEGIS_COPILOT_PROBE_BEGIN synthetic_slate=1 fixture_service={int(fixture)}"]
        markers += ["AEGIS_COPILOT_ASSERT_PASS " + name for name in gate.ASSERTIONS]
        markers += ["AEGIS_COPILOT_PROBE_PASS checks=13"]
        return "\n".join(map(direct, markers))

    def reject(self, report=None, log=None, fixture=False):
        self.png.reset_mock()
        with self.assertRaises(ValueError):
            gate.validate(self.report if report is None else report,
                          self.log if log is None else log, self.output, fixture)
        self.png.assert_not_called()

    def test_valid_actual_scope_inspects_all_four_expected_pngs(self):
        results = gate.validate(self.report, self.log, self.output, False)
        self.assertEqual(len(results), 4)
        self.assertEqual([call.args[0] for call in self.png.call_args_list],
                         [self.output / name for name in gate.SCREENSHOTS])

    def test_valid_disclosed_transport_fixture_scope(self):
        self.report["fixtureService"] = True
        self.report["provider"] = "Local / disclosed-transport-fixture"
        self.assertEqual(len(gate.validate(self.report, self.make_log(True), self.output, True)), 4)

    def test_fixture_cannot_be_counted_as_actual_inference(self):
        self.report["fixtureService"] = True
        self.reject(log=self.make_log(True), fixture=False)

    def test_actual_report_cannot_be_counted_as_transport_fixture(self):
        self.reject(fixture=True)

    def test_native_scope_marker_must_match_requested_scope(self):
        self.reject(log=self.make_log(True))

    def test_false_pass_wrong_world_or_human_scope_is_rejected(self):
        for field, value in (("passed", False), ("engine", "portable-simulation"),
                             ("worldType", 3), ("begunPlay", False),
                             ("syntheticSlateInput", False), ("humanUsabilityTest", True)):
            with self.subTest(field=field):
                report = copy.deepcopy(self.report)
                report[field] = value
                self.reject(report=report)

    def test_numeric_values_cannot_impersonate_boolean_scope_flags(self):
        for field, value in (("passed", 1), ("begunPlay", 1), ("syntheticSlateInput", 1),
                             ("humanUsabilityTest", 0), ("fixtureService", 0), ("worldType", True)):
            with self.subTest(field=field):
                report = copy.deepcopy(self.report)
                report[field] = value
                self.reject(report=report)

    def test_missing_report_scope_field_is_rejected(self):
        for field in ("passed", "engine", "worldType", "begunPlay", "syntheticSlateInput",
                      "humanUsabilityTest", "fixtureService"):
            with self.subTest(field=field):
                report = copy.deepcopy(self.report)
                del report[field]
                self.reject(report=report)

    def test_missing_unknown_or_duplicate_report_assertion_is_rejected(self):
        for variant in ("missing", "unknown", "duplicate"):
            with self.subTest(variant=variant):
                report = copy.deepcopy(self.report)
                if variant == "missing":
                    report["assertions"].pop()
                elif variant == "unknown":
                    report["assertions"][-1]["name"] = "made_up_pass"
                else:
                    report["assertions"][-1] = copy.deepcopy(report["assertions"][0])
                self.reject(report=report)

    def test_false_or_truthy_nonboolean_assertion_is_rejected(self):
        for value in (False, 1, "true", None):
            with self.subTest(value=value):
                report = copy.deepcopy(self.report)
                report["assertions"][0]["passed"] = value
                self.reject(report=report)

    def test_missing_native_assertion_is_rejected_despite_report_pass(self):
        self.reject(log=self.log.replace(direct("AEGIS_COPILOT_ASSERT_PASS " + gate.ASSERTIONS[0]), ""))

    def test_duplicate_native_assertion_is_rejected(self):
        self.reject(log=self.log + "\n" + direct("AEGIS_COPILOT_ASSERT_PASS " + gate.ASSERTIONS[0]))

    def test_missing_or_duplicate_begin_and_final_markers_are_rejected(self):
        for marker in ("AEGIS_COPILOT_PROBE_BEGIN synthetic_slate=1 fixture_service=0",
                       "AEGIS_COPILOT_PROBE_PASS checks=13"):
            for variant in ("missing", "duplicate"):
                with self.subTest(marker=marker, variant=variant):
                    log = (self.log.replace(direct(marker), "") if variant == "missing" else
                           self.log + "\n" + direct(marker))
                    self.reject(log=log)

    def test_replayed_console_markers_cannot_replace_direct_evidence(self):
        replay = "\n".join("LogAutomationController: Display: [log] " + line
                           for line in self.log.splitlines())
        self.reject(log=replay)

    def test_embedded_or_quoted_markers_cannot_replace_direct_evidence(self):
        for prefix, suffix in (("echo ", ""), ('"', '"'), ("LogTemp: Display: ", "")):
            with self.subTest(prefix=prefix):
                self.reject(log="\n".join(prefix + line + suffix for line in self.log.splitlines()))

    def test_wrong_final_assertion_count_is_rejected(self):
        self.reject(log=self.log.replace("PROBE_PASS checks=13", "PROBE_PASS checks=12"))

    def test_conflicting_extra_begin_or_final_cannot_hide_beside_valid_markers(self):
        for extra in ("AEGIS_COPILOT_PROBE_BEGIN synthetic_slate=1 fixture_service=1",
                      "AEGIS_COPILOT_PROBE_PASS checks=12"):
            with self.subTest(extra=extra):
                self.reject(log=self.log + "\n" + direct(extra))

    def test_native_failure_or_engine_ensure_overrides_all_passes(self):
        for failure in ("AEGIS_COPILOT_ASSERT_FAIL example", "AEGIS_COPILOT_PROBE_FAIL",
                        "Fatal error: example", "Assertion failed: example",
                        "Ensure condition failed: example", "Handled ensure: example"):
            with self.subTest(failure=failure):
                self.reject(log=self.log + "\nLogOutputDevice: Error: " + failure)

    def test_duration_rejects_nonfinite_nonpositive_and_excessive_values(self):
        for value in (float("nan"), float("inf"), float("-inf"), -1, 0, 70.01, "14.5", None, True):
            with self.subTest(value=value):
                report = copy.deepcopy(self.report)
                report["wallSeconds"] = value
                self.reject(report=report)

    def test_screenshot_set_rejects_missing_duplicates_and_outside_files(self):
        for variant in ("missing", "duplicate", "outside", "wrong_name", "extra"):
            with self.subTest(variant=variant):
                report = copy.deepcopy(self.report)
                if variant == "missing": report["screenshots"].pop()
                elif variant == "duplicate": report["screenshots"][-1] = report["screenshots"][0]
                elif variant == "outside": report["screenshots"][0] = str(self.output.parent / gate.SCREENSHOTS[0])
                elif variant == "wrong_name": report["screenshots"][0] = str(self.output / "invented.png")
                else: report["screenshots"].append(str(self.output / "extra.png"))
                self.reject(report=report)

    def test_screenshot_paths_must_be_absolute_even_when_relative_paths_resolve(self):
        self.report["screenshots"] = [os.path.relpath(path, Path.cwd()) for path in self.report["screenshots"]]
        self.reject()

    def test_png_decode_failure_cannot_be_hidden_by_native_pass(self):
        self.png.side_effect = ValueError("Invalid PNG dimensions or signature")
        with self.assertRaises(ValueError):
            gate.validate(self.report, self.log, self.output, False)
        self.assertEqual(self.png.call_count, 1)


if __name__ == "__main__":
    unittest.main()
