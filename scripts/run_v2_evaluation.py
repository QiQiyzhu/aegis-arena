"""Frozen small paired native runs. Keeps losses and runner failures; no parameter search."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import subprocess
import sys

from run_unreal_functional import snapshot_inputs
from run_portfolio_probe import binary_snapshot
from run_v2_capture import WRAPPERS, MODE

ROOT = Path(__file__).resolve().parents[1]


def wrapper_snapshot():
    return {name: hashlib.sha256((ROOT/'scripts'/name).read_bytes()).hexdigest()
            for name in (*WRAPPERS, 'run_v2_evaluation.py')}


def render_size(proof):
    """The fixed-pixel input filter makes viewport size part of the protocol."""
    values = []
    for prefix in ('-ResX=', '-ResY='):
        matches = [flag[len(prefix):] for flag in proof['command'] if flag.startswith(prefix)]
        assert len(matches) == 1 and matches[0].isdigit(), 'Missing unique development viewport'
        values.append(int(matches[0]))
    assert 640 <= values[0] <= 7680 and 360 <= values[1] <= 4320
    return tuple(values)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--development-evidence', type=Path, required=True)
    p.add_argument('--seeds', default='2203,3307,4409')
    p.add_argument('--prior-evaluation', type=Path,
                   help='Retain a prior protocol attempt and prohibit reusing its exposed holdout seeds')
    a = p.parse_args()
    seeds = [int(s) for s in a.seeds.split(',')]
    assert len(seeds) == len(set(seeds)) and 1101 not in seeds
    assert a.development_evidence.is_file()
    development = json.loads(a.development_evidence.read_text(encoding='utf-8-sig'))
    assert development.get('passed') is True, 'Development capture must pass its native audit'
    assert development.get('seed') == 1101 and development.get('mode') == MODE
    assert development.get('launch') == 'packaged-development'
    width, height = render_size(development)
    previous = None
    if a.prior_evaluation:
        previous = json.loads(a.prior_evaluation.read_text(encoding='utf-8-sig'))
        assert previous.get('complete') is True, 'Keep every attempted prior run before revising the protocol'
        assert not set(seeds) & set(previous['frozen']['holdoutSeeds']), 'Previously exposed seeds are no longer holdouts'
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    frozen = {'timestampUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'inputs': snapshot_inputs(ROOT), 'binary': binary_snapshot(a.exe.resolve()),
              'wrappers': wrapper_snapshot(),
              'developmentEvidence': str(a.development_evidence.resolve()), 'developmentSeed': 1101,
              'developmentEvidenceSha256': hashlib.sha256(a.development_evidence.read_bytes()).hexdigest(),
              'holdoutSeeds': seeds, 'policies': ['legacy', 'improved'],
              'renderConfig': {'width': width, 'height': height, 'derivedFromDevelopment': True},
              'onlyPolicyDifference': '-AegisGuardLegacy disables Guard candidate shot-line qualification',
              'playerDriver': 'same scripted inputs and rules; route north-first; upgrades3 then1',
              'ruleConfig': {'energyInitial': 60, 'chargedCost': 12, 'pulseCost': 35, 'overclockCost': 35,
                             'overclockSeconds': 6, 'objectiveSeconds': [8,10,10,4]},
              'training': False, 'humanPlaytest': False, 'populationWinRateClaim': False}
    if previous is not None:
        assert previous['frozen']['inputs'] == frozen['inputs'], 'This protocol correction must not change runtime source/content'
        assert previous['frozen']['binary'] == frozen['binary'], 'This protocol correction must not change the game package'
        frozen['priorEvaluation'] = {
            'path': str(a.prior_evaluation.resolve()),
            'sha256': hashlib.sha256(a.prior_evaluation.read_bytes()).hexdigest(),
            'excludedExposedSeeds': previous['frozen']['holdoutSeeds'],
            'correction': 'Match the calibrated development viewport; preserve all prior outcomes. No runtime/AI/rule changes.'}
    assert development.get('inputsBefore') == frozen['inputs'] == development.get('inputsAfter'), 'Development capture does not match frozen source/content'
    assert development.get('binariesBefore') == frozen['binary'] == development.get('binariesAfter'), 'Development capture does not match frozen package'
    for name in WRAPPERS:
        assert development['wrappersBefore'][name] == frozen['wrappers'][name] == development['wrappersAfter'][name]
    (out/'freeze.json').write_text(json.dumps(frozen, indent=2)+'\n', encoding='utf-8')
    rows = []
    for index, seed in enumerate(seeds):
        for policy in (['legacy', 'improved'] if index % 2 == 0 else ['improved', 'legacy']):
            current = snapshot_inputs(ROOT)
            assert current == frozen['inputs'], 'Source/content changed after freeze'
            assert binary_snapshot(a.exe.resolve()) == frozen['binary'], 'Binary changed after freeze'
            assert wrapper_snapshot() == frozen['wrappers'], 'Evaluation/capture scripts changed after freeze'
            directory = out/f'{seed}-{policy}'
            command = [sys.executable, str(ROOT/'scripts/run_v2_capture.py'), '--exe', str(a.exe.resolve()),
                       '--output', str(directory), '--seed', str(seed), '--no-frames', '--width', str(width), '--height', str(height)]
            if policy == 'legacy': command.append('--guard-legacy')
            run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, encoding='utf-8', errors='replace')
            (out/f'{seed}-{policy}-runner.log').write_text(run.stdout+run.stderr, encoding='utf-8')
            row = {'seed': seed, 'policy': policy, 'runnerExitCode': run.returncode, 'output': str(directory)}
            summaries = list((directory/'system').glob('portfolio-*.json'))
            if len(summaries) == 1:
                data = json.loads(summaries[0].read_text(encoding='utf-8-sig'))
                for key in ('outcome','completed','trialElapsedSeconds','stagesRewarded','alliedActualDamageDealt',
                            'playerActualDamageTaken','companionActualDamageTaken','companionSurvived',
                            'companionStateChanges','companionShortReversalsUnder1s','companionShots',
                            'companionMoveRequests','guardSlotChecks','guardSlotRejections','guardAlternateSelections',
                            'chargedShots','overclocks','pulsesUsed','repairsUsed','energySpent','energyFinal','energyOverflow',
                            'symbiosisPlayerActualHealing','symbiosisCompanionActualHealing'):
                    row[key] = data.get(key)
            rows.append(row)
            result = {'scope':'Small paired native scripted runs; every attempted outcome retained',
                      'frozen': frozen, 'rows': rows, 'allRunnersPassed': all(r['runnerExitCode']==0 for r in rows),
                      'complete': len(rows) == len(seeds)*2,
                      'stabilityDefinition':'DebugState transitions include normal aim/fire/reposition cycles; not Utility oscillation'}
            (out/'evaluation.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
            print(json.dumps(row), flush=True)
    assert snapshot_inputs(ROOT) == frozen['inputs']
    assert binary_snapshot(a.exe.resolve()) == frozen['binary']
    assert wrapper_snapshot() == frozen['wrappers']
    return 0 if result['allRunnersPassed'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
