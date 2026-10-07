"""Evidence-gate tests only; native behavior is verified in the PIE fixture."""
import unittest

from run_unreal_tactical import ASSERTIONS, MARKER, OFFLINE_MARKER, TEST_PATH, validate


class TacticalEvidenceGateTests(unittest.TestCase):
    def setUp(self):
        self.report = dict(succeeded=1, succeededWithWarnings=0, failed=0, notRun=0, inProcess=0,
                           tests=[dict(state="Success", fullTestPath=TEST_PATH + ".Tactical")])
        self.log = OFFLINE_MARKER + "\n" + "\n".join(
            "[timestamp][1]LogTemp: Display: AEGIS_TACTICAL_ASSERT_PASS " + name for name in ASSERTIONS)
        self.log += "\n[timestamp][2]LogTemp: Display: " + MARKER

    def test_exact_native_evidence_passes(self):
        validate(self.report, self.log)

    def test_every_missing_assertion_is_rejected(self):
        for name in ASSERTIONS:
            with self.subTest(name=name), self.assertRaises(ValueError):
                validate(self.report, self.log.replace("AEGIS_TACTICAL_ASSERT_PASS " + name, "removed"))

    def test_replayed_or_duplicate_evidence_is_rejected(self):
        with self.assertRaises(ValueError):
            validate(self.report, self.log.replace("LogTemp: Display:", "LogAutomationController:"))
        with self.assertRaises(ValueError):
            validate(self.report, self.log + "\nLogTemp: Display: AEGIS_TACTICAL_ASSERT_PASS tactical_companion_enabled")

    def test_wrong_world_or_failed_native_run_is_rejected(self):
        for log in (self.log.replace("WorldType=3", "WorldType=2"), self.log + "\nHandled ensure",
                    self.log.replace(OFFLINE_MARKER, "")):
            with self.subTest(log=log[-100:]), self.assertRaises(ValueError):
                validate(self.report, log)
        self.report["succeededWithWarnings"] = 1
        with self.assertRaises(ValueError):
            validate(self.report, self.log)

    def test_pre_regression_29_assertion_report_is_rejected(self):
        old_log = "\n".join(line for line in self.log.splitlines() if "natural_spawn_order_" not in line)
        old_log = old_log.replace("Assertions=32", "Assertions=29")
        with self.assertRaises(ValueError):
            validate(self.report, old_log)
