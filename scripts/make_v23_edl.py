"""Make a chronological, uncut v2.3 EDL from validated native clocks and events.

No video is generated. Every original PNG remains selected exactly once.
--preview explicitly permits sparse capture frames for timeline review only.
"""
import argparse
from bisect import bisect_left, bisect_right
import hashlib
import json
import math
from pathlib import Path

from run_decision_lab import strict_json
from run_v2_capture import validate_system_capture

FPS = 30
EPSILON = 1e-5


def require(ok, reason):
    if not ok: raise ValueError(reason)


def number(value):
    require(type(value) in (int, float) and math.isfinite(value), 'Nonfinite or missing clock')
    return float(value)


def sha(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''): result.update(block)
    return result.hexdigest()


class ClockMap:
    """Interpolate running time, preserving video time spent at a paused world clock."""
    def __init__(self, samples):
        require(isinstance(samples, list) and len(samples) >= 2, 'Missing native clock samples')
        self.groups = []
        previous_world = previous_video = -1
        for sample in samples:
            world, video = number(sample.get('worldSeconds')), number(sample.get('videoSeconds'))
            require(world >= previous_world and video >= previous_video, 'Native clock regressed')
            if self.groups and world == self.groups[-1]['world']:
                self.groups[-1]['rows'].append(sample)
                self.groups[-1]['last'] = video
            else:
                self.groups.append(dict(world=world, first=video, last=video, rows=[sample]))
            previous_world, previous_video = world, video
        self.worlds = [group['world'] for group in self.groups]

    def video(self, world, active=True):
        world = number(world)
        index = bisect_left(self.worlds, world)
        for candidate in (index, index - 1):
            if 0 <= candidate < len(self.groups) and abs(self.worlds[candidate] - world) <= EPSILON:
                group = self.groups[candidate]
                rows = [row for row in group['rows'] if row.get('phase') == 1] if active else []
                return rows[0]['videoSeconds'] if rows else group['first']
        require(0 < index < len(self.groups), 'Event world clock is outside native samples')
        before, after = self.groups[index - 1], self.groups[index]
        fraction = (world - before['world']) / (after['world'] - before['world'])
        # After a pause, interpolation begins at its last video sample. Before a
        # pause, it ends at the first video sample with that world timestamp.
        return before['last'] + fraction * (after['first'] - before['last'])


def frame_clock(capture, preview):
    rows = capture.get('frameTimes')
    require(isinstance(rows, list) and len(rows) >= 2, 'EDL requires captured PNG frames')
    require(capture.get('frames') == capture.get('completedFrames') == len(rows), 'Incomplete frames')
    times = [number(row.get('videoSeconds')) for row in rows]
    interval = times[1] - times[0]
    require(interval > 0 and all(abs(b - a - interval) < EPSILON for a, b in zip(times, times[1:])),
            'Irregular native frame clock')
    require(preview or abs(interval - 1 / FPS) < EPSILON,
            'Final EDL requires native 30 fps; sparse captures need --preview')
    for index, row in enumerate(rows):
        require(row.get('file') == f'frame-{index:06d}.png', 'Noncanonical frame order')
    if preview:
        end = number(capture.get('videoSeconds'))
        require(times[-1] < end <= times[-1] + interval + EPSILON, 'Invalid preview video end')
    else:
        end = times[-1] + 1 / FPS
    return times, end


