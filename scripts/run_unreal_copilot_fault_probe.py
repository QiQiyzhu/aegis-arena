"""Verify bounded real HTTP failures/cancellation against a disclosed loopback fixture.

This is native transport/lifecycle evidence, not model inference or a UI playtest.
The native probe invokes RequestPlan and uses normal Enter/P/Z/R controller input.
"""
import argparse
from collections import Counter
import datetime
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import threading
import time

from run_unreal_functional import snapshot_inputs
from run_unreal_input_probe import hash_executable
from run_unreal_trial import OFFLINE_ARGUMENT

ROOT = Path(__file__).resolve().parents[1]
MODEL = 'disclosed-copilot-fault-fixture'
CASES = ('malformed', 'manual_cancel', 'restart_cancel', 'http_503', 'timeout')
DELAYS = (0.25, 2.0, 2.0, 0.25, 12.0)
ASSERTIONS = (
    'begun_game_world', 'deployed_living_linked_companion', 'normal_pause_freezes_gameplay',
    'malformed_request_reaches_pending', 'malformed_response_rejected', 'malformed_returns_classic_guard',
    'manual_delay_request_pending', 'normal_resume_before_manual_guard', 'manual_guard_cancels_pending',
    'delayed_manual_response_never_accepted', 'delayed_manual_response_preserves_guard',
    'restart_delay_request_pending', 'normal_restart_replaces_pawn', 'restart_invalidates_pending_plan',
    'delayed_restart_response_never_accepted', 'restarted_companion_remains_guard',
    'redeploy_before_transport_failures', 'transport_requests_run_in_normal_pause',
    'http503_request_pending', 'http503_triggers_explicit_fallback', 'timeout_request_pending',
    'timeout_is_bounded_real_wait', 'timeout_triggers_explicit_fallback',
    'all_requests_accounted_no_plan_accepted', 'no_later_execution_after_timeout',
)
BEGIN = 'AEGIS_COPILOT_FAULT_BEGIN fixture_service=1 synthetic_keyboard=1 direct_planner_api=1 actual_model=0'
PASS = 'AEGIS_COPILOT_FAULT_PASS checks=25 requests=5'
DIRECT = re.compile(r'^(?:\[[^\]\r\n]*\])*LogTemp: Display: (AEGIS_COPILOT_FAULT_[^\r\n]+)$')


def finite(value, minimum=0, maximum=None):
    if (type(value) not in (int, float) or not math.isfinite(value) or value < minimum or
            (maximum is not None and value > maximum)):
        raise ValueError('Invalid numeric evidence field')
    return value


