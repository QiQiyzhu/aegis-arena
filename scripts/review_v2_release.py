"""Read-only release evidence gate. Never launches Unreal or creates missing proof.

Writes a fresh acceptance JSON only after every required native, media and explicit
visual-review gate passes. Existing failures stay failures; observations are not
training, human playtesting, general win rates or hardware performance results.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import re

from run_decision_lab import read_json, sha
from run_unreal_functional import snapshot_inputs
from run_portfolio_probe import binary_snapshot
import run_v2_probe as control_gate
import run_decision_lab_input as lab_gate
import run_unreal_guard_slot as guard_gate
import run_v2_capture as capture_gate

ROOT = Path(__file__).resolve().parents[1]
FINAL_SOURCE = 'df57a548edcfcfce40e0d1855286d75894985e7cbf89c71173c80a765ad65b69'
FINAL_CONTENT = '9667582db87935a68d8823eca0801b1c47ade897f12a1c848f926a4e02e88fd5'
HOLDOUT_SEEDS = (2203, 3307, 4409)
METRICS = ('outcome', 'completed', 'trialElapsedSeconds', 'stagesRewarded', 'alliedActualDamageDealt',
           'playerActualDamageTaken', 'companionActualDamageTaken', 'companionSurvived',
           'companionStateChanges', 'companionShortReversalsUnder1s', 'companionShots',
           'companionMoveRequests', 'guardSlotChecks', 'guardSlotRejections', 'guardAlternateSelections',
           'chargedShots', 'overclocks', 'pulsesUsed', 'repairsUsed', 'energySpent', 'energyFinal',
           'energyOverflow', 'symbiosisPlayerActualHealing', 'symbiosisCompanionActualHealing')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def exact(value, expected, label):
    require(type(value) is type(expected) and value == expected, label)


def file_record(path):
    path = Path(path).resolve(strict=True)
    require(path.is_file(), f'Not a file: {path}')
    return {'path': str(path), 'bytes': path.stat().st_size, 'sha256': sha(path)}


def bound_file(path, expected):
    actual = file_record(path)
    digest = expected if isinstance(expected, str) else expected.get('sha256')
    require(isinstance(digest, str) and re.fullmatch('[a-f0-9]{64}', digest) is not None,
            f'Missing SHA256: {path}')
    require(actual['sha256'] == digest, f'Hash changed: {path}')
    if isinstance(expected, dict) and 'bytes' in expected:
        exact(actual['bytes'], expected['bytes'], f'Byte count changed: {path}')
    return actual


def child(directory, name):
    require(isinstance(name, str) and name, 'Missing relative evidence path')
    base = Path(directory).resolve()
    target = (base / name).resolve()
    require(target != base and base in target.parents, 'Evidence path escapes its directory')
    return target


def recorded_files(directory, mapping):
    require(isinstance(mapping, dict) and bool(mapping), 'Missing native artifact hash manifest')
    for name, expected in mapping.items():
        bound_file(child(directory, name), expected)


def visual_review(path, expected_images):
    """Explicit review must cover every exact encoded frame/rendered page once."""
    review = read_json(path)
    exact(review.get('passed'), True, f'Visual review is not explicitly passed: {path}')
    images = review.get('reviewedImages')
    require(isinstance(images, list) and len(images) == len(expected_images), 'Incomplete visual review image coverage')
    expected = {str(Path(p).resolve()): digest for p, digest in expected_images}
    require(len(expected) == len(expected_images), 'Duplicate expected visual-review image')
    seen = set()
    for row in images:
        require(isinstance(row, dict) and isinstance(row.get('path'), str) and Path(row['path']).is_absolute(),
                'Visual-review image requires an absolute path')
        image = str(Path(row['path']).resolve())
        require(image in expected and image not in seen, 'Unexpected or duplicate visually reviewed image')
        require(row.get('sha256') == expected[image], 'Visual review does not bind the expected image')
        if 'passed' in row:
            exact(row['passed'], True, 'A reviewed image did not pass')
        bound_file(image, expected[image])
        seen.add(image)
    return {'record': file_record(path), 'reviewedImageCount': len(seen), 'explicitReviewPassed': True,
            'scope': 'Recorded human/agent visual inspection; no human gameplay or usability claim'}


def validate_holdout_seeds(seeds):
    require(isinstance(seeds, (list, tuple)) and len(seeds) == 3 and
            all(type(seed) is int and 0 <= seed <= 2147483647 and seed != 1101 for seed in seeds) and
            len(set(seeds)) == 3, 'Require three unique integer holdout seeds distinct from development seed 1101')
    return tuple(seeds)


def validate_pair_rows(rows, seeds=HOLDOUT_SEEDS):
    seeds = validate_holdout_seeds(seeds)
    require(isinstance(rows, list) and len(rows) == 6, 'Exactly six attempted holdout runs are required')
    seen = set()
    for row in rows:
        require(isinstance(row, dict), 'Invalid evaluation row')
        key = (row.get('seed'), row.get('policy'))
        require(type(key[0]) is int and key[0] in seeds and key[1] in ('legacy', 'improved'),
                'Unexpected holdout seed/policy')
        require(key not in seen, 'Duplicate holdout seed/policy')
        seen.add(key)
        exact(row.get('runnerExitCode'), 0, 'A holdout runner failed')
        require(row.get('outcome') in ('won', 'lost'), 'Holdout has no natural outcome')
        exact(row.get('completed'), True, 'Holdout did not reach a natural result')
    require(seen == {(s, p) for s in seeds for p in ('legacy', 'improved')}, 'Missing paired holdout run')


class Audit:
    def __init__(self, source, content, package_exe):
        self.inputs = {'sourceSha256': source, 'contentSha256': content}
        require(snapshot_inputs(ROOT) == self.inputs, 'Current runtime source/content differ from the frozen release')
        self.package_exe = Path(package_exe).resolve(strict=True)
        self.package = binary_snapshot(self.package_exe)
        require(set(('launcher', 'gamePayload', 'cookedContainers')) <= set(self.package), 'Incomplete package snapshot')
        self.read_proofs = []

    def proof(self, path):
        record = file_record(path)
        self.read_proofs.append(record)
        return record

    def native(self, directory, count=None, packaged=True, lab=False):
        directory = Path(directory).resolve(strict=True)
        p = read_json(directory/'provenance.json')
        exact(p.get('passed'), True, f'Native wrapper did not pass: {directory}')
        require('failure' not in p, f'Contradictory native failure: {directory}')
        exact(p.get('exitCode'), 0, f'Native process did not exit normally: {directory}')
        require(p.get('engine') == 'unreal-runtime', 'Native engine classification missing')
        if packaged:
            require(p.get('launch') == 'packaged-development', 'A packaged runtime proof is required')
        if count is not None:
            exact(p.get('nativeAssertions'), count, f'Wrong assertion count: {directory}')
        require(p.get('inputsBefore') == self.inputs == p.get('inputsAfter'), f'Source/content mismatch: {directory}')
        before = p.get('binariesBefore')
        require(isinstance(before, dict) and before and before == p.get('binariesAfter'), 'Unstable/unbound native binaries')
        if packaged:
            if lab:
                # This historical input wrapper did not bind containers. Do not
                # silently add a stronger before/after guarantee after the fact.
                require(set(before) == {'launcher', 'gamePayload'}, 'Unexpected Lab binary schema')
                for name in before:
                    require(before[name]['sha256'] == self.package[name]['sha256'] and
                            Path(before[name]['path']).resolve() == Path(self.package[name]['path']).resolve(),
                            'Lab uses a different release executable')
                    bound_file(before[name]['path'], before[name])
            else:
                require(before == self.package, 'Package executable or cooked containers differ')
        else:
            for artifact in before.values():
                if isinstance(artifact, dict) and 'path' in artifact:
                    bound_file(artifact['path'], artifact)
        if lab:
            bound_file(ROOT/'scripts/run_decision_lab_input.py', p.get('gateSha256'))
        else:
            wrappers = p.get('wrappersBefore')
            require(isinstance(wrappers, dict) and wrappers and wrappers == p.get('wrappersAfter'), 'Unbound/changing wrappers')
            for name, digest in wrappers.items():
                bound_file(child(ROOT/'scripts', name), digest)
        recorded_files(directory, p.get('files'))
        self.proof(directory/'provenance.json')
        return p

    def capture(self, directory, no_frames=False):
        directory = Path(directory).resolve()
        p = self.native(directory)
        exact(p.get('scriptedInput'), True, 'Capture input scope missing')
        exact(p.get('fixtureDamage'), False, 'Natural observation cannot inject fixture damage')
        exact(p.get('humanPlaytest'), False, 'Capture must disclose synthetic input')
        require(p.get('mode') == capture_gate.MODE, 'Wrong capture mode')
        capture = read_json(directory/'capture.json')
        command = p.get('command', [])
        def resolution(name):
            values = [v.split('=', 1)[1] for v in command if v.startswith(name+'=')]
            require(len(values) == 1 and values[0].isdigit(), 'Unbound capture resolution')
            return int(values[0])
        width, height = resolution('-ResX'), resolution('-ResY')
        log = (directory/'engine.log').read_text(encoding='utf-8-sig')
        native = capture_gate.validate_capture(capture, log, directory, no_frames=no_frames, width=width, height=height)
        system = capture_gate.validate_system_capture(directory, capture, log)
        require(system == p.get('systemEvidence'), 'Native system validation changed from its recorded result')
        for key, value in native.items():
            require(p.get(key) == value, 'Native capture validation changed: '+key)
        summary = read_json(system['path'])
        return {'provenance': self.proof(directory/'provenance.json'), 'outcome': capture['outcome'],
                'seed': p.get('seed'), 'policy': p.get('guardPolicy'), 'metrics': {k: summary[k] for k in METRICS},
                'systemEvidence': system, 'nativeCapture': native, 'resolution': [width, height]}, p, capture


def review_video(path, capture_dir, capture, qa_path):
    path, capture_dir = Path(path).resolve(), Path(capture_dir).resolve()
    video = read_json(path)
    for key, expected in {'passed': True, 'preview': False, 'fullDecodePassed': True, 'prepared': True,
                          'humanPlaytest': False, 'fixtureDamage': False, 'nativeGameplayUncropped': True,
                          'gameplayInterpolation': False}.items():
        exact(video.get(key), expected, 'Invalid video proof: '+key)
    require('failure' not in video and video.get('encodePending') is not True, 'Video build is unfinished')
    require(Path(video.get('capturePath', '')).resolve() == capture_dir, 'Video uses a different capture')
    inputs = video.get('inputs')
    expected_inputs = {capture_dir/'capture.json', capture_dir/'provenance.json'}
    expected_inputs.update((capture_dir/'system').glob('portfolio-*.json'))
    expected_inputs.update((capture_dir/'system').glob('portfolio-*.jsonl'))
    require(isinstance(inputs, dict) and {Path(p).resolve() for p in inputs} == expected_inputs, 'Video input bindings incomplete')
    for source, expected in inputs.items():
        bound_file(source, expected)
    ledgers = list((capture_dir/'system').glob('portfolio-*.json'))
    require(len(ledgers) == 1 and video.get('fullRunLedger') == read_json(ledgers[0]) and
            video.get('outcome') == capture.get('outcome'), 'Video metrics differ from the actual completed run')
    manifest_path = path.parent/'native-frame-manifest.json'
    bound_file(manifest_path, video.get('nativeFrameManifestSha256'))
    manifest = read_json(manifest_path)
    frames = capture.get('frameTimes')
    require(isinstance(frames, list) and len(frames) > 2 and len(manifest) == len(frames), 'Missing full native frame manifest')
    exact(video.get('nativeFrames'), len(frames), 'Video source frame count differs')
    interval = video.get('nativeIntervalSeconds')
    require(type(interval) in (int, float) and math.isclose(interval, 1/30, abs_tol=1e-7), 'Final video requires native 30fps')
    for i, (entry, frame) in enumerate(zip(manifest, frames)):
        require(entry.get('file') == frame.get('file') == f'frame-{i:06d}.png', 'Native frame order changed')
        require(entry.get('videoSeconds') == frame.get('videoSeconds') and entry.get('worldSeconds') == frame.get('worldSeconds'),
                'Video frame time differs from native capture')
        if i:
            require(math.isclose(frame['videoSeconds']-frames[i-1]['videoSeconds'], 1/30, abs_tol=1e-5),
                    'Native capture is sparse rather than continuous')
        bound_file(capture_dir/'frames'/entry['file'], entry)
    require(video.get('introSeconds') == 5 and video.get('outroSeconds') == 7, 'Unexpected film sequence')
    expected_count = len(frames)+12*30
    exact(video.get('decodedFrames'), expected_count, 'Full decode does not contain the full native sequence')
    require(math.isclose(video.get('encodedDurationSeconds', -1), expected_count/30, abs_tol=.15), 'Encoded duration mismatch')
    details = video.get('video')
    require(isinstance(details, dict), 'Missing encoded video')
    artifact = bound_file(child(path.parent, details.get('file')), details)
    require(0 < artifact['bytes'] < 300_000_000, 'Video must be below 300 MB')
    for key, expected in {'codec': 'H.264', 'audioCodec': 'AAC', 'pixelFormat': 'yuv420p', 'fps': 30}.items():
        exact(details.get(key), expected, 'Unexpected video format: '+key)
    log = (path.parent/'full-decode.log').read_text(encoding='utf-8-sig')
    decoded = re.findall(r'^frame=(\d+)\s*$', log, re.M)
    require(decoded and int(decoded[-1]) == expected_count and re.search(r'^progress=end\s*$', log, re.M), 'Incomplete full decode log')
    require(not re.search(r'error|invalid|corrupt|failed', log, re.I), 'Decode log contains errors')
    audio = video.get('audio')
    require(isinstance(audio, dict) and type(audio.get('eventCount')) is int and audio['eventCount'] > 0,
            'No genuine engine audio-event proof')
    events_path = path.parent/'audio-events.json'
    bound_file(events_path, video.get('audioEventsSha256'))
    require(read_json(events_path) == capture.get('audioEvents') and audio['eventCount'] == len(capture['audioEvents']),
            'Video audio events differ from the native run')
    bound_file(path.parent/'event-mix.wav', audio.get('sha256'))
    for asset in audio.get('assets', {}).values():
        bound_file(asset['path'], asset)
    expected_qa = []
    require(isinstance(video.get('qaFrames'), list) and len(video['qaFrames']) == 5, 'Missing five encoded QA frames')
    for image in video['qaFrames']:
        destination = child(path.parent, image.get('file'))
        bound_file(destination, image)
        expected_qa.append((destination, image['sha256']))
    return {'record': file_record(path), 'video': artifact, 'decodedFrames': expected_count,
            'durationSeconds': video['encodedDurationSeconds'], 'nativeFrames': len(frames),
            'audioScope': 'Real engine-triggered audio events remixed from source WAVs, not loopback recording',
            'visualReview': visual_review(qa_path, expected_qa)}


def review_document(path, qa_path, source):
    path = Path(path).resolve()
    document = read_json(path)
    exact(document.get('schemaVersion'), 2, 'Wrong document evidence schema')
    exact(document.get('pageCount'), 10, 'Document must contain all ten pages')
    require(document.get('sourceSha256') == source, 'Document describes another runtime source')
    exact(document.get('visualReviewRequired'), True, 'Document must require a separate visual review')
    hashes = document.get('inputHashes')
    require(isinstance(hashes, dict) and hashes, 'Document inputs are unbound')
    for name, digest in hashes.items():
        bound_file(name, digest)
    for key in ('source', 'manifest'):
        bound_file(document[key]['path'], document[key])
    manifest = read_json(document['manifest']['path'])
    exact(manifest.get('reviewed'), True, 'Document evidence prose is unreviewed')
    require(manifest.get('sourceSha256') == source, 'Document manifest source differs')
    outputs = document.get('outputs')
    require(isinstance(outputs, list) and len(outputs) == 2 and
            {Path(o.get('path', '')).suffix.lower() for o in outputs} == {'.pdf', '.docx'}, 'Missing design PDF/DOCX')
    files = [bound_file(row['path'], row) for row in outputs]
    pages = document.get('pageImages')
    require(isinstance(pages, list) and len(pages) == 10 and len(set(pages)) == 10, 'Incomplete document page renders')
    expected = [(Path(p).resolve(), sha(p)) for p in pages]
    # Build records originally store page paths; the independent review provides
    # their immutable hashes. Require every page, not just a contact sheet.
    review = visual_review(qa_path, expected)
    for row in document.get('nativeImages', []) + document.get('evidenceFiles', []) + document.get('designDiagrams', []):
        bound_file(row['path'], row)
    return {'record': file_record(path), 'pageCount': 10, 'outputs': files, 'visualReview': review}


def preserve_failures(root, extra):
    root = Path(root).resolve(strict=True)
    required = ('guard-baseline-01', 'guard-baseline-02', 'guard-improved-01', 'control-editor-01', 'assets-01')
    directories = {root/name for name in required}
    directories.update(Path(p).resolve() for p in extra)
    reasons = {root/name: 'Known historical failure/warning run retained without reinterpretation' for name in required}
    for proof in root.rglob('provenance.json'):
        p = read_json(proof)
        if p.get('passed') is False:
            directories.add(proof.parent)
            reasons[proof.parent] = p.get('failure', 'Wrapper passed=false')
    for log in root.glob('builds-*/**/build.log'):
        text = log.read_text(encoding='utf-8-sig', errors='replace')
        if re.search(r'Result:\s*Failed|error C\d+|error LNK\d+|BUILD FAILED', text):
            directories.add(log.parent)
            reasons[log.parent] = 'Compiler/build failure preserved'
    result = []
    for directory in sorted(directories):
        require(directory.is_dir(), f'Historical failure directory missing: {directory}')
        files = [file_record(p) for p in sorted(directory.rglob('*'))
                 if p.is_file() and p.suffix.lower() in ('.json', '.jsonl', '.log', '.csv', '.txt')]
        require(files, f'Historical failure has no original raw record: {directory}')
        result.append({'path': str(directory), 'classification': 'preserved historical failure or warning',
                       'reason': reasons.get(directory, 'Explicitly supplied historical failure'), 'files': files})
    return {'historyRoot': str(root), 'scope': 'All failed provenance/build records found under this v2 report root, plus explicit paths',
            'runs': result}


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('control', 'lab', 'guard-baseline', 'guard-improved', 'capture', 'evaluation', 'video', 'document',
                 'video-qa', 'document-qa', 'package-exe', 'history-root', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--expected-source', default=FINAL_SOURCE)
    p.add_argument('--expected-content', default=FINAL_CONTENT)
    p.add_argument('--expected-holdout-seeds', type=int, nargs=3, default=HOLDOUT_SEEDS, metavar=('SEED1', 'SEED2', 'SEED3'),
                   help='Three predeclared unique holdout integers; default preserves 2203 3307 4409 identity. '
                        'Every evaluation run must also match the 1920x1080 development capture.')
    p.add_argument('--failed-run', type=Path, action='append', default=[])
    p.add_argument('--test-evidence', type=Path, action='append', default=[],
                   help='Additional original unit/build proof, attached without inventing aggregate assertion counts')
    a = p.parse_args(argv)
    expected_seeds = validate_holdout_seeds(a.expected_holdout_seeds)
    output = a.output.resolve()
    require(not output.exists(), 'Acceptance output must be a fresh path')
    audit = Audit(a.expected_source, a.expected_content, a.package_exe)
    control = audit.native(a.control, 36)
    control_report = read_json(a.control/'v2-probe.json')
    control_gate.validate_report(control_report, (a.control/'engine.log').read_text(encoding='utf-8-sig'))
    sessions = control_gate.validate_sessions(control_report, a.control)
    lab = audit.native(a.lab, 35, lab=True)
    lab_images = lab_gate.validate(read_json(a.lab/'lab-input.json'), (a.lab/'engine.log').read_text(encoding='utf-8-sig'), a.lab)
    guards = {}
    guard_proofs = []
    for mode, directory in (('baseline', a.guard_baseline), ('improved', a.guard_improved)):
        proof = audit.native(directory, 12, packaged=False)
        require(proof.get('mode') == mode, 'Guard mode mismatch')
        native = read_json(directory/'guard-slot.json')
        guard_gate.validate(read_json(directory/'index.json'), native, (directory/'engine.log').read_text(encoding='utf-8-sig'), mode)
        guards[mode] = {'provenance': audit.proof(directory/'provenance.json'), 'facts': native}
        guard_proofs.append(proof)
    require(guard_proofs[0]['binariesBefore'] == guard_proofs[1]['binariesBefore'] and
            guard_proofs[0]['wrappersBefore'] == guard_proofs[1]['wrappersBefore'], 'Guard pair uses different binary/wrappers')
    for key in ('engagementStartPosition', 'initialPreferredSlotOffsetCm', 'preferredNavigation', 'alternateNavigation'):
        require(guards['baseline']['facts'].get(key) is not None and
                guards['baseline']['facts'][key] == guards['improved']['facts'].get(key), 'Guard fixture geometry differs: '+key)
    for run in guards.values():
        exact(run['facts'].get('movementMeasured'), True, 'Final Guard movement was not actually sampled')
    full, capture_proof, captured = audit.capture(a.capture)
    require(full['seed'] == 1101 and full['policy'] == 'improved', 'Wrong frozen development demonstration')
    require(full['resolution'] == [1920, 1080], 'Final native capture must be 1920x1080')
    evaluation = read_json(a.evaluation)
    exact(evaluation.get('complete'), True, 'Holdout evaluation unfinished')
    exact(evaluation.get('allRunnersPassed'), True, 'A holdout runner failed')
    frozen = evaluation.get('frozen', {})
    require(frozen.get('inputs') == audit.inputs and frozen.get('binary') == audit.package, 'Holdout freeze differs')
    require(frozen.get('holdoutSeeds') == list(expected_seeds) and frozen.get('policies') == ['legacy', 'improved'], 'Holdout protocol changed')
    for name in ('training', 'humanPlaytest', 'populationWinRateClaim'):
        exact(frozen.get(name), False, 'Unsupported evaluation claim: '+name)
    bound_file(a.capture/'provenance.json', frozen.get('developmentEvidenceSha256'))
    require(Path(frozen.get('developmentEvidence', '')).resolve() == (a.capture/'provenance.json').resolve(), 'Holdout development proof differs')
    for name, digest in frozen.get('wrappers', {}).items():
        bound_file(child(ROOT/'scripts', name), digest)
    validate_pair_rows(evaluation.get('rows'), expected_seeds)
    evaluation_rows = []
    for row in evaluation['rows']:
        run, proof, _ = audit.capture(Path(row['output']), no_frames=True)
        require(run['seed'] == row['seed'] and run['policy'] == row['policy'], 'Evaluation run identity mismatch')
        require(run['resolution'] == full['resolution'], 'Evaluation resolution differs from the calibrated development capture')
        for key in METRICS:
            exact(row.get(key), run['metrics'][key], 'Evaluation summary disagrees with raw run: '+key)
        expected_legacy = row['policy'] == 'legacy'
        require(('-AegisGuardLegacy' in proof['command']) == expected_legacy, 'Policy does not match launched guard flag')
        evaluation_rows.append(run)
    video = review_video(a.video, a.capture, captured, a.video_qa)
    document = review_document(a.document, a.document_qa, a.expected_source)
    failures = preserve_failures(a.history_root, a.failed_run)
    extras = [{'artifact': file_record(path), 'classification': 'additional original unit/build evidence; not gameplay observations'}
              for path in a.test_evidence]
    require(snapshot_inputs(ROOT) == audit.inputs and binary_snapshot(audit.package_exe) == audit.package,
            'Runtime source/content/package changed during final review')
    for proof in audit.read_proofs:
        bound_file(proof['path'], proof)
    result = {'schemaVersion': 1, 'deliveryReady': True, 'reviewedAtUtc': datetime.now(timezone.utc).isoformat(),
              'reviewerScript': file_record(__file__), 'frozenInputs': audit.inputs, 'package': audit.package,
              'evaluationProtocol': {'expectedHoldoutSeeds': list(expected_seeds), 'policies': ['legacy', 'improved'],
                                     'developmentSeed': 1101, 'requiredResolution': full['resolution'],
                                     'seedSelection': 'Explicit CLI protocol; gate does not select or optimize seeds',
                                     'explicitPreservedHistoryDirectories': [str(path.resolve()) for path in a.failed_run]},
              'evidenceCategories': {
                  'nativeControl': {'assertions': 36, 'provenance': audit.proof(a.control/'provenance.json'), 'sessions': sessions,
                                    'fixtureDamage': True, 'fixturePlacement': True, 'aiFrozen': True, 'notCovered': control['notCovered']},
                  'nativeDecisionLabInput': {'assertions': 35, 'provenance': audit.proof(a.lab/'provenance.json'),
                                             'screenshots': lab_images, 'scope': 'Classic Utility/BT/EQS entry and input proof, not a repeat of v1.4 policy evaluation'},
                  'controlledGuardMechanism': {'assertionsPerRun': 12, 'runs': guards,
                                               'scope': 'Damage-disabled, stationary leader/target, controlled PIE geometry; baseline PASS reproduces the defect'},
                  'fullNativeDemonstration': full, 'pairedNativeObservations': {'record': audit.proof(a.evaluation), 'rows': evaluation_rows},
                  'encodedVideo': video, 'designDocument': document, 'additionalUnitOrBuildEvidence': extras},
              'preservedHistoricalFailures': failures,
              'limitations': [
                  'No model training, reinforcement learning, human playtest, general win-rate or hardware-performance claim.',
                  'Six holdout runs are three paired seeds; retain every natural won/lost outcome and all tradeoffs.',
                  'Tactical DebugState changes/short ABA counts include ordinary aim/fire/reposition; they are not Utility oscillation metrics.',
                  'Final Guard movementCm is end-of-test net displacement, including the occlusion stage; it is not path length or exact first-shot displacement.',
                  'Final Guard slotReason can describe the later no-visible-target formation stage rather than the earlier accepted firing slot.',
                  'Guard editor reports bind launcher/runtime DLL, not a historical before/after snapshot of the Editor fixture DLL.',
                  'Decision Lab input wrapper binds executables but not cooked containers before/after; the other packaged gates and final review bind the package containers.',
                  'Native controlled assertion counts and pure unit assertions are separate; no aggregate gameplay reliability score is created.'
              ]}
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write('\n')
    print(json.dumps({'deliveryReady': True, 'acceptance': file_record(output)}, ensure_ascii=False))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
