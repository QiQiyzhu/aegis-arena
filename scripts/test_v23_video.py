"""v2.3 native-frame delivery encoding contracts; no Unreal or final footage."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import wave

import numpy as np
from PIL import Image

import build_v23_video as video


class V23TimelineTests(unittest.TestCase):
    def test_nonzero_first_frame_maps_audio_once_and_keeps_full_duration(self):
        origin = 1 / 30
        frames = [dict(videoSeconds=origin + i / 30) for i in range(30)]
        capture = dict(frameTimes=frames, musicEvents=[dict(
            sequence=1, state='restart', asset='', assetPath='', videoSeconds=0,
            worldSeconds=1, volume=0, fadeSeconds=0, channel=-1, loop=False, reason='fixture')],
            audioEvents=[dict(asset='S_Test', assetPath='/Game/Aegis/V21/Audio/S_Test.S_Test',
                              pitch=1, volume=1, videoSeconds=origin + .5)])
        clip = np.zeros((480, 2), dtype=np.float32)
        clip[0] = .5
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ranges = [(origin, origin + .4), (origin + .4, origin + 1)]
            with patch.object(video.v21_audio, 'read_sources', return_value=(
                    {'S_Test': clip}, {'S_Test': {'loop': False}}, {})):
                video.prepare_capture_audio(capture, ranges, root / 'native.wav', root / 'film.wav')
            with wave.open(str(root / 'film.wav'), 'rb') as stream:
                pcm = np.frombuffer(stream.readframes(stream.getnframes()), dtype='<i2').reshape(-1, 2)
                self.assertEqual(stream.getnframes(), 13 * video.RATE)
            sounding = np.flatnonzero(np.any(pcm != 0, axis=1))
            self.assertEqual(sounding.tolist(), [round(5.5 * video.RATE)])

    def test_audio_range_cannot_silently_truncate_the_film(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with wave.open(str(root / 'source.wav'), 'wb') as stream:
                stream.setparams((2, 2, video.RATE, 0, 'NONE', 'not compressed'))
                stream.writeframes(bytes(4 * video.RATE))
            with self.assertRaisesRegex(ValueError, 'absolute capture clock'):
                video.concat_audio(root / 'source.wav', [(1 / 30, 1 + 1 / 30)], root / 'film.wav')

    @unittest.skipUnless(video.DEFAULT_FFMPEG.is_file(), 'requires the local delivery ffmpeg')
    def test_png_concat_preserves_every_original_frame_at_30fps(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            entries = []
            expected = []
            for index in range(30):
                path = root / f'frame-{index:06d}.png'
                value = (index * 7, 255 - index * 5, index * 3)
                Image.new('RGB', (8, 8), value).save(path)
                expected.append(bytes(value) * 64)
                entries.append(video.concat_png_entry(path, 1 / 30))
            entries.append(video.concat_png_entry(path))
            listing = root / 'frames.txt'
            listing.write_text(''.join(entries), encoding='utf-8')
            result = subprocess.run([str(video.DEFAULT_FFMPEG), '-v', 'error', '-f', 'concat',
                                     '-safe', '0', '-i', str(listing), '-vf', 'fps=30',
                                     '-frames:v', '30', '-pix_fmt', 'rgb24', '-f', 'rawvideo', '-'],
                                    capture_output=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, b''.join(expected))


class V23EncodingTests(unittest.TestCase):
    def test_decimal_size_budget_includes_sound_and_mux_reserve(self):
        for duration in (123.01, 152, 182):
            with self.subTest(duration=duration):
                plan = video.encoding_plan(duration, target_mb=50)
                payload = (plan["videoBitrate"] + plan["audioBitrate"]) * duration / 8
                self.assertEqual(plan["targetBytes"], 50_000_000)
                self.assertLessEqual(payload, 49_000_000)
                self.assertGreater(payload, 49_000_000 - duration * 1000 / 8)

    def test_invalid_and_unusable_targets_fail_before_encoding(self):
        for target in (0, -1, 300, float("inf"), float("nan"), True):
            with self.subTest(target=target), self.assertRaises(ValueError):
                video.encoding_plan(150, target_mb=target)
        with self.assertRaisesRegex(ValueError, "increase --target-mb"):
            video.encoding_plan(150, target_mb=1)
        for duration in (0, -1, float("inf"), float("nan"), True):
            with self.subTest(duration=duration), self.assertRaises(ValueError):
                video.encoding_plan(duration, target_mb=50)

    def commands(self, plan):
        return video.encoding_commands(Path("ffmpeg.exe"), Path("delivery"),
                                       Path("delivery/event-mix.wav"), Path("delivery/demo.mp4"),
                                       152, "native-filter-graph", plan)

    def test_default_crf_remains_single_pass(self):
        commands = self.commands(video.encoding_plan(152))
        self.assertEqual(len(commands), 1)
        name, command = commands[0]
        self.assertEqual(name, "encode.log")
        self.assertEqual(command[command.index("-crf") + 1], "20")
        self.assertNotIn("-pass", command)
        self.assertIn("4:a:0", command)

    def test_both_passes_read_original_sources_and_same_graph(self):
        commands = self.commands(video.encoding_plan(152, target_mb=50))
        self.assertEqual(len(commands), 2)
        inputs = []
        for pass_number, (_, command) in enumerate(commands, 1):
            inputs.append([str(command[i + 1]) for i, value in enumerate(command) if value == "-i"])
            self.assertFalse(any(path.endswith(".mp4") for path in inputs[-1]))
            self.assertEqual(command[command.index("-filter_complex") + 1], "native-filter-graph")
            self.assertEqual(command[command.index("-frames:v") + 1], "4560")
            self.assertEqual(command[command.index("-pass") + 1], str(pass_number))
            self.assertEqual(command[command.index("-passlogfile") + 1], Path("delivery/x264-pass"))
            self.assertNotIn("-crf", command)
        self.assertEqual(inputs[0], inputs[1])
        self.assertIn("-an", commands[0][1])
        self.assertNotIn("4:a:0", commands[0][1])
        self.assertIn("4:a:0", commands[1][1])
        self.assertEqual(commands[1][1][-1], Path("delivery/demo.mp4"))

    def test_cli_rejects_ambiguous_quality_selection(self):
        result = subprocess.run([sys.executable, str(Path(video.__file__)),
                                 "--capture", "unused-capture", "--output", "unused-output",
                                 "--crf", "18", "--target-mb", "50"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("not allowed with argument", result.stderr)


class V23EditorialEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.capture = dict(visualDeliveryVersion="2.3", languageDemo=True,
                            presentationVersion="2.1", scriptedPlayer=True,
                            fixtureDamage=False, humanPlaytest=False)
        self.provenance = dict(visualDeliveryVersion="2.3")
        self.ledger = dict(northRouteFirst=False, chargedShots=1, pulsesUsed=1,
                           repairsUsed=1, overclocks=1, companionStateChanges=3,
                           companionSurvived=True, stagesRewarded=3, outcome="won")

    def evidence(self, capture):
        return video.validate_editorial_capture(capture, self.provenance, self.ledger, "2.3")

    def gameplay(self):
        return dict(self.capture, gameplayVersion="2.3", samples=[dict(
            surveyKeys=1, surveySupplies=1, surveyBoosts=1, surveyClaimedMask=3)])

    def test_preserved_visual_only_capture_does_not_claim_survey(self):
        result = self.evidence(self.capture)
        self.assertTrue(result["checked"])
        self.assertNotIn("surveyKeys", result["gameplaySignals"])
        self.assertIsNone(result["captureGameplayVersion"])

    def test_new_gameplay_records_complete_native_survey_signals(self):
        result = self.evidence(self.gameplay())
        self.assertEqual(result["captureGameplayVersion"], "2.3")
        self.assertEqual(result["gameplaySignals"]["surveyKeys"], 1)
        self.assertEqual(result["gameplaySignals"]["surveySupplies"], 1)
        self.assertEqual(result["gameplaySignals"]["surveyBoosts"], 1)
        self.assertEqual(result["gameplaySignals"]["surveyClaimedMask"], 3)

    def test_new_gameplay_cannot_drop_any_required_survey_demonstration(self):
        for key in ("surveyKeys", "surveySupplies", "surveyBoosts", "surveyClaimedMask"):
            for value in (0, None, True, 1.5):
                changed = self.gameplay(); changed["samples"][-1][key] = value
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    self.evidence(changed)
        changed = self.gameplay(); changed["samples"] = []
        with self.assertRaisesRegex(ValueError, "final survey sample"):
            self.evidence(changed)

    def test_last_sample_must_confirm_mechanics_not_an_earlier_sample(self):
        changed = self.gameplay()
        changed["samples"].append(dict(surveyKeys=0, surveySupplies=0, surveyBoosts=0, surveyClaimedMask=0))
        with self.assertRaises(ValueError): self.evidence(changed)


if __name__ == "__main__":
    unittest.main()
