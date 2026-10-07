"""Offline adversarial checks for the capture result-page X acceptance gate.

Every capture and log here is synthetic in-memory test evidence. These tests do
not run Unreal, touch recorded captures, or alter the original 25-check probe.
"""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_portfolio_capture as gate


PREFIX = '[2026.09.18-01.00.00:000][ 12]'
COMPLETE = 'AEGIS_PORTFOLIO_CAPTURE_COMPLETE valid=1 frames=150 completed=150'
EXIT = PREFIX + 'LogExit: Exiting.'


def native(marker):
    return PREFIX + 'LogTemp: Display: ' + marker


def make_evidence(outcome='won'):
    """Keep the fixture independent of gate constants and native output files."""
    capture = dict(outcome=outcome,
                   quitPath='normal PlayerController X from result page',
                   quitKey='X', quitMenuOpen=False, quitPaused=False,
                   resultHoldSeconds=4.25,
                   samples=[{'phase': 1}, {'phase': 3 if outcome == 'won' else 4}])
    lines = [native(COMPLETE),
             native('AEGIS_PORTFOLIO_CAPTURE_RESULT_X outcome=' + outcome),
             native('AEGIS_RESULT_X_QUIT outcome=' + outcome),
             EXIT]
    return capture, '\n'.join(lines) + '\n'


