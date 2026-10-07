"""Synthetic native-probe evidence attacks; does not execute Unreal."""
from copy import deepcopy
import json
from pathlib import Path
import tempfile
import unittest

import run_v23_probe as gate


def fixture_report():
    state = dict(energy=60, energySpent=0, playerHealth=140, companionHealth=120,
                 paused=False, surveyKeys=0, surveySupplies=0, surveyBoosts=0,
                 surveyCancelled=0, surveyClaimedMask=0, surveyNode=-1, surveyProgress=0,
                 surveyBoostRelayKey=-1, surveyKeyPending=False, supplySelected=False,
                 surveyPlayerActualHealing=0, surveyCompanionActualHealing=0, relayCharge=0)
    times = [.2, .2, .4, .6, .8, 1, 1.2, 2, 2.2, 2.6, 2.6,
             3.2, 3.8, 6.8, 7.3, 7.5, 7.7, 8, 11, 11, 11.4, 11.8, 13.5, 18, 18.2]
    changes = {
        5: dict(supplySelected=True), 7: dict(supplySelected=False, surveyNode=0, surveyProgress=.2),
        8: dict(surveyNode=-1, surveyProgress=0, surveyCancelled=1),
        9: dict(paused=True, surveyCancelled=2),
        11: dict(paused=False, surveyCancelled=3, playerHealth=135),
        12: dict(surveyCancelled=4), 13: dict(surveyKeys=1, surveyClaimedMask=1, surveyKeyPending=True),
        17: dict(supplySelected=True, playerHealth=105, companionHealth=95),
        18: dict(playerHealth=125, companionHealth=110, surveyPlayerActualHealing=20,
                 surveyCompanionActualHealing=15, surveySupplies=1, surveyClaimedMask=3),
        21: dict(surveyKeyPending=False, surveyBoostRelayKey=10, surveyBoosts=1, relayCharge=.5),
        22: dict(relayCharge=2.625), 23: dict(relayCharge=8, surveyBoostRelayKey=-1),
        24: dict(paused=True),
    }
    rows = []
    for index, name in enumerate(gate.ASSERTIONS):
        state.update(changes.get(index, {}))
        rows.append(dict(state, name=name, passed=True, gameSeconds=times[index],
                         wallSeconds=times[index] + (.6 if index >= 10 else 0)))
    interventions = [dict(type='fixture_placement', actor='AegisPlayerCharacter_0', location=[0, 0, 100])
                     for _ in range(11)]
    interventions += [dict(type='fixture_damage', actor='AegisPlayerCharacter_0' if amount != 25 else 'AegisAICharacter_0',
                           source='AegisAICharacter_1', requested=amount, applied=amount) for amount in (5, 30, 25)]
    report = dict(schemaVersion=1, passed=True, engine='unreal-runtime', syntheticKeyboardInput=True,
                  fixtureDamage=True, fixturePlacement=True, aiFrozen=True, humanPlaytest=False,
                  policyPerformanceTest=False, renderOffscreen=True, nullRHI=False, reason=gate.REASON,
                  quitPath='normal PlayerController X from paused menu', notCovered=gate.NOT_COVERED,
                  frozenControllerCount=3, wallSeconds=20, lastStage=29, assertions=rows, interventions=interventions)
    log = '\n'.join('LogTemp: Display: ' + item for item in
                    [gate.BEGIN, *('AEGIS_V23_PROBE_ASSERT_PASS ' + name for name in gate.ASSERTIONS), gate.PASS])
    return report, log + '\nLogExit: Exiting.'


