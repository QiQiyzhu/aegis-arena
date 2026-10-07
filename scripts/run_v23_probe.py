"""Development survey input fixture with disclosed placement/damage; not natural play."""
import argparse
import datetime
import json
import os
from pathlib import Path
import re
import subprocess
import time

from run_unreal_functional import snapshot_inputs
from run_unreal_trial import OFFLINE_ARGUMENT
from run_decision_lab import sha, read_json, strict_json, validate_engine_messages
from run_portfolio_probe import binary_snapshot, number, required_fields
from v23_survey_gate import SurveyLedger

ROOT = Path(__file__).resolve().parents[1]
ASSERTIONS = (
    'begun_game_world_v23', 'briefing_survey_defaults', 'briefing_survey_inputs_rejected',
    'deploys_frozen_active_trial', 'out_of_range_scan_rejected', 'h_selects_supply',
    'full_health_supply_rejected', 'held_g_advances_partial_scan', 'release_cancels_without_claim',
    'pause_cancels_scan', 'paused_survey_inputs_preserve_state_time',
    'damage_cancels_without_automatic_retry', 'leaving_radius_cancels_scan',
    'continuous_hold_claims_one_key', 'continued_hold_cannot_repeat_claim',
    'claimed_cache_repress_rejected', 'pending_key_cannot_stack_at_second_cache',
    'supply_selection_preserves_pending_key', 'supply_heals_actual_twenty_fifteen',
    'supply_consumes_cache_without_energy_cost', 'claimed_supply_repress_rejected',
    'productive_relay_binds_key_once', 'bound_key_accelerates_actual_relay_progress',
    'completed_relay_releases_boost_without_energy_change', 'normal_menu_quit_available',
)
NOT_COVERED = ['human input and natural combat', 'real window focus loss',
               'wave-three key rejection in native play', 'supply line-of-sight occlusion']
BEGIN = 'AEGIS_V23_PROBE_BEGIN syntheticKeyboard=1 fixtureDamage=1 fixturePlacement=1 aiFrozen=1'
PASS = 'AEGIS_V23_PROBE_PASS checks=25'
REASON = 'Completed v2.3 survey input fixture; normal menu X requests quit'
DIRECT = re.compile(r'^(?:\[[^\]\r\n]*\])*LogTemp: Display: (AEGIS_V23_PROBE_[^\r\n]+)$')
EXIT = re.compile(r'^(?:\[[^\]\r\n]*\])*LogExit: Exiting\.$')


