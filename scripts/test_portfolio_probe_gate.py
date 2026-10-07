"""Offline adversarial checks for the disclosed native Portfolio fixture gate.

All ledgers below are temporary synthetic test data. No Unreal process, actual
game report, configuration, network, or provider credential is used or changed.
"""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_portfolio_probe as gate


NAMES = (
    'begun_game_world', 'briefing_preset_and_budget', 'briefing_rejects_pulse_and_repair',
    'enter_deploys_active_trial', 'full_health_repair_has_no_cost_or_cooldown',
    'disclosed_fixture_damage_applies_45', 'repair_spends_exactly_40',
    'repair_applies_and_records_actual_30', 'repair_starts_game_clock_cooldown',
    'repair_cooldown_rejects_repeat', 'low_energy_pulse_preserves_gameplay_cooldown',
    'restart_replaces_player_pawn', 'restart_resets_energy_health_cooldown_and_statistics',
    'second_deploy_uses_normal_enter', 'accepted_pulse_spends_exactly_35',
    'accepted_pulse_updates_actual_counter_and_cooldown',
    'pulse_cooldown_repeat_does_not_double_charge', 'second_restart_resets_both_abilities',
    'normal_pause_menu_opens', 'paused_q_and_e_preserve_time_and_resources',
    'restart_from_pause_restores_briefing', 'normal_held_fire_actually_fires',
    'restart_during_held_fire_replaces_pawn', 'redeploy_does_not_retain_held_fire',
    'normal_quit_menu_is_available',
)


def native(marker):
    return '[2026.09.18-01.00.00:000][ 12]LogTemp: Display: ' + marker


def write_json(path, value):
    path.write_text(json.dumps(value, allow_nan=False) + '\n', encoding='utf-8')


def make_evidence(output):
    """An independent minimal model of the documented five native lifecycles."""
    system = output / 'system'
    system.mkdir()
    paths = []
    for index in range(5):
        run_id = f'{index + 1:032X}'
        path = system / f'portfolio-{run_id}.jsonl'
        paths.append(str(path))
        spent = 40 if index == 0 else 35 if index == 1 else 0
        summary = dict(mode='aegis-portfolio-v1.5', completed=False,
                       outcome='aborted' if index == 4 else 'restarted',
                       ledgerBalanced=True, traceComplete=True, runId=run_id,
                       tracePath=str(path), eventCount=3,
                       energyInitial=60, energyFinal=60-spent, energyEarned=0,
                       energySpent=spent, energyOverflow=0,
                       pulsesUsed=int(index == 1), repairsUsed=int(index == 0),
                       enemiesRewarded=0, stagesRewarded=0,
                       repairPlayerActualHealing=30 if index == 0 else 0,
                       repairCompanionActualHealing=0,
                       playerActualDamageTaken=45 if index == 0 else 0)
        transaction = dict(before=60, actualDelta=-spent, after=60-spent)
        events = [dict(runId=run_id, sequence=1, event='session_started', gameSeconds=1., energy=60, data={}),
                  dict(runId=run_id, sequence=2, event='energy_transaction' if spent else 'phase_changed',
                       gameSeconds=2., energy=60-spent, data=transaction if spent else {}),
                  dict(runId=run_id, sequence=3, event='session_finished', gameSeconds=3.,
                       energy=60-spent, data=copy.deepcopy(summary))]
        path.write_text(''.join(json.dumps(event) + '\n' for event in events), encoding='utf-8')
        write_json(path.with_suffix('.json'), summary)
    rows = []
    for index, name in enumerate(NAMES):
        energy, health, pulses = 60, 140, 0
        if index == 5: health = 95
        if 6 <= index <= 10: energy, health = 20, 125
        if 14 <= index <= 16: energy, pulses = 25, 1
        rows.append(dict(name=name, passed=True, wallSeconds=.2+index*.1, gameSeconds=1+index*.1,
                         energy=energy, playerHealth=health, pulseActivations=pulses,
                         actualPlayerShots=2 if index == 21 else 0))
    report = dict(schemaVersion=1, passed=True, engine='unreal-runtime',
                  scope='portfolio transaction/input fixture; not natural combat, policy or human evaluation',
                  fixtureDamage=True, fixtureDamageApplied=45,
                  fixtureDamageSource='existing hostile actor; direct Health.ApplyDamage API',
                  aiFrozen=True, frozenControllerCount=20, syntheticKeyboardInput=True,
                  humanUsabilityTest=False, policyPerformanceTest=False, renderOffscreen=True,
                  nullRHI=False, reason='completed 25 transaction and input checks; normal menu X requests quit',
                  quitPath='normal PlayerController X from paused menu', wallSeconds=5.25,
                  lastStage=20, assertions=rows, portfolioTracePaths=paths)
    markers = ['AEGIS_PORTFOLIO_PROBE_BEGIN syntheticKeyboard=1 fixtureDamage=1 aiFrozen=1 humanPlay=0',
               *('AEGIS_PORTFOLIO_PROBE_ASSERT_PASS ' + name for name in NAMES),
               'AEGIS_PORTFOLIO_PROBE_PASS checks=25']
    log = '\n'.join(map(native, markers)) + '\n[2026.09.18-01.00.05:000][ 90]LogExit: Exiting.\n'
    return report, log


class PortfolioProbeGateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='aegis-portfolio-gate-')
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name).resolve()
        self.report, self.log = make_evidence(self.output)

    def reject(self, report=None, log=None):
        with self.assertRaises((ValueError, OSError)):
            gate.validate(self.report if report is None else report,
                          self.log if log is None else log, self.output)

    def modify_events(self, index, change):
        path = Path(self.report['portfolioTracePaths'][index])
        events = [json.loads(line) for line in path.read_text().splitlines()]
        change(events)
        path.write_text(''.join(json.dumps(item) + '\n' for item in events), encoding='utf-8')

    def test_positive_validates_five_independent_session_hashes(self):
        records = gate.validate(self.report, self.log, self.output)
        self.assertEqual(len(records), 5)
        self.assertEqual([item['outcome'] for item in records], ['restarted']*4 + ['aborted'])
        self.assertTrue(all(len(item['sha256']) == len(item['traceSha256']) == 64 for item in records))
        self.assertEqual(tuple(gate.ASSERTIONS), NAMES)

    def test_missing_duplicate_reordered_and_failed_assertions(self):
        for kind in ('missing', 'duplicate', 'reorder', 'false', 'unknown'):
            with self.subTest(kind=kind):
                report = copy.deepcopy(self.report)
                rows = report['assertions']
                if kind == 'missing': rows.pop()
                elif kind == 'duplicate': rows[-1] = copy.deepcopy(rows[-2])
                elif kind == 'reorder': rows[0], rows[1] = rows[1], rows[0]
                elif kind == 'false': rows[0]['passed'] = False
                else: rows[0]['name'] = 'invented_check'
                self.reject(report=report)

    def test_scope_booleans_cannot_be_truthy_strings_or_numbers(self):
        for key in ('passed', 'fixtureDamage', 'aiFrozen', 'syntheticKeyboardInput',
                    'humanUsabilityTest', 'policyPerformanceTest', 'renderOffscreen', 'nullRHI'):
            for value in (str(self.report[key]).lower(), int(self.report[key]), not self.report[key]):
                with self.subTest(key=key, value=value):
                    report = copy.deepcopy(self.report); report[key] = value
                    self.reject(report=report)

    def test_each_assertion_requires_literal_true(self):
        for value in ('true', 1, None):
            report=copy.deepcopy(self.report);report['assertions'][0]['passed']=value
            self.reject(report=report)

    def test_nonfinite_and_invalid_numeric_report_fields(self):
        for key, value in (('wallSeconds', float('nan')), ('wallSeconds', float('inf')),
                           ('wallSeconds', -1), ('wallSeconds', 35.01), ('wallSeconds', True),
                           ('fixtureDamageApplied', 44), ('frozenControllerCount', 1),
                           ('frozenControllerCount', 2.5), ('lastStage', 19), ('schemaVersion', True)):
            with self.subTest(key=key, value=value):
                report=copy.deepcopy(self.report);report[key]=value;self.reject(report=report)

    def test_assertion_numeric_contradictions_are_not_hidden_by_pass(self):
        for index, key, value in ((1,'energy',59), (5,'playerHealth',96), (7,'playerHealth',124),
                                  (15,'pulseActivations',0), (21,'actualPlayerShots',0),
                                  (23,'actualPlayerShots',1), (2,'energy',float('nan')),
                                  (2,'actualPlayerShots',True), (2,'gameSeconds',0),
                                  (2,'wallSeconds',0), (24,'wallSeconds',6)):
            with self.subTest(index=index, key=key):
                report=copy.deepcopy(self.report);report['assertions'][index][key]=value
                self.reject(report=report)

    def test_log_requires_original_markers_not_console_replay(self):
        self.reject(log=self.log.replace('LogTemp: Display: ', 'LogAutomationController: Display: [log] '))
        self.reject(log=self.log.replace('LogTemp: Display: ', 'LogConsoleManager: Display: echo '))

    def test_missing_duplicate_or_malformed_pass_and_begin(self):
        for change in (lambda text: text.replace(native(gate.PASS), ''),
                       lambda text: text+'\n'+native(gate.PASS),
                       lambda text: text+'\n'+native(gate.BEGIN),
                       lambda text: text.replace('checks=25', 'checks=24'),
                       lambda text: text.replace('humanPlay=0', 'humanPlay=1'),
                       lambda text: text+'\n'+native('AEGIS_PORTFOLIO_PROBE_ASSERT_PASS '+NAMES[0])):
            self.reject(log=change(self.log))

    def test_engine_warning_error_fatal_ensure_and_failure_rejected(self):
        for line in ('LogTemp: Warning: unexplained warning', 'LogTemp: Error: runtime problem',
                     'Fatal error: crash', 'Handled ensure: bad condition',
                     'Ensure condition failed: something', 'Assertion failed: invariant',
                     native('AEGIS_PORTFOLIO_PROBE_FAIL checks=25')):
            with self.subTest(line=line):self.reject(log=self.log+'\n'+line)

    def test_unique_normal_shutdown_is_required(self):
        shutdown='[2026.09.18-01.00.05:000][ 90]LogExit: Exiting.'
        self.reject(log=self.log.replace(shutdown, ''))
        self.reject(log=self.log+'\n'+shutdown)
        self.reject(log=self.log.replace('LogExit: Exiting.', 'LogTemp: Display: LogExit: Exiting.'))

    def test_missing_or_duplicate_sessions_rejected(self):
        report=copy.deepcopy(self.report);report['portfolioTracePaths'].pop();self.reject(report=report)
        report=copy.deepcopy(self.report);report['portfolioTracePaths'][-1]=report['portfolioTracePaths'][0]
        self.reject(report=report)

    def test_relative_outside_missing_and_unreported_paths_rejected(self):
        for bad in ('relative.jsonl', str(self.output/'outside.jsonl'), str(self.output/'system'/'missing.jsonl')):
            report=copy.deepcopy(self.report);report['portfolioTracePaths'][0]=bad;self.reject(report=report)
        (self.output/'system'/('portfolio-'+'A'*32+'.jsonl')).write_text('{}\n')
        self.reject()

    def test_corrupt_transaction_or_undisclosed_energy_change_rejected(self):
        path=Path(self.report['portfolioTracePaths'][0]);original=path.read_bytes()
        for change in (lambda rows: rows[1]['data'].__setitem__('before', 61),
                       lambda rows: rows[1]['data'].__setitem__('actualDelta', -39),
                       lambda rows: rows[1].__setitem__('energy', 21),
                       lambda rows: rows[0].__setitem__('energy', 59)):
            path.write_bytes(original);self.modify_events(0,change);self.reject()

    def test_corrupt_summary_ledger_or_fixture_scope_rejected(self):
        path=Path(self.report['portfolioTracePaths'][0]).with_suffix('.json')
        original=json.loads(path.read_text())
        for key,value in (('energyFinal',21), ('energySpent',39), ('energyEarned',1),
                          ('repairsUsed',0), ('repairPlayerActualHealing',29),
                          ('playerActualDamageTaken',44), ('ledgerBalanced','true'),
                          ('completed',True), ('outcome','won'), ('traceComplete',False)):
            with self.subTest(key=key):
                summary=copy.deepcopy(original);summary[key]=value;write_json(path,summary);self.reject()

    def test_trace_sequence_end_identity_and_clock_rejected(self):
        path=Path(self.report['portfolioTracePaths'][0]);original=path.read_bytes()
        for change in (lambda rows: rows.pop(),
                       lambda rows: rows[1].__setitem__('sequence',1),
                       lambda rows: rows[1].__setitem__('runId','wrong'),
                       lambda rows: rows[2].__setitem__('event','phase_changed'),
                       lambda rows: rows[1].__setitem__('gameSeconds',0),
                       lambda rows: rows[2]['data'].__setitem__('energyFinal',21)):
            path.write_bytes(original);self.modify_events(0,change);self.reject()

    def test_packaged_command_keeps_fixture_and_renderer_disclosure(self):
        exe=self.output/'AegisArena.exe'
        command=gate.build_command(exe,self.output,editor=False)
        self.assertEqual(command[0],str(exe));self.assertEqual(command[1],'/Game/Aegis/Maps/AegisArena')
        for flag in ('-AegisPortfolio','-AegisPortfolioProbe','-AegisInputProbe','-RenderOffscreen','-d3d11'):
            self.assertIn(flag,command)
        self.assertNotIn('-NullRHI',command);self.assertNotIn('-game',command)
        self.assertFalse(any(value.endswith('.uproject') for value in command))

    def test_editor_command_explicitly_launches_game_world(self):
        command=gate.build_command(self.output/'UnrealEditor-Cmd.exe',self.output,editor=True)
        self.assertEqual(command[1],str(gate.ROOT/'AegisArena.uproject'))
        self.assertIn('-game',command)
        self.assertIn(f'-AegisPortfolioOutput={self.output/"system"}',command)

    def test_packaged_payload_requires_and_hashes_cooked_containers(self):
        project=self.output/'AegisArena';exe=project/'Binaries/Win64/AegisArena.exe'
        exe.parent.mkdir(parents=True);exe.write_bytes(b'offline unit fixture')
        with mock.patch.object(gate,'lab_binary_snapshot',return_value={'offline':True}):
            with self.assertRaises(ValueError):gate.binary_snapshot(exe)
            paks=project/'Content/Paks';paks.mkdir(parents=True)
            (paks/'pakchunk0.ucas').write_bytes(b'cooked offline fixture')
            result=gate.binary_snapshot(exe)
            self.assertEqual(result['cookedContainers']['pakchunk0.ucas']['sha256'],gate.sha(paks/'pakchunk0.ucas'))


if __name__=='__main__':
    unittest.main()
