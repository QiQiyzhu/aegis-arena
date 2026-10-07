"""Offline adversarial gates with explicitly synthetic evidence; these are not native gameplay results."""
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import run_v2_capture as gate


class V2Gate(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.out = Path(self.temp.name).resolve()
        (self.out / 'system').mkdir(); (self.out / 'frames').mkdir()
        self.run = 'A' * 32
        self.report = self.out / 'system' / ('portfolio-' + self.run + '.json')
        self.trace = self.report.with_suffix('.jsonl')
        self.events = []
        self.balance = 60
        self.earned = self.spent = self.overflow = 0
        def event(kind, data, stamp):
            row = dict(runId=self.run, sequence=len(self.events)+1, event=kind,
                       gameSeconds=stamp, energy=self.balance, data=data)
            self.events.append(row)
            return row
        def tx(reason, offered, stamp):
            before = self.balance
            actual = min(offered, 100 - before) if offered > 0 else offered
            self.balance += actual
            self.earned += max(actual, 0); self.spent += max(-actual, 0)
            self.overflow += max(0, offered - actual)
            event('energy_transaction', dict(reason=reason, before=before, offeredDelta=offered,
                  actualDelta=actual, after=self.balance, overflowTotal=self.overflow), stamp)
        self.event, self.tx = event, tx
        event('session_started', dict(mode=gate.MODE, initialEnergy=60, maximumEnergy=100,
              pulseCost=35, repairCost=40, killReward=12, stageReward=25, repairCooldownSeconds=8,
              chargedShotCost=12, overclockCost=35, overclockDuration=6,
              repairPricing='ceil(actualHealing*0.8), minimum8 maximum40; zero-heal rejected',
              relayBaseSeconds='8 / 10+10 / 4'), 0)
        event('route_selected', dict(northFirst=True), 1)
        event('phase_changed', dict(phase='active', wave=1), 4)
        tx('charged_shot', -12, 5)
        event('damage', dict(actualDamage=70, victimHealthAfter=0, victimRunIdentity=1,
              victim='Enemy_1', enemyVictim=True), 6)
        tx('enemy_defeated', 12, 6)
        tx('overclock', -35, 7)
        event('overclock_activated', dict(relayKey=10, endsAtGameSeconds=13, contestedAtPurchase=False), 7)
        event('phase_changed', dict(phase='intermission', wave=1), 10)
        tx('stage_1_complete', 25, 10)
        event('upgrade_selected', dict(upgradeIndex=3, upgrade='RELAY SYMBIOSIS'), 10)
        event('phase_changed', dict(phase='active', wave=2), 11)
        event('damage', dict(actualDamage=14, victimHealthAfter=126, victimRunIdentity=2,
              victim='AegisPlayerCharacter_0', enemyVictim=False), 11)
        tx('repair', -12, 12)
        event('repair_applied', dict(playerActualHealing=14, companionActualHealing=0, quotedHealing=14,
              cost=12, quoteReconciled=True, cooldownReadyGameSeconds=20), 12)
        tx('overclock', -35, 13)
        event('overclock_activated', dict(relayKey=20, endsAtGameSeconds=19, contestedAtPurchase=True), 13)
        event('damage', dict(actualDamage=1, victimHealthAfter=139, victimRunIdentity=2,
              victim='AegisPlayerCharacter_0', enemyVictim=False), 14)
        event('symbiosis_applied', dict(playerActualHealing=.1, companionActualHealing=0, progressGameSeconds=.05), 14.1)
        event('companion_state_changed', dict(**{'from': '', 'to': 'Aim'}, ownSight=True, guardSlotReason='preferred_visible_firing_slot'), 14.1)
        event('companion_state_changed', dict(**{'from': 'Aim', 'to': 'Fire'}, ownSight=True, guardSlotReason='preferred_visible_firing_slot'), 14.2)
        event('companion_state_changed', dict(**{'from': 'Fire', 'to': 'Aim'}, ownSight=True, guardSlotReason='preferred_visible_firing_slot'), 14.3)
        event('phase_changed', dict(phase='lost', wave=2), 20)
        self.summary = dict(mode=gate.MODE, completed=True, ledgerBalanced=True, traceComplete=True,
            runId=self.run, outcome='lost', tracePath=str(self.trace), energyInitial=60, energyFinal=self.balance,
            energyEarned=self.earned, energySpent=self.spent, energyOverflow=self.overflow,
            pulsesUsed=0, repairsUsed=1, enemiesRewarded=1, stagesRewarded=1,
            repairPlayerActualHealing=14, repairCompanionActualHealing=0,
            alliedActualDamageDealt=70, playerActualDamageTaken=15, companionActualDamageTaken=0,
            upgradeMask=4, chargedShots=1, overclocks=2,
            symbiosisPlayerActualHealing=.1, symbiosisCompanionActualHealing=0,
            northRouteFirst=True, companionSurvived=True, companionStateChanges=2,
            companionShortReversalsUnder1s=1, stabilityMetricDefinition=gate.STABILITY,
            companionShots=3, companionMoveRequests=5, guardSlotChecks=4, guardSlotRejections=1,
            guardAlternateSelections=1, sessionGameSeconds=20, trialElapsedSeconds=16)
        event('session_finished', {k: v for k, v in self.summary.items() if k not in ('traceComplete', 'tracePath')}, 20)
        self.summary['eventCount'] = len(self.events)
        self.sample = dict(videoSeconds=24, worldSeconds=24, frame=1, phase=4, wave=2, charge=2, relay=0,
            enemies=3, contested=False, diagnostics=False, pathPoints=2, pathIndex=1,
            driverGoal=[0, 0, 0], driverDirection=[0, 0], heldWasd=[False]*4, actualWasd=[False]*4,
            health=0, positionZ=90, speed=0, combatEnabled=False, moveInputIgnored=False, shots=10,
            pulses=0, charging=False, chargeFraction=0, position=[0, 0], energy=self.balance,
            repairs=1, repairQuote=0, chargedShots=1, overclocks=2, overclockRemaining=0,
            symbiosisHealing=.1, northRouteFirst=True, requiredObjectiveSeconds=10,
            companionHealth=100, companionState='Aim', companionSight=False, guardSlotReason='none')
        self.capture = dict(mode=gate.MODE, validRun=True, scriptedPlayer=True, fixtureDamage=False,
            humanPlaytest=False, outcome='lost', quitPath='normal PlayerController X from result page',
            quitKey='X', quitMenuOpen=False, quitPaused=False, resultHoldSeconds=4, inputs=24,
            frames=1, completedFrames=1, videoSeconds=24, samples=[self.sample],
            frameTimes=[dict(file='frame-000000.png', videoSeconds=1, worldSeconds=1)],
            audioEvents=[dict(asset='S_Shot', videoSeconds=5, worldSeconds=5, volume=.5, location=[0, 0, 0])],
            audioCapture='Engine-triggered event log for offline mix; not loopback recording')
        (self.out / 'frames' / 'frame-000000.png').write_bytes(b'png-decoding-is-mocked-only-in-offline-tests')
        self.log = '\n'.join([
            'LogTemp: Display: AEGIS_PORTFOLIO_CAPTURE_BEGIN scripted_input=1 fixture_damage=0 frames=1',
            f'LogTemp: Display: AEGIS_PORTFOLIO_COMPLETE run={self.run} outcome=lost report={self.report}',
            'LogTemp: Display: AEGIS_PORTFOLIO_CAPTURE_COMPLETE valid=1 frames=1 completed=1',
            'LogTemp: Display: AEGIS_PORTFOLIO_CAPTURE_RESULT_X outcome=lost',
            'LogTemp: Display: AEGIS_RESULT_X_QUIT outcome=lost', 'LogExit: Exiting.'])
        self.png = patch.object(gate, 'inspect_png', return_value=dict(width=1280, height=720)).start()
        self.addCleanup(patch.stopall)

    def write(self):
        self.report.write_text(json.dumps(self.summary), encoding='utf-8')
        self.trace.write_text('\n'.join(json.dumps(row) for row in self.events), encoding='utf-8')

    def system(self):
        self.write()
        return gate.validate_system_capture(self.out, self.capture, self.log)

    def capture_gate(self, no_frames=False):
        return gate.validate_capture(self.capture, self.log, self.out, no_frames=no_frames)

    def event_named(self, kind):
        return next(row for row in self.events if row['event'] == kind)

    def transaction(self, reason):
        return next(row for row in self.events if row['event'] == 'energy_transaction' and row['data']['reason'] == reason)

    def test_positive_loss_is_not_rejected(self):
        self.assertTrue(self.system()['ledgerReconciled'])
        self.assertEqual(self.capture_gate()['frames'], 1)

    def test_positive_no_frames(self):
        self.capture.update(frames=0, completedFrames=0, frameTimes=[])
        self.sample['frame'] = 0
        self.log = self.log.replace('frames=1', 'frames=0').replace('completed=1', 'completed=0')
        (self.out/'frames'/'frame-000000.png').unlink()
        self.assertEqual(self.capture_gate(True)['frames'], 0)
        self.assertTrue(self.system()['ledgerReconciled'])

    def test_positive_won_three_stages(self):
        self.events = self.events[:-2]
        self.event('phase_changed', dict(phase='intermission', wave=2), 20)
        self.tx('stage_2_complete', 25, 20)
        self.event('upgrade_selected', dict(upgradeIndex=1), 20)
        self.event('phase_changed', dict(phase='active', wave=3), 21)
        self.event('phase_changed', dict(phase='won', wave=3), 30)
        self.tx('stage_3_complete', 25, 30)
        self.summary.update(outcome='won', energyFinal=self.balance, energyEarned=self.earned,
                            stagesRewarded=3, upgradeMask=5, sessionGameSeconds=30, trialElapsedSeconds=26)
        self.event('session_finished', {k: v for k, v in self.summary.items()
                                      if k not in ('traceComplete', 'tracePath', 'eventCount')}, 30)
        self.summary['eventCount'] = len(self.events)
        self.sample.update(phase=3, wave=3, requiredObjectiveSeconds=4, charge=4,
                           videoSeconds=34, worldSeconds=34, energy=self.balance)
        self.capture.update(outcome='won', videoSeconds=34)
        self.log = self.log.replace('outcome=lost', 'outcome=won')
        self.assertTrue(self.system()['ledgerReconciled'])
        self.assertEqual(self.capture_gate()['frames'], 1)

    def test_same_tick_samples_are_valid(self):
        self.capture['samples'].append(copy.deepcopy(self.sample))
        self.assertEqual(self.capture_gate()['frames'], 1)

    def test_boolean_truthy_string(self):
        for key, value in [('validRun', 'true'), ('fixtureDamage', 0), ('humanPlaytest', 0)]:
            with self.subTest(key=key):
                old = self.capture[key]; self.capture[key] = value
                with self.assertRaises(ValueError): self.capture_gate()
                self.capture[key] = old

    def test_v15_scope_rejected(self):
        self.capture['mode'] = 'aegis-portfolio-v1.5'
        with self.assertRaises(ValueError): self.capture_gate()

    def test_nan_sample(self):
        self.sample['driverGoal'][0] = float('nan')
        with self.assertRaises(ValueError): self.capture_gate()

    def test_nonfinite_wall(self):
        self.capture['videoSeconds'] = float('inf')
        with self.assertRaises(ValueError): self.capture_gate()

    def test_extra_error(self):
        self.log += '\nLogTemp: Error: deliberate failure'
        with self.assertRaises(ValueError): self.capture_gate()

    def test_unrecognized_warning(self):
        self.log += '\nLogTemp: Warning: deliberate warning'
        with self.assertRaises(ValueError): self.capture_gate()

    def test_ensure(self):
        self.log += '\nLogOutputDevice: Ensure condition failed'
        with self.assertRaises(ValueError): self.capture_gate()

    def test_duplicate_pass(self):
        self.log += '\nLogTemp: Display: AEGIS_PORTFOLIO_CAPTURE_COMPLETE valid=1 frames=1 completed=1'
        with self.assertRaises(ValueError): self.capture_gate()

    def test_console_replay(self):
        self.log += '\nLogAutomationController: [log] LogTemp: Display: AEGIS_PORTFOLIO_CAPTURE_COMPLETE valid=1 frames=1 completed=1'
        with self.assertRaises(ValueError): self.capture_gate()

    def test_missing_begin(self):
        self.log = '\n'.join(self.log.splitlines()[1:])
        with self.assertRaises(ValueError): self.capture_gate()

    def test_missing_normal_quit(self):
        self.log = self.log.replace('LogTemp: Display: AEGIS_RESULT_X_QUIT outcome=lost\n', '')
        with self.assertRaises(ValueError): self.capture_gate()

    def test_wrong_result_phase(self):
        self.sample['phase'] = 1
        with self.assertRaises(ValueError): self.capture_gate()

    def test_short_result_hold(self):
        self.capture['resultHoldSeconds'] = 3.99
        with self.assertRaises(ValueError): self.capture_gate()

    def test_missing_png(self):
        (self.out/'frames'/'frame-000000.png').unlink()
        with self.assertRaises(ValueError): self.capture_gate()

    def test_frame_path_escape(self):
        self.capture['frameTimes'][0]['file'] = '../frame-000000.png'
        with self.assertRaises(ValueError): self.capture_gate()

    def test_wrong_dimensions(self):
        self.png.return_value = dict(width=960, height=540)
        with self.assertRaises(ValueError): self.capture_gate()

    def test_extra_png(self):
        (self.out/'frames'/'frame-000005.png').write_bytes(b'fake')
        with self.assertRaises(ValueError): self.capture_gate()

    def test_duplicate_frame_clock(self):
        self.capture.update(frames=2, completedFrames=2)
        self.capture['frameTimes'].append(dict(file='frame-000001.png', videoSeconds=1, worldSeconds=1))
        (self.out/'frames'/'frame-000001.png').write_bytes(b'fake')
        self.log = self.log.replace('valid=1 frames=1 completed=1', 'valid=1 frames=2 completed=2')
        with self.assertRaises(ValueError): self.capture_gate()

    def test_sample_counter_regression(self):
        previous = copy.deepcopy(self.sample); previous['shots'] += 1
        self.capture['samples'].insert(0, previous)
        with self.assertRaises(ValueError): self.capture_gate()

    def test_bad_repair_preview(self):
        self.sample['repairQuote'] = 7
        with self.assertRaises(ValueError): self.capture_gate()

    def test_wrong_objective_duration(self):
        self.sample['requiredObjectiveSeconds'] = 4
        with self.assertRaises(ValueError): self.capture_gate()

    def test_wrong_charge_cost(self):
        self.transaction('charged_shot')['data']['offeredDelta'] = -11
        with self.assertRaises(ValueError): self.system()

    def test_wrong_overclock_cost(self):
        self.transaction('overclock')['data']['offeredDelta'] = -34
        with self.assertRaises(ValueError): self.system()

    def test_missing_overclock_application(self):
        self.event_named('overclock_activated')['event'] = 'pulse_rejected'
        with self.assertRaises(ValueError): self.system()

    def test_overclock_extraction_disallowed(self):
        self.event_named('overclock_activated')['data']['relayKey'] = 30
        with self.assertRaises(ValueError): self.system()

    def test_overclock_duration_tamper(self):
        self.event_named('overclock_activated')['data']['endsAtGameSeconds'] += .1
        with self.assertRaises(ValueError): self.system()

    def test_duplicate_overclock_relay(self):
        [row for row in self.events if row['event'] == 'overclock_activated'][1]['data']['relayKey'] = 10
        with self.assertRaises(ValueError): self.system()

    def test_repair_quote_truthy_falsepass(self):
        self.event_named('repair_applied')['data']['quoteReconciled'] = 'true'
        with self.assertRaises(ValueError): self.system()

    def test_repair_quote_differs_actual(self):
        self.event_named('repair_applied')['data']['quotedHealing'] = 15
        with self.assertRaises(ValueError): self.system()

    def test_repair_cost_differs_actual(self):
        self.event_named('repair_applied')['data']['cost'] = 13
        with self.assertRaises(ValueError): self.system()

    def test_repair_zero_benefit(self):
        self.event_named('repair_applied')['data']['playerActualHealing'] = 0
        with self.assertRaises(ValueError): self.system()

    def test_repair_cooldown(self):
        self.event_named('repair_applied')['data']['cooldownReadyGameSeconds'] = 12
        with self.assertRaises(ValueError): self.system()

    def test_free_repair_application(self):
        self.transaction('repair')['event'] = 'repair_rejected'
        with self.assertRaises(ValueError): self.system()

    def test_repair_rounding_matches_double_contract(self):
        self.assertEqual([gate.repair_cost(value) for value in (1, 10, 14, 30, 50)], [8, 8, 12, 24, 40])

    def test_repair_ceil_boundary_above_not_rounded_down(self):
        # These are exactly representable float recipient values. Their float sum
        # rounds to 15, whereas C++'s double sum correctly costs 13, not 12.
        actual = 14.000000953674316 + .9999997615814209
        self.assertEqual(gate.repair_cost(actual), 13)
        self.assertEqual(gate.repair_cost(15.0), 12)

    def test_repair_ceil_boundary_below(self):
        self.assertEqual(gate.repair_cost(14.999999046325684), 12)
        self.assertEqual(gate.repair_cost(12.5), 10)

    def test_rounded_quote_does_not_override_actual_cost(self):
        applied = self.event_named('repair_applied')['data']
        applied.update(playerActualHealing=14.000000953674316, companionActualHealing=.9999997615814209,
                       quotedHealing=15.0, cost=13)
        paid = False
        for row in self.events:
            if row is self.transaction('repair'):
                paid = True
                row['data']['offeredDelta'] = row['data']['actualDelta'] = -13
                row['data']['after'] -= 1
            elif paid and row['event'] == 'energy_transaction':
                row['data']['before'] -= 1; row['data']['after'] -= 1
            if paid: row['energy'] -= 1
        self.summary.update(energyFinal=self.summary['energyFinal']-1, energySpent=self.summary['energySpent']+1,
                            repairPlayerActualHealing=applied['playerActualHealing'],
                            repairCompanionActualHealing=applied['companionActualHealing'])
        self.events[-1]['data'] = {k: v for k, v in self.summary.items() if k not in ('eventCount', 'tracePath', 'traceComplete')}
        self.sample['energy'] -= 1
        self.assertTrue(self.system()['ledgerReconciled'])

    def test_symbiosis_rate_tamper(self):
        self.event_named('symbiosis_applied')['data']['playerActualHealing'] = .11
        with self.assertRaises(ValueError): self.system()

    def test_symbiosis_without_upgrade(self):
        self.event_named('upgrade_selected')['data']['upgradeIndex'] = 1
        with self.assertRaises(ValueError): self.system()

    def test_symbiosis_dt_invalid(self):
        self.event_named('symbiosis_applied')['data']['progressGameSeconds'] = .251
        with self.assertRaises(ValueError): self.system()

    def test_route_not_a_toggle(self):
        self.event_named('route_selected')['data']['northFirst'] = False
        with self.assertRaises(ValueError): self.system()

    def test_companion_state_chain(self):
        self.event_named('companion_state_changed')['data']['from'] = 'Retreat'
        with self.assertRaises(ValueError): self.system()

    def test_companion_reversals_reconcile(self):
        self.summary['companionShortReversalsUnder1s'] = 0
        with self.assertRaises(ValueError): self.system()

    def test_companion_survival_falsepass(self):
        self.summary['companionSurvived'] = 1
        with self.assertRaises(ValueError): self.system()

    def test_guard_counter_contradiction(self):
        self.summary['guardAlternateSelections'] = 3
        with self.assertRaises(ValueError): self.system()

    def test_summary_counter_tamper(self):
        self.summary['chargedShots'] = 2
        with self.assertRaises(ValueError): self.system()

    def test_energy_without_transaction(self):
        self.event_named('route_selected')['energy'] = 99
        with self.assertRaises(ValueError): self.system()

    def test_duplicate_json_key_rejected(self):
        self.write()
        text = self.report.read_text(encoding='utf-8')
        self.report.write_text(text.replace('"completed": true', '"completed": true, "completed": true'), encoding='utf-8')
        with self.assertRaises(ValueError): gate.validate_system_capture(self.out, self.capture, self.log)

    def test_nonfinite_json_rejected(self):
        self.summary['sessionGameSeconds'] = float('nan')
        with self.assertRaises(ValueError): self.system()

    def test_trace_clock_regression(self):
        self.events[5]['gameSeconds'] = 0
        with self.assertRaises(ValueError): self.system()

    def test_trace_sequence_missing(self):
        self.events[5]['sequence'] += 1
        with self.assertRaises(ValueError): self.system()

    def test_session_identity_tamper(self):
        self.events[5]['runId'] = 'B'*32
        with self.assertRaises(ValueError): self.system()

    def test_trace_path_escape(self):
        self.summary['tracePath'] = str(self.out/'elsewhere.jsonl')
        with self.assertRaises(ValueError): self.system()

    def test_duplicate_native_session(self):
        (self.out/'system'/'portfolio-other.json').write_text('{}')
        with self.assertRaises(ValueError): self.system()

    def test_missing_system_marker(self):
        self.log = '\n'.join(line for line in self.log.splitlines() if 'AEGIS_PORTFOLIO_COMPLETE' not in line)
        with self.assertRaises(ValueError): self.system()

    def test_sample_ledger_mismatch(self):
        self.sample['energy'] += 1
        with self.assertRaises(ValueError): self.system()

    def test_won_without_three_stages(self):
        self.summary['outcome'] = 'won'; self.capture['outcome'] = 'won'
        self.events[-2]['data']['phase'] = 'won'
        with self.assertRaises(ValueError): self.system()

    def test_command_guard_comparison_only_one_flag(self):
        a = gate.build_command(self.out/'game.exe', self.out, seed=2203)
        b = gate.build_command(self.out/'game.exe', self.out, seed=2203, guard_legacy=True)
        self.assertEqual(b, a + ['-AegisGuardLegacy'])
        self.assertIn('-AegisV2', a); self.assertNotIn('-NullRHI', a)
        self.assertNotIn('-game', a)

    def test_command_editor_and_no_frames(self):
        command = gate.build_command(self.out/'UnrealEditor-Cmd.exe', self.out, editor=True, no_frames=True)
        self.assertIn('-game', command); self.assertIn('-AegisPortfolioNoFrames', command)


if __name__ == '__main__': unittest.main()
