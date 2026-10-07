"""These validate the Python evidence gate; they do not execute Unreal."""
import unittest
from pathlib import Path
import tempfile
from run_unreal_functional import ASSERTIONS, MARKER, MEMORY_MARKER, snapshot_inputs, validate


class NativeEvidenceGateTests(unittest.TestCase):
    def test_json_success_without_world_assertions_is_rejected(self):
        report = dict(succeeded=1, failed=0, notRun=0, inProcess=0)
        with self.assertRaises(ValueError):
            validate(report, "No functional testing script on map")

    def test_success_requires_all_assertions_and_no_ensure(self):
        report = dict(succeeded=1, failed=0, notRun=0, inProcess=0)
        log = MARKER + "\n" + MEMORY_MARKER + "\n" + "\n".join(
            f"Assertion passed ({name})" for name in ASSERTIONS)
        validate(report, log)
        with self.assertRaises(ValueError):
            validate(report, log + "\nHandled ensure")
        for name in ASSERTIONS:
            with self.subTest(missing_assertion=name), self.assertRaises(ValueError):
                validate(report, log.replace(f"Assertion passed ({name})", ""))

    def test_memory_evidence_requires_config_and_game_world(self):
        report = dict(succeeded=1, failed=0, notRun=0, inProcess=0)
        assertions = "\n".join(f"Assertion passed ({name})" for name in ASSERTIONS)
        for marker in ("", MEMORY_MARKER.replace("2.500", "1.000"),
                       MEMORY_MARKER.replace("WorldType=3", "WorldType=2"),
                       MEMORY_MARKER.replace("BegunPlay=1", "BegunPlay=0")):
            with self.subTest(marker=marker), self.assertRaises(ValueError):
                validate(report, MARKER + "\n" + marker + "\n" + assertions)

    def test_input_provenance_tracks_source_and_asset_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "Source/Fixture.cpp"
            asset = root / "Content/Aegis/Fixture.uasset"
            source.parent.mkdir(parents=True)
            asset.parent.mkdir(parents=True)
            source.write_bytes(b"original source")
            asset.write_bytes(b"original asset")
            (root / "AegisArena.uproject").write_text("{}", encoding="utf-8")
            before = snapshot_inputs(root)
            (root / "report.json").write_text("{}", encoding="utf-8")
            self.assertEqual(snapshot_inputs(root), before)
            source.write_bytes(b"changed source")
            changed = snapshot_inputs(root)
            self.assertNotEqual(changed["sourceSha256"], before["sourceSha256"])
            self.assertEqual(changed["contentSha256"], before["contentSha256"])
            asset.rename(asset.with_name("Renamed.uasset"))
            self.assertNotEqual(snapshot_inputs(root)["contentSha256"], changed["contentSha256"])
