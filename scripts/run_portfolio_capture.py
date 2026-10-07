"""Fresh native playable Portfolio run, scripted input; preserve every outcome and frame."""
import argparse
import json
import math
from pathlib import Path
import re
import subprocess
import time
from run_unreal_functional import snapshot_inputs
from run_decision_lab import sha, validate_engine_messages, known_engine_warnings, read_json, strict_json
from run_portfolio_probe import binary_snapshot, number, required_fields
from run_unreal_input_probe import inspect_png
from run_unreal_trial import OFFLINE_ARGUMENT

ROOT = Path(__file__).resolve().parents[1]


def validate_result_quit(capture, log):
    """Prove that a finished capture dispatched X through the actual result-page binding."""
    required_fields(capture, dict(quitPath='normal PlayerController X from result page',
                                  quitKey='X', quitMenuOpen=False, quitPaused=False))
    outcome = capture.get('outcome')
    if outcome not in ('won', 'lost'):
        raise ValueError('Result-page quit requires an actual won or lost outcome')
    number(capture.get('resultHoldSeconds'), 4, 215)
    rows = capture.get('samples')
    phase = 3 if outcome == 'won' else 4
    if (not isinstance(rows, list) or not rows or not isinstance(rows[-1], dict) or
            type(rows[-1].get('phase')) is not int or rows[-1]['phase'] != phase):
        raise ValueError('Final native sample does not show the reported result page')
    direct = re.compile(r'^(?:\[[^\]\r\n]*\])*LogTemp: Display: '
                        r'(AEGIS_(?:PORTFOLIO_CAPTURE_COMPLETE|PORTFOLIO_CAPTURE_RESULT_X|RESULT_X_QUIT)\b[^\r\n]*)$')
    normal_exit = re.compile(r'^(?:\[[^\]\r\n]*\])*LogExit: Exiting\.$')
    events = []
    for line in log.splitlines():
        if match := direct.fullmatch(line): events.append(match.group(1))
        elif normal_exit.fullmatch(line): events.append('normal_exit')
    if len(events) != 4:
        raise ValueError('Missing, malformed or duplicate result-key/shutdown evidence')
    completion = re.fullmatch(r'AEGIS_PORTFOLIO_CAPTURE_COMPLETE valid=1 frames=(\d+) completed=(\d+)', events[0])
    if not completion or int(completion[1]) != int(completion[2]):
        raise ValueError('Result-key quit did not follow successful completed capture')
    if events[1:] != [f'AEGIS_PORTFOLIO_CAPTURE_RESULT_X outcome={outcome}',
                       f'AEGIS_RESULT_X_QUIT outcome={outcome}', 'normal_exit']:
        raise ValueError('Result X was not received before the unique normal engine shutdown')
    return dict(outcome=outcome, key='X', normalExit=True, resultHoldSeconds=capture['resultHoldSeconds'])