def validate_report(report, log):
    required_fields(report, dict(schemaVersion=1, passed=True, engine='unreal-runtime',
        syntheticKeyboardInput=True, fixtureDamage=True, fixturePlacement=True, aiFrozen=True,
        humanPlaytest=False, policyPerformanceTest=False, renderOffscreen=True, nullRHI=False,
        reason=REASON, quitPath='normal PlayerController X from paused menu'))
    if report.get('notCovered') != NOT_COVERED:
        raise ValueError('Coverage limitations must remain explicit')
    frozen = number(report.get('frozenControllerCount'), 2, 1000, True)
    wall = number(report.get('wallSeconds'), .1, 60)
    if number(report.get('lastStage'), integer=True) != 29:
        raise ValueError('Wrong final native stage')
    rows = report.get('assertions')
    if (not isinstance(rows, list) or len(rows) != len(ASSERTIONS) or
            any(not isinstance(row, dict) or row.get('passed') is not True for row in rows) or
            tuple(row.get('name') for row in rows) != ASSERTIONS):
        raise ValueError('All 25 distinct native assertions must pass in exact order')
    previous_wall = previous_game = -1
    for row in rows:
        w, g = number(row.get('wallSeconds'), 0, wall), number(row.get('gameSeconds'))
        if w < previous_wall or g < previous_game: raise ValueError('Assertion clock regressed')
        previous_wall, previous_game = w, g
        if number(row.get('energy'), 0, 100, True) != 60 or number(row.get('energySpent'), 0, 100, True) != 0:
            raise ValueError('Survey fixture must never spend or earn combat energy')
        number(row.get('playerHealth'), 0, 140); number(row.get('companionHealth'), 0, 120)
        for key in ('surveyKeys', 'surveySupplies', 'surveyBoosts', 'surveyClaimedMask'):
            number(row.get(key), 0, 3, True)
        number(row.get('surveyCancelled'), 0, 4, True)
        number(row.get('surveyNode'), -1, 1, True); number(row.get('surveyProgress'), 0, 1)
        number(row.get('surveyBoostRelayKey'), -1, 21, True); number(row.get('relayCharge'))
        number(row.get('surveyPlayerActualHealing'), 0, 20)
        number(row.get('surveyCompanionActualHealing'), 0, 15)
        for key in ('paused', 'surveyKeyPending', 'supplySelected'):
            if type(row.get(key)) is not bool: raise ValueError('Missing native boolean: ' + key)
    snapshots = {row['name']: row for row in rows}
    expected = {
        'briefing_survey_defaults': dict(surveyNode=-1, surveyClaimedMask=0, supplySelected=False),
        'h_selects_supply': dict(supplySelected=True),
        'full_health_supply_rejected': dict(surveyNode=-1, surveyClaimedMask=0, surveySupplies=0),
        'release_cancels_without_claim': dict(surveyNode=-1, surveyProgress=0, surveyCancelled=1, surveyKeys=0),
        'pause_cancels_scan': dict(paused=True, surveyNode=-1, surveyCancelled=2),
        'paused_survey_inputs_preserve_state_time': dict(paused=True, surveyNode=-1, surveyCancelled=2, supplySelected=False),
        'damage_cancels_without_automatic_retry': dict(playerHealth=135, surveyNode=-1, surveyCancelled=3),
        'leaving_radius_cancels_scan': dict(surveyNode=-1, surveyCancelled=4, surveyClaimedMask=0),
        'continuous_hold_claims_one_key': dict(surveyKeys=1, surveyClaimedMask=1, surveyKeyPending=True, surveyNode=-1),
        'continued_hold_cannot_repeat_claim': dict(surveyKeys=1, surveySupplies=0, surveyClaimedMask=1),
        'claimed_cache_repress_rejected': dict(surveyNode=-1, surveyKeys=1),
        'pending_key_cannot_stack_at_second_cache': dict(surveyNode=-1, surveyKeys=1, surveyClaimedMask=1),
        'supply_selection_preserves_pending_key': dict(playerHealth=105, companionHealth=95, supplySelected=True, surveyKeyPending=True),
        'supply_heals_actual_twenty_fifteen': dict(playerHealth=125, companionHealth=110, surveyPlayerActualHealing=20, surveyCompanionActualHealing=15),
        'supply_consumes_cache_without_energy_cost': dict(surveyKeys=1, surveySupplies=1, surveyClaimedMask=3, surveyKeyPending=True),
        'claimed_supply_repress_rejected': dict(surveyNode=-1, surveySupplies=1, surveyPlayerActualHealing=20),
        'productive_relay_binds_key_once': dict(surveyKeyPending=False, surveyBoostRelayKey=10, surveyBoosts=1),
        'completed_relay_releases_boost_without_energy_change': dict(surveyBoostRelayKey=-1, surveyBoosts=1),
        'normal_menu_quit_available': dict(paused=True, surveyNode=-1, surveyKeys=1, surveySupplies=1,
            surveyBoosts=1, surveyCancelled=4, surveyClaimedMask=3, surveyKeyPending=False, surveyBoostRelayKey=-1),
    }
    for name, fields in expected.items():
        if any(snapshots[name].get(key) != value for key, value in fields.items()):
            raise ValueError('Numeric evidence contradicts a passing native check: ' + name)
    progress = snapshots['held_g_advances_partial_scan']
    if progress['surveyNode'] != 0 or not .1 < progress['surveyProgress'] < .8:
        raise ValueError('No actual partial scan progress')
    if abs(snapshots['pause_cancels_scan']['gameSeconds'] - snapshots['paused_survey_inputs_preserve_state_time']['gameSeconds']) > .001:
        raise ValueError('Game clock advanced during paused fixture')
    before, after = (snapshots[name] for name in
                     ('productive_relay_binds_key_once', 'bound_key_accelerates_actual_relay_progress'))
    seconds = after['gameSeconds'] - before['gameSeconds']
    if seconds < 1.6 or not 1.1 < (after['relayCharge'] - before['relayCharge']) / seconds < 1.4:
        raise ValueError('Actual relay progress does not show the survey multiplier')
    interventions = report.get('interventions')
    if not isinstance(interventions, list) or any(not isinstance(row, dict) for row in interventions):
        raise ValueError('Missing fixture interventions')
    wounds = [row for row in interventions if row.get('type') == 'fixture_damage']
    placements = [row for row in interventions if row.get('type') == 'fixture_placement']
    if len(wounds) != 3 or len(placements) != 9 + frozen - 1 or len(interventions) != len(wounds) + len(placements):
        raise ValueError('Unexpected fixture intervention set')
    if [row.get('requested') for row in wounds] != [5, 30, 25]:
        raise ValueError('Wrong disclosed wounds')
    for row in wounds:
        if number(row.get('applied')) != number(row.get('requested')) or not row.get('source') or not row.get('actor'):
            raise ValueError('Actual wound differs from explicit intervention')
    for row in placements:
        location = row.get('location')
        if not row.get('actor') or not isinstance(location, list) or len(location) != 3:
            raise ValueError('Missing fixture position')
        for value in location: number(value, -10000, 10000)
    markers = [match.group(1) for line in log.splitlines() if (match := DIRECT.fullmatch(line))]
    if markers != [BEGIN, *('AEGIS_V23_PROBE_ASSERT_PASS ' + name for name in ASSERTIONS), PASS]:
        raise ValueError('Missing, duplicate or failing native markers')
    if sum(bool(EXIT.fullmatch(line)) for line in log.splitlines()) != 1:
        raise ValueError('Normal menu exit not observed')
    if any(value in log for value in ('Handled ensure', 'Ensure condition failed', 'Assertion failed', 'Fatal error:')):
        raise ValueError('Engine failure in log')
    validate_engine_messages(log, rendered=True)


