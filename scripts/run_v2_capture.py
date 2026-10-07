"""Fresh v2 native scripted run: exact transactions, frames and result-key exit; never a human trial."""
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
from v23_survey_gate import SurveyLedger

ROOT = Path(__file__).resolve().parents[1]
MODE = 'aegis-prism-fall-v2.0'
STABILITY = 'Observed tactical DebugState transitions; includes normal aim/fire/reposition cycles, not Utility switching'
WRAPPERS = ('run_v2_capture.py', 'v23_survey_gate.py', 'run_portfolio_probe.py', 'run_decision_lab.py',
            'run_unreal_functional.py', 'run_unreal_input_probe.py', 'run_unreal_trial.py')


def boolean(value):
    if type(value) is not bool: raise ValueError('Expected an actual boolean')
    return value


def finite_tree(value):
    if isinstance(value, dict):
        for child in value.values(): finite_tree(child)
    elif isinstance(value, list):
        for child in value: finite_tree(child)
    elif type(value) in (int, float) and not math.isfinite(value):
        raise ValueError('Nonfinite native evidence')


def repair_cost(healing):
    """Match UE's double sum/multiply of the two actual float recipient amounts."""
    return min(40, max(8, math.ceil(healing * .8)))


def exact_native(log, prefix):
    pattern = re.compile(r'^(?:\[[^\]\r\n]*\])*LogTemp: Display: (' + re.escape(prefix) + r'[^\r\n]*)$')
    matching = [match.group(1) for line in log.splitlines() if (match := pattern.fullmatch(line))]
    if sum(prefix in line for line in log.splitlines()) != len(matching):
        raise ValueError('Replayed or malformed native marker: ' + prefix)
    return matching


