"""Adversarial operation evidence validation; does not launch Unreal."""
import copy
import unittest

from run_unreal_operation import (
    ASSERTIONS, ASSERTION_PREFIX, MARKER, NATIVE_ASSERTIONS, OFFLINE_MARKER, TEST_PATH, validate,
)


class OperationEvidenceGateTests(unittest.TestCase):
    def setUp(self):
        self.report = dict(succeeded=1, succeededWithWarnings=0, failed=0, notRun=0, inProcess=0,
                           tests=[dict(state="Success", fullTestPath=TEST_PATH + ".Operation objectives")])
        self.log = OFFLINE_MARKER + "\nLogTemp: Display: " + MARKER + "\n" + "\n".join(
            "LogTemp: Display: " + ASSERTION_PREFIX + name for name in ASSERTIONS.elements())

    def test_complete_native_contract_is_accepted(self):
        self.assertEqual(NATIVE_ASSERTIONS, 31)
        validate(self.report, self.log)

    def test_report_only_wrong_world_or_hidden_interventions_are_rejected(self):
        for log in ("", self.log.replace(MARKER, ""),
                    self.log.replace("WorldType=3", "WorldType=2"),
                    self.log.replace("BegunPlay=1", "BegunPlay=0"),
                    self.log.replace("Assertions=31", "Assertions=30"),
                    self.log.replace("FixtureDamage=1", "FixtureDamage=0"),
                    self.log.replace("AIStopped=1", "AIStopped=0"),
                    self.log.replace("PlacedOccupants=1", "PlacedOccupants=0"),
                    self.log + "\nLogTemp: Display: " + MARKER):
            with self.subTest(log=log[:100]), self.assertRaises(ValueError):
                validate(self.report, log)

    def test_each_named_world_assertion_is_required(self):
        for name in ASSERTIONS:
            missing = self.log.replace(ASSERTION_PREFIX + name, "", 1)
            with self.subTest(name=name), self.assertRaises(ValueError):
                validate(self.report, missing)
            substitute = missing + "\nLogTemp: Display: " + ASSERTION_PREFIX + "unrelated_success"
            with self.subTest(substitute=name), self.assertRaises(ValueError):
                validate(self.report, substitute)

    def test_duplicate_or_unknown_assertion_is_rejected(self):
        for name in ("begun_pie_world", "unknown_world_check"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                validate(self.report, self.log + "\nLogTemp: Display: " + ASSERTION_PREFIX + name)

    def test_replay_does_not_count_as_original_execution(self):
        replay = "\n".join("LogAutomationController: " + line + " [log]"
                           for line in self.log.splitlines() if line.startswith("LogTemp:"))
        validate(self.report, self.log + "\n" + replay)
        with self.assertRaises(ValueError):
            validate(self.report, OFFLINE_MARKER + "\n" + replay)

    def test_incomplete_warning_or_unrelated_report_is_rejected(self):
        for field, value in (("succeeded", 0), ("succeeded", 2), ("succeeded", True),
                             ("succeededWithWarnings", 1), ("failed", 1), ("notRun", 1), ("inProcess", 1)):
            report = copy.deepcopy(self.report)
            report[field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                validate(report, self.log)
        for tests in ([], self.report["tests"] * 2,
                      [dict(state="Success", fullTestPath="Project.Functional Tests.OtherFixture")],
                      [dict(state="Fail", fullTestPath=TEST_PATH + ".Operation")]):
            report = copy.deepcopy(self.report)
            report["tests"] = tests
            with self.subTest(tests=tests), self.assertRaises(ValueError):
                validate(report, self.log)

    def test_engine_failure_and_missing_offline_override_are_rejected(self):
        for failure in ("AEGIS_OPERATION_FUNCTIONAL_FAIL", "Handled ensure", "Ensure condition failed",
                        "No functional testing script", "TestResult=Failed", "Fatal error:", "Assertion failed"):
            with self.subTest(failure=failure), self.assertRaises(ValueError):
                validate(self.report, self.log + "\n" + failure)
        with self.assertRaises(ValueError):
            validate(self.report, self.log.replace(OFFLINE_MARKER, ""))
