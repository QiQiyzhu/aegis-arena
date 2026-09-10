"""These validate the Python evidence gate; they do not execute Unreal."""
import unittest
from run_unreal_functional import ASSERTIONS, MARKER, validate


class NativeEvidenceGateTests(unittest.TestCase):
    def test_json_success_without_world_assertions_is_rejected(self):
        report = dict(succeeded=1, failed=0, notRun=0, inProcess=0)
        with self.assertRaises(ValueError):
            validate(report, "No functional testing script on map")

    def test_success_requires_all_assertions_and_no_ensure(self):
        report = dict(succeeded=1, failed=0, notRun=0, inProcess=0)
        log = MARKER + "\n" + "\n".join(f"Assertion passed ({name})" for name in ASSERTIONS)
        validate(report, log)
        with self.assertRaises(ValueError):
            validate(report, log + "\nHandled ensure")
        with self.assertRaises(ValueError):
            validate(report, log.replace(f"Assertion passed ({ASSERTIONS[-1]})", ""))