def validate_session(report, output):
    system = Path(output).resolve() / 'system'
    name = report.get('portfolioTracePath')
    if not isinstance(name, str) or not Path(name).is_absolute():
        raise ValueError('Trace path must be absolute')
    trace = Path(name).resolve(strict=True)
    if trace.parent != system or not re.fullmatch(r'portfolio-[0-9A-Fa-f]{32}\.jsonl', trace.name):
        raise ValueError('Trace outside this run')
    if {path.resolve() for path in system.glob('portfolio-*.jsonl')} != {trace}:
        raise ValueError('Unexpected extra fixture session')
    summary = read_json(trace.with_suffix('.json'))
    required_fields(summary, dict(mode='aegis-prism-fall-v2.0', gameplayVersion='2.3', completed=False,
                                 ledgerBalanced=True, traceComplete=True, outcome='aborted'))
    run_id = trace.stem.removeprefix('portfolio-')
    if summary.get('runId') != run_id or Path(summary.get('tracePath', '')).resolve() != trace:
        raise ValueError('Session identity mismatch')
    events = [strict_json(line) for line in trace.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
    if (len(events) < 3 or any(not isinstance(row, dict) for row in events) or
            number(summary.get('eventCount'), integer=True) != len(events) or
            events[0].get('event') != 'session_started' or events[-1].get('event') != 'session_finished'):
        raise ValueError('Incomplete original session trace')
    survey = SurveyLedger()
    previous, phase, wave = -1, 'briefing', 0
    damage = []
    cancellations = []
    for sequence, row in enumerate(events, 1):
        if row.get('runId') != run_id or number(row.get('sequence'), integer=True) != sequence:
            raise ValueError('Event identity or sequence mismatch')
        stamp = number(row.get('gameSeconds'))
        if stamp < previous: raise ValueError('Session clock regressed')
        previous = stamp
        data, kind = row.get('data'), row.get('event')
        if not isinstance(data, dict) or not isinstance(kind, str): raise ValueError('Missing native event data')
        if number(row.get('energy'), 0, 100, True) != 60:
            raise ValueError('Undisclosed combat energy change')
        if kind == 'phase_changed':
            phase, wave = data.get('phase'), number(data.get('wave'), 0, 1, True)
            if phase not in ('briefing', 'active'): raise ValueError('Unexpected trial progression')
        elif kind == 'damage':
            if data.get('enemyVictim') is not False: raise ValueError('Undisclosed hostile damage')
            damage.append((data.get('victim'), number(data.get('actualDamage'), .00001),
                           number(data.get('victimHealthAfter'))))
        elif kind == 'survey_cancelled':
            cancellations.append(data.get('reason'))
        elif kind not in ('session_started', 'session_finished', 'companion_state_changed') and not kind.startswith('survey_'):
            raise ValueError('Unexpected fixture event: ' + kind)
        survey.observe(kind, data, stamp, phase, wave)
    if cancellations != ['released', 'unavailable', 'damaged', 'left_range']:
        raise ValueError('Missing ordered native cancellation reasons')
    if len(damage) != 3 or [(amount, remaining) for _, amount, remaining in damage] != [(5, 135), (30, 105), (25, 95)]:
        raise ValueError('Native damage differs from disclosed wounds')
    if not all(re.fullmatch(r'AegisPlayerCharacter(?:_\d+)?', str(row[0])) for row in damage[:2]) or not re.fullmatch(r'AegisAICharacter(?:_\d+)?', str(damage[2][0])):
        raise ValueError('Unexpected fixture damage recipient')
    expected = dict(energyInitial=60, energyFinal=60, energyEarned=0, energySpent=0, energyOverflow=0,
                    pulsesUsed=0, repairsUsed=0, enemiesRewarded=0, stagesRewarded=0,
                    repairPlayerActualHealing=0, repairCompanionActualHealing=0, alliedActualDamageDealt=0,
                    playerActualDamageTaken=35, companionActualDamageTaken=25, chargedShots=0, overclocks=0,
                    surveyKeys=1, surveySupplies=1, surveyBoosts=1, surveyCancelled=4,
                    surveyPlayerActualHealing=20, surveyCompanionActualHealing=15, surveyClaimedMask=3,
                    surveyBoostRelayKey=-1)
    for key, value in expected.items():
        if number(summary.get(key), -1) != value or events[-1]['data'].get(key) != summary.get(key):
            raise ValueError('Session result contradicts fixture: ' + key)
    if summary.get('surveyKeyPending') is not False or events[-1]['data'].get('surveyKeyPending') is not False:
        raise ValueError('Survey key was not consumed by relay')
    result = survey.finish(summary, report['assertions'][-1])
    if result is None: raise ValueError('Survey ledger never enabled')
    return dict(path=str(trace), sha256=sha(trace), summarySha256=sha(trace.with_suffix('.json')), survey=result)


def build_command(exe, output, editor=False):
    exe, output = Path(exe).resolve(), Path(output).resolve()
    command = [str(exe)] + ([str(ROOT / 'AegisArena.uproject'), '/Game/Aegis/Maps/AegisArena', '-game'] if editor else [])
    return command + ['-AegisV2', '-AegisV23', '-AegisPortfolio', '-AegisV23Probe', '-AegisInputProbe',
        '-RenderOffscreen', '-d3d11', '-ForceRes', '-ResX=1280', '-ResY=720', '-windowed', '-unattended',
        '-nop4', '-nosound', '-nosplash', OFFLINE_ARGUMENT, '-AegisPortfolioSeed=1101',
        '-stdout', '-FullStdOutLogOutput', f'-AegisV23ProbeOutput={output}',
        f'-AegisPortfolioOutput={output / "system"}', f'-abslog={output / "engine.log"}']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True); parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--editor', action='store_true'); parser.add_argument('--cache-root', type=Path)
    args = parser.parse_args(); exe, output = args.exe.resolve(), args.output.resolve()
    if not exe.is_file(): parser.error('Executable missing')
    if args.editor != exe.name.casefold().startswith('unrealeditor'): parser.error('Editor mode does not match executable')
    output.mkdir(parents=True, exist_ok=False)
    command = build_command(exe, output, args.editor)
    record = dict(schemaVersion=1, passed=False, engine='unreal-runtime',
        launch='editor-game' if args.editor else 'packaged-development', command=command,
        expectedAssertions=list(ASSERTIONS), notCovered=NOT_COVERED, fixtureDamage=True, fixturePlacement=True,
        aiFrozen=True, syntheticKeyboardInput=True, humanPlaytest=False, policyPerformanceTest=False,
        actorDeadlineSeconds=60, processTimeoutSeconds=180,
        startedAtUtc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    environment = os.environ.copy()
    if args.cache_root:
        cache = args.cache_root.resolve(); (cache / 'Temp').mkdir(parents=True, exist_ok=True)
        environment.update(TEMP=str(cache / 'Temp'), TMP=str(cache / 'Temp'))
        environment['UE-LocalDataCachePath'] = str(cache / 'DerivedDataCache')
    wrappers = ('run_v23_probe.py', 'v23_survey_gate.py', 'run_portfolio_probe.py', 'run_decision_lab.py',
                'run_unreal_functional.py', 'run_unreal_trial.py', 'run_unreal_input_probe.py')
    started = time.monotonic()
    try:
        record['inputsBefore'] = snapshot_inputs(ROOT); record['binariesBefore'] = binary_snapshot(exe, args.editor)
        record['wrappersBefore'] = {name: sha(ROOT / 'scripts' / name) for name in wrappers}
        with (output / 'stdout.log').open('w', encoding='utf-8') as stream:
            result = subprocess.run(command, cwd=ROOT if args.editor else exe.parent, env=environment,
                                    stdout=stream, stderr=subprocess.STDOUT, timeout=180, check=False)
        record['exitCode'] = result.returncode
        if result.returncode: raise ValueError(f'Native fixture exit code {result.returncode}')
        report = read_json(output / 'v23-probe.json')
        log = (output / 'engine.log').read_text(encoding='utf-8-sig')
        validate_report(report, log); record['session'] = validate_session(report, output)
        record['nativeAssertions'] = len(ASSERTIONS)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        record['failure'] = str(error)
    finally:
        try:
            record['inputsAfter'] = snapshot_inputs(ROOT); record['binariesAfter'] = binary_snapshot(exe, args.editor)
            record['wrappersAfter'] = {name: sha(ROOT / 'scripts' / name) for name in wrappers}
            for label in ('inputs', 'binaries', 'wrappers'):
                if record.get(label + 'Before') != record.get(label + 'After'):
                    record['failure'] = record.get('failure', '') + f' | {label} changed or unbound'
        except (OSError, ValueError) as error:
            record['failure'] = record.get('failure', '') + ' | final provenance: ' + str(error)
        record['passed'] = 'failure' not in record and record.get('nativeAssertions') == len(ASSERTIONS)
        record['wallSeconds'] = time.monotonic() - started
        record['files'] = {str(path.relative_to(output)): dict(bytes=path.stat().st_size, sha256=sha(path))
                           for path in sorted(output.rglob('*')) if path.is_file() and path.name != 'provenance.json'}
        (output / 'provenance.json').write_text(json.dumps(record, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    print(json.dumps({key: record[key] for key in ('passed', 'nativeAssertions', 'failure', 'wallSeconds') if key in record}))
    return 0 if record['passed'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
