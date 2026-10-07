"""Controlled PIE Guard-slot regression. Baseline PASS means the defect was reproduced, not a win."""
import argparse
import datetime
import json
import os
from pathlib import Path
import re
import subprocess
import time

from run_unreal_functional import snapshot_inputs
from run_unreal_trial import OFFLINE_ARGUMENT, OFFLINE_MARKER
from run_decision_lab import read_json, sha, validate_engine_messages
from run_portfolio_probe import binary_snapshot, number, required_fields

ROOT = Path(__file__).resolve().parents[1]
MAP = '/Game/Aegis/Maps/AegisGuardSlotFunctional'
TEST_PATH = 'Project.Functional Tests.Aegis.Maps.AegisGuardSlotFunctional'
COMMON_START = ('game_world_begun', 'v2_enable_matches_launch', 'own_sight_authorizes_target',
                'preferred_slot_weapon_trace_blocked', 'alternate_slot_weapon_trace_clear',
                'both_formation_slots_have_complete_paths')
MODE_CHECKS = {
    'baseline': ('baseline_prefers_blocked_slot', 'baseline_cannot_fire_through_obstacle', 'baseline_v2_counters_unused'),
    'improved': ('v2_rejects_primary_and_selects_alternate', 'v2_physically_moves_toward_other_flank',
                 'v2_winds_up_and_fires_normally'),
}
COMMON_END = ('opaque_cover_removes_sight', 'hidden_target_does_not_fire', 'hidden_target_does_not_run_slot_checks')
DIRECT = re.compile(r'^(?:\[[^\]\r\n]*\])*LogTemp: Display: (AEGIS_GUARD_SLOT_[^\r\n]+)$')


def assertions(mode):
    return COMMON_START + MODE_CHECKS[mode] + COMMON_END


def build_command(editor, output, *, improved=False):
    # UE 5.8 UnifiedError smoke tests compare localized FText against English
    # literals (Core/Tests/Experimental/UnifiedError/UnifiedErrorTests.cpp:479).
    # Pin this isolated editor fixture's culture instead of accepting the real
    # startup CHECK errors. The engine error validator below stays unchanged.
    command = [str(editor), str(ROOT/'AegisArena.uproject'), MAP, '-unattended', '-nop4', '-NullRHI',
        '-culture=en', '-stdout', '-FullStdOutLogOutput', OFFLINE_ARGUMENT,
        f'-AegisGuardSlotOutput={output}', f'-ExecCmds=Automation RunTests {TEST_PATH}',
        '-TestExit=Automation Test Queue Empty', f'-ReportExportPath={output}',
        f'-abslog={Path(output)/"engine.log"}']
    if improved: command += ['-AegisV2']
    return command


