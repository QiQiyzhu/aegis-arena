"""Verify the real Slate -> HTTP plan -> tactical skill path in a rendered game.

--fixture uses a disclosed local transport stub, never counted as model inference.
Without it, the saved provider is used for one actual request and its normal fee.
"""
import argparse
from collections import Counter
import datetime
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import threading
import time

from aegis_ai_config import build_environment
from run_unreal_functional import snapshot_inputs
from run_unreal_input_probe import inspect_png
from run_unreal_trial import OFFLINE_ARGUMENT

ROOT = Path(__file__).resolve().parents[1]
SCREENSHOTS = ('copilot-open.png', 'copilot-request.png', 'copilot-plan.png', 'copilot-executing.png')
ASSERTIONS = (
    'begun_game_world', 'deployed_with_planner', 'tab_opens_paused_slate_panel',
    'slate_text_and_enter_submit_real_request', 'provider_plan_passes_runtime_validation',
    'goal_preserves_capture_then_guard_order', 'network_response_does_not_advance_paused_game',
    'slate_tab_returns_to_combat', 'validated_capture_drives_real_companion_movement',
    'manual_guard_cancels_model_plan', 'restart_clears_plan_and_replaces_pawn',
    'planner_can_reopen_after_restart', 'slate_escape_closes_panel',
)
DIRECT = re.compile(r'^(?:\[[^\]\r\n]*\])*LogTemp: Display: (AEGIS_COPILOT_[^\r\n]+)$')

def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''): digest.update(block)
    return digest.hexdigest()

def validate(report, log, output, fixture):
    expected_flags = {'passed': True, 'engine': 'unreal-runtime', 'worldType': 1,
                      'begunPlay': True, 'syntheticSlateInput': True,
                      'humanUsabilityTest': False, 'fixtureService': fixture}
    if any(type(report.get(k)) is not type(v) or report.get(k) != v for k, v in expected_flags.items()):
        raise ValueError('Native copilot result or declared evidence scope failed')
    rows = report.get('assertions', [])
    if (len(rows) != len(ASSERTIONS) or any(row.get('passed') is not True for row in rows) or
            Counter(row.get('name') for row in rows) != Counter(ASSERTIONS)):
        raise ValueError('Missing or repeated native copilot assertions')
    events = [m.group(1) for line in log.splitlines() if (m := DIRECT.fullmatch(line))]
    if (Counter(event.removeprefix('AEGIS_COPILOT_ASSERT_PASS ') for event in events
                if event.startswith('AEGIS_COPILOT_ASSERT_PASS ')) != Counter(ASSERTIONS) or
            [event for event in events if event.startswith('AEGIS_COPILOT_PROBE_BEGIN')] !=
            [f'AEGIS_COPILOT_PROBE_BEGIN synthetic_slate=1 fixture_service={int(fixture)}'] or
            [event for event in events if event.startswith('AEGIS_COPILOT_PROBE_PASS')] !=
            ['AEGIS_COPILOT_PROBE_PASS checks=13']):
        raise ValueError('Native log does not independently confirm the assertion set')
    if any(text in log for text in ('AEGIS_COPILOT_ASSERT_FAIL', 'AEGIS_COPILOT_PROBE_FAIL',
                                    'Fatal error:', 'Assertion failed', 'Ensure condition failed', 'Handled ensure')):
        raise ValueError('Unreal reported a failure or ensure')
    if type(report.get('wallSeconds')) not in (int, float) or not 0 < report['wallSeconds'] <= 70:
        raise ValueError('Invalid probe duration')
    images = report.get('screenshots', [])
    if (len(images) != 4 or any(not isinstance(x, str) or not Path(x).is_absolute() for x in images) or
            {Path(x).resolve() for x in images} !=
            {(Path(output) / name).resolve() for name in SCREENSHOTS}):
        raise ValueError('Screenshot paths do not belong to this invocation')
    return [inspect_png(Path(output) / name) for name in SCREENSHOTS]

