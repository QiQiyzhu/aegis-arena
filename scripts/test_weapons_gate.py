"""Adversarial evidence-gate tests only; these do not execute Unreal."""
import copy
import unittest

from run_unreal_weapons import (
    ASSERTIONS, ASSERTION_PREFIX, FIXTURE_MARKER, FULL_TEST_PATH,
    MARKER, NATIVE_ASSERTIONS, OFFLINE_MARKER, validate,
)


class WeaponsEvidenceGateTests(unittest.TestCase):
    def setUp(self):
        self.report = dict(succeeded=1, succeededWithWarnings=0, failed=0, notRun=0, inProcess=0,
                           tests=[dict(state="Success", fullTestPath=FULL_TEST_PATH)])
        self.log = OFFLINE_MARKER + "\n" + "\n".join(
            "LogTemp: Display: " + event for event in
            [FIXTURE_MARKER, *(ASSERTION_PREFIX + name for name in ASSERTIONS), MARKER])

    def test_complete_evidence_is_accepted(self):
        self.assertEqual(NATIVE_ASSERTIONS, 31)
        validate(self.report, self.log)

    def test_report_only_or_wrong_world_is_rejected(self):
        for log in ("", MARKER, self.log.replace(MARKER, ""),
                    self.log.replace("WorldType=3", "WorldType=2"),
                    self.log.replace("BegunPlay=1", "BegunPlay=0"),
                    self.log.replace("Assertions=31", "Assertions=30")):
            with self.subTest(log=log[:80]), self.assertRaises(ValueError):
                validate(self.report, log)

    def test_each_assertion_is_required(self):
        for name in ASSERTIONS:
            with self.subTest(name=name), self.assertRaises(ValueError):
                validate(self.report, self.log.replace(ASSERTION_PREFIX + name, "", 1))

    def test_duplicates_and_unknown_assertions_are_rejected(self):
        for event in (MARKER, FIXTURE_MARKER, ASSERTION_PREFIX + ASSERTIONS[0], ASSERTION_PREFIX + "Unknown check"):
            with self.subTest(event=event), self.assertRaises(ValueError):
                validate(self.report, self.log + "\nLogTemp: Display: " + event)

    def test_replayed_logs_cannot_replace_original_assertions(self):
        replay = "\n".join("LogAutomationController: " + line + " [log]" for line in self.log.splitlines())
        validate(self.report, self.log + "\n" + replay)
        with self.assertRaises(ValueError):
            validate(self.report, OFFLINE_MARKER + "\n" + replay)

    def test_fixture_interventions_and_offline_context_are_required(self):
        for log in (self.log.replace(FIXTURE_MARKER, ""), self.log.replace(OFFLINE_MARKER, ""),
                    self.log.replace("FrozenMovement=1", "FrozenMovement=0"),
                    self.log.replace("SeededWindup=1", "SeededWindup=0"),
                    self.log.replace("FixtureDamage=1", "FixtureDamage=0")):
            with self.subTest(log=log[:80]), self.assertRaises(ValueError):
                validate(self.report, log)

    def test_unrelated_or_incomplete_reports_are_rejected(self):
        for key, value in (("succeeded", True), ("succeeded", 0), ("succeeded", 2),
                           ("failed", 1), ("succeededWithWarnings", 1), ("notRun", 1), ("inProcess", 1)):
            report = copy.deepcopy(self.report)
            report[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                validate(report, self.log)
        for tests in ([], self.report["tests"] * 2, [dict(state="Success", fullTestPath=FULL_TEST_PATH + "Other")],
                      [dict(state="Fail", fullTestPath=FULL_TEST_PATH)]):
            report = copy.deepcopy(self.report)
            report["tests"] = tests
            with self.subTest(tests=tests), self.assertRaises(ValueError):
                validate(report, self.log)

    def test_errors_override_success(self):
        for failure in ("Handled ensure", "Ensure condition failed", "No functional testing script",
                        "TestResult=Failed", "Fatal error:", "Assertion failed", "AEGIS_WEAPONS_FUNCTIONAL_FAIL"):
            with self.subTest(failure=failure), self.assertRaises(ValueError):
                validate(self.report, self.log + "\n" + failure)