def validate(report, log):
    flags = dict(schemaVersion=1, passed=True, engine='unreal-runtime', worldType=1, begunPlay=True,
                 rendering='NullRHI', fixtureService=True, syntheticKeyboardInput=True,
                 directPlannerApi=True, normalGameplayPauseUsed=True, actualModelInference=False,
                 syntheticSlateInput=False, humanUsabilityTest=False, fixtureDamage=False,
                 combatValuesChanged=False)
    if any(type(report.get(key)) is not type(value) or report[key] != value for key, value in flags.items()):
        raise ValueError('Fault probe context or disclosed intervention flags do not match')
    seconds = finite(report.get('wallSeconds'), minimum=13, maximum=45)
    finite(report.get('timeoutObservedSeconds'), minimum=9, maximum=12.5)
    counters = dict(requests=5, acceptedPlans=0, rejectedPlans=3, fallbacks=3, completedSteps=0)
    if report.get('plannerCounters') != counters or any(
            type(report['plannerCounters'].get(key)) is not int for key in counters):
        raise ValueError('All five requests and three explicit fallbacks must be accounted for')
    rows = report.get('assertions')
    if (not isinstance(rows, list) or len(rows) != 25 or
            any(not isinstance(row, dict) or row.get('passed') is not True for row in rows) or
            Counter(row.get('name') for row in rows) != Counter(ASSERTIONS)):
        raise ValueError('Missing, repeated or failing native fault assertions')
    previous_wall = previous_game = -1
    for row in rows:
        wall = finite(row.get('wallSeconds'), maximum=seconds)
        game = finite(row.get('gameSeconds'))
        if wall < previous_wall or game < previous_game:
            raise ValueError('Fault assertion clocks regress')
        previous_wall, previous_game = wall, game
    events = [match.group(1) for line in log.splitlines() if (match := DIRECT.fullmatch(line))]
    prefix = 'AEGIS_COPILOT_FAULT_ASSERT_PASS '
    if (Counter(item[len(prefix):] for item in events if item.startswith(prefix)) != Counter(ASSERTIONS) or
            events.count(BEGIN) != 1 or events.count(PASS) != 1):
        raise ValueError('Original native log does not independently confirm every fault case')
    if any(marker in log for marker in ('AEGIS_COPILOT_FAULT_ASSERT_FAIL', 'AEGIS_COPILOT_FAULT_FAIL',
                                       'Handled ensure', 'Ensure condition failed', 'Assertion failed', 'Fatal error:',
                                       'AEGIS_PLAN_TRACE_LIMIT', 'AEGIS_PLAN_TRACE_WRITE_FAILED')):
        raise ValueError('Native run includes a failure, ensure or incomplete planner trace')


def validate_transport(trace, http):
    if not isinstance(trace, list) or not isinstance(http, list):
        raise ValueError('Planner and service traces must be original JSONL event arrays')
    kinds = Counter(row.get('event') for row in trace)
    if (kinds['request'] != 5 or kinds['fallback'] != 3 or not 2 <= kinds['response'] <= 3 or
            any(kinds[name] for name in ('plan_accepted', 'step_started', 'step_completed', 'plan_completed'))):
        raise ValueError('Trace must show rejected/cancelled transport without accepted or executed model plans')
    cancelled = [row.get('detail') for row in trace if row.get('event') == 'plan_cancelled']
    if cancelled.count('manual override') != 1 or cancelled.count('restart') != 1:
        raise ValueError('Missing unique manual override and restart cancellation events')
    received = [row for row in http if row.get('event') == 'received']
    attempts = [row for row in http if row.get('event') == 'response_attempt']
    finished = [row for row in http if row.get('event') == 'response_finished']
    if (len(received) != 5 or len(attempts) != 5 or len(finished) != 5 or
            [row.get('case') for row in received] != list(CASES)):
        raise ValueError('All five fixture requests and delayed response attempts must be retained')
    requests = [row for row in trace if row.get('event') == 'request']
    for index, (engine, service) in enumerate(zip(requests, received)):
        if (service.get('index') != index or service.get('authorizationPresent') is not False or
                service.get('request') != engine.get('data', {}).get('request_body') or
                service['request'].get('model') != MODEL):
            raise ValueError('Service request and exact native request body disagree or contain credentials')
        attempt = [row for row in attempts if row.get('index') == index]
        done = [row for row in finished if row.get('index') == index]
        if len(attempt) != 1 or len(done) != 1:
            raise ValueError('Duplicate or missing fixture completion')
        expected_status = 503 if index == 3 else 200
        elapsed = finite(attempt[0].get('wallSeconds')) - finite(service.get('wallSeconds'))
        if (attempt[0].get('status') != expected_status or elapsed < DELAYS[index] - 0.05 or
                done[0].get('result') not in ('sent', 'client_disconnected')):
            raise ValueError('Fixture timing or response status does not match the planned failure')
        if index in (0, 3) and done[0]['result'] != 'sent':
            raise ValueError('Malformed and HTTP503 cases must actually receive their response')
        if index in (1, 2, 4):
            content = json.loads(attempt[0]['response']['choices'][0]['message']['content'])
            if content['steps'][0] != {'skill': 'capture_relay', 'target': 'none'}:
                raise ValueError('Late reply must contain an otherwise-valid plan that would override Guard')