def fixture_server(output):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_): pass
        def do_POST(self):
            size = int(self.headers.get('Content-Length', '0'))
            if not 0 < size <= 16384 or self.path != '/v1/chat/completions':
                self.send_error(400); return
            request = json.loads(self.rfile.read(size))
            # Deliberate transport fixture. This is never represented as model reasoning.
            plan = {'label': 'Transport fixture: capture then guard', 'steps': [
                {'skill': 'capture_relay', 'target': 'none'}, {'skill': 'guard', 'target': 'none'}]}
            response = {'model': 'disclosed-transport-fixture', 'choices': [
                {'finish_reason': 'stop', 'message': {'role': 'assistant', 'content': json.dumps(plan)}}]}
            with (output / 'fixture-http.jsonl').open('a', encoding='utf-8') as stream:
                stream.write(json.dumps({'request': request, 'response': response}, ensure_ascii=False) + '\n')
            time.sleep(0.7)
            body = json.dumps(response).encode()
            self.send_response(200); self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine-root', required=True, type=Path)
    parser.add_argument('--cache-root', type=Path)
    parser.add_argument('--packaged-exe', type=Path)
    parser.add_argument('--config', type=Path)
    parser.add_argument('--fixture', action='store_true')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=False)
    server = fixture_server(output) if args.fixture else None
    # Only process environment carries a real credential. Never serialize it.
    environment = os.environ.copy() if args.fixture else build_environment(args.config)
    if server:
        for name in ('AEGIS_AI_API_KEY', 'AEGIS_AI_ENDPOINT', 'AEGIS_AI_MODEL', 'AEGIS_AI_FORMAT', 'AEGIS_AI_PROVIDER'):
            environment.pop(name, None)
        environment.update(AEGIS_AI_ENDPOINT=f'http://127.0.0.1:{server.server_port}/v1/chat/completions',
                           AEGIS_AI_MODEL='disclosed-transport-fixture', AEGIS_AI_FORMAT='json_object')
    if args.cache_root:
        cache = args.cache_root.resolve(); (cache / 'Temp').mkdir(parents=True, exist_ok=True)
        environment.update(TEMP=str(cache / 'Temp'), TMP=str(cache / 'Temp'))
        environment['UE-LocalDataCachePath'] = str(cache / 'DerivedDataCache')
    executable = (args.packaged_exe.resolve() if args.packaged_exe else
                  args.engine_root.resolve() / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe')
    command = [str(executable)]
    if not args.packaged_exe: command += [str(ROOT / 'AegisArena.uproject')]
    command += ['/Game/Aegis/Maps/AegisArena']
    if not args.packaged_exe: command += ['-game']
    command += ['-RenderOffscreen', '-AegisCopilotProbe', '-AegisInputProbe', '-AegisQuit',
                f'-AegisCopilotOutput={output}', '-ResX=1280', '-ResY=720', '-windowed',
                '-unattended', '-nop4', OFFLINE_ARGUMENT, '-stdout', '-FullStdOutLogOutput',
                f'-abslog={output / "engine.log"}']
    if args.fixture: command += ['-AegisPlannerFixture']
    inputs = snapshot_inputs(); started = time.monotonic()
    record = dict(passed=False, **inputs, command=command, actualModelInference=not args.fixture,
                  fixtureService=args.fixture, syntheticSlateInput=True, humanUsabilityTest=False,
                  providerEndpoint=environment.get('AEGIS_AI_ENDPOINT'), model=environment.get('AEGIS_AI_MODEL'),
                  executableSha256=sha(executable), packaged=bool(args.packaged_exe),
                  startedAtUtc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    try:
        with (output / 'stdout.log').open('w', encoding='utf-8') as stream:
            result = subprocess.run(command, env=environment, stdout=stream, stderr=subprocess.STDOUT,
                                    cwd=executable.parent if args.packaged_exe else ROOT, timeout=120)
        record['exitCode'] = result.returncode
        if result.returncode != 0: raise RuntimeError('Native copilot process failed')
        report = json.loads((output / 'copilot-probe.json').read_text(encoding='utf-8-sig'))
        log = (output / 'engine.log').read_text(encoding='utf-8-sig', errors='replace')
        record['screenshots'] = validate(report, log, output, args.fixture)
        trace_path = Path(report['tracePath'])
        if (not trace_path.is_absolute() or trace_path.parent.name != 'AegisPlans' or
                not re.fullmatch(r'[0-9a-fA-F]{32}\.jsonl', trace_path.name)):
            raise ValueError('Unexpected planner trace path')
        shutil.copyfile(trace_path, output / 'planner-trace.jsonl')
        trace = [json.loads(line) for line in (output / 'planner-trace.jsonl').read_text(encoding='utf-8-sig').splitlines()]
        kinds = Counter(row['event'] for row in trace)
        if kinds['request'] != 1 or kinds['response'] != 1 or kinds['plan_accepted'] != 1 or not kinds['step_started'] or not kinds['plan_cancelled']:
            raise ValueError('Missing unique model request, accepted plan, execution or cancellation in trace')
        record.update(inputsAfter=snapshot_inputs(), executableSha256After=sha(executable))
        if record['inputsAfter'] != inputs or record['executableSha256After'] != record['executableSha256']:
            raise ValueError('Code, assets or executable changed during this run')
        record.update(passed=True, nativeAssertions=13, provider=report['provider'], plan=report['plan'], traceSource=str(trace_path))
        print(f"Copilot probe PASS: 13 checks; {'transport fixture' if args.fixture else 'actual provider inference'}; {output}")
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        record['failure'] = str(error)
        raise
    finally:
        if server: server.shutdown(); server.server_close()
        record['wallSeconds'] = time.monotonic() - started
        record['files'] = {p.name: sha(p) for p in output.iterdir() if p.is_file()}
        (output / 'provenance.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')

if __name__ == '__main__': main()