def validate_system_capture(output, capture, log):
    """Reconcile the original ledger; both naturally won and lost captures are valid."""
    output = Path(output).resolve()
    reports = list((output / 'system').glob('portfolio-*.json'))
    if len(reports) != 1:
        raise ValueError('Expected exactly one native system report, without omitted restarts')
    report_path = reports[0]
    summary = read_json(report_path)
    required_fields(summary, dict(mode='aegis-portfolio-v1.5', completed=True,
                                  ledgerBalanced=True, traceComplete=True))
    if summary.get('outcome') not in ('won', 'lost') or summary['outcome'] != capture.get('outcome'):
        raise ValueError('Capture outcome differs from the actual system outcome')
    run_id = summary.get('runId')
    if not isinstance(run_id, str) or not re.fullmatch(r'[0-9A-Fa-f]{32}', run_id):
        raise ValueError('Missing original session identity')
    trace = report_path.with_suffix('.jsonl')
    if (report_path.stem != 'portfolio-' + run_id or Path(summary.get('tracePath', '')).resolve() != trace or
            set((output / 'system').glob('portfolio-*.jsonl')) != {trace}):
        raise ValueError('Session trace identity or path mismatch')
    events = [strict_json(line) for line in trace.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
    if (len(events) < 4 or number(summary.get('eventCount'), integer=True) != len(events) or
            any(not isinstance(event, dict) for event in events)):
        raise ValueError('Incomplete native session trace')
    kinds = [event.get('event') for event in events]
    if (kinds[0] != 'session_started' or kinds[-1] != 'session_finished' or
            kinds.count('session_started') != 1 or kinds.count('session_finished') != 1):
        raise ValueError('Session must have exactly one enclosing start and finish')
    balance, earned, spent, overflow = 60, 0, 0, 0
    pulses = repairs = upgrade_mask = 0
    player_heal = companion_heal = damage_dealt = player_damage = companion_damage = unknown_damage = 0.0
    kills, stages, completed_stages = set(), set(), set()
    previous_time, previous_event = -1, None
    phase, wave, pending_repair = 'briefing', 0, False
    identity_names = {}
    for sequence, event in enumerate(events, 1):
        if event.get('runId') != run_id or number(event.get('sequence'), integer=True) != sequence:
            raise ValueError('Native event identity/sequence mismatch')
        stamp = number(event.get('gameSeconds'))
        if stamp < previous_time: raise ValueError('Native event chronology regressed')
        previous_time = stamp
        data, kind = event.get('data'), event['event']
        if not isinstance(data, dict): raise ValueError('Missing event data')
        if pending_repair and kind != 'repair_applied':
            raise ValueError('Paid repair has no immediately recorded real healing')
        if kind == 'session_started':
            for key, target in dict(initialEnergy=60, maximumEnergy=100, pulseCost=35, repairCost=40,
                                    killReward=12, stageReward=25, repairCooldownSeconds=8).items():
                if number(data.get(key)) != target: raise ValueError('Unexpected economic rules in native session')
        elif kind == 'phase_changed':
            phase, wave = data.get('phase'), number(data.get('wave'), 0, 3, True)
            if phase not in ('briefing', 'active', 'intermission', 'won', 'lost'):
                raise ValueError('Unknown native phase')
            if phase == 'intermission': completed_stages.add(wave)
            if phase == 'won': completed_stages.add(3)
        elif kind == 'damage':
            amount = number(data.get('actualDamage'), 0.000001)
            number(data.get('victimHealthAfter'))
            identity = number(data.get('victimRunIdentity'), 1, integer=True)
            victim = data.get('victim')
            if not isinstance(victim, str) or not victim or type(data.get('enemyVictim')) is not bool:
                raise ValueError('Damage identity/role is malformed')
            if identity in identity_names and identity_names[identity] != victim:
                raise ValueError('A native run identity was reused for another victim')
            identity_names[identity] = victim
            if data['enemyVictim']: damage_dealt += amount
            elif re.fullmatch(r'AegisPlayerCharacter(?:_\d+)?', victim): player_damage += amount
            elif re.fullmatch(r'AegisAICharacter(?:_\d+)?', victim): companion_damage += amount
            else: unknown_damage += amount
        elif kind == 'energy_transaction':
            before = number(data.get('before'), 0, 100, True)
            reason = data.get('reason')
            if before != balance: raise ValueError('Transaction previous balance does not reconcile')
            if reason in ('pulse', 'repair'):
                if phase != 'active': raise ValueError('Ability spent energy outside active gameplay')
                offered = -35 if reason == 'pulse' else -40
                if balance < -offered: raise ValueError('Native transaction overspent energy')
                actual = offered
                if reason == 'pulse': pulses += 1
                else: repairs += 1; pending_repair = True
            elif reason == 'enemy_defeated':
                if not previous_event or previous_event.get('event') != 'damage':
                    raise ValueError('Kill reward does not follow an actual damage event')
                hit = previous_event['data']
                identity = hit.get('victimRunIdentity')
                if hit.get('enemyVictim') is not True or hit.get('victimHealthAfter') != 0 or identity in kills:
                    raise ValueError('Kill reward lacks a unique actual enemy death')
                kills.add(identity); offered = 12; actual = min(12, 100 - balance)
            elif isinstance(reason, str) and re.fullmatch(r'stage_[123]_complete', reason):
                stage = int(reason[6])
                if stage not in completed_stages or stage in stages or phase == 'lost':
                    raise ValueError('Stage reward repeated or not backed by completion')
                stages.add(stage); offered = 25; actual = min(25, 100 - balance)
            else: raise ValueError('Unknown resource transaction reason')
            if data.get('offeredDelta') != offered or data.get('actualDelta') != actual:
                raise ValueError('Transaction cost/reward or cap clipping is incorrect')
            balance += actual
            earned += max(actual, 0); spent += max(-actual, 0)
            overflow += max(0, offered - actual)
            if data.get('after') != balance or data.get('overflowTotal') != overflow:
                raise ValueError('Transaction ending balance/overflow mismatch')
        elif kind == 'repair_applied':
            if not pending_repair: raise ValueError('Healing is not backed by a paid repair')
            self_heal = number(data.get('playerActualHealing'), 0, 30)
            ally_heal = number(data.get('companionActualHealing'), 0, 20)
            if self_heal + ally_heal <= 0: raise ValueError('A paid repair healed nobody')
            if not math.isclose(number(data.get('cooldownReadyGameSeconds')) - stamp, 8, abs_tol=0.001):
                raise ValueError('Repair did not use the declared 8-second cooldown')
            player_heal += self_heal; companion_heal += ally_heal; pending_repair = False
        elif kind == 'upgrade_selected':
            index = number(data.get('upgradeIndex'), 1, 3, True)
            bit = 1 << (int(index) - 1)
            if upgrade_mask & bit or phase != 'intermission': raise ValueError('Repeated or out-of-phase upgrade')
            upgrade_mask |= bit
        elif kind not in ('pulse_rejected', 'repair_rejected', 'session_finished'):
            raise ValueError('Unknown native event')
        if number(event.get('energy'), 0, 100, True) != balance:
            raise ValueError('Energy changed without a matching transaction')
        previous_event = event
    if phase != summary['outcome'] or pending_repair: raise ValueError('Trace did not reach the reported final state')
    expected = dict(energyInitial=60, energyFinal=balance, energyEarned=earned, energySpent=spent,
                    energyOverflow=overflow, pulsesUsed=pulses, repairsUsed=repairs, enemiesRewarded=len(kills),
                    stagesRewarded=len(stages), repairPlayerActualHealing=player_heal,
                    repairCompanionActualHealing=companion_heal, alliedActualDamageDealt=damage_dealt,
                    upgradeMask=upgrade_mask)
    if unknown_damage == 0:
        expected.update(playerActualDamageTaken=player_damage, companionActualDamageTaken=companion_damage)
    elif not math.isclose(number(summary.get('playerActualDamageTaken')) + number(summary.get('companionActualDamageTaken')),
                          player_damage + companion_damage + unknown_damage, abs_tol=0.02, rel_tol=1e-6):
        raise ValueError('Combined allied damage taken does not reconcile')
    for key, value in expected.items():
        if not math.isclose(number(summary.get(key)), value, abs_tol=0.02 if isinstance(value, float) else 0, rel_tol=1e-6):
            raise ValueError(f'Final native summary differs from trace: {key}')
        if events[-1]['data'].get(key) != summary.get(key):
            raise ValueError(f'Final event differs from native summary: {key}')
    if earned + overflow != 12 * len(kills) + 25 * len(stages) or balance != 60 + earned - spent:
        raise ValueError('Economic conservation failed')
    marker = re.compile(r'^(?:\[[^\]\r\n]*\])*LogTemp: Display: AEGIS_PORTFOLIO_COMPLETE run=([^ ]+) outcome=([^ ]+) report=(.+)$')
    markers = [match.groups() for line in log.splitlines() if (match := marker.fullmatch(line))]
    if len(markers) != 1 or markers[0][:2] != (run_id, summary['outcome']) or Path(markers[0][2]).resolve() != report_path:
        raise ValueError('Missing unique matching actual system completion marker')
    return {'path': str(report_path), 'sha256': sha(report_path), 'traceSha256': sha(trace),
            'events': len(events), 'outcome': summary['outcome'], 'ledgerReconciled': True,
            'damageRoleVerification': 'full' if unknown_damage == 0 else 'combined-only',
            'pulsesUsed': pulses, 'repairsUsed': repairs, 'actualRepairHealing': player_heal + companion_heal}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--editor', action='store_true')
    p.add_argument('--no-frames', action='store_true')
    p.add_argument('--interval', type=float, default=1/30)
    p.add_argument('--width', type=int, default=1280)
    p.add_argument('--height', type=int, default=720)
    p.add_argument('--seed', type=int, default=1101)
    args = p.parse_args()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    exe = args.exe.resolve()
    inputs = snapshot_inputs(ROOT)
    command = [str(exe)]
    if args.editor: command += [str(ROOT/'AegisArena.uproject'), '/Game/Aegis/Maps/AegisArena', '-game']
    command += ['-AegisPortfolio', '-AegisPortfolioCapture', '-AegisInputProbe', '-RenderOffscreen', '-d3d11',
                '-windowed', '-ForceRes', f'-ResX={args.width}', f'-ResY={args.height}', '-UseFixedTimeStep', '-FPS=30',
                '-unattended', '-nop4', '-nosound', '-nosplash', '-stdout', '-FullStdOutLogOutput', OFFLINE_ARGUMENT,
                f'-AegisPortfolioCaptureOutput={out}', f'-AegisPortfolioOutput={out / "system"}',
                f'-AegisPortfolioFrameInterval={args.interval}', f'-AegisPortfolioSeed={args.seed}', f'-abslog={out / "engine.log"}']
    if args.no_frames: command += ['-AegisPortfolioNoFrames']
    record = {'schemaVersion': 1, 'engine': 'unreal-runtime', 'launch': 'editor-game' if args.editor else 'packaged-development',
              'scriptedInput': True, 'fixtureDamage': False, 'humanPlaytest': False, 'phase': 'development-demonstration',
              'source': inputs, 'inputsBefore': inputs, 'command': command,
              'passed': False, 'recording': 'native rendered PNGs with game/video timestamps; no generated gameplay frames'}
    started = time.monotonic()
    try:
        record['binariesBefore'] = binary_snapshot(exe, editor=args.editor)
        record['binary'] = record['binariesBefore'].get('projectModule', record['binariesBefore'].get('gamePayload', record['binariesBefore']['launcher']))
        record['wrappersBefore'] = {name: sha(ROOT/'scripts'/name) for name in (
            'run_portfolio_capture.py', 'run_portfolio_probe.py', 'run_decision_lab.py',
            'run_unreal_functional.py', 'run_unreal_input_probe.py', 'run_unreal_trial.py')}
        with (out/'stdout.log').open('w', encoding='utf-8') as log:
            result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=1600)
        record['exitCode'] = result.returncode
        if result.returncode != 0: raise ValueError('Capture process exited unsuccessfully')
        log = (out/'engine.log').read_text(encoding='utf-8-sig')
        validate_engine_messages(log, rendered=True)
        if any(marker in log for marker in ('Handled ensure', 'Ensure condition failed', 'Assertion failed', 'Fatal error:')):
            raise ValueError('Capture log contains an ensure/fatal failure')
        capture = read_json(out/'capture.json')
        required_fields(capture, dict(validRun=True, scriptedPlayer=True, fixtureDamage=False, humanPlaytest=False))
        frames = number(capture.get('frames'), 0 if args.no_frames else 1, integer=True)
        if number(capture.get('completedFrames'), integer=True) != frames: raise ValueError('Incomplete native frames')
        if not isinstance(capture.get('samples'), list) or not capture['samples']: raise ValueError('Missing capture samples')
        number(capture.get('inputs'), 1, integer=True); number(capture.get('videoSeconds'), 0.001)
        completion = re.compile(r'^(?:\[[^\]\r\n]*\])*LogTemp: Display: AEGIS_PORTFOLIO_CAPTURE_COMPLETE valid=(\d+) frames=(\d+) completed=(\d+)$')
        markers = [tuple(map(int, m.groups())) for line in log.splitlines() if (m := completion.fullmatch(line))]
        if markers != [(1, frames, frames)]: raise ValueError('Missing unique original capture completion marker')
        record['resultQuitEvidence'] = validate_result_quit(capture, log)
        record['systemEvidence'] = validate_system_capture(out, capture, log)
        pngs = sorted((out/'frames').glob('*.png'))
        if len(pngs) != frames: raise ValueError('Native image count differs from capture')
        for png in pngs:
            # inspect_png uses strict native dimensions and nonempty PNG decoding header checks.
            info = inspect_png(png, expected_size=(args.width, args.height))
            if info['width'] != args.width or info['height'] != args.height: raise ValueError('Native resolution mismatch')
        record.update(outcome=capture['outcome'], frames=len(pngs), videoSeconds=capture['videoSeconds'],
                      knownEngineWarnings=known_engine_warnings(log, True))
    except Exception as e:
        record['failure'] = repr(e)
    finally:
        try:
            record['inputsAfter'] = snapshot_inputs(ROOT)
            record['binariesAfter'] = binary_snapshot(exe, editor=args.editor)
            record['wrappersAfter'] = {name: sha(ROOT/'scripts'/name) for name in (
                'run_portfolio_capture.py', 'run_portfolio_probe.py', 'run_decision_lab.py',
                'run_unreal_functional.py', 'run_unreal_input_probe.py', 'run_unreal_trial.py')}
            for label in ('inputs', 'binaries', 'wrappers'):
                if record.get(label+'Before') != record.get(label+'After'):
                    record['failure'] = record.get('failure', '') + f' | {label} changed or could not be bound'
        except (OSError, ValueError) as error:
            record['failure'] = record.get('failure', '') + f' | final snapshot failed: {error}'
        record['passed'] = ('failure' not in record and 'systemEvidence' in record and
                            'resultQuitEvidence' in record and 'frames' in record)
        record['wallSeconds'] = time.monotonic()-started
        record['files'] = {str(f.relative_to(out)): {'bytes': f.stat().st_size, 'sha256': sha(f)}
                           for f in sorted(out.rglob('*')) if f.is_file() and f.name != 'provenance.json'}
        (out/'provenance.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
    print(json.dumps({k:v for k,v in record.items() if k not in ('files','command')}, indent=2))
    return 0 if record['passed'] else 2

if __name__ == '__main__': raise SystemExit(main())