class PortfolioResultQuitGateTests(unittest.TestCase):
    def setUp(self):
        self.capture, self.log = make_evidence()

    def reject(self, capture=None, log=None):
        with self.assertRaises(ValueError):
            gate.validate_result_quit(self.capture if capture is None else capture,
                                      self.log if log is None else log)

    def test_both_natural_outcomes_return_normal_x_exit_evidence(self):
        for outcome in ('won', 'lost'):
            with self.subTest(outcome=outcome):
                capture, log = make_evidence(outcome)
                evidence = gate.validate_result_quit(capture, log)
                self.assertIsInstance(evidence, dict)
                self.assertEqual(evidence['outcome'], outcome)
                self.assertEqual(evidence['key'], 'X')
                self.assertIs(evidence['normalExit'], True)

    def test_hold_bounds_are_accepted(self):
        for value in (4, 215):
            with self.subTest(value=value):
                self.capture['resultHoldSeconds'] = value
                gate.validate_result_quit(self.capture, self.log)

    def test_unprefixed_native_lines_are_accepted(self):
        gate.validate_result_quit(self.capture, self.log.replace(PREFIX, ''))

    def test_validation_does_not_mutate_capture(self):
        before = copy.deepcopy(self.capture)
        gate.validate_result_quit(self.capture, self.log)
        self.assertEqual(self.capture, before)

    def test_zero_frame_completion_does_not_require_capture_frame_fields(self):
        # The outer wrapper reconciles the marker against capture frame fields;
        # the result-page helper also supports successful no-frames captures.
        log = self.log.replace('frames=150 completed=150', 'frames=0 completed=0')
        gate.validate_result_quit(self.capture, log)

    def test_every_required_capture_field_must_exist(self):
        for key in self.capture:
            with self.subTest(key=key):
                capture = copy.deepcopy(self.capture)
                del capture[key]
                self.reject(capture=capture)

    def test_unknown_or_malformed_capture_outcome_is_rejected(self):
        for outcome in ('aborted', 'restarted', 'Won', '', None, True, 3, []):
            with self.subTest(outcome=outcome):
                capture = copy.deepcopy(self.capture)
                capture['outcome'] = outcome
                self.reject(capture=capture)

    def test_quit_path_and_key_must_describe_result_page_x(self):
        for key, value in (
                ('quitPath', 'normal PlayerController X from paused menu'),
                ('quitPath', 'RequestExit'), ('quitPath', ''), ('quitPath', None),
                ('quitKey', 'Escape'), ('quitKey', 'x'), ('quitKey', ''),
                ('quitKey', None), ('quitKey', True)):
            with self.subTest(key=key, value=value):
                capture = copy.deepcopy(self.capture)
                capture[key] = value
                self.reject(capture=capture)

    def test_menu_and_pause_require_literal_false(self):
        for key in ('quitMenuOpen', 'quitPaused'):
            for value in (True, 0, 1, 0.0, 'false', '', None, []):
                with self.subTest(key=key, value=value):
                    capture = copy.deepcopy(self.capture)
                    capture[key] = value
                    self.reject(capture=capture)

    def test_hold_rejects_boolean_nonfinite_missing_and_out_of_range_values(self):
        for value in (True, False, None, '4', float('nan'), float('inf'),
                      float('-inf'), -1, 0, 3.999, 215.001):
            with self.subTest(value=value):
                capture = copy.deepcopy(self.capture)
                capture['resultHoldSeconds'] = value
                self.reject(capture=capture)

    def test_samples_require_nonempty_list_and_final_object(self):
        for samples in (None, [], {}, 'phase=3', ({'phase': 3},), [None], [3], [[]]):
            with self.subTest(samples=samples):
                capture = copy.deepcopy(self.capture)
                capture['samples'] = samples
                self.reject(capture=capture)

    def test_final_sample_phase_matches_each_outcome(self):
        for outcome in ('won', 'lost'):
            for phase in (0, 1, 2, 4 if outcome == 'won' else 3, None, True,
                          False, '3' if outcome == 'won' else '4', float('nan'),
                          float('inf'), 3.5, 3.0 if outcome == 'won' else 4.0):
                with self.subTest(outcome=outcome, phase=phase):
                    capture, log = make_evidence(outcome)
                    capture['samples'][-1]['phase'] = phase
                    self.reject(capture=capture, log=log)
        self.capture['samples'][-1] = {}
        self.reject()

    def test_earlier_result_sample_cannot_replace_final_result_sample(self):
        self.capture['samples'] = [{'phase': 3}, {'phase': 1}]
        self.reject()

    def test_each_native_marker_and_normal_exit_is_required(self):
        lines = self.log.splitlines()
        for index in range(len(lines)):
            with self.subTest(index=index):
                self.reject(log='\n'.join(lines[:index] + lines[index + 1:]))

    def test_each_native_marker_and_normal_exit_must_be_unique(self):
        for line in self.log.splitlines():
            with self.subTest(line=line):
                self.reject(log=self.log + line + '\n')

    def test_malformed_or_conflicting_extra_native_markers_are_rejected(self):
        for marker in ('AEGIS_PORTFOLIO_CAPTURE_COMPLETE valid=0 frames=150 completed=150',
                       'AEGIS_PORTFOLIO_CAPTURE_COMPLETE invalid',
                       'AEGIS_PORTFOLIO_CAPTURE_RESULT_X outcome=lost',
                       'AEGIS_PORTFOLIO_CAPTURE_RESULT_X',
                       'AEGIS_RESULT_X_QUIT outcome=lost',
                       'AEGIS_RESULT_X_QUIT outcome=won extra=1'):
            with self.subTest(marker=marker):
                self.reject(log=self.log + native(marker) + '\n')

    def test_marker_order_must_match_complete_capture_x_native_x_then_exit(self):
        lines = self.log.splitlines()
        for first, second in ((0, 1), (1, 2), (2, 3), (0, 3)):
            with self.subTest(first=first, second=second):
                reordered = list(lines)
                reordered[first], reordered[second] = reordered[second], reordered[first]
                self.reject(log='\n'.join(reordered))

    def test_both_result_markers_must_match_capture_outcome(self):
        for outcome in ('won', 'lost'):
            capture, log = make_evidence(outcome)
            other = 'lost' if outcome == 'won' else 'won'
            for marker in ('AEGIS_PORTFOLIO_CAPTURE_RESULT_X', 'AEGIS_RESULT_X_QUIT'):
                for invalid in (other, 'aborted', 'Won', '', '1'):
                    with self.subTest(outcome=outcome, marker=marker, invalid=invalid):
                        self.reject(capture=capture, log=log.replace(
                            marker + ' outcome=' + outcome, marker + ' outcome=' + invalid))

    def test_successful_completion_marker_requires_exact_syntax(self):
        for replacement in (
                'AEGIS_PORTFOLIO_CAPTURE_COMPLETE valid=0 frames=150 completed=150',
                'AEGIS_PORTFOLIO_CAPTURE_COMPLETE valid=true frames=150 completed=150',
                'AEGIS_PORTFOLIO_CAPTURE_COMPLETE valid=1 frames=-1 completed=150',
                'AEGIS_PORTFOLIO_CAPTURE_COMPLETE valid=1 frames=150.0 completed=150',
                'AEGIS_PORTFOLIO_CAPTURE_COMPLETE valid=1 frames=True completed=150',
                'AEGIS_PORTFOLIO_CAPTURE_COMPLETE valid=1 frames=150 completed=NaN',
                'AEGIS_PORTFOLIO_CAPTURE_COMPLETE valid=1 frames=150',
                COMPLETE + ' replay=1', COMPLETE + ' ', 'echo ' + COMPLETE):
            with self.subTest(replacement=replacement):
                self.reject(log=self.log.replace(COMPLETE, replacement))

    def test_result_markers_require_exact_syntax(self):
        for marker in ('AEGIS_PORTFOLIO_CAPTURE_RESULT_X', 'AEGIS_RESULT_X_QUIT'):
            original = marker + ' outcome=won'
            for replacement in (marker, marker + ' outcome =won',
                                original + ' extra=1', original + ' ', 'echo ' + original):
                with self.subTest(replacement=replacement):
                    self.reject(log=self.log.replace(original, replacement))

    def test_markers_require_direct_native_display_category(self):
        for category in ('LogAutomationController: Display: [log] ',
                         'LogConsoleManager: Display: echo ',
                         'LogTemp: Warning: ', 'LogTemp: Error: '):
            with self.subTest(category=category):
                self.reject(log=self.log.replace('LogTemp: Display: ', category))

    def test_prefixed_replays_cannot_substitute_for_direct_native_lines(self):
        lines = self.log.splitlines()
        for index in range(len(lines)):
            for prefix in ('echo ', 'LogTemp: Display: ',
                           'LogAutomationController: Display: replay ', 'some text '):
                with self.subTest(index=index, prefix=prefix):
                    replayed = list(lines)
                    replayed[index] = prefix + replayed[index]
                    self.reject(log='\n'.join(replayed))

    def test_prefixed_replays_are_ignored_when_direct_evidence_is_present(self):
        replays = '\n'.join('echo ' + line for line in self.log.splitlines())
        gate.validate_result_quit(self.capture, replays + '\n' + self.log)

    def test_normal_exit_requires_exact_direct_message(self):
        for replacement in (PREFIX + 'LogExit: Exiting',
                            PREFIX + 'LogExit: Exiting. extra',
                            PREFIX + 'LogExit: Warning: Exiting.',
                            native('LogExit: Exiting.')):
            with self.subTest(replacement=replacement):
                self.reject(log=self.log.replace(EXIT, replacement))


if __name__ == '__main__':
    unittest.main()