def validate(report, native, log, mode):
    if mode not in MODE_CHECKS: raise ValueError('Unknown Guard slot mode')
    required_fields(report, dict(succeeded=1, succeededWithWarnings=0, failed=0, notRun=0, inProcess=0))
    tests = report.get('tests')
    if (not isinstance(tests, list) or len(tests) != 1 or not isinstance(tests[0], dict) or
            tests[0].get('state') != 'Success' or not tests[0].get('fullTestPath', '').startswith(TEST_PATH + '.')):
        raise ValueError('Expected one completed original Guard slot PIE fixture')
    improved = mode == 'improved'
    required_fields(native, dict(schemaVersion=1, passed=True, mode=mode, worldType=3, begunPlay=True,
        damageDisabled=True, stationaryLeaderAndTarget=True, setupBrainPaused=True, sensesToggled=False,
        baselineDefectObserved=not improved, observedWindup=improved,
        hiddenChecksUnchanged=True, hiddenShotsUnchanged=True))
    seconds = number(native.get('gameSeconds'), 0.5, 18.1)
    number(native.get('wallSeconds'), 0.01, 30.1)
    shots = number(native.get('shots'), 0, 100, True)
    movement = number(native.get('movementCm'), 0, 3000)
    checks = number(native.get('slotChecks'), 0, 1000, True)
    alternates = number(native.get('alternateSelections'), 0, checks, True)
    if improved:
        if shots < 1 or movement < 120 or checks < 2 or alternates < 1:
            raise ValueError('Improved fixture did not move, choose the alternate and actually fire')
    elif shots != 0 or movement > 50 or checks != 0 or alternates != 0:
        raise ValueError('Baseline does not reproduce the declared blocked-slot failure')
    expected = assertions(mode)
    rows = native.get('assertions')
    if (not isinstance(rows, list) or len(rows) != 12 or
            any(not isinstance(row, dict) or row.get('passed') is not True for row in rows) or
            tuple(row.get('name') for row in rows) != expected):
        raise ValueError('All 12 exact native assertions must pass in order')
    last = -1
    for row in rows:
        stamp = number(row.get('gameSeconds'), 0, seconds)
        if stamp < last: raise ValueError('Assertion game clock regressed')
        last = stamp
    events = [m.group(1) for line in log.splitlines() if (m := DIRECT.fullmatch(line))]
    marker = f'AEGIS_GUARD_SLOT_FUNCTIONAL_PASS mode={mode} assertions=12 world=3 damageDisabled=1'
    if events != ['AEGIS_GUARD_SLOT_ASSERT_PASS ' + name for name in expected] + [marker]:
        raise ValueError('Missing, duplicate, reordered or malformed direct native evidence')
    if OFFLINE_MARKER not in log: raise ValueError('Missing offline engine startup evidence')
    if any(x in log for x in ('Handled ensure', 'Ensure condition failed', 'Assertion failed', 'Fatal error:',
                             'No functional testing script', 'TestResult=Failed')):
        raise ValueError('Native failure in fixture log')
    validate_engine_messages(log, rendered=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cache-root', type=Path)
    parser.add_argument('--improved', action='store_true', help='Enable only -AegisV2 Guard firing-slot qualification')
    args = parser.parse_args()
    mode = 'improved' if args.improved else 'baseline'
    engine = args.engine_root.resolve()
    editor = engine / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    if not editor.is_file(): parser.error('UnrealEditor-Cmd.exe missing')
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=False)
    command = build_command(editor, output, improved=args.improved)
    record = dict(passed=False, schemaVersion=1, engine='unreal-runtime', world='PIE', mode=mode,
        command=command, engineCulture='en', expectedAssertions=list(assertions(mode)), damageDisabled=True,
        humanPlaytest=False, policyWinRateTest=False, naturalCombat=False,
        scope='Controlled low-cover geometry; baseline PASS reproduces the blocked first-slot defect',
        interventions='Stationary leader and target; companion brain paused only during setup; ranged damage zero; '
                      'no sense toggles; opaque cover and hidden target relocation verify permission boundary',
        startedAtUtc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    environment = os.environ.copy()
    if args.cache_root:
        cache = args.cache_root.resolve(); (cache/'Temp').mkdir(parents=True, exist_ok=True)
        environment.update(TEMP=str(cache/'Temp'), TMP=str(cache/'Temp'))
        environment['UE-LocalDataCachePath'] = str(cache/'DerivedDataCache')
    wrapper_names = ('run_unreal_guard_slot.py', 'run_unreal_functional.py', 'run_unreal_trial.py',
                     'run_portfolio_probe.py', 'run_decision_lab.py')
    started = time.monotonic()
    try:
        record['inputsBefore'] = snapshot_inputs(ROOT)
        record['binariesBefore'] = binary_snapshot(editor, editor=True)
        record['wrappersBefore'] = {name: sha(ROOT/'scripts'/name) for name in wrapper_names}
        record['engineBuild'] = read_json(engine/'Engine/Build/Build.version')
        with (output/'stdout.log').open('w', encoding='utf-8') as stream:
            result = subprocess.run(command, cwd=ROOT, env=environment, stdout=stream, stderr=subprocess.STDOUT,
                                    timeout=150, check=False)
        record['exitCode'] = result.returncode
        if result.returncode: raise ValueError(f'Unreal exit code {result.returncode}')
        validate(read_json(output/'index.json'), read_json(output/'guard-slot.json'),
                 (output/'engine.log').read_text(encoding='utf-8-sig'), mode)
        record['nativeAssertions'] = 12
        record['baselineDefectObserved'] = not args.improved
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        record['failure'] = str(error)
    finally:
        try:
            record['inputsAfter'] = snapshot_inputs(ROOT)
            record['binariesAfter'] = binary_snapshot(editor, editor=True)
            record['wrappersAfter'] = {name: sha(ROOT/'scripts'/name) for name in wrapper_names}
            for label in ('inputs', 'binaries', 'wrappers'):
                if record.get(label+'Before') != record.get(label+'After'):
                    record['failure'] = record.get('failure', '') + f' | {label} changed or unbound'
        except (OSError, ValueError) as error:
            record['failure'] = record.get('failure', '') + ' | final provenance: ' + str(error)
        record['passed'] = 'failure' not in record and record.get('nativeAssertions') == 12
        record['wallSeconds'] = time.monotonic()-started
        record['files'] = {p.name: {'bytes': p.stat().st_size, 'sha256': sha(p)} for p in output.iterdir()
                           if p.is_file() and p.suffix in ('.json', '.log') and p.name != 'provenance.json'}
        (output/'provenance.json').write_text(json.dumps(record, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    print(json.dumps({k:record[k] for k in ('passed','mode','nativeAssertions','failure') if k in record}))
    return 0 if record['passed'] else 2


if __name__ == '__main__': raise SystemExit(main())
