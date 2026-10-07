"""Strict rendered native Portfolio input/transaction fixture; never a human or policy evaluation."""
from __future__ import annotations
import argparse
import datetime
import json
import math
import os
from pathlib import Path
import platform
import re
import subprocess
import time

from run_decision_lab import (binary_snapshot as lab_binary_snapshot, known_engine_warnings,
                             read_json, sha, strict_json, validate_engine_messages)
from run_unreal_functional import snapshot_inputs
from run_unreal_trial import OFFLINE_ARGUMENT

ROOT = Path(__file__).resolve().parents[1]
MAP = '/Game/Aegis/Maps/AegisArena'
ASSERTIONS = (
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
BEGIN = 'AEGIS_PORTFOLIO_PROBE_BEGIN syntheticKeyboard=1 fixtureDamage=1 aiFrozen=1 humanPlay=0'
PASS = 'AEGIS_PORTFOLIO_PROBE_PASS checks=25'
DIRECT = re.compile(r'^(?:\[[^\]\r\n]*\])*LogTemp: Display: (AEGIS_PORTFOLIO_PROBE_[^\r\n]+)$')
EXIT = re.compile(r'^(?:\[[^\]\r\n]*\])*LogExit: Exiting\.$')
REASON = 'completed 25 transaction and input checks; normal menu X requests quit'


def number(value, low=0, high=math.inf, integer=False):
    if (type(value) not in (int, float) or not math.isfinite(value) or not low <= value <= high or
            (integer and value != int(value))):
        raise ValueError('Missing, nonfinite or out-of-range numeric evidence')
    return value


def required_fields(value, expected):
    if not isinstance(value, dict):
        raise ValueError('Expected an evidence object')
    for key, target in expected.items():
        if type(value.get(key)) is not type(target) or value[key] != target:
            raise ValueError(f'Unexpected evidence field {key}')


def validate_sessions(report, output):
    """Bind every observed restart to complete native ledgers written by normal teardown."""
    system = Path(output).resolve() / 'system'
    paths = report.get('portfolioTracePaths')
    if not isinstance(paths, list) or len(paths) != 5 or len(set(paths)) != 5:
        raise ValueError('Expected five distinct native sessions across four restarts')
    summaries = []
    for index, name in enumerate(paths):
        if not isinstance(name, str) or not Path(name).is_absolute():
            raise ValueError('Native trace path must be absolute')
        path = Path(name).resolve(strict=True)
        if path.parent != system or not re.fullmatch(r'portfolio-[0-9A-Fa-f]{32}\.jsonl', path.name):
            raise ValueError('Native trace is not contained in this run system directory')
        summary = read_json(path.with_suffix('.json'))
        required_fields(summary, dict(mode='aegis-portfolio-v1.5', completed=False,
                                      outcome='aborted' if index == 4 else 'restarted',
                                      ledgerBalanced=True, traceComplete=True))
        run_id = path.stem.removeprefix('portfolio-')
        if summary.get('runId') != run_id or Path(summary.get('tracePath', '')).resolve() != path:
            raise ValueError('Session identity/path mismatch')
        events = [strict_json(line) for line in path.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
        if (len(events) < 3 or len(events) != summary.get('eventCount') or
                not isinstance(events[0], dict) or events[0].get('event') != 'session_started' or
                not isinstance(events[-1], dict) or events[-1].get('event') != 'session_finished'):
            raise ValueError('Session trace is incomplete')
        balance, earned, spent, previous = 60, 0, 0, -1
        for sequence, event in enumerate(events, 1):
            if not isinstance(event, dict) or event.get('runId') != run_id or event.get('sequence') != sequence:
                raise ValueError('Session event sequence or identity mismatch')
            stamp = number(event.get('gameSeconds'))
            if stamp < previous: raise ValueError('Session game clock regressed')
            previous = stamp
            data = event.get('data')
            if not isinstance(data, dict): raise ValueError('Missing session event data')
            if event.get('event') == 'energy_transaction':
                before = number(data.get('before'), 0, 100, True)
                delta = number(data.get('actualDelta'), -40, 25, True)
                if before != balance or data.get('after') != before + delta:
                    raise ValueError('Transaction does not reconcile with previous balance')
                balance += delta
                earned += max(0, delta)
                spent += max(0, -delta)
            if number(event.get('energy'), 0, 100, True) != balance:
                raise ValueError('Energy changed without an accounting transaction')
        pulses, repairs = (1 if index == 1 else 0), (1 if index == 0 else 0)
        expected = dict(energyInitial=60, energyFinal=balance, energyEarned=earned, energySpent=spent,
                        energyOverflow=0, pulsesUsed=pulses, repairsUsed=repairs, enemiesRewarded=0,
                        stagesRewarded=0, repairPlayerActualHealing=30 if index == 0 else 0,
                        repairCompanionActualHealing=0, playerActualDamageTaken=45 if index == 0 else 0)
        for key, value in expected.items():
            if number(summary.get(key)) != value: raise ValueError(f'Fixture ledger mismatch: {key}')
        if spent != pulses * 35 + repairs * 40 or balance != 60 + earned - spent:
            raise ValueError('Fixture expenditure does not match accepted real abilities')
        final = events[-1]['data']
        if any(final.get(key) != summary.get(key) for key in expected):
            raise ValueError('Final session event differs from summary')
        summaries.append({'path': str(path.with_suffix('.json')), 'sha256': sha(path.with_suffix('.json')),
                          'traceSha256': sha(path), 'outcome': summary['outcome']})
    if {p.resolve() for p in system.glob('portfolio-*.jsonl')} != {Path(p).resolve() for p in paths}:
        raise ValueError('Unreported native session in this run')
    return summaries


def validate(report, log, output):
    required_fields(report, dict(schemaVersion=1, passed=True, engine='unreal-runtime', fixtureDamage=True,
                                aiFrozen=True, syntheticKeyboardInput=True, humanUsabilityTest=False,
                                policyPerformanceTest=False, renderOffscreen=True, nullRHI=False,
                                fixtureDamageSource='existing hostile actor; direct Health.ApplyDamage API',
                                quitPath='normal PlayerController X from paused menu', reason=REASON))
    if number(report.get('fixtureDamageApplied')) != 45 or number(report.get('lastStage'), integer=True) != 20:
        raise ValueError('Probe did not execute the expected damage/lifecycle stages')
    number(report.get('frozenControllerCount'), 2, 1000, True)
    seconds = number(report.get('wallSeconds'), 0.1, 35)
    rows = report.get('assertions')
    if (not isinstance(rows, list) or len(rows) != 25 or
            any(not isinstance(row, dict) or row.get('passed') is not True for row in rows) or
            [row.get('name') for row in rows] != list(ASSERTIONS)):
        raise ValueError('All 25 unique native assertions must pass in original order')
    previous_wall = previous_game = -1
    for row in rows:
        wall, game = number(row.get('wallSeconds'), 0, seconds), number(row.get('gameSeconds'))
        if wall < previous_wall or game < previous_game: raise ValueError('Native assertion clocks regressed')
        previous_wall, previous_game = wall, game
        number(row.get('energy'), 0, 100, True)
        number(row.get('playerHealth'), 0, 140)
        number(row.get('pulseActivations'), 0, 1000, True)
        number(row.get('actualPlayerShots'), 0, 1000, True)
    snapshots = {row['name']: row for row in rows}
    for name, expected in {
        'briefing_preset_and_budget': (60, 140, 0),
        'disclosed_fixture_damage_applies_45': (60, 95, 0),
        'repair_applies_and_records_actual_30': (20, 125, 0),
        'low_energy_pulse_preserves_gameplay_cooldown': (20, 125, 0),
        'restart_resets_energy_health_cooldown_and_statistics': (60, 140, 0),
        'accepted_pulse_updates_actual_counter_and_cooldown': (25, 140, 1),
        'pulse_cooldown_repeat_does_not_double_charge': (25, 140, 1),
        'paused_q_and_e_preserve_time_and_resources': (60, 140, 0),
        'redeploy_does_not_retain_held_fire': (60, 140, 0),
    }.items():
        row = snapshots[name]
        if tuple(row[key] for key in ('energy', 'playerHealth', 'pulseActivations')) != expected:
            raise ValueError('Native numeric observation contradicts passing assertion')
    if (snapshots['normal_held_fire_actually_fires']['actualPlayerShots'] <= 0 or
            snapshots['redeploy_does_not_retain_held_fire']['actualPlayerShots'] != 0):
        raise ValueError('Held-fire release was not observed')
    events = [match.group(1) for line in log.splitlines() if (match := DIRECT.fullmatch(line))]
    expected_events = [BEGIN, *('AEGIS_PORTFOLIO_PROBE_ASSERT_PASS ' + name for name in ASSERTIONS), PASS]
    if events != expected_events:
        raise ValueError('Missing, duplicate, malformed or failing original native log markers')
    if any(marker in log for marker in ('Handled ensure', 'Ensure condition failed', 'Fatal error:',
                                        'Assertion failed', 'AEGIS_PORTFOLIO_PROBE_FAIL')):
        raise ValueError('Engine log contains a failure or ensure')
    validate_engine_messages(log, rendered=True)
    if sum(bool(EXIT.fullmatch(line)) for line in log.splitlines()) != 1:
        raise ValueError('No unique normal engine shutdown after menu quit')
    return validate_sessions(report, output)


def build_command(exe, output, editor=False):
    exe, output = Path(exe).resolve(), Path(output).resolve()
    prefix = [str(exe)] + ([str(ROOT / 'AegisArena.uproject'), MAP, '-game'] if editor else [MAP])
    return prefix + ['-AegisPortfolio', '-AegisPortfolioProbe', '-AegisInputProbe', '-RenderOffscreen', '-d3d11',
                     f'-AegisPortfolioProbeOutput={output}', f'-AegisPortfolioOutput={output / "system"}',
                     '-ResX=1280', '-ResY=720', '-windowed', '-unattended', '-nop4', '-nosound', '-nosplash',
                     OFFLINE_ARGUMENT, '-stdout', '-FullStdOutLogOutput', f'-abslog={output / "engine.log"}']


def binary_snapshot(exe, editor=False):
    exe = Path(exe).resolve(strict=True)
    result = lab_binary_snapshot(exe, packaged=not editor)
    if not editor:
        project = exe.parent.parent.parent if exe.parent.name.casefold() == 'win64' else exe.parent / 'AegisArena'
        pak_dir = project / 'Content/Paks'
        containers = sorted(p for p in pak_dir.glob('*') if p.is_file() and p.suffix.lower() in ('.pak', '.utoc', '.ucas', '.sig'))
        if not any(p.suffix.lower() in ('.pak', '.utoc', '.ucas') for p in containers):
            raise ValueError('Packaged cooked containers are missing')
        result['cookedContainers'] = {p.name: {'bytes': p.stat().st_size, 'sha256': sha(p)} for p in containers}
    return result


def wrapper_snapshot():
    names = ('run_portfolio_probe.py', 'run_decision_lab.py', 'run_unreal_functional.py',
             'run_unreal_input_probe.py', 'run_unreal_trial.py')
    return {name: sha(ROOT / 'scripts' / name) for name in names}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True, help='Editor-Cmd or packaged Development launcher/payload')
    parser.add_argument('--editor', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cache-root', type=Path)
    parser.add_argument('--engine-root', type=Path, help='Optional explicit engine Build.version provenance')
    args = parser.parse_args()
    exe = args.exe.resolve()
    if not exe.is_file(): parser.error('Executable is missing')
    if args.editor != exe.name.casefold().startswith('unrealeditor'):
        parser.error('--editor must match an UnrealEditor executable; otherwise use a packaged Development game')
    if 'shipping' in exe.name.casefold(): parser.error('This disclosed probe is compiled only in Development')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    command = build_command(exe, output, args.editor)
    record = dict(schemaVersion=1, passed=False, engine='unreal-runtime',
                  launch='editor-game' if args.editor else 'packaged-development', command=command,
                  host=platform.platform(), startedAtUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  syntheticKeyboardInput=True, fixtureDamage=True, aiFrozen=True, humanUsabilityTest=False,
                  policyPerformanceTest=False, rendering='RenderOffscreen', actorDeadlineSeconds=35,
                  processTimeoutSeconds=120, expectedAssertions=list(ASSERTIONS),
                  scope='Native transaction/input fixture; one explicit hostile damage call and frozen AI',
                  sourceDigestScope='Source, core/include, Config, AegisArena.uproject; sorted relative path + bytes',
                  contentDigestScope='Content/Aegis; sorted relative path + bytes; packaged cooked containers separately hashed')
    started = time.monotonic()
    try:
        record['inputsBefore'] = snapshot_inputs(ROOT)
        record['binariesBefore'] = binary_snapshot(exe, args.editor)
        record['wrappersBefore'] = wrapper_snapshot()
        record['gitHead'] = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
        record['gitStatus'] = subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain'], text=True).splitlines()
        engine = args.engine_root.resolve() if args.engine_root else (exe.parents[3] if args.editor else None)
        if engine:
            build_path = engine / 'Engine/Build/Build.version'
            record['engineBuild'] = read_json(build_path)
            record['engineBuildSha256'] = sha(build_path)
        environment = os.environ.copy()
        if args.cache_root:
            cache = args.cache_root.resolve()
            (cache / 'Temp').mkdir(parents=True, exist_ok=True)
            environment.update(TEMP=str(cache / 'Temp'), TMP=str(cache / 'Temp'))
            environment['UE-LocalDataCachePath'] = str(cache / 'DerivedDataCache')
        with (output / 'stdout.log').open('w', encoding='utf-8') as stream:
            result = subprocess.run(command, cwd=ROOT if args.editor else exe.parent, env=environment,
                                    stdout=stream, stderr=subprocess.STDOUT, timeout=120, check=False)
        record['exitCode'] = result.returncode
        if result.returncode != 0: raise RuntimeError(f'Native probe exited {result.returncode}')
        log = (output / 'engine.log').read_text(encoding='utf-8-sig', errors='replace')
        report = read_json(output / 'portfolio-probe.json')
        record['sessions'] = validate(report, log, output)
        record['nativeAssertions'] = len(ASSERTIONS)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        record['failure'] = f'{type(error).__name__}: {error}'
    finally:
        try:
            record['inputsAfter'] = snapshot_inputs(ROOT)
            record['binariesAfter'] = binary_snapshot(exe, args.editor)
            record['wrappersAfter'] = wrapper_snapshot()
            for label in ('inputs', 'binaries', 'wrappers'):
                if record.get(label + 'Before') != record.get(label + 'After'):
                    record['failure'] = record.get('failure', '') + f' | {label} changed or could not be bound'
        except (OSError, ValueError) as error:
            record['failure'] = record.get('failure', '') + f' | final snapshot failed: {error}'
        log_path = output / 'engine.log'
        record['knownEngineWarnings'] = known_engine_warnings(
            log_path.read_text(encoding='utf-8-sig', errors='replace') if log_path.is_file() else '', True)
        record['wallSeconds'] = time.monotonic() - started
        record['passed'] = 'failure' not in record and record.get('nativeAssertions') == 25
        record['files'] = {p.relative_to(output).as_posix(): {'bytes': p.stat().st_size, 'sha256': sha(p)}
                           for p in sorted(output.rglob('*')) if p.is_file() and p.name != 'provenance.json'}
        (output / 'provenance.json').write_text(json.dumps(record, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in record.items() if k in ('passed', 'failure', 'nativeAssertions', 'wallSeconds')}, indent=2))
    return 0 if record['passed'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
