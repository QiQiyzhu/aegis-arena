"""Synthetic chronological evidence for the survey gate; not native playtest results."""
from copy import deepcopy
import unittest

from v23_survey_gate import SurveyLedger


def rules(**changes):
    data = dict(gameplayVersion='2.3', surveySeconds=2.5, surveyRadius=180,
                surveyPlayerHeal=20, surveyCompanionHeal=15, surveyRelayMultiplier=1.25)
    data.update(changes)
    return data


def located(**changes):
    node = changes.get('node', 0)
    position = [-1050, -900, 8] if node == 0 else [1050, 650, 8]
    data = dict(node=0, supply=False, distance=100,
                playerPosition=[position[0] + 60, position[1] - 80, 90], nodePosition=position)
    data.update(changes)
    return data


def reward(**changes):
    return located(**dict(dict(elapsedGameSeconds=2.5, scanSeconds=2.5,
                              playerActualHealing=0, companionActualHealing=0,
                              claimedMask=1, keyPending=True), **changes))


def event(kind, data, stamp, phase='active', wave=1):
    return (kind, data, stamp, phase, wave)


def opening():
    return [event('session_started', rules(), 0, phase='briefing')]


def key_run():
    return opening() + [
        event('survey_started', located(), 5),
        event('survey_claimed', reward(), 7.5),
        event('survey_boost_bound', dict(relayKey=10, multiplier=1.25), 8),
        event('survey_boost_finished', dict(relayKey=10, reason='relay_complete'),
              12, phase='intermission'),
    ]


def summary(**changes):
    data = dict(gameplayVersion='2.3', surveyKeys=0, surveySupplies=0, surveyBoosts=0,
                surveyCancelled=0, surveyPlayerActualHealing=0,
                surveyCompanionActualHealing=0, surveyClaimedMask=0,
                surveyKeyPending=False, surveyBoostRelayKey=-1)
    data.update(changes)
    return data


def sample(result):
    return {key: result[key] for key in
            ('surveyClaimedMask', 'surveyKeys', 'surveySupplies', 'surveyBoosts')}


