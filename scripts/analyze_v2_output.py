"""Attribute observed enemy damage from frozen native traces; no Unreal launch.

Attribution relies on the bound C++ OnDamage contract: enemyVictim events are
written only when Source is the current Player or current companion. Exact native
class names distinguish those two roles; unrecognized names remain unknown.
This is descriptive output contribution, not training or a general AI-quality score.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import re

from run_decision_lab import read_json, strict_json, sha
from run_unreal_functional import snapshot_inputs
from run_v2_capture import validate_system_capture

ROOT = Path(__file__).resolve().parents[1]
SOURCE = '6dad19526dbc6b093a841ccf46f040e48148e37700d3e5a2184d6f66d42b3cc2'


def require(value, message):
    if not value:
        raise ValueError(message)


def artifact(path):
    path = Path(path).resolve(strict=True)
    return dict(path=str(path), bytes=path.stat().st_size, sha256=sha(path))


def bind(path, expected):
    actual = artifact(path)
    require(isinstance(expected, dict) and actual['sha256'] == expected.get('sha256') and
            actual['bytes'] == expected.get('bytes'), f'Unbound/changed raw evidence: {path}')
    return actual


def role(name):
    if isinstance(name, str):
        if re.fullmatch(r'AegisPlayerCharacter(?:_\d+)?', name):
            return 'player'
        if re.fullmatch(r'AegisAICharacter(?:_\d+)?', name):
            return 'companion'
    return 'unknown'


def split_damage(events, expected_total):
    """Caller must first validate the session and bind the audited source contract."""
    amounts = defaultdict(list)
    identities = {'player': set(), 'companion': set()}
    sources = defaultdict(lambda: {'amounts': [], 'events': 0})
    enemies = set()
    for event in events:
        if event.get('event') != 'damage':
            continue
        data = event['data']
        require(type(data.get('enemyVictim')) is bool, 'Damage event has no boolean victim team')
        if data['enemyVictim']:
            enemies.add(data.get('victim'))
        else:
            kind = role(data.get('victim'))
            if kind in identities:
                identities[kind].add(data['victim'])
    for event in events:
        if event.get('event') != 'damage' or not event['data']['enemyVictim']:
            continue
        data = event['data']
        source = data.get('source')
        require(isinstance(source, str) and bool(source), 'Missing damage source identity')
        require(source not in enemies, 'Enemy victim identity also appears as an allied damage source')
        kind = role(source)
        amount = data.get('actualDamage')
        require(type(amount) in (int, float) and math.isfinite(amount) and amount > 0, 'Invalid actual damage')
        if kind in identities:
            identities[kind].add(source)
        amounts[kind].append(amount)
        sources[source]['amounts'].append(amount)
        sources[source]['events'] += 1
    require(all(len(names) <= 1 for names in identities.values()), 'Multiple identities for one allied role in a single session')
    totals = {kind: math.fsum(amounts[kind]) for kind in ('player', 'companion', 'unknown')}
    total = math.fsum(totals.values())
    require(type(expected_total) in (int, float) and math.isfinite(expected_total) and
            math.isclose(total, expected_total, rel_tol=1e-6, abs_tol=.02), 'Source damage sum differs from native team total')
    return {**totals, 'total': total, 'fullyAttributed': totals['unknown'] == 0,
            'companionShareOfTotal': totals['companion']/total if total else None,
            'playerShareOfTotal': totals['player']/total if total else None,
            'roleIdentities': {k: sorted(v) for k, v in identities.items()},
            'sources': [{'name': name, 'role': role(name), 'actualDamage': math.fsum(value['amounts']),
                         'damageEvents': value['events']} for name, value in sorted(sources.items())]}


def analyze_run(directory, expected_inputs):
    directory = Path(directory).resolve(strict=True)
    proof_path = directory/'provenance.json'
    proof = read_json(proof_path)
    require(proof.get('passed') is True and 'failure' not in proof and type(proof.get('exitCode')) is int and proof['exitCode'] == 0,
            'Only completed strict native PASS runs can enter this analysis')
    require(proof.get('fixtureDamage') is False and proof.get('humanPlaytest') is False and proof.get('scriptedInput') is True,
            'Expected disclosed natural-rule scripted observation')
    require(proof.get('inputsBefore') == expected_inputs == proof.get('inputsAfter'), 'Runtime source/content mismatch')
    for prefix in ('binaries', 'wrappers'):
        require(proof.get(prefix+'Before') and proof[prefix+'Before'] == proof.get(prefix+'After'), 'Unstable '+prefix)
    capture_path, log_path = directory/'capture.json', directory/'engine.log'
    files = proof.get('files', {})
    bound = [artifact(proof_path)]
    for path in (capture_path, log_path):
        bound.append(bind(path, files.get(str(path.relative_to(directory)))))
    capture = read_json(capture_path)
    result = validate_system_capture(directory, capture, log_path.read_text(encoding='utf-8-sig'))
    require(result == proof.get('systemEvidence'), 'Revalidated native ledger differs from original wrapper result')
    summary_path = Path(result['path']).resolve(strict=True)
    trace_path = summary_path.with_suffix('.jsonl')
    for path in (summary_path, trace_path):
        require(directory in path.parents, 'Native trace escaped its run directory')
        bound.append(bind(path, files.get(str(path.relative_to(directory)))))
    events = [strict_json(line) for line in trace_path.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
    summary = read_json(summary_path)
    damage = split_damage(events, summary['alliedActualDamageDealt'])
    return {'runDirectory': str(directory), 'seed': proof['seed'], 'policy': proof['guardPolicy'],
            'outcome': summary['outcome'], 'taskGameSeconds': summary['trialElapsedSeconds'],
            'stagesCompleted': summary['stagesRewarded'], 'damage': damage,
            'damagePerTaskSecond': {name: damage[name]/summary['trialElapsedSeconds'] if summary['trialElapsedSeconds'] else None
                                    for name in ('player', 'companion', 'unknown', 'total')},
            'boundOriginals': bound, 'binaryBinding': proof['binariesBefore']}


def paired_differences(rows):
    by_seed = defaultdict(dict)
    for row in rows:
        require(row['policy'] in ('legacy', 'improved'), 'Unknown paired policy')
        require(row['policy'] not in by_seed[row['seed']], 'Duplicate seed/policy run')
        by_seed[row['seed']][row['policy']] = row
    pairs = []
    for seed, policies in sorted(by_seed.items()):
        require(set(policies) == {'legacy', 'improved'}, 'Incomplete seed pair')
        old, new = policies['legacy'], policies['improved']
        attributed = old['damage']['fullyAttributed'] and new['damage']['fullyAttributed']
        pairs.append({'seed': seed, 'legacyOutcome': old['outcome'], 'improvedOutcome': new['outcome'],
                      'deltaDefinition': 'improved minus legacy; all observed outcomes retained',
                      'taskSecondsDelta': new['taskGameSeconds']-old['taskGameSeconds'],
                      'damageDelta': {name: new['damage'][name]-old['damage'][name]
                                      for name in ('player', 'companion', 'unknown', 'total')},
                      'completeRoleAttribution': attributed,
                      'companionShareDelta': (new['damage']['companionShareOfTotal']-old['damage']['companionShareOfTotal'])
                          if attributed and old['damage']['total'] and new['damage']['total'] else None})
    return pairs


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    source = p.add_mutually_exclusive_group(required=True)
    source.add_argument('--run', type=Path, action='append', help='One or more independent calibration/demo directories')
    source.add_argument('--evaluation', type=Path, help='Completed frozen paired evaluation.json')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--expected-source', default=SOURCE)
    a = p.parse_args(argv)
    output = a.output.resolve()
    require(not output.exists(), 'Analysis output must be a fresh path')
    inputs = snapshot_inputs(ROOT)
    require(inputs['sourceSha256'] == a.expected_source, 'Audited C++ contract differs from requested source')
    evaluation = None
    if a.evaluation:
        evaluation = read_json(a.evaluation)
        require(evaluation.get('complete') is True and evaluation.get('allRunnersPassed') is True, 'Evaluation incomplete or has failed runners')
        frozen = evaluation['frozen']
        require(frozen.get('inputs') == inputs and frozen.get('policies') == ['legacy', 'improved'], 'Evaluation freeze differs')
        require(frozen.get('training') is False and frozen.get('humanPlaytest') is False, 'Unsupported evaluation scope')
        expected = {(s, mode) for s in frozen['holdoutSeeds'] for mode in ('legacy', 'improved')}
        records = evaluation.get('rows', [])
        require(len(records) == len(expected) and {(r['seed'], r['policy']) for r in records} == expected,
                'Missing, duplicated or extra paired run')
        directories = [Path(row['output']) for row in records]
    else:
        directories = a.run
    require(len({p.resolve() for p in directories}) == len(directories), 'Duplicate run directory')
    rows = [analyze_run(directory, inputs) for directory in directories]
    if evaluation:
        for original, actual in zip(records, rows):
            require(type(original.get('runnerExitCode')) is int and original['runnerExitCode'] == 0, 'Failed evaluation row')
            require((original['seed'], original['policy'], original['outcome']) ==
                    (actual['seed'], actual['policy'], actual['outcome']), 'Evaluation identity/outcome differs from raw run')
            require(original['alliedActualDamageDealt'] == actual['damage']['total'] and
                    original['trialElapsedSeconds'] == actual['taskGameSeconds'], 'Evaluation numbers differ from raw trace')
            require(actual['binaryBinding'] == frozen['binary'], 'Paired run uses another package')
    result = {'schemaVersion': 1, 'analysisComplete': True, 'createdUtc': datetime.now(timezone.utc).isoformat(),
              'script': artifact(__file__), 'frozenInputs': inputs,
              'auditedContract': {'file': artifact(ROOT/'Source/AegisArena/Private/AegisPortfolio.cpp'),
                                  'method': 'AAegisPortfolio::OnDamage',
                                  'rule': 'enemyVictim damage events require Source == current Player or current companion; native class names discriminate those two roles',
                                  'unknownPolicy': 'Unrecognized source names remain unknown; no allocation or inferred redistribution'},
              'evaluation': artifact(a.evaluation) if a.evaluation else None, 'rows': rows,
              'pairedDifferences': paired_differences(rows) if evaluation else [],
              'allDamageAttributed': all(row['damage']['fullyAttributed'] for row in rows),
              'scope': 'Descriptive actual HP removed, including all damage abilities and health-clamped final hits; no damage attempted or overkill credit',
              'limitations': ['No training, human playtest, general win-rate or causal population claim.',
                             'Task-second rate includes objective waiting and traversal; it is not combat-only DPS.',
                             'Companion share is contribution in these scripted runs, not a fairness, balance or general intelligence score.',
                             'Role inference is valid only under the bound native C++ event filter and unrenamed native actor classes. Unknown identities stay visible.']}
    require(snapshot_inputs(ROOT) == inputs, 'Runtime source/content changed during analysis')
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write('\n')
    print(json.dumps({'output': artifact(output), 'runs': len(rows), 'allDamageAttributed': result['allDamageAttributed']}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