def validate_capture(capture, log, output, *, no_frames=False, width=1280, height=720):
    """Validate capture scope, ordered original PNGs, sample clocks and normal X shutdown."""
    finite_tree(capture)
    validate_engine_messages(log, rendered=True)
    if any(token in log for token in ('Handled ensure', 'Ensure condition failed', 'Assertion failed', 'Fatal error:')):
        raise ValueError('Capture log contains an ensure/fatal failure')
    required_fields(capture, dict(mode=MODE, validRun=True, scriptedPlayer=True,
                                  fixtureDamage=False, humanPlaytest=False,
                                  audioCapture='Engine-triggered event log for offline mix; not loopback recording'))
    frames = int(number(capture.get('frames'), 0 if no_frames else 1, integer=True))
    if no_frames and frames != 0: raise ValueError('No-frames run reported screenshots')
    if number(capture.get('completedFrames'), integer=True) != frames: raise ValueError('Incomplete native frames')
    duration = number(capture.get('videoSeconds'), .001, 215)
    number(capture.get('inputs'), 1, integer=True)
    begin = f'AEGIS_PORTFOLIO_CAPTURE_BEGIN scripted_input=1 fixture_damage=0 frames={int(not no_frames)}'
    if exact_native(log, 'AEGIS_PORTFOLIO_CAPTURE_BEGIN') != [begin]: raise ValueError('Missing unique capture BEGIN')
    complete = f'AEGIS_PORTFOLIO_CAPTURE_COMPLETE valid=1 frames={frames} completed={frames}'
    if exact_native(log, 'AEGIS_PORTFOLIO_CAPTURE_COMPLETE') != [complete]: raise ValueError('Missing unique capture COMPLETE')
    for prefix in ('AEGIS_PORTFOLIO_CAPTURE_RESULT_X', 'AEGIS_RESULT_X_QUIT'):
        exact_native(log, prefix)
    quit_proof = validate_result_quit(capture, log)
    times = capture.get('frameTimes')
    if not isinstance(times, list) or len(times) != frames: raise ValueError('Missing frame timestamp rows')
    frame_dir = Path(output).resolve() / 'frames'
    expected_names = [f'frame-{index:06d}.png' for index in range(frames)]
    actual_names = sorted(path.name for path in frame_dir.glob('*.png'))
    if actual_names != expected_names: raise ValueError('Missing, additional or nonconsecutive native PNG')
    previous_video, previous_world = -1, -1
    for index, row in enumerate(times):
        if not isinstance(row, dict) or row.get('file') != expected_names[index]:
            raise ValueError('Frame path/order is not the original native sequence')
        video = number(row.get('videoSeconds'), 0, duration)
        world = number(row.get('worldSeconds'))
        if video <= previous_video or world < previous_world: raise ValueError('Frame clocks regressed or duplicated')
        info = inspect_png(frame_dir / row['file'], expected_size=(width, height))
        if (info['width'], info['height']) != (width, height): raise ValueError('Wrong native PNG size')
        previous_video, previous_world = video, world
    rows = capture.get('samples')
    if not isinstance(rows, list) or not rows: raise ValueError('Missing native samples')
    previous_video = previous_world = -1
    previous_counts = {}
    for row in rows:
        if not isinstance(row, dict): raise ValueError('Malformed sample')
        video = number(row.get('videoSeconds'), 0, duration)
        world = number(row.get('worldSeconds'))
        # The runtime samples both a phase transition and the periodic timer in one tick.
        if video < previous_video or world < previous_world: raise ValueError('Sample clocks regressed')
        previous_video, previous_world = video, world
        number(row.get('frame'), 0, frames, True)
        number(row.get('phase'), 0, 4, True); wave = number(row.get('wave'), 0, 3, True)
        number(row.get('relay'), 0, 2, True); number(row.get('enemies'), 0, 8, True)
        number(row.get('energy'), 0, 100, True); number(row.get('health'), 0, 140)
        number(row.get('charge'), 0, 10); number(row.get('chargeFraction'), 0, 1)
        quote = number(row.get('repairQuote'), 0, 40, True)
        if 0 < quote < 8: raise ValueError('Invalid preview repair price')
        number(row.get('overclockRemaining'), 0, 6.0001)
        required = number(row.get('requiredObjectiveSeconds'), 0, 10)
        if wave and required != {1: 8, 2: 10, 3: 4}[wave]: raise ValueError('Wrong v2 objective timing')
        for key in ('charging', 'contested', 'diagnostics', 'combatEnabled', 'moveInputIgnored', 'northRouteFirst'):
            boolean(row.get(key))
        for key, length in (('position', 2), ('driverDirection', 2), ('driverGoal', 3)):
            values = row.get(key)
            if not isinstance(values, list) or len(values) != length: raise ValueError('Malformed sampled vector')
            for value in values: number(value, -1e7, 1e7)
        for key in ('heldWasd', 'actualWasd'):
            values = row.get(key)
            if not isinstance(values, list) or len(values) != 4: raise ValueError('Malformed input snapshot')
            for value in values: boolean(value)
        for key in ('shots', 'pulses', 'repairs', 'chargedShots', 'overclocks', 'symbiosisHealing'):
            value = number(row.get(key), integer=key != 'symbiosisHealing')
            if value < previous_counts.get(key, 0): raise ValueError('Cumulative sample counter regressed')
            previous_counts[key] = value
        if 'companionHealth' in row: number(row['companionHealth'], 0, 1000)
        if 'companionSight' in row: boolean(row['companionSight'])
        for key in ('companionState', 'guardSlotReason'):
            if key in row and not isinstance(row[key], str): raise ValueError('Malformed companion state')
    if duration - previous_video > .3: raise ValueError('No sample near the actual capture end')
    audio = capture.get('audioEvents')
    if not isinstance(audio, list): raise ValueError('Missing actual audio event array')
    last_audio = -1
    for row in audio:
        if not isinstance(row, dict) or not isinstance(row.get('asset'), str) or not re.fullmatch(r'S_[A-Za-z0-9_]+', row['asset']):
            raise ValueError('Malformed actual sound event')
        stamp = number(row.get('videoSeconds'), 0, duration)
        if stamp < last_audio: raise ValueError('Sound chronology regressed')
        last_audio = stamp
        number(row.get('worldSeconds')); number(row.get('volume'), 0, 4)
        if not isinstance(row.get('location'), list) or len(row['location']) != 3: raise ValueError('Malformed sound position')
        for value in row['location']: number(value, -1e7, 1e7)
    return {'resultQuitEvidence': quit_proof, 'frames': frames, 'videoSeconds': duration,
            'knownEngineWarnings': known_engine_warnings(log, True)}


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
    """Reconcile actual v2 events; both naturally won and lost captures are valid."""
    output = Path(output).resolve()
    reports = list((output / 'system').glob('portfolio-*.json'))
    if len(reports) != 1:
        raise ValueError('Expected exactly one native system report, without omitted restarts')
    report_path = reports[0]
    summary = read_json(report_path)
    finite_tree(summary)
    required_fields(summary, dict(mode=MODE, completed=True,
                                  ledgerBalanced=True, traceComplete=True))
    if summary.get('outcome') not in ('won', 'lost') or summary['outcome'] != capture.get('outcome'):
        raise ValueError('Capture outcome differs from the actual system outcome')
    run_id = summary.get('runId')
    if not isinstance(run_id, str) or not re.fullmatch(r'[0-9A-Fa-f]{32}', run_id):
        raise ValueError('Missing original session identity')
    trace = report_path.with_suffix('.jsonl')
    if (not isinstance(summary.get('tracePath'), str) or not Path(summary['tracePath']).is_absolute() or
            report_path.stem != 'portfolio-' + run_id or Path(summary['tracePath']).resolve() != trace or
            set((output / 'system').glob('portfolio-*.jsonl')) != {trace}):
        raise ValueError('Session trace identity or path mismatch')
    events = [strict_json(line) for line in trace.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
    finite_tree(events)
    if (len(events) < 4 or number(summary.get('eventCount'), integer=True) != len(events) or
            any(not isinstance(event, dict) for event in events)):
        raise ValueError('Incomplete native session trace')
    kinds = [event.get('event') for event in events]
    if (kinds[0] != 'session_started' or kinds[-1] != 'session_finished' or
            kinds.count('session_started') != 1 or kinds.count('session_finished') != 1):
        raise ValueError('Session must have exactly one enclosing start and finish')
    balance, earned, spent, overflow = 60, 0, 0, 0
    pulses = repairs = upgrade_mask = 0
    charged = overclocks = changes = reversals = 0
    symbiosis_player = symbiosis_companion = 0.0
    north_first = False
    companion_last = companion_previous = ''
    companion_changed_at = -math.inf
    overclock_keys = set()
    player_heal = companion_heal = damage_dealt = player_damage = companion_damage = unknown_damage = 0.0
    kills, stages, completed_stages = set(), set(), set()
    previous_time, previous_event = -1, None
    phase, wave, pending_repair, pending_overclock = 'briefing', 0, 0, False
    repair_ready = -math.inf
    identity_names = {}
    survey = SurveyLedger()
    for sequence, event in enumerate(events, 1):
        if event.get('runId') != run_id or number(event.get('sequence'), integer=True) != sequence:
            raise ValueError('Native event identity/sequence mismatch')
        stamp = number(event.get('gameSeconds'))
        if stamp < previous_time: raise ValueError('Native event chronology regressed')
        previous_time = stamp
        data, kind = event.get('data'), event['event']
        if not isinstance(data, dict): raise ValueError('Missing event data')
        survey_event = survey.observe(kind, data, stamp, phase, wave)
        if pending_repair and kind != 'repair_applied':
            raise ValueError('Paid repair has no immediately recorded real healing')
        if pending_overclock and kind != 'overclock_activated':
            raise ValueError('Paid overclock has no immediately recorded activation')
        if kind == 'session_started':
            for key, target in dict(initialEnergy=60, maximumEnergy=100, pulseCost=35, repairCost=40,
                                    killReward=12, stageReward=25, repairCooldownSeconds=8,
                                    chargedShotCost=12, overclockCost=35, overclockDuration=6).items():
                if number(data.get(key)) != target: raise ValueError('Unexpected economic rules in native session')
            required_fields(data, dict(mode=MODE, relayBaseSeconds='8 / 10+10 / 4',
                repairPricing='ceil(actualHealing*0.8), minimum8 maximum40; zero-heal rejected'))
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
            if reason in ('pulse', 'repair', 'charged_shot', 'overclock'):
                if phase != 'active': raise ValueError('Ability spent energy outside active gameplay')
                offered = -12 if reason == 'charged_shot' else -35
                if reason == 'repair':
                    offered = number(data.get('offeredDelta'), -40, -8, True)
                    if stamp + .00001 < repair_ready: raise ValueError('Repair repeated before cooldown expired')
                if balance < -offered: raise ValueError('Native transaction overspent energy')
                actual = offered
                if reason == 'pulse': pulses += 1
                elif reason == 'charged_shot': charged += 1
                elif reason == 'overclock':
                    if wave not in (1, 2): raise ValueError('Overclock purchased outside a data relay stage')
                    overclocks += 1; pending_overclock = True
                else: repairs += 1; pending_repair = -offered
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
            if number(data.get('offeredDelta'), -40, 25, True) != offered or number(data.get('actualDelta'), -40, 25, True) != actual:
                raise ValueError('Transaction cost/reward or cap clipping is incorrect')
            balance += actual
            earned += max(actual, 0); spent += max(-actual, 0)
            overflow += max(0, offered - actual)
            if number(data.get('after'), 0, 100, True) != balance or number(data.get('overflowTotal'), integer=True) != overflow:
                raise ValueError('Transaction ending balance/overflow mismatch')
        elif kind == 'repair_applied':
            if not pending_repair: raise ValueError('Healing is not backed by a paid repair')
            self_heal = number(data.get('playerActualHealing'), 0, 30)
            ally_heal = number(data.get('companionActualHealing'), 0, 20)
            if self_heal + ally_heal <= 0: raise ValueError('A paid repair healed nobody')
            required_fields(data, dict(quoteReconciled=True))
            quoted = number(data.get('quotedHealing'), .000001, 50)
            if not math.isclose(quoted, self_heal + ally_heal, abs_tol=.0001, rel_tol=0):
                raise ValueError('Repair quote does not match actual recipients')
            cost = number(data.get('cost'), 8, 40, True)
            # quotedHealing is a float32 aggregate; C++ deliberately computes cost from
            # the double sum of each recipient. Near a ceil boundary the aggregates differ.
            if cost != pending_repair or cost != repair_cost(self_heal + ally_heal):
                raise ValueError('Repair price is not the actual-healing price')
            if not math.isclose(number(data.get('cooldownReadyGameSeconds')) - stamp, 8, abs_tol=0.001):
                raise ValueError('Repair did not use the declared 8-second cooldown')
            repair_ready = data['cooldownReadyGameSeconds']
            player_heal += self_heal; companion_heal += ally_heal; pending_repair = 0
        elif kind == 'overclock_activated':
            if not pending_overclock: raise ValueError('Free overclock activation')
            key = number(data.get('relayKey'), 10, 21, True)
            if key not in (10, 20, 21) or key // 10 != wave or key in overclock_keys:
                raise ValueError('Overclock relay repeated or wrong stage')
            if not math.isclose(number(data.get('endsAtGameSeconds')) - stamp, 6, abs_tol=.001):
                raise ValueError('Overclock duration differs from six game seconds')
            boolean(data.get('contestedAtPurchase'))
            overclock_keys.add(key); pending_overclock = False
        elif kind == 'symbiosis_applied':
            if phase != 'active' or not upgrade_mask & 4: raise ValueError('Symbiosis without its active upgrade')
            dt = number(data.get('progressGameSeconds'), .0000001, .25)
            self_heal = number(data.get('playerActualHealing'), 0, 2 * dt + .0001)
            ally_heal = number(data.get('companionActualHealing'), 0, 2 * dt + .0001)
            if self_heal + ally_heal <= 0: raise ValueError('Empty Symbiosis event')
            symbiosis_player += self_heal; symbiosis_companion += ally_heal
        elif kind == 'route_selected':
            value = boolean(data.get('northFirst'))
            if not (phase == 'briefing' or phase == 'intermission' and wave == 1) or value == north_first:
                raise ValueError('Route change outside the permitted choice, or not a toggle')
            north_first = value
        elif kind == 'companion_state_changed':
            before, after = data.get('from'), data.get('to')
            if before != companion_last or not isinstance(after, str) or not after or before == after:
                raise ValueError('Companion state chain is broken')
            boolean(data.get('ownSight'))
            if not isinstance(data.get('guardSlotReason'), str): raise ValueError('Missing guard diagnostic')
            if before: changes += 1
            if after == companion_previous and stamp - companion_changed_at < 1: reversals += 1
            companion_previous, companion_last, companion_changed_at = before, after, stamp
        elif kind == 'upgrade_selected':
            index = number(data.get('upgradeIndex'), 1, 3, True)
            bit = 1 << (int(index) - 1)
            if upgrade_mask & bit or phase != 'intermission': raise ValueError('Repeated or out-of-phase upgrade')
            upgrade_mask |= bit
        elif not survey_event and kind not in ('pulse_rejected', 'repair_rejected', 'session_finished'):
            raise ValueError('Unknown native event')
        if number(event.get('energy'), 0, 100, True) != balance:
            raise ValueError('Energy changed without a matching transaction')
        previous_event = event
    if phase != summary['outcome'] or pending_repair or pending_overclock: raise ValueError('Trace did not reach the reported final state')
    if summary['outcome'] == 'won' and stages != {1, 2, 3}: raise ValueError('Won report omitted a completed stage')
    expected = dict(energyInitial=60, energyFinal=balance, energyEarned=earned, energySpent=spent,
                    energyOverflow=overflow, pulsesUsed=pulses, repairsUsed=repairs, enemiesRewarded=len(kills),
                    stagesRewarded=len(stages), repairPlayerActualHealing=player_heal,
                    repairCompanionActualHealing=companion_heal, alliedActualDamageDealt=damage_dealt,
                    upgradeMask=upgrade_mask, chargedShots=charged, overclocks=overclocks,
                    symbiosisPlayerActualHealing=symbiosis_player,
                    symbiosisCompanionActualHealing=symbiosis_companion,
                    companionStateChanges=changes, companionShortReversalsUnder1s=reversals)
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
    required_fields(summary, dict(northRouteFirst=north_first, stabilityMetricDefinition=STABILITY))
    boolean(summary.get('companionSurvived'))
    for key in ('companionShots', 'companionMoveRequests', 'guardSlotChecks', 'guardSlotRejections', 'guardAlternateSelections'):
        number(summary.get(key), integer=True)
    if not summary['guardAlternateSelections'] <= summary['guardSlotRejections'] <= summary['guardSlotChecks']:
        raise ValueError('Guard counters are contradictory')
    number(summary.get('sessionGameSeconds'), .001, 215)
    number(summary.get('trialElapsedSeconds'), 0, summary['sessionGameSeconds'] + .01)
    # FinishSession writes these fields into the final event before adding file metadata.
    for key, value in summary.items():
        if key not in ('eventCount', 'tracePath', 'traceComplete') and events[-1]['data'].get(key) != value:
            raise ValueError('Native final event/report mismatch: ' + key)
    if not math.isclose(events[-1]['gameSeconds'] - events[0]['gameSeconds'], summary['sessionGameSeconds'], abs_tol=.02):
        raise ValueError('Session duration differs from its trace clocks')
    rows = capture.get('samples')
    if not isinstance(rows, list) or not rows: raise ValueError('Missing sampled outcomes')
    last = rows[-1]
    survey_evidence = survey.finish(summary, last)
    for sample_key, summary_key in (('energy', 'energyFinal'), ('pulses', 'pulsesUsed'), ('repairs', 'repairsUsed'),
                                    ('chargedShots', 'chargedShots'), ('overclocks', 'overclocks')):
        if number(last.get(sample_key), integer=True) != summary[summary_key]:
            raise ValueError('Last captured counter differs from actual ledger: ' + sample_key)
    if not math.isclose(number(last.get('symbiosisHealing')), symbiosis_player + symbiosis_companion, abs_tol=.02, rel_tol=1e-6):
        raise ValueError('Sampled Symbiosis healing differs from ledger')
    if boolean(last.get('northRouteFirst')) != north_first: raise ValueError('Sampled route differs from actual choice')
    if 'companionHealth' in last and (number(last['companionHealth']) > 0) != summary['companionSurvived']:
        raise ValueError('Companion survival differs from final native health')
    if earned + overflow != 12 * len(kills) + 25 * len(stages) or balance != 60 + earned - spent:
        raise ValueError('Economic conservation failed')
    marker = re.compile(r'^(?:\[[^\]\r\n]*\])*LogTemp: Display: AEGIS_PORTFOLIO_COMPLETE run=([^ ]+) outcome=([^ ]+) report=(.+)$')
    markers = [match.groups() for line in log.splitlines() if (match := marker.fullmatch(line))]
    exact_native(log, 'AEGIS_PORTFOLIO_COMPLETE')
    if len(markers) != 1 or markers[0][:2] != (run_id, summary['outcome']) or Path(markers[0][2]).resolve() != report_path:
        raise ValueError('Missing unique matching actual system completion marker')
    return {'path': str(report_path), 'sha256': sha(report_path), 'traceSha256': sha(trace),
            'events': len(events), 'outcome': summary['outcome'], 'ledgerReconciled': True,
            'damageRoleVerification': 'full' if unknown_damage == 0 else 'combined-only',
            'pulsesUsed': pulses, 'repairsUsed': repairs, 'actualRepairHealing': player_heal + companion_heal,
            'chargedShots': charged, 'overclocks': overclocks, 'northRouteFirst': north_first,
            'survey': survey_evidence,
            'symbiosisActualHealing': symbiosis_player + symbiosis_companion,
            'companionStateChanges': changes, 'companionShortReversalsUnder1s': reversals,
            'stabilityMetricDefinition': STABILITY,
            'guardCounterScope': 'native reported totals; complete gameplay is observational, not a causal fixture'}

def build_command(exe, out, *, editor=False, no_frames=False, interval=1/30, width=1280, height=720,
                  seed=1101, guard_legacy=False, v23=False):
    number(interval, .016, 5); number(width, 640, 7680, True); number(height, 360, 4320, True)
    number(seed, 0, 2147483647, True)
    command = [str(Path(exe).resolve())]
    out = Path(out).resolve()
    if editor: command += [str(ROOT/'AegisArena.uproject'), '/Game/Aegis/Maps/AegisArena', '-game']
    command += ['-AegisPortfolio', '-AegisV2', '-AegisPortfolioCapture', '-AegisInputProbe', '-RenderOffscreen', '-d3d11',
                '-windowed', '-ForceRes', f'-ResX={width}', f'-ResY={height}', '-UseFixedTimeStep', '-FPS=30',
                '-unattended', '-nop4', '-nosound', '-nosplash', '-stdout', '-FullStdOutLogOutput', OFFLINE_ARGUMENT,
                f'-AegisPortfolioCaptureOutput={out}', f'-AegisPortfolioOutput={out / "system"}',
                f'-AegisPortfolioFrameInterval={interval}', f'-AegisPortfolioSeed={seed}', f'-abslog={out / "engine.log"}']
    if no_frames: command += ['-AegisPortfolioNoFrames']
    if guard_legacy: command += ['-AegisGuardLegacy']
    if v23: command += ['-AegisV23']
    return command


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
    p.add_argument('--guard-legacy', action='store_true', help='Disable only the v2 Guard firing-lane slot mechanism')
    p.add_argument('--v23', action='store_true', help='Enable the v2.3 visual/language presentation pass')
    args = p.parse_args()
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    exe = args.exe.resolve()
    inputs = snapshot_inputs(ROOT)
    command = build_command(exe, out, editor=args.editor, no_frames=args.no_frames, interval=args.interval,
                            width=args.width, height=args.height, seed=args.seed, guard_legacy=args.guard_legacy,
                            v23=args.v23)
    record = {'schemaVersion': 1, 'engine': 'unreal-runtime', 'launch': 'editor-game' if args.editor else 'packaged-development',
              'scriptedInput': True, 'fixtureDamage': False, 'humanPlaytest': False, 'phase': 'development-demonstration',
              'mode': MODE, 'seed': args.seed, 'guardPolicy': 'legacy' if args.guard_legacy else 'improved',
              'visualDeliveryVersion': '2.3' if args.v23 else '2.1',
              'evaluationScope': 'scripted full-run observation; paired seeds do not establish general win-rate improvement',
              'source': inputs, 'inputsBefore': inputs, 'command': command,
              'passed': False, 'recording': 'native rendered PNGs with game/video timestamps; no generated gameplay frames'}
    started = time.monotonic()
    try:
        record['binariesBefore'] = binary_snapshot(exe, editor=args.editor)
        record['binary'] = record['binariesBefore'].get('projectModule', record['binariesBefore'].get('gamePayload', record['binariesBefore']['launcher']))
        record['wrappersBefore'] = {name: sha(ROOT/'scripts'/name) for name in WRAPPERS}
        with (out/'stdout.log').open('w', encoding='utf-8') as log:
            result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=2600)
        record['exitCode'] = result.returncode
        if result.returncode != 0: raise ValueError('Capture process exited unsuccessfully')
        log = (out/'engine.log').read_text(encoding='utf-8-sig')
        capture = read_json(out/'capture.json')
        record.update(validate_capture(capture, log, out, no_frames=args.no_frames, width=args.width, height=args.height))
        record['systemEvidence'] = validate_system_capture(out, capture, log)
        record['outcome'] = capture['outcome']
    except Exception as e:
        record['failure'] = repr(e)
    finally:
        try:
            record['inputsAfter'] = snapshot_inputs(ROOT)
            record['binariesAfter'] = binary_snapshot(exe, editor=args.editor)
            record['wrappersAfter'] = {name: sha(ROOT/'scripts'/name) for name in WRAPPERS}
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