def make_edl(capture, events, preview=False):
    require(capture.get('validRun') is True and capture.get('gameplayVersion') == '2.3' and
            capture.get('languageDemo') is True,
            'A validated v2.3 gameplay capture is required')
    require(capture.get('outcome') == 'won', 'This complete three-stage delivery requires a naturally won run')
    times, end = frame_clock(capture, preview)
    samples = capture.get('samples', [])
    clock = ClockMap(samples)
    phases = []
    previous = None
    for row in samples:
        state = (row.get('phase'), row.get('wave'))
        if state != previous:
            phases.append((number(row['videoSeconds']), state))
            previous = state
    states = [state for _, state in phases]
    require(states == [(0, 0), (1, 1), (2, 1), (1, 2), (2, 2), (1, 3), (3, 3)],
            'Unexpected three-stage phase order')
    phases[0] = (0.0, phases[0][1])
    require(phases[-1][0] < end, 'Result hold missing from native timeline')
    extraction_samples = [row for row in samples if row.get('phase') == 1 and
                          row.get('wave') == 3 and row.get('enemies') == 0]
    extraction_start = number(extraction_samples[0]['videoSeconds']) if extraction_samples else None
    require(isinstance(events, list) and events, 'Missing original portfolio event stream')
    previous = -1
    for row in events:
        stamp = number(row.get('gameSeconds'))
        require(stamp >= previous, 'Event clock regressed')
        require(isinstance(row.get('data'), dict), 'Missing event data')
        previous = stamp

    windows = []
    for supply, label in ((False, 'survey_key'), (True, 'survey_supply')):
        claims = [row for row in events if row.get('event') == 'survey_claimed' and row['data'].get('supply') is supply]
        require(len(claims) == 1, 'Expected exactly one key and one actual supply claim')
        claim = claims[0]
        node = claim['data'].get('node')
        starts = [row for row in events if row.get('event') == 'survey_started' and
                  row['data'].get('node') == node and row['data'].get('supply') is supply and
                  row['gameSeconds'] <= claim['gameSeconds']]
        require(starts, 'Survey claim lacks its source input event')
        claim_video = clock.video(claim['gameSeconds'])
        phase_index = bisect_right([stamp for stamp, _ in phases], claim_video) - 1
        phase_start, state = phases[phase_index]
        require(state[0] == 1 and phase_index + 1 < len(phases), 'Survey claim outside active phase')
        phase_end = phases[phase_index + 1][0]
        start = max(phase_start, clock.video(starts[0]['gameSeconds']))
        if supply:
            choices = [row for row in events if row.get('event') == 'survey_mode_selected' and
                       row['data'].get('supply') is True and row['gameSeconds'] <= starts[0]['gameSeconds']]
            require(choices, 'Supply chapter lacks the actual H choice event')
            start = max(phase_start, clock.video(choices[-1]['gameSeconds']))
            stop = min(phase_end, claim_video + 1.5)
            reason = '从原生 H 补给选择开始，保留绕路、扫描与实际治疗反馈。'
        else:
            binds = [row for row in events if row.get('event') == 'survey_boost_bound' and
                     row['gameSeconds'] >= claim['gameSeconds']]
            require(binds, 'Key chapter lacks an actual relay binding')
            bound = clock.video(binds[0]['gameSeconds'])
            stop = min(phase_end, bound if claim_video < bound <= claim_video + 3 else claim_video + 1)
            reason = '从首次原生扫描开始，完整保留中断、取消与重试，直到密钥领取并转入中继。'
        require(start < claim_video < stop + EPSILON, 'Invalid survey chapter clock range')
        windows.append(dict(label=label, start=start, end=stop, reason=reason))

    # Neutral upgrade captions describe the actual choices independently of
    # where the language demonstration happens within a captured run.
    labels = {(0, 0): 'mission_briefing', (1, 1): 'stage1_open', (2, 1): 'upgrade_first',
              (1, 2): 'stage2_transfer', (2, 2): 'upgrade_second', (1, 3): 'stage3_open', (3, 3): 'mission_result'}
    reasons = {
        'mission_briefing': '完整原生简报与中文默认 / 英文切换，直到实际部署。',
        'stage1_open': '实际第一阶段开局与接近扫描缓存，保留全部移动和交火。',
        'stage1_objective': '领取密钥后的原生中继推进，包含实际密钥绑定与加速。',
        'upgrade_first': '完整第一次升级与阶段转场，保留世界时钟暂停时的画面。',
        'stage2_transfer': '完整第二阶段转移、战斗与中继流程。',
        'upgrade_second': '完整第二次升级与阶段转场，保留世界时钟暂停时的画面。',
        'stage3_open': '第三阶段原生战斗与移动。',
        'stage3_finish': '清敌后的原生撤离移动、目标推进与收束。',
        'mission_result': '完整原生结算停留，保留到最后一帧。',
    }
    cuts = sorted({0.0, end, *(stamp for stamp, _ in phases),
                   *(window[key] for window in windows for key in ('start', 'end')),
                   *([] if extraction_start is None else [extraction_start])})
    phase_times = [stamp for stamp, _ in phases]
    result = []
    def snap(value):
        if value <= 0: return 0.0
        if value >= end: return end
        index = bisect_left(times, value - EPSILON)
        return times[index] if index < len(times) else end
    for start, stop in zip(cuts, cuts[1:]):
        midpoint = (start + stop) / 2
        state = phases[bisect_right(phase_times, midpoint) - 1][1]
        label = labels[state]
        reason = reasons[label]
        if state == (1, 1) and midpoint >= windows[0]['end']:
            label = 'stage1_objective'; reason = reasons[label]
        if state == (1, 3) and extraction_start is not None and midpoint >= extraction_start:
            label = 'stage3_finish'; reason = reasons[label]
        for window in windows:
            if state[0] == 1 and window['start'] <= midpoint < window['end']:
                label, reason = window['label'], window['reason']
        first, last = snap(start), snap(stop)
        if first == last: continue
        if result and result[-1]['label'] == label:
            result[-1]['end'] = last
        else:
            result.append(dict(label=label, start=first, end=last, reason=reason))
    require(result and result[0]['start'] == 0 and result[-1]['end'] == end, 'EDL lost source endpoints')
    selected = []
    for index, row in enumerate(result):
        require(row['end'] > row['start'], 'Empty chapter')
        if index: require(row['start'] == result[index - 1]['end'], 'EDL gap or overlap')
        first, last = bisect_left(times, row['start']), bisect_left(times, row['end'])
        require(last > first, 'Chapter has no original frame')
        selected.extend(range(first, last))
    require(selected == list(range(len(times))), 'EDL must select every native frame exactly once')
    return result


