"""Test the rendered-probe evidence gate, without running Unreal or creating evidence."""
import copy
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zlib

import run_unreal_input_probe as gate


class InputProbeEvidenceGateTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.output = Path(self.directory.name).resolve()
        self.names = ("Move check", "Pause check")
        self.report = dict(schemaVersion=1, engine="unreal-runtime", syntheticInput=True, renderOffscreen=True,
                           humanUsabilityTest=False, passed=True, wallSeconds=7.5,
                           fixtureDamage=False, worldType=1, begunPlay=True, renderingEnabled=True,
                           reason="fixture completed",
                           screenshotRequests=4, screenshotProcessed=4,
                           assertions=[dict(name=name, passed=True, detail="fixture") for name in self.names],
                           screenshots=[str(self.output / name) for name in gate.SCREENSHOTS])
        self.log = "\n".join("LogTemp: Display: AEGIS_INPUT_ASSERT " + name + " | pass=1 | fixture"
                             for name in self.names) + (
                                 "\nLogTemp: Display: AEGIS_INPUT_PROBE_PASS checks=2 screenshots=4 wall=7.500 reason=fixture completed")
        self.contract = patch.object(gate, "ASSERTIONS", self.names)
        self.contract.start()
        self.addCleanup(self.contract.stop)

    def test_complete_contract_requires_four_image_validations(self):
        with patch.object(gate, "inspect_png", return_value={}) as images:
            self.assertEqual(len(gate.validate(self.report, self.log, self.output)), 4)
            self.assertEqual([call.args[0] for call in images.call_args_list],
                             [self.output / name for name in gate.SCREENSHOTS])

    def test_packaged_launch_uses_only_game_executable_and_common_probe_flags(self):
        engine = self.output / "metadata-engine"
        packaged = self.output / "Package/AegisArena/Binaries/Win64/AegisArena.exe"
        command = gate.build_command(engine, self.output, packaged)
        self.assertEqual(command[:2], [str(packaged), gate.MAP])
        self.assertNotIn("-game", command)
        self.assertNotIn("-NullRHI", command)
        self.assertFalse(any("UnrealEditor" in arg or ".uproject" in arg for arg in command))
        self.assertIn("-RenderOffscreen", command)
        self.assertIn("-AegisInputProbe", command)
        self.assertIn(f"-AegisProbeOutput={self.output}", command)
        self.assertIn("-ResX=1280", command)
        self.assertIn("-ResY=720", command)

    def test_default_launch_keeps_editor_project_and_game_arguments(self):
        engine = self.output / "EngineInstallation"
        command = gate.build_command(engine, self.output)
        self.assertEqual(command[:4], [str(engine / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"),
                                      str(gate.ROOT / "AegisArena.uproject"), gate.MAP, "-game"])
        self.assertNotIn("-NullRHI", command)

    def test_wrong_scope_partial_capture_or_actor_timeout_is_rejected(self):
        for key, value in (("syntheticInput", False), ("renderOffscreen", False),
                           ("humanUsabilityTest", True), ("passed", False),
                           ("worldType", 3), ("begunPlay", False), ("fixtureDamage", True),
                           ("renderingEnabled", False),
                           ("schemaVersion", True), ("screenshotRequests", 3),
                           ("screenshotProcessed", 3), ("wallSeconds", 31),
                           ("wallSeconds", float("nan")), ("wallSeconds", float("inf"))):
            report = copy.deepcopy(self.report)
            report[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                gate.validate(report, self.log, self.output)

    def test_success_json_cannot_replace_real_unique_assertion_emissions(self):
        replay = "\n".join("LogAutomationController: " + line + " [log]" for line in self.log.splitlines())
        for log in ("", replay, self.log.replace(" | pass=1", " | pass=0", 1),
                    self.log.replace("Move check", "Different check"),
                    self.log + "\nLogTemp: Display: AEGIS_INPUT_PROBE_PASS",
                    self.log + "\nHandled ensure", self.log + "\nFatal error:",
                    self.log + "\nAEGIS_INPUT_PROBE_FAIL"):
            with self.subTest(log=log[:120]), self.assertRaises(ValueError):
                gate.validate(self.report, log, self.output)

    def test_report_labels_and_screenshot_destinations_are_exact(self):
        changes = []
        report = copy.deepcopy(self.report)
        report["assertions"][0]["passed"] = False
        changes.append(report)
        report = copy.deepcopy(self.report)
        report["assertions"][0]["name"] = "Unknown"
        changes.append(report)
        report = copy.deepcopy(self.report)
        report["screenshots"][0] = str(self.output.parent / "briefing.png")
        changes.append(report)
        report = copy.deepcopy(self.report)
        report["screenshots"][0] = "briefing.png"
        changes.append(report)
        report = copy.deepcopy(self.report)
        report["screenshots"][0] = report["screenshots"][1]
        changes.append(report)
        for report in changes:
            with self.subTest(report=report), self.assertRaises(ValueError):
                gate.validate(report, self.log, self.output)

    def test_png_checks_bytes_dimensions_crc_and_real_scanline_stream(self):
        def chunk(kind, body):
            return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

        # A one-pixel in-memory unit-test fixture, never a native evidence image.
        header = chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0))
        signature = b"\x89PNG\r\n\x1a\n"
        end = chunk(b"IEND", b"")
        data = signature + header + chunk(b"IDAT", zlib.compress(b"\x00\x00\x00\x00\xff")) + end
        path = self.output / "unit-test-bytes.png"
        path.write_bytes(data)
        self.assertEqual(gate.inspect_png(path, (1, 1))["width"], 1)
        self.assertEqual(path.read_bytes(), data)
        with self.assertRaises(ValueError):
            gate.inspect_png(path)
        corruptions = [b"not an image", data[:-1], data[:-13] + b"X" + data[-12:],
                       signature + header + end,
                       signature + header + chunk(b"IDAT", zlib.compress(b"\x00")) + end]
        for altered in corruptions:
            path.write_bytes(altered)
            with self.subTest(bytes=len(altered)), self.assertRaises(ValueError):
                gate.inspect_png(path, (1, 1))
