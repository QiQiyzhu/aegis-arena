"""Synthetic adversarial gate tests; never native playback or listening evidence."""
import array
import copy
import json
import math
from pathlib import Path
import tempfile
import unittest
import wave

import run_v21_av_probe as gate


def valid_report():
    # Explicit fixture facts model the 15 C++ checks, including a final live crossfade.
    phases = [
        ('briefing', False, False, 1, [.38, 0], 1),
        ('briefing', False, False, 1, [.38, 0], 1),
        ('briefing', False, False, 1, [.38, 0], 1),
        ('muted', True, False, 0, [0, 0], 3),
        ('briefing', False, False, 1, [0, .38], 5),
        ('relay1', False, False, 1, [.4, 0], 7),
        ('paused', False, True, 1, [0, .16], 8),
        ('muted', True, True, 0, [0, 0], 8),
        ('paused', False, True, 1, [.16, 0], 8),
        ('relay1', False, False, 1, [0, .4], 9),
        ('briefing', False, False, 1, [.38, 0], 10),
        ('briefing', False, False, 1, [.38, 0], 10),
        ('briefing', False, False, 1, [0, .38], 11),
        ('paused', False, True, 2, [.10, .14], 12),
        ('paused', False, True, 2, [.10, .14], 12),
    ]
    rows = []
    for index, (state, muted, paused, voices, gains, game) in enumerate(phases):
        rows.append(dict(name=gate.ASSERTIONS[index], passed=True, wallSeconds=index + 1,
                         gameSeconds=game, musicState=state, muted=muted, paused=paused,
                         playingVoices=voices, loadedTracks=7, gains=gains))
    return dict(schemaVersion=1, passed=True, presentationVersion='2.1', engine='unreal-runtime',
                soundEnabled=True, syntheticKeyboardInput=True, fixtureDamage=False, fixturePlacement=False,
                humanPlaytest=False, hardwareLoopback=False, quitPath='normal menu X', wallSeconds=16,
                assertions=rows)


class ReportGateTests(unittest.TestCase):
    def setUp(self): self.report = valid_report()

    def rejects(self, mutate):
        report = copy.deepcopy(self.report)
        mutate(report)
        with self.assertRaises(ValueError): gate.validate_report(report)

    def test_valid_native_shape_and_crossfade(self): gate.validate_report(self.report)

    def test_final_crossfade_may_have_settled(self):
        for row in self.report['assertions'][13:]:
            row.update(playingVoices=1, gains=[.16, 0])
        gate.validate_report(self.report)

    def test_scope_boolean_numbers_rejected(self):
        for field in ('soundEnabled', 'syntheticKeyboardInput', 'fixtureDamage', 'fixturePlacement',
                      'humanPlaytest', 'hardwareLoopback'):
            with self.subTest(field=field):
                self.rejects(lambda r: r.update({field: int(r[field])}))

    def test_schema_boolean_rejected(self): self.rejects(lambda r: r.update(schemaVersion=True))
    def test_assertion_numeric_pass_rejected(self):
        self.rejects(lambda r: r['assertions'][3].update(passed=1))

    def test_row_boolean_numbers_rejected(self):
        for field in ('muted', 'paused'):
            with self.subTest(field=field):
                self.rejects(lambda r: r['assertions'][7].update({field: 1}))

    def test_muted_voice_rejected(self):
        self.rejects(lambda r: r['assertions'][3].update(playingVoices=1))
    def test_muted_nonzero_gain_rejected(self):
        self.rejects(lambda r: r['assertions'][7].update(gains=[0, .01]))
    def test_muted_wrong_state_rejected(self):
        self.rejects(lambda r: r['assertions'][7].update(musicState='relay1'))
    def test_restore_silent_rejected(self):
        self.rejects(lambda r: r['assertions'][4].update(playingVoices=0, gains=[0, 0]))
    def test_restore_zero_gain_with_voice_rejected(self):
        self.rejects(lambda r: r['assertions'][4].update(gains=[0, 0]))
    def test_relay_state_wrong_rejected(self):
        self.rejects(lambda r: r['assertions'][5].update(musicState='briefing'))
    def test_paused_state_false_rejected(self):
        self.rejects(lambda r: r['assertions'][6].update(paused=False))
    def test_paused_score_too_loud_rejected(self):
        self.rejects(lambda r: r['assertions'][6].update(gains=[0, .2]))
    def test_paused_clock_advance_rejected(self):
        self.rejects(lambda r: r['assertions'][8].update(gameSeconds=8.002))
    def test_resume_clock_can_advance(self):
        self.report['assertions'][9]['gameSeconds'] = 100
        gate.validate_report(self.report)
    def test_restart_previous_state_rejected(self):
        self.rejects(lambda r: r['assertions'][11].update(musicState='relay1'))
    def test_same_stage_facts_disagree_rejected(self):
        self.rejects(lambda r: r['assertions'][14].update(gains=[.11, .14]))
    def test_final_menu_must_be_paused(self):
        self.rejects(lambda r: r['assertions'][14].update(paused=False))

    def test_invalid_numbers_rejected(self):
        for field, value in (('wallSeconds', True), ('wallSeconds', float('nan')),
                             ('gameSeconds', float('inf')), ('gameSeconds', '8'),
                             ('loadedTracks', 7.0), ('playingVoices', True)):
            with self.subTest(field=field, value=value):
                self.rejects(lambda r: r['assertions'][7].update({field: value}))

    def test_invalid_gain_types_rejected(self):
        for value in ([False, 0], [0, float('nan')], [0, '0'], [0], None):
            with self.subTest(value=value):
                self.rejects(lambda r: r['assertions'][7].update(gains=value))

    def test_bad_order_or_missing_assertion_rejected(self):
        self.rejects(lambda r: r['assertions'].pop())
        self.rejects(lambda r: r['assertions'].reverse())
        self.rejects(lambda r: r.update(assertions=['bad'] * 15))

    def test_report_clock_before_last_check_rejected(self):
        self.rejects(lambda r: r.update(wallSeconds=10))


class ArtifactGateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'av-probe.json').write_text(json.dumps(valid_report()), encoding='utf-8')
        events = ['AEGIS_AV_PROBE_BEGIN keyboard=1 fixtureDamage=0 fixturePlacement=0 soundEnabled=1',
                  *('AEGIS_AV_PROBE_ASSERT_PASS ' + name for name in gate.ASSERTIONS),
                  'AEGIS_AV_PROBE_PASS checks=15']
        self.log = '\n'.join('LogTemp: Display: ' + event for event in events) + '\nLogExit: Exiting.\n'
        (self.root / 'engine.log').write_text(self.log, encoding='utf-8')
        (self.root / 'audio').mkdir()
        for name in ('on', 'muted', 'restored'): self.wav(name, 0 if name == 'muted' else 1500)

    def wav(self, name, amplitude):
        rate = 22050
        samples = array.array('h', (round(amplitude * math.sin(2 * math.pi * 440 * index / rate))
                                   for index in range(rate)))
        with wave.open(str(self.root / 'audio' / f'bgm-{name}.wav'), 'wb') as output:
            output.setnchannels(1); output.setsampwidth(2); output.setframerate(rate)
            output.writeframes(samples.tobytes())

    def test_complete_synthetic_artifact_passes(self):
        self.assertEqual(gate.validate(self.root)['nativeAssertions'], 15)

    def test_exact_device_conversion_pair_is_disclosed(self):
        diagnostics = ('LogAudioMixerWasapi: Warning: Sample rate mismatch. Engine sample rate: 48000 Device sample rate: 44100\n'
                       'LogAudioMixerWasapi: Warning: Device level sample rate conversion will be used.\n')
        (self.root / 'engine.log').write_text(diagnostics + self.log)
        self.assertEqual(len(gate.validate(self.root)['deviceConversionDiagnostics']), 2)

    def test_unpaired_device_conversion_is_rejected(self):
        (self.root / 'engine.log').write_text('LogAudioMixerWasapi: Warning: Device level sample rate conversion will be used.\n' + self.log)
        with self.assertRaises(ValueError): gate.validate(self.root)

    def test_other_audio_warning_is_rejected(self):
        (self.root / 'engine.log').write_text('LogAudioMixerWasapi: Warning: Different failure\n' + self.log)
        with self.assertRaises(ValueError): gate.validate(self.root)

    def test_duplicate_pass_rejected(self):
        (self.root / 'engine.log').write_text(self.log + 'LogTemp: Display: AEGIS_AV_PROBE_PASS checks=15\n')
        with self.assertRaises(ValueError): gate.validate(self.root)

    def test_missing_normal_exit_rejected(self):
        (self.root / 'engine.log').write_text(self.log.replace('LogExit: Exiting.\n', ''))
        with self.assertRaises(ValueError): gate.validate(self.root)

    def test_silent_restoration_rejected(self):
        self.wav('restored', 0)
        with self.assertRaises(ValueError): gate.validate(self.root)

    def test_muted_master_still_loud_rejected(self):
        self.wav('muted', 1500)
        with self.assertRaises(ValueError): gate.validate(self.root)


if __name__ == '__main__': unittest.main()