class FaultServer(ThreadingHTTPServer):
    daemon_threads = True


def fixture_server(output):
    lock = threading.Condition()
    state = {'received': 0, 'pending': 0}
    started = time.monotonic()
    path = output / 'fixture-http.jsonl'

    def record(event):
        with lock:
            event['wallSeconds'] = time.monotonic() - started
            with path.open('a', encoding='utf-8') as stream:
                stream.write(json.dumps(event, ensure_ascii=False) + '\n')

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_POST(self):
            size = int(self.headers.get('Content-Length', '0'))
            if self.path != '/v1/chat/completions' or not 0 < size <= 65536:
                self.send_error(400)
                return
            request = json.loads(self.rfile.read(size))
            with lock:
                index = state['received']
                state['received'] += 1
                state['pending'] += 1
            case = CASES[index] if index < len(CASES) else 'unexpected'
            record(dict(event='received', index=index, case=case, request=request,
                        authorizationPresent=bool(self.headers.get('Authorization'))))
            plan = {'label': 'Disclosed delayed transport fixture', 'steps': [
                {'skill': 'capture_relay', 'target': 'none'}, {'skill': 'guard', 'target': 'none'}]}
            content = json.dumps({'label': 'Deliberately missing steps'}) if index == 0 else json.dumps(plan)
            response = {'model': MODEL, 'choices': [
                {'finish_reason': 'stop', 'message': {'role': 'assistant', 'content': content}}]}
            status = 503 if index == 3 else 200 if index < 5 else 409
            if status != 200:
                response = {'error': {'message': 'Disclosed transport fixture service failure'}}
            delay = DELAYS[index] if index < 5 else 0
            try:
                time.sleep(delay)
                record(dict(event='response_attempt', index=index, case=case, status=status, response=response))
                body = json.dumps(response).encode('utf-8')
                try:
                    self.send_response(status)
                    self.send_header('Content-Type', 'application/json')
                    self.send_header('Content-Length', str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                    self.wfile.flush()
                    result = 'sent'
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, OSError):
                    result = 'client_disconnected'
                record(dict(event='response_finished', index=index, case=case, result=result))
            finally:
                with lock:
                    state['pending'] -= 1
                    lock.notify_all()

    server = FaultServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()

    def drain():
        with lock:
            return lock.wait_for(lambda: state['pending'] == 0, timeout=14)
    return server, drain


def read_jsonl(path):
    return [json.loads(line) for line in Path(path).read_text(encoding='utf-8-sig').splitlines() if line]