class V23ProbeGateTests(unittest.TestCase):
    def setUp(self):
        self.report, self.log = fixture_report()

    def test_consistent_disclosed_fixture_report_passes(self):
        gate.validate_report(self.report, self.log)

    def test_positive_marker_cannot_hide_numeric_failure(self):
        for index, key, value in ((18, 'playerHealth', 140), (19, 'energy', 61),
                                  (9, 'surveyCancelled', 0), (21, 'surveyKeyPending', True)):
            changed = deepcopy(self.report)
            changed['assertions'][index][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                gate.validate_report(changed, self.log)

    def test_report_needs_exact_assertion_order_and_original_markers(self):
        changed = deepcopy(self.report)
        changed['assertions'][0], changed['assertions'][1] = changed['assertions'][1], changed['assertions'][0]
        with self.assertRaises(ValueError): gate.validate_report(changed, self.log)
        with self.assertRaises(ValueError): gate.validate_report(self.report, self.log + '\nLogTemp: Display: ' + gate.PASS)
        with self.assertRaises(ValueError): gate.validate_report(self.report, self.log.replace(gate.BEGIN, ''))

    def test_actual_speedup_and_paused_clock_are_checked(self):
        changed = deepcopy(self.report)
        changed['assertions'][22]['relayCharge'] = 2.2
        with self.assertRaisesRegex(ValueError, 'multiplier'): gate.validate_report(changed, self.log)
        changed = deepcopy(self.report)
        changed['assertions'][10]['gameSeconds'] += .2
        with self.assertRaisesRegex(ValueError, 'paused'): gate.validate_report(changed, self.log)

    def test_fixture_disclosure_cannot_be_removed(self):
        for key in ('fixtureDamage', 'fixturePlacement', 'aiFrozen'):
            changed = deepcopy(self.report); changed[key] = False
            with self.subTest(key=key), self.assertRaises(ValueError): gate.validate_report(changed, self.log)
        changed = deepcopy(self.report); changed['interventions'].pop()
        with self.assertRaises(ValueError): gate.validate_report(changed, self.log)

    def write_session(self, output):
        system = output / 'system'; system.mkdir()
        run_id = 'A' * 32; trace = system / ('portfolio-' + run_id + '.jsonl')
        self.report['portfolioTracePath'] = str(trace)
        summary = dict(mode='aegis-prism-fall-v2.0', gameplayVersion='2.3', completed=False,
            ledgerBalanced=True, traceComplete=True, outcome='aborted', runId=run_id, tracePath=str(trace),
            energyInitial=60, energyFinal=60, energyEarned=0, energySpent=0, energyOverflow=0,
            pulsesUsed=0, repairsUsed=0, enemiesRewarded=0, stagesRewarded=0,
            repairPlayerActualHealing=0, repairCompanionActualHealing=0, alliedActualDamageDealt=0,
            playerActualDamageTaken=35, companionActualDamageTaken=25, chargedShots=0, overclocks=0,
            surveyKeys=1, surveySupplies=1, surveyBoosts=1, surveyCancelled=4,
            surveyPlayerActualHealing=20, surveyCompanionActualHealing=15, surveyClaimedMask=3,
            surveyBoostRelayKey=-1, surveyKeyPending=False)
        events = []
        def add(kind, stamp, **data):
            events.append(dict(runId=run_id, sequence=len(events) + 1, event=kind,
                               gameSeconds=stamp, energy=60, data=data))
        def start(stamp, node=0, supply=False):
            position = [-1050, -900, 8] if node == 0 else [1050, 650, 8]
            add('survey_started', stamp, node=node, supply=supply, distance=100,
                playerPosition=[position[0] + 100, position[1], 90], nodePosition=position)
        def cancel(stamp, reason, progress):
            add('survey_cancelled', stamp, node=0, progressSeconds=progress, reason=reason)
        def wound(stamp, victim, amount, health):
            add('damage', stamp, victim=victim, actualDamage=amount, victimHealthAfter=health, enemyVictim=False)
        add('session_started', 0, gameplayVersion='2.3', surveySeconds=2.5, surveyRadius=180,
            surveyPlayerHeal=20, surveyCompanionHeal=15, surveyRelayMultiplier=1.25)
        add('phase_changed', .5, phase='active', wave=1)
        add('survey_mode_selected', 1, supply=True); add('survey_mode_selected', 1.4, supply=False)
        start(1.5); cancel(2.2, 'released', .7)
        start(2.3); cancel(2.6, 'unavailable', .3)
        start(2.8); wound(3, 'AegisPlayerCharacter_0', 5, 135); cancel(3, 'damaged', .2)
        start(3.3); cancel(3.8, 'left_range', .5)
        start(4.3)
        add('survey_claimed', 6.8, node=0, supply=False, distance=100,
            playerPosition=[-950, -900, 90], nodePosition=[-1050, -900, 8], elapsedGameSeconds=2.5,
            scanSeconds=2.5, playerActualHealing=0, companionActualHealing=0, claimedMask=1, keyPending=True)
        add('survey_mode_selected', 7.9, supply=True)
        wound(7.9, 'AegisPlayerCharacter_0', 30, 105); wound(7.9, 'AegisAICharacter_0', 25, 95)
        start(8.5, 1, True)
        add('survey_claimed', 11, node=1, supply=True, distance=100,
            playerPosition=[1150, 650, 90], nodePosition=[1050, 650, 8], elapsedGameSeconds=2.5,
            scanSeconds=2.5, playerActualHealing=20, companionActualHealing=15, claimedMask=3, keyPending=True)
        add('survey_boost_bound', 11.4, relayKey=10, multiplier=1.25)
        add('survey_boost_finished', 17.8, relayKey=10, reason='relay_complete')
        add('session_finished', 18.2, **summary)
        summary['eventCount'] = len(events)
        def save():
            trace.write_text('\n'.join(json.dumps(row) for row in events), encoding='utf-8')
            trace.with_suffix('.json').write_text(json.dumps(summary), encoding='utf-8')
        save()
        return summary, events, save

    def test_original_session_matches_disclosed_damage_healing_and_zero_energy_cost(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory).resolve(); self.write_session(output)
            result = gate.validate_session(self.report, output)
            self.assertEqual(result['survey']['surveyPlayerActualHealing'], 20)
            self.assertEqual(result['survey']['surveyBoosts'], 1)

    def test_session_rejects_changed_healing_or_hidden_energy(self):
        for attack in ('healing', 'energy', 'cancel'):
            with self.subTest(attack=attack), tempfile.TemporaryDirectory() as directory:
                output = Path(directory).resolve(); summary, events, save = self.write_session(output)
                if attack == 'healing': summary['surveyPlayerActualHealing'] = 19
                elif attack == 'energy': events[3]['energy'] = 61
                else: next(row for row in events if row['event'] == 'survey_cancelled')['data']['reason'] = 'released_wrong'
                save()
                with self.assertRaises(ValueError): gate.validate_session(self.report, output)

    def test_session_cannot_substitute_another_trace(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory).resolve(); self.write_session(output)
            extra = output / 'system' / ('portfolio-' + 'B' * 32 + '.jsonl')
            extra.write_text('{}', encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'extra'): gate.validate_session(self.report, output)

    def test_editor_command_explicitly_discloses_input_fixture_and_offline_mode(self):
        command = gate.build_command('UnrealEditor.exe', 'new-output', True)
        for flag in ('-game', '-AegisV23Probe', '-AegisInputProbe', '-RenderOffscreen', '-AegisV23', gate.OFFLINE_ARGUMENT):
            self.assertIn(flag, command)
        self.assertNotIn('-NullRHI', command)
        self.assertNotIn('-AegisPortfolioCapture', command)


if __name__ == '__main__':
    unittest.main()
