"""Synthetic clock/EDL contracts; no native capture is created or modified."""
from bisect import bisect_left
from copy import deepcopy
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import make_v23_edl as edl
import build_v23_video as video


def fixture(sparse=False):
    samples = []
    for tick in range(601):
        video = tick / 10
        world = (video + 1 if video <= 20 else 21 if video <= 23 else
                 video - 2 if video <= 40 else 38 if video <= 43 else video - 5)
        phase, wave = ((0, 0) if video < 7 else (1, 1) if video < 20 else
                       (2, 1) if video < 28 else (1, 2) if video < 40 else
                       (2, 2) if video < 48 else (1, 3) if video < 58 else (3, 3))
        enemies = 0 if phase == 3 or (phase == 1 and wave == 3 and video >= 57.5) else 3
        samples.append(dict(videoSeconds=video, worldSeconds=world, phase=phase, wave=wave, enemies=enemies))
    interval, count = (1, 60) if sparse else (1 / 30, 1800)
    capture = dict(validRun=True, gameplayVersion='2.3', languageDemo=True, outcome='won', videoSeconds=60,
                   frames=count, completedFrames=count, samples=samples,
                   frameTimes=[dict(file=f'frame-{i:06d}.png', videoSeconds=i * interval) for i in range(count)])
    events = []
    def event(kind, stamp, **data): events.append(dict(event=kind, gameSeconds=stamp, data=data))
    event('session_started', 1)
    event('survey_started', 10, node=0, supply=False)
    event('survey_cancelled', 11, node=0, reason='damaged')
    event('survey_started', 13, node=0, supply=False)
    event('survey_claimed', 15.5, node=0, supply=False)
    event('survey_boost_bound', 16, relayKey=10)
    event('survey_mode_selected', 46, supply=True)
    event('survey_started', 48, node=1, supply=True)
    event('survey_claimed', 50.5, node=1, supply=True)
    event('session_finished', 53)
    return capture, events