def load_validated_capture(folder):
    folder = Path(folder).resolve()
    provenance = strict_json((folder / 'provenance.json').read_text(encoding='utf-8-sig'))
    require(provenance.get('passed') is True, 'Capture wrapper did not pass')
    traces = list((folder / 'system').glob('portfolio-*.jsonl'))
    require(len(traces) == 1, 'Expected one original portfolio event stream')
    trace = traces[0]
    paths = [folder / 'capture.json', folder / 'engine.log', trace, trace.with_suffix('.json')]
    manifest = provenance.get('files', {})
    for path in paths:
        relative = str(path.relative_to(folder))
        bound = manifest.get(relative, manifest.get(relative.replace('\\', '/')))
        require(isinstance(bound, dict) and path.stat().st_size == bound.get('bytes') and sha(path) == bound.get('sha256'),
                'Source differs from passed capture provenance: ' + relative)
    capture = strict_json(paths[0].read_text(encoding='utf-8-sig'))
    validate_system_capture(folder, capture, paths[1].read_text(encoding='utf-8-sig'))
    events = [strict_json(line) for line in trace.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
    return capture, events


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='new JSON EDL file; never overwrites')
    parser.add_argument('--preview', action='store_true', help='explicit sparse-frame timeline review only')
    args = parser.parse_args()
    output = args.output.resolve()
    require(not output.exists(), 'Output already exists; choose a new EDL filename')
    require(output != args.capture.resolve() and args.capture.resolve() not in output.parents,
            'EDL output must be outside the preserved capture')
    capture, events = load_validated_capture(args.capture)
    edl = make_edl(capture, events, args.preview)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('x', encoding='utf-8') as stream:
        json.dump(edl, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write('\n')
    print(json.dumps(dict(output=str(output), previewOnly=args.preview, allNativeFramesRetained=True,
                          nativeFrames=capture['frames'], sourceEnd=edl[-1]['end'],
                          chapters=[{key: row[key] for key in ('label', 'start', 'end')} for row in edl]), indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