class SurveyGateTests(unittest.TestCase):
    def replay(self, events):
        ledger = SurveyLedger()
        for item in events:
            ledger.observe(*item)
        return ledger

    def reject(self, events, pattern=None):
        if pattern:
            with self.assertRaisesRegex(ValueError, pattern):
                self.replay(events)
        else:
            with self.assertRaises(ValueError):
                self.replay(events)

    def test_key_binding_and_phase_transition_finish_reconcile(self):
        expected = summary(surveyKeys=1, surveyBoosts=1, surveyClaimedMask=1)
        actual = self.replay(key_run()).finish(expected, sample(expected))
        self.assertEqual(actual['surveyKeys'], 1)
        self.assertEqual(actual['surveyBoosts'], 1)
        self.assertFalse(actual['surveyKeyPending'])

    def test_supply_records_only_actual_healing_and_consumes_one_cache(self):
        events = opening() + [
            event('survey_mode_selected', dict(supply=True), 4),
            event('survey_started', located(node=1, supply=True), 5),
            event('survey_claimed', reward(node=1, supply=True, claimedMask=2,
                  keyPending=False, playerActualHealing=12.25, companionActualHealing=4.5), 7.5),
        ]
        expected = summary(surveySupplies=1, surveyClaimedMask=2,
                           surveyPlayerActualHealing=12.25, surveyCompanionActualHealing=4.5)
        self.assertEqual(self.replay(events).finish(expected, sample(expected))['surveySupplies'], 1)

    def test_empty_or_excessive_supply_is_rejected(self):
        for player, companion in ((0, 0), (21, 0), (0, 16), (-1, 2)):
            with self.subTest(player=player, companion=companion):
                self.reject(opening() + [
                    event('survey_mode_selected', dict(supply=True), 4),
                    event('survey_started', located(supply=True), 5),
                    event('survey_claimed', reward(supply=True, keyPending=False,
                          playerActualHealing=player, companionActualHealing=companion), 7.5),
                ])

    def test_claimed_cache_cannot_be_scanned_or_claimed_again(self):
        self.reject(key_run() + [event('survey_started', located(), 14)], 'Repeated')
        self.reject(key_run() + [event('survey_claimed', reward(), 14)], 'unique scan')

    def test_overlapping_scans_are_rejected(self):
        self.reject(opening() + [event('survey_started', located(), 5),
                    event('survey_started', located(node=1), 6)], 'overlapping')

    def test_premature_or_forged_elapsed_scan_is_rejected(self):
        for claimed, elapsed in ((7.3, 2.3), (7, 2.5), (7.5, 3.5)):
            with self.subTest(claimed=claimed, elapsed=elapsed):
                self.reject(opening() + [event('survey_started', located(), 5),
                            event('survey_claimed', reward(elapsedGameSeconds=elapsed), claimed)])
        self.reject(opening() + [event('survey_started', located(), 5),
                    event('survey_claimed', reward(scanSeconds=2), 7.5)], 'required scan')

    def test_unselected_reward_mode_or_mid_scan_switch_is_rejected(self):
        self.reject(opening() + [event('survey_started', located(supply=True), 5)], 'not selected')
        self.reject(opening() + [event('survey_started', located(), 5),
                    event('survey_claimed', reward(supply=True), 7.5)], 'choice changed')
        self.reject(opening() + [event('survey_started', located(), 5),
                    event('survey_mode_selected', dict(supply=True), 6)], 'mode change')
        self.reject(opening() + [event('survey_mode_selected', dict(supply=False), 4)], 'mode change')

    def test_distance_is_recomputed_from_both_positions(self):
        for mutation in (dict(distance=99), dict(playerPosition=[1000, -25, 90]),
                         dict(nodePosition=[40, -55, 0]), dict(distance=181),
                         dict(playerPosition=[0, 0]), dict(nodePosition=[True, 55, 0])):
            with self.subTest(mutation=mutation):
                self.reject(opening() + [event('survey_started', located(**mutation), 5)])
                self.reject(opening() + [event('survey_started', located(), 5),
                            event('survey_claimed', reward(**mutation), 7.5)])

    def test_pending_key_cannot_stack_and_wave_three_rejects_keys(self):
        self.reject(key_run()[:3] + [event('survey_started', located(node=1), 9)], 'key unavailable')
        self.reject(opening() + [event('survey_started', located(), 5, wave=3)], 'key unavailable')
        self.reject(opening() + [event('survey_started', located(), 5, wave=2),
                    event('survey_claimed', reward(), 7.5, wave=3)], 'key reward')

    def test_active_boost_can_reserve_one_key_for_a_later_relay(self):
        events = key_run()[:4] + [
            event('survey_started', located(node=1), 9),
            event('survey_claimed', reward(node=1, claimedMask=3), 11.5),
            event('survey_boost_finished', dict(relayKey=10, reason='relay_complete'), 12, phase='intermission'),
            event('survey_boost_bound', dict(relayKey=20, multiplier=1.25), 13, wave=2),
            event('survey_boost_finished', dict(relayKey=20, reason='relay_complete'), 20, wave=2),
        ]
        expected = summary(surveyKeys=2, surveyBoosts=2, surveyClaimedMask=3)
        self.assertEqual(self.replay(events).finish(expected, sample(expected))['surveyBoosts'], 2)

    def test_second_key_can_bind_a_different_later_relay(self):
        events = key_run() + [
            event('survey_started', located(node=1), 14, wave=2),
            event('survey_claimed', reward(node=1, claimedMask=3), 16.5, wave=2),
            event('survey_boost_bound', dict(relayKey=21, multiplier=1.25), 17, wave=2),
            event('survey_boost_finished', dict(relayKey=21, reason='relay_changed'), 18, wave=2),
        ]
        expected = summary(surveyKeys=2, surveyBoosts=2, surveyClaimedMask=3)
        self.assertEqual(self.replay(events).finish(expected, sample(expected))['surveyBoosts'], 2)

    def test_binding_requires_key_correct_wave_multiplier_and_unused_relay(self):
        self.reject(opening() + [event('survey_boost_bound', dict(relayKey=10, multiplier=1.25), 5)], 'Unbacked')
        for key, multiplier, wave in ((11, 1.25, 1), (20, 1.25, 1), (10, 2, 1)):
            with self.subTest(key=key, multiplier=multiplier):
                self.reject(key_run()[:3] + [event('survey_boost_bound',
                            dict(relayKey=key, multiplier=multiplier), 8, wave=wave)])
        self.reject(key_run() + [event('survey_started', located(node=1), 14),
                    event('survey_claimed', reward(node=1, claimedMask=3), 16.5),
                    event('survey_boost_bound', dict(relayKey=10, multiplier=1.25), 17)], 'repeated')

    def test_unknown_events_or_undeclared_version_are_rejected(self):
        self.reject(opening() + [event('survey_magic_reward', {}, 5)], 'Unknown survey event')
        for declaration in ({}, rules(gameplayVersion='2.2'), rules(gameplayVersion='2.4')):
            with self.subTest(declaration=declaration):
                self.reject([event('session_started', declaration, 0, phase='briefing'),
                             event('survey_started', located(), 5)], 'undeclared')
        self.reject([event('survey_started', located(), 5)], 'undeclared')

    def test_rule_tampering_or_non_finite_values_are_rejected(self):
        for key in ('surveySeconds', 'surveyRadius', 'surveyPlayerHeal',
                    'surveyCompanionHeal', 'surveyRelayMultiplier'):
            with self.subTest(key=key):
                self.reject([event('session_started', rules(**{key: 99}), 0)])
        for value in (True, float('nan'), float('inf'), '100'):
            with self.subTest(value=value):
                self.reject(opening() + [event('survey_started', located(distance=value), 5)])

    def test_nonactive_actions_are_rejected(self):
        for phase in ('briefing', 'intermission', 'won', 'lost'):
            with self.subTest(phase=phase):
                self.reject(opening() + [event('survey_started', located(), 5, phase=phase)], 'outside active')

    def test_cancelled_scan_can_retry_same_cache_and_reconcile(self):
        events = opening() + [
            event('survey_started', located(), 5),
            event('damage', dict(victim='AegisPlayerCharacter_0', actualDamage=5), 6),
            event('survey_cancelled', dict(node=0, progressSeconds=1, reason='damaged'), 6),
            event('survey_started', located(), 7),
            event('survey_claimed', reward(), 9.5),
        ]
        expected = summary(surveyKeys=1, surveyCancelled=1, surveyClaimedMask=1, surveyKeyPending=True)
        result = self.replay(events).finish(expected, sample(expected))
        self.assertEqual(result['surveyCancelled'], 1)
        self.assertTrue(result['surveyKeyPending'])

    def test_omitted_damage_or_phase_cancellation_cannot_grant_reward(self):
        interruptions = [
            [event('damage', dict(victim='AegisPlayerCharacter_0', actualDamage=5), 6)],
            [event('phase_changed', dict(phase='intermission', wave=1), 6, phase='intermission'),
             event('phase_changed', dict(phase='active', wave=2), 7, wave=2)],
        ]
        for interruption in interruptions:
            with self.subTest(interruption=interruption):
                self.reject(opening() + [event('survey_started', located(), 5)] + interruption +
                            [event('survey_claimed', reward(), 7.5)], 'Interrupted')

    def test_cache_cannot_move_during_scan_between_retries_or_relative_to_other_cache(self):
        def shift(data):
            data = deepcopy(data)
            data['nodePosition'][0] += 100
            data['playerPosition'][0] += 100
            return data
        self.reject(opening() + [event('survey_started', located(), 5),
                    event('survey_claimed', shift(reward()), 7.5)], 'moved during')
        self.reject(opening() + [event('survey_started', located(), 5),
                    event('survey_cancelled', dict(node=0, progressSeconds=1, reason='released'), 6),
                    event('survey_started', shift(located()), 7)], 'between attempts')
        self.reject(key_run() + [event('survey_started', shift(located(node=1)), 14)], 'map positions')

    def test_cancellation_cannot_create_progress_or_a_reward(self):
        self.reject(opening() + [event('survey_cancelled', dict(node=0, progressSeconds=0, reason='released'), 5)],
                    'without a scan')
        for mutation in (dict(node=1), dict(progressSeconds=2), dict(reason='invented')):
            data = dict(node=0, progressSeconds=1, reason='released')
            data.update(mutation)
            with self.subTest(mutation=mutation):
                self.reject(opening() + [event('survey_started', located(), 5),
                            event('survey_cancelled', data, 6)])
        self.reject(opening() + [event('survey_started', located(), 5),
                    event('survey_cancelled', dict(node=0, progressSeconds=1, reason='released'), 6),
                    event('survey_claimed', reward(), 7.5)], 'unique scan')

    def test_open_scan_or_boost_cannot_finish_session(self):
        for events in (key_run()[:2], key_run()[:4]):
            with self.subTest(events=len(events)), self.assertRaisesRegex(ValueError, 'left open'):
                self.replay(events).finish(summary(), sample(summary()))

    def test_summary_and_final_sample_tampering_are_rejected(self):
        expected = summary(surveyKeys=1, surveyBoosts=1, surveyClaimedMask=1)
        for key, value in expected.items():
            changed = deepcopy(expected)
            changed[key] = ('2.2' if type(value) is str else not value if type(value) is bool else value + 1)
            with self.subTest(summary=key), self.assertRaises(ValueError):
                self.replay(key_run()).finish(changed, sample(expected))
        for key in sample(expected):
            changed = sample(expected)
            changed[key] += 1
            with self.subTest(sample=key), self.assertRaises(ValueError):
                self.replay(key_run()).finish(expected, changed)

    def test_summary_counters_cannot_hide_fractional_tampering_in_float_tolerance(self):
        expected = summary(surveyKeys=1, surveyBoosts=1, surveyClaimedMask=1)
        for key in ('surveyKeys', 'surveySupplies', 'surveyBoosts', 'surveyCancelled',
                    'surveyClaimedMask', 'surveyBoostRelayKey'):
            changed = dict(expected, **{key: expected[key] + .01})
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.replay(key_run()).finish(changed, sample(expected))

    def test_previous_version_without_survey_still_passes_through(self):
        ledger = SurveyLedger()
        self.assertFalse(ledger.observe('session_started', {}, 0, 'briefing', 0))
        self.assertFalse(ledger.observe('energy_transaction', {}, 1, 'active', 1))
        self.assertIsNone(ledger.finish({}, {}))


if __name__ == '__main__':
    unittest.main()