class V23EDLTests(unittest.TestCase):
    def test_world_clock_interpolation_respects_paused_video_time(self):
        capture, _ = fixture()
        clock = edl.ClockMap(capture['samples'])
        self.assertAlmostEqual(clock.video(20.5), 19.5)
        self.assertAlmostEqual(clock.video(21, active=False), 20)
        self.assertAlmostEqual(clock.video(21.5), 23.5)
        self.assertAlmostEqual(clock.video(50.5), 55.5)
        with self.assertRaisesRegex(ValueError, 'outside'): clock.video(1000)

    def test_full_edl_preserves_every_frame_once_without_gaps(self):
        capture, events = fixture()
        rows = edl.make_edl(capture, events)
        self.assertEqual(rows[0]['label'], 'mission_briefing')
        self.assertEqual((rows[0]['start'], rows[0]['end']), (0, 7))
        self.assertEqual(rows[-1]['label'], 'mission_result')
        self.assertIn('stage2_transfer', [row['label'] for row in rows])
        self.assertNotIn('stage2_charge', [row['label'] for row in rows])
        self.assertIn('upgrade_first', [row['label'] for row in rows])
        self.assertAlmostEqual(rows[-1]['end'], 60)
        times = [row['videoSeconds'] for row in capture['frameTimes']]
        selected = []
        for index, row in enumerate(rows):
            if index: self.assertEqual(row['start'], rows[index - 1]['end'])
            self.assertGreater(row['end'], row['start'])
            selected.extend(range(bisect_left(times, row['start']), bisect_left(times, row['end'])))
        self.assertEqual(selected, list(range(1800)))

    def test_scan_retries_and_late_supply_choice_are_inside_their_chapters(self):
        capture, events = fixture()
        rows = {row['label']: row for row in edl.make_edl(capture, events)}
        # Key chapter begins with the first failed attempt, not just successful retry.
        self.assertAlmostEqual(rows['survey_key']['start'], 9)
        self.assertAlmostEqual(rows['survey_key']['end'], 15)
        self.assertLess(rows['survey_key']['start'], 10)
        self.assertGreater(rows['survey_key']['end'], 14.5)
        # Supply choice at world=46 maps to video=51 after both menu pauses.
        self.assertAlmostEqual(rows['survey_supply']['start'], 51)
        self.assertAlmostEqual(rows['survey_supply']['end'], 57)
        self.assertAlmostEqual(rows['stage3_finish']['start'], 57.5)

    def test_wave_two_supply_does_not_relabel_entire_third_wave_as_extraction(self):
        capture, events = fixture()
        for row in events:
            if row['event'] == 'survey_mode_selected': row['gameSeconds'] = 30
            elif row['data'].get('supply') is True:
                row['gameSeconds'] = 32 if row['event'] == 'survey_started' else 34.5
        rows = edl.make_edl(capture, events)
        by_label = {row['label']: row for row in rows}
        self.assertAlmostEqual(by_label['survey_supply']['start'], 32)
        self.assertAlmostEqual(by_label['survey_supply']['end'], 38)
        self.assertAlmostEqual(by_label['stage3_open']['start'], 48)
        self.assertAlmostEqual(by_label['stage3_open']['end'], 57.5)
        self.assertAlmostEqual(by_label['stage3_finish']['start'], 57.5)
        self.assertAlmostEqual(by_label['stage3_finish']['end'], 58)

    def test_missing_active_clear_sample_does_not_invent_extraction_chapter(self):
        capture, events = fixture()
        for row in capture['samples']:
            if row['phase'] == 1 and row['wave'] == 3: row['enemies'] = 1
        self.assertNotIn('stage3_finish', [row['label'] for row in edl.make_edl(capture, events)])

    def test_sparse_capture_needs_explicit_preview_and_keeps_all_frames(self):
        capture, events = fixture(sparse=True)
        with self.assertRaisesRegex(ValueError, '30 fps'): edl.make_edl(capture, events)
        rows = edl.make_edl(capture, events, preview=True)
        self.assertEqual(rows[-1]['end'], capture['videoSeconds'])
        self.assertEqual(sum(row['end'] - row['start'] for row in rows), 60)

    def test_incomplete_misordered_or_missing_source_evidence_is_rejected(self):
        capture, events = fixture()
        for field, value in (('completedFrames', 1799), ('validRun', False), ('gameplayVersion', '2.2')):
            changed = dict(capture, **{field: value})
            with self.subTest(field=field), self.assertRaises(ValueError): edl.make_edl(changed, events)
        changed = deepcopy(capture); changed['samples'][150]['worldSeconds'] -= 2
        with self.assertRaisesRegex(ValueError, 'regressed'): edl.make_edl(changed, events)
        with self.assertRaisesRegex(ValueError, 'clock regressed'): edl.make_edl(capture, list(reversed(events)))
        with self.assertRaisesRegex(ValueError, 'H choice'): edl.make_edl(capture, [row for row in events if row['event'] != 'survey_mode_selected'])

    def test_loading_binds_source_hashes_before_revalidating_native_ledger(self):
        capture, events = fixture()
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory); (folder / 'system').mkdir()
            trace = folder / 'system' / ('portfolio-' + 'A' * 32 + '.jsonl')
            contents = {folder / 'capture.json': json.dumps(capture), folder / 'engine.log': 'synthetic log',
                        trace: '\n'.join(json.dumps(row) for row in events), trace.with_suffix('.json'): '{}'}
            for path, text in contents.items(): path.write_text(text, encoding='utf-8')
            manifest = {str(path.relative_to(folder)): dict(bytes=path.stat().st_size, sha256=edl.sha(path)) for path in contents}
            (folder / 'provenance.json').write_text(json.dumps(dict(passed=True, files=manifest)), encoding='utf-8')
            with patch.object(edl, 'validate_system_capture') as native:
                read_capture, read_events = edl.load_validated_capture(folder)
                self.assertEqual(read_capture, capture); self.assertEqual(read_events, events)
                native.assert_called_once()
            (folder / 'capture.json').write_text(json.dumps(capture) + '\n', encoding='utf-8')
            with patch.object(edl, 'validate_system_capture') as native, self.assertRaisesRegex(ValueError, 'provenance'):
                edl.load_validated_capture(folder)
            native.assert_not_called()

    def test_cli_exclusive_output_and_preview_disclosure(self):
        capture, events = fixture(sparse=True)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'preview.json'
            argv = ['make_v23_edl.py', '--capture', str(Path(directory) / 'capture'), '--output', str(output), '--preview']
            stdout = io.StringIO()
            with patch('sys.argv', argv), patch('sys.stdout', stdout), patch.object(edl, 'load_validated_capture', return_value=(capture, events)):
                self.assertEqual(edl.main(), 0)
            report = json.loads(stdout.getvalue())
            self.assertTrue(report['previewOnly']); self.assertTrue(report['allNativeFramesRetained'])
            with patch('sys.argv', argv), self.assertRaisesRegex(ValueError, 'already exists'):
                edl.main()

    @unittest.skipUnless(Path('C:/Windows/Fonts/msyh.ttc').is_file(), 'Windows delivery font unavailable')
    def test_every_generated_chapter_renders_inside_actual_caption_rail(self):
        capture, events = fixture()
        art = video.Art(Path('C:/Windows/Fonts'))
        with tempfile.TemporaryDirectory() as directory:
            for row in edl.make_edl(capture, events):
                title, subtitle = video.short_copy(capture, row['start'], row['label'])
                output = Path(directory) / (row['label'] + '.png')
                art.gameplay(title, subtitle, output)
                self.assertTrue(output.is_file())


if __name__ == '__main__':
    unittest.main()