def retain_trace(report, output):
    trace_path = Path(report.get('tracePath', ''))
    if (not trace_path.is_absolute() or trace_path.parent.name != 'AegisPlans' or
            not re.fullmatch(r'[0-9a-fA-F]{32}\.jsonl', trace_path.name)):
        raise ValueError('Unexpected planner trace path')
    shutil.copyfile(trace_path, output / 'planner-trace.jsonl')
    return str(trace_path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine-root', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--cache-root', type=Path)
    parser.add_argument('--packaged-exe', type=Path)
    args = parser.parse_args()
    executable = (args.packaged_exe.resolve() if args.packaged_exe else
                  args.engine_root.resolve() / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe')
    if not executable.is_file():
        parser.error('Runtime executable is missing')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    server, drain = fixture_server(output)
    environment = os.environ.copy()
    for key in ('AEGIS_AI_ENDPOINT', 'AEGIS_AI_MODEL', 'AEGIS_AI_API_KEY', 'AEGIS_AI_FORMAT', 'AEGIS_AI_PROVIDER'):
        environment.pop(key, None)
    endpoint = f'http://127.0.0.1:{server.server_port}/v1/chat/completions'
    environment.update(AEGIS_AI_ENDPOINT=endpoint, AEGIS_AI_MODEL=MODEL, AEGIS_AI_FORMAT='json_object')
    if args.cache_root:
        cache = args.cache_root.resolve()
        (cache / 'Temp').mkdir(parents=True, exist_ok=True)
        environment.update(TEMP=str(cache / 'Temp'), TMP=str(cache / 'Temp'))
        environment['UE-LocalDataCachePath'] = str(cache / 'DerivedDataCache')
    command = [str(executable)]
    if not args.packaged_exe:
        command.append(str(ROOT / 'AegisArena.uproject'))
    command.append('/Game/Aegis/Maps/AegisArena')
    if not args.packaged_exe:
        command.append('-game')
    command += ['-NullRHI', '-RenderOffscreen', '-AegisCopilotFaultProbe', '-AegisPlannerFixture',
                '-AegisInputProbe', '-AegisQuit', f'-AegisCopilotFaultOutput={output}', '-unattended',
                '-nop4', OFFLINE_ARGUMENT, '-stdout', '-FullStdOutLogOutput', f'-abslog={output / "engine.log"}']
    inputs = snapshot_inputs()
    started = time.monotonic()
    provenance = dict(passed=False, **inputs, engine='unreal-runtime', world='Game', rendering='NullRHI',
                      actualModelInference=False, fixtureService=True, syntheticKeyboardInput=True,
                      directPlannerApi=True, syntheticSlateInput=False, humanUsabilityTest=False,
                      normalGameplayPauseUsed=True, fixtureDamage=False, combatValuesChanged=False,
                      scope='Five disclosed real HTTP failure/cancellation cases; not model or UI evaluation',
                      command=command, endpoint=endpoint, model=MODEL, packaged=bool(args.packaged_exe),
                      actorDeadlineSeconds=45, processTimeoutSeconds=100, fixtureCases=list(CASES),
                      sourceDigestScope='Source, core/include, Config, AegisArena.uproject; sorted relative path + bytes',
                      contentDigestScope='Content/Aegis; sorted relative path + bytes',
                      startedAtUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                      executableSha256=hash_executable(executable),
                      gateSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    try:
        with (output / 'stdout.log').open('w', encoding='utf-8') as stream:
            result = subprocess.run(command, env=environment, stdout=stream, stderr=subprocess.STDOUT,
                                    cwd=executable.parent if args.packaged_exe else ROOT, timeout=100, check=False)
        provenance['exitCode'] = result.returncode
        if not drain():
            raise RuntimeError('Fixture service still had unfinished delayed responses')
        report = json.loads((output / 'copilot-fault-probe.json').read_text(encoding='utf-8-sig'))
        provenance['traceSource'] = retain_trace(report, output)
        if result.returncode != 0:
            raise RuntimeError('Native fault probe process failed; raw failure evidence retained')
        log = (output / 'engine.log').read_text(encoding='utf-8-sig', errors='replace')
        validate(report, log)
        validate_transport(read_jsonl(output / 'planner-trace.jsonl'), read_jsonl(output / 'fixture-http.jsonl'))
        provenance.update(inputsAfter=snapshot_inputs(), executableSha256After=hash_executable(executable))
        if provenance['inputsAfter'] != inputs or provenance['executableSha256After'] != provenance['executableSha256']:
            raise RuntimeError('Source, assets or executable changed during the fault probe')
        provenance.update(passed=True, nativeAssertions=25, timeoutObservedSeconds=report['timeoutObservedSeconds'])
        print(f'Copilot fault probe PASS: 25 native checks, five disclosed HTTP fixture cases; {output}')
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        provenance['failure'] = str(error)
        raise
    finally:
        provenance['delayedResponsesDrained'] = drain()
        server.shutdown()
        server.server_close()
        provenance['wallSeconds'] = time.monotonic() - started
        provenance['files'] = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                               for path in output.iterdir() if path.is_file()}
        (output / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
