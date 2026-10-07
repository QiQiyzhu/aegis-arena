"""Offline adversarial checks; no Unreal launch or native evidence generation."""
import copy
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import run_decision_lab_input as gate
from run_decision_lab import KNOWN_RENDER_WARNING


class DecisionLabInputGateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name).resolve()
        self.report = dict(schemaVersion=1, engine="unreal-runtime", syntheticInput=True,
                           humanUsabilityTest=False, policyQualityTest=False, fixtureDamage=False,
                           renderOffscreen=True, renderingEnabled=True, passed=True, worldType=1,
                           begunPlay=True, screenshotRequests=4, screenshotProcessed=4,
                           reason=gate.REASON, wallSeconds=8.5, seed=2001, walkDistanceCm=90.0,
                           dashDistanceCm=340.0, heldShots=3,
                           assertions=[dict(name=name, passed=True, detail="test-only fixture") for name in gate.ASSERTIONS],
                           screenshots=[str(self.output / name) for name in gate.SCREENSHOTS])
        events = [gate.BEGIN] + [f"AEGIS_LAB_INPUT_CHECK {name} pass=1 test-only fixture" for name in gate.ASSERTIONS]
        events += [f"AEGIS_LAB_INPUT_COMPLETE checks={len(gate.ASSERTIONS)}"]
        self.log = "\n".join("[2026.09.17-00.00.00:000][  0]LogTemp: Display: " + event for event in events)

    def validate(self, report=None, log=None):
        with patch.object(gate, "inspect_png", return_value={}) as pictures:
            result = gate.validate(self.report if report is None else report,
                                   self.log if log is None else log, self.output)
            return result, pictures

    def test_complete_exact_contract_checks_all_original_images(self):
        result, pictures = self.validate()
        self.assertEqual(len(gate.ASSERTIONS), 35)
        self.assertEqual(len(result), 4)
        self.assertEqual([call.args[0] for call in pictures.call_args_list],
                         [self.output / name for name in gate.SCREENSHOTS])

    def test_scope_timeout_and_real_measurements_cannot_be_weakened(self):
        for field, value in (("schemaVersion", True), ("worldType", 3), ("passed", False),
                             ("fixtureDamage", True), ("humanUsabilityTest", True), ("policyQualityTest", True),
                             ("renderOffscreen", False), ("renderingEnabled", False), ("begunPlay", False),
                             ("wallSeconds", 31), ("wallSeconds", float("nan")), ("wallSeconds", 0),
                             ("walkDistanceCm", 40), ("dashDistanceCm", 180), ("heldShots", True),
                             ("heldShots", 1), ("seed", True), ("screenshotProcessed", 3)):
            report = copy.deepcopy(self.report)
            report[field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                self.validate(report)

    def test_logs_must_be_original_unique_ordered_and_match_details(self):
        alternatives = ["", "\n".join("LogAutomationController: " + line for line in self.log.splitlines()),
                        self.log + "\n" + self.log.splitlines()[-1],
                        self.log.replace("pass=1", "pass=0", 1),
                        self.log.replace("test-only fixture", "different detail", 1),
                        self.log.replace("checks=35", "checks=34"),
                        self.log + "\nLogTemp: Warning: something went wrong",
                        self.log + "\n[2026][  0]LogRenderer: Error: bad state",
                        self.log + "\nHandled ensure", self.log + "\nFatal error:"]
        for log in alternatives:
            with self.subTest(log=log[-120:]), self.assertRaises(ValueError):
                self.validate(log=log)

    def test_only_one_exact_diagnosed_engine_warning_is_allowed_and_retained(self):
        warning = "[2026.09.17-07.58.46:955][  0]" + KNOWN_RENDER_WARNING
        log = self.log + "\n" + warning
        self.validate(log=log)
        self.assertEqual(gate.known_engine_warnings(log, True), [warning])
        for extra in (warning, "LogTemp: Warning: unrelated", warning.replace("MotionVectorSimulation", "DifferentCVar"),
                      warning + " additional message", warning.replace("Warning:", "Error:")):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                self.validate(log=log + "\n" + extra)

    def test_checks_cannot_be_missing_duplicated_reordered_or_report_only(self):
        for alteration in ("missing", "duplicate", "reorder", "failed", "rename"):
            report = copy.deepcopy(self.report)
            if alteration == "missing": report["assertions"].pop()
            if alteration == "duplicate": report["assertions"][0] = report["assertions"][1]
            if alteration == "reorder": report["assertions"].reverse()
            if alteration == "failed": report["assertions"][0]["passed"] = False
            if alteration == "rename": report["assertions"][0]["name"] = "different_check"
            with self.subTest(alteration=alteration), self.assertRaises(ValueError):
                self.validate(report)

    def test_png_paths_cannot_escape_repeat_or_change_order(self):
        for replacement in ("lab-briefing.png", str(self.output.parent / "lab-briefing.png"),
                            self.report["screenshots"][1]):
            report = copy.deepcopy(self.report)
            report["screenshots"][0] = replacement
            with self.subTest(replacement=replacement), self.assertRaises(ValueError):
                self.validate(report)
        report = copy.deepcopy(self.report)
        report["screenshots"].reverse()
        with self.assertRaises(ValueError): self.validate(report)

    def test_strict_json_rejects_duplicate_keys_and_nonfinite_numbers(self):
        for text in ('{"passed":false,"passed":true}', '{"value":NaN}', '{"value":1e999}'):
            with self.subTest(text=text), self.assertRaises(ValueError): gate.strict_json(text)

    def test_launch_contract_keeps_editor_and_packaged_paths_separate(self):
        engine = self.output / "engine"
        package = self.output / "game/Binaries/Win64/AegisArena.exe"
        editor = gate.build_command(engine, self.output)
        standalone = gate.build_command(engine, self.output, package)
        self.assertIn("-game", editor)
        self.assertIn(str(gate.ROOT / "AegisArena.uproject"), editor)
        self.assertEqual(standalone[:2], [str(package), gate.MAP])
        self.assertNotIn("-game", standalone)
        self.assertFalse(any("UnrealEditor" in value or ".uproject" in value for value in standalone))
        for command in (editor, standalone):
            for value in ("-AegisDecisionLab", "-AegisLabInputProbe", "-AegisInputProbe", "-RenderOffscreen", "-d3d11",
                          "-ResX=1280", "-ResY=720", f"-AegisLabProbeOutput={self.output}"):
                self.assertIn(value, command)
            self.assertNotIn("-NullRHI", command)
            self.assertNotIn("-AegisLabAutomated", command)
            self.assertNotIn("-AegisLabCapture", command)

    def test_editor_hash_binds_manifest_selected_dll_not_only_launcher(self):
        folder = self.output / "Binaries/Win64"
        folder.mkdir(parents=True)
        executable = self.output / "unit-test-launcher.bin"
        executable.write_bytes(b"not executable; unit test bytes")
        manifest = folder / "UnrealEditor.modules"
        manifest.write_text('{"Modules":{"AegisArena":"UnrealEditor-AegisArena-test.dll"}}')
        module = folder / "UnrealEditor-AegisArena-test.dll"
        module.write_bytes(b"test module one")
        with patch.object(gate, "ROOT", self.output):
            before = gate.binary_snapshot(executable)
            module.write_bytes(b"test module two")
            after = gate.binary_snapshot(executable)
            self.assertEqual(before["launcher"], after["launcher"])
            self.assertNotEqual(before["AegisArena"]["sha256"], after["AegisArena"]["sha256"])
            manifest.write_text('{"Modules":{"AegisArena":"../outside.dll"}}')
            with self.assertRaises(ValueError): gate.binary_snapshot(executable)

    def test_packaged_hash_identifies_bootstrap_and_real_payload(self):
        bootstrap = self.output / "AegisArena.exe"
        bootstrap.write_bytes(b"unit test bootstrap bytes")
        payload = self.output / "AegisArena/Binaries/Win64/AegisArena.exe"
        payload.parent.mkdir(parents=True)
        payload.write_bytes(b"unit test payload bytes")
        snapshots = gate.binary_snapshot(bootstrap, True)
        self.assertEqual(snapshots["gamePayload"]["path"], str(payload))
        direct = gate.binary_snapshot(payload, True)
        self.assertEqual(direct["launcher"]["sha256"], direct["gamePayload"]["sha256"])


if __name__ == "__main__":
    unittest.main()
