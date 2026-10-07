"""Adversarial evidence tests only; no native engine or behavioral claims."""
import copy
from pathlib import Path
import unittest

from run_unreal_guard_slot import assertions, build_command, validate, TEST_PATH, OFFLINE_MARKER, MAP


def fixture(mode):
    improved = mode == 'improved'
    report = dict(succeeded=1, succeededWithWarnings=0, failed=0, notRun=0, inProcess=0,
                  tests=[dict(state='Success', fullTestPath=TEST_PATH+'.GuardSlot')])
    native = dict(schemaVersion=1, passed=True, mode=mode, worldType=3, begunPlay=True, damageDisabled=True,
                  stationaryLeaderAndTarget=True, setupBrainPaused=True, sensesToggled=False,
                  baselineDefectObserved=not improved, observedWindup=improved,
                  hiddenChecksUnchanged=True, hiddenShotsUnchanged=True, gameSeconds=6, wallSeconds=6,
                  shots=1 if improved else 0, movementCm=210 if improved else 0,
                  slotChecks=2 if improved else 0, alternateSelections=1 if improved else 0,
                  assertions=[dict(name=name, passed=True, gameSeconds=index*0.1)
                              for index,name in enumerate(assertions(mode))])
    events = ['AEGIS_GUARD_SLOT_ASSERT_PASS '+name for name in assertions(mode)]
    events += [f'AEGIS_GUARD_SLOT_FUNCTIONAL_PASS mode={mode} assertions=12 world=3 damageDisabled=1']
    log = OFFLINE_MARKER+'\n'+'\n'.join('[timestamp][1]LogTemp: Display: '+event for event in events)
    return report, native, log


class GuardSlotGateTests(unittest.TestCase):
    def test_fixture_command_pins_culture_without_disabling_checks(self):
        editor = Path('D:/Engine/UnrealEditor-Cmd.exe')
        output = Path('D:/Reports/fresh-guard')
        for improved in (False, True):
            command = build_command(editor, output, improved=improved)
            with self.subTest(improved=improved):
                self.assertEqual(command.count('-culture=en'), 1)
                self.assertIn(MAP, command)
                self.assertIn('-NullRHI', command)
                self.assertIn(f'-ExecCmds=Automation RunTests {TEST_PATH}', command)
                self.assertIn('-TestExit=Automation Test Queue Empty', command)
                self.assertEqual('-AegisV2' in command, improved)
                self.assertFalse(any('skip' in arg.lower() or 'suppress' in arg.lower() for arg in command))
                self.assertIn(f'-AegisGuardSlotOutput={output}', command)

    def test_localized_startup_smoke_failures_remain_rejected(self):
        report, native, log = fixture('baseline')
        error = '[2026.09.18-02.06.59:119][  0]LogAutomationTest: Error: Condition failed\n'
        boundary = '[2026.09.18-02.06.59:120][  0]LogEngine: Initializing Engine...\n'
        # Exact observed 13-line startup block is a real engine test failure,
        # not an allowed warning. No count, position or signature exemption.
        for count in (1, 12, 13, 14):
            for bad in (error*count+boundary+log, boundary+log+'\n'+error*count):
                with self.subTest(count=count, startup=bad.startswith(error)), self.assertRaises(ValueError):
                    validate(report,native,bad,'baseline')

    def test_both_modes_validate_with_distinct_scope(self):
        for mode in ('baseline','improved'): validate(*fixture(mode), mode)

    def test_wrong_requested_mode_cannot_relabel_baseline(self):
        with self.assertRaises(ValueError): validate(*fixture('baseline'), 'improved')

    def test_each_native_assertion_required(self):
        for mode in ('baseline','improved'):
            for index in range(12):
                report,native,log=fixture(mode)
                native['assertions'].pop(index)
                with self.subTest(mode=mode,index=index), self.assertRaises(ValueError):
                    validate(report,native,log,mode)

    def test_failed_duplicate_or_reordered_rows_rejected(self):
        for variant in ('false','duplicate','reverse'):
            report,native,log=fixture('improved')
            if variant=='false': native['assertions'][3]['passed']=False
            elif variant=='duplicate': native['assertions'][3]=copy.deepcopy(native['assertions'][2])
            else: native['assertions'].reverse()
            with self.subTest(variant=variant), self.assertRaises(ValueError): validate(report,native,log,'improved')

    def test_truthy_fixture_flags_are_not_accepted(self):
        for key in ('passed','damageDisabled','stationaryLeaderAndTarget','setupBrainPaused','sensesToggled',
                    'baselineDefectObserved','observedWindup','hiddenChecksUnchanged','hiddenShotsUnchanged'):
            report,native,log=fixture('improved'); native[key]=int(native[key])
            with self.subTest(key=key), self.assertRaises(ValueError): validate(report,native,log,'improved')

    def test_improvement_requires_real_movement_shot_and_alternate(self):
        for key,value in (('shots',0),('movementCm',119),('slotChecks',1),('alternateSelections',0)):
            report,native,log=fixture('improved'); native[key]=value
            with self.subTest(key=key), self.assertRaises(ValueError): validate(report,native,log,'improved')

    def test_baseline_requires_actual_blocked_first_slot(self):
        for key,value in (('shots',1),('movementCm',51),('slotChecks',1),('alternateSelections',1)):
            report,native,log=fixture('baseline'); native[key]=value
            with self.subTest(key=key), self.assertRaises(ValueError): validate(report,native,log,'baseline')

    def test_invalid_measurements_and_clocks_rejected(self):
        for key in ('shots','movementCm','slotChecks','alternateSelections','gameSeconds','wallSeconds'):
            for value in (True,float('nan'),float('inf'),-1):
                report,native,log=fixture('improved'); native[key]=value
                with self.subTest(key=key,value=value), self.assertRaises(ValueError): validate(report,native,log,'improved')
        report,native,log=fixture('improved'); native['assertions'][-1]['gameSeconds']=0
        with self.assertRaises(ValueError): validate(report,native,log,'improved')

    def test_replayed_missing_duplicate_or_reordered_log_rejected(self):
        report,native,log=fixture('improved'); lines=log.splitlines()
        variants=(log.replace('LogTemp: Display:','LogAutomationController: Display:'),
                  '\n'.join(lines[:-1]), log+'\n'+lines[-1], '\n'.join(lines[:1]+list(reversed(lines[1:]))),
                  log.replace('world=3','world=2'))
        for bad in variants:
            with self.subTest(log=bad[-90:]), self.assertRaises(ValueError): validate(report,native,bad,'improved')

    def test_exact_native_test_report_required(self):
        for key in ('succeededWithWarnings','failed','notRun','inProcess'):
            report,native,log=fixture('improved'); report[key]=1
            with self.subTest(key=key), self.assertRaises(ValueError): validate(report,native,log,'improved')
        report,native,log=fixture('improved'); report['tests'][0]['fullTestPath']='Other.Test'
        with self.assertRaises(ValueError): validate(report,native,log,'improved')

    def test_offline_marker_and_clean_native_log_required(self):
        report,native,log=fixture('improved')
        for bad in (log.replace(OFFLINE_MARKER,''),log+'\nHandled ensure',log+'\nLogTemp: Warning: unexpected'):
            with self.subTest(log=bad[-90:]), self.assertRaises(ValueError): validate(report,native,bad,'improved')


if __name__ == '__main__': unittest.main()
