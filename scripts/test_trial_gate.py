"""Adversarial evidence-gate tests; these do not execute Unreal."""
import copy
import unittest

from run_unreal_trial import (
    ASSERTIONS, ASSERTION_PREFIX, MARKER, NATIVE_ASSERTIONS, OFFLINE_MARKER, TEST_PATH, validate,
)


class TrialEvidenceGateTests(unittest.TestCase):
    def setUp(self):
        self.report = dict(succeeded=1, succeededWithWarnings=0, failed=0, notRun=0, inProcess=0,
                           tests=[dict(state="Success", fullTestPath=TEST_PATH + ".Aegis Trial Functional Test")])
        self.log = OFFLINE_MARKER + "\nLogTemp: Display: " + MARKER + "\n" + "\n".join(
            "LogTemp: Display: " + ASSERTION_PREFIX + name for name in ASSERTIONS.elements())

    def test_complete_fixture_evidence_is_accepted(self):
        self.assertEqual(NATIVE_ASSERTIONS, 54)
        validate(self.report, self.log)

    def test_report_only_or_wrong_world_success_is_rejected(self):
        for log in ("", MARKER, self.log.replace(MARKER, ""),
                    self.log.replace("WorldType=3", "WorldType=2"),
                    self.log.replace("BegunPlay=1", "BegunPlay=0"),
                    self.log.replace("FixtureDamage=1", "FixtureDamage=0"),
                    self.log.replace("Assertions=54", "Assertions=53"),
                    self.log + "\nLogTemp: Display: " + MARKER):
            with self.subTest(log=log[:100]), self.assertRaises(ValueError):
                validate(self.report, log)

    def test_each_assertion_and_retry_occurrence_is_required(self):
        for name in ASSERTIONS:
            missing = self.log.replace(ASSERTION_PREFIX + name, "", 1)
            with self.subTest(missing=name), self.assertRaises(ValueError):
                validate(self.report, missing)
            # Equal totals still cannot substitute another successful check.
            substituted = missing + "\nLogTemp: Display: " + ASSERTION_PREFIX + "Exactly one configured trial runner"
            with self.subTest(substituted=name):
                if name != "Exactly one configured trial runner":
                    with self.assertRaises(ValueError):
                        validate(self.report, substituted)

    def test_extra_unknown_assertion_is_rejected(self):
        for extra in ("Unknown check", "Trial damage counters reset"):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                validate(self.report, self.log + "\nLogTemp: Display: " + ASSERTION_PREFIX + extra)

    def test_automation_replay_cannot_replace_original_fixture_evidence(self):
        replay = "\n".join("LogAutomationController: " + line + " [log]"
                           for line in self.log.splitlines() if line.startswith("LogTemp:"))
        validate(self.report, self.log + "\n" + replay)
        with self.assertRaises(ValueError):
            validate(self.report, OFFLINE_MARKER + "\n" + replay)
        failed = copy.deepcopy(self.report)
        failed.update(succeeded=0, failed=1)
        with self.assertRaises(ValueError):
            validate(failed, self.log + "\n" + replay)

    def test_offline_startup_override_must_have_executed(self):
        with self.assertRaises(ValueError):
            validate(self.report, self.log.replace(OFFLINE_MARKER, ""))

    def test_dirty_incomplete_or_unrelated_report_is_rejected(self):
        for field, value in (("succeeded", 0), ("succeeded", 2), ("succeeded", True),
                             ("succeededWithWarnings", 1), ("failed", 1),
                             ("notRun", 1), ("inProcess", 1)):
            report = copy.deepcopy(self.report)
            report[field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                validate(report, self.log)
        report = copy.deepcopy(self.report)
        del report["succeededWithWarnings"]
        with self.assertRaises(ValueError):
            validate(report, self.log)
        for tests in ([], [dict(state="Fail", fullTestPath=TEST_PATH + ".Trial")],
                      [dict(state="Success", fullTestPath="AnotherFixture")], self.report["tests"] * 2):
            report = copy.deepcopy(self.report)
            report["tests"] = tests
            with self.subTest(tests=tests), self.assertRaises(ValueError):
                validate(report, self.log)

    def test_fatal_ensure_or_failed_assertion_overrides_success(self):
        for failure in ("Handled ensure", "Ensure condition failed", "No functional testing script",
                        "TestResult=Failed", "Fatal error:", "Assertion failed"):
            with self.subTest(failure=failure), self.assertRaises(ValueError):
                validate(self.report, self.log + "\n" + failure)
