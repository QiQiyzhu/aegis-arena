"""Run the v2.4 release-menu fixture in a real rendered Unreal game world.

Menu actions and focus callback are synthetic; this is not a human playtest or
an OS focus-switch test. The probe exists only in Development/Editor builds.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import struct
import time

ROOT = Path(__file__).resolve().parents[1]
CHECKS = (
    'release_profile_opens_latest_briefing',
    'isolated_preferences_default_chinese_music_on',
    'clickable_language_and_music_controls',
    'deploy_starts_active_operation',
    'focus_callback_pauses_active_game',
    'focus_return_does_not_resume_or_advance_time',
    'manual_resume_clears_focus_pause',
    'restart_requires_confirmation_and_preserves_attempt',
    'cancel_restart_keeps_existing_attempt_paused',
    'confirmed_restart_returns_clean_briefing',
)
ART_CHECKS = ('tactical_pool_has_seven_fixed_components', 'real_charge_input_drives_semantic_windup',
    'reduced_effects_retains_semantic_charge', 'accepted_charge_release_emits_cue',
    'transient_cues_decay_to_zero', 'standard_cue_has_bounded_attack_and_decay',
    'reduced_cue_emits_fewer_decorations', 'presentation_does_not_change_health_or_energy',
    'stress_drops_decorations_without_allocating_components', 'stress_cues_expire_and_leave_semantic_layer',
    'real_dash_input_emits_directional_trail')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--editor', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cache-root', type=Path, required=True)
    parser.add_argument('--art', action='store_true')
    parser.add_argument('--width', type=int, default=1600)
    parser.add_argument('--height', type=int, default=900)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    command = [str(args.editor.resolve()), str(ROOT / 'AegisArena.uproject'),
        '/Game/Aegis/Maps/AegisArena', '-game', '-AegisRelease', '-AegisReleaseProbe',
        '-RenderOffscreen', '-d3d11', '-windowed', '-ForceRes', f'-ResX={args.width}', f'-ResY={args.height}',
        '-unattended', '-nosound', '-nosplash', '-nop4', '-stdout', '-FullStdOutLogOutput',
        f'-AegisReleaseProbeOutput={output / "native.json"}',
        f'-AegisPortfolioOutput={output / "sessions"}', f'-abslog={output / "engine.log"}']
    expected = CHECKS + (ART_CHECKS if args.art else ())
    if args.art: command += ['-AegisArtProbe', '-AegisInputProbe']
    environment = os.environ.copy()
    (args.cache_root / 'Temp').mkdir(parents=True, exist_ok=True)
    environment.update(TEMP=str(args.cache_root / 'Temp'), TMP=str(args.cache_root / 'Temp'))
    environment['UE-LocalDataCachePath'] = str(args.cache_root / 'DerivedDataCache')
    started = time.monotonic()
    report = dict(passed=False, command=command, humanPlaytest=False, shippingRuntime=False,
                  syntheticMenuActions=True, simulatedFocusCallback=True, realWindowFocusTest=False)
    try:
        with (output / 'stdout.log').open('w', encoding='utf-8') as stream:
            result = subprocess.run(command, cwd=ROOT, env=environment, stdout=stream,
                                    stderr=subprocess.STDOUT, timeout=300)
        report['exitCode'] = result.returncode
        if result.returncode: raise ValueError('Engine did not exit successfully')
        native = json.loads((output / 'native.json').read_text(encoding='utf-8-sig'))
        log = (output / 'engine.log').read_text(encoding='utf-8-sig')
        observed = [line.split('AEGIS_RELEASE_PROBE_CHECK ', 1)[1] for line in log.splitlines()
                    if 'LogTemp: Display: AEGIS_RELEASE_PROBE_CHECK ' in line]
        if observed != [name + ' PASS' for name in expected]: raise ValueError('Missing, failing or reordered native checks')
        if native.get('passed') is not True or native.get('checks') != len(expected): raise ValueError('Native summary mismatch')
        if any(term in log for term in ('Fatal error:', 'Assertion failed', 'Ensure condition failed')): raise ValueError('Engine error')
        if 'LogExit: Exiting.' not in log: raise ValueError('Normal game quit missing')
        views = ('briefing.png', 'focus-pause.png', 'restart-confirm.png')
        if args.art: views += ('briefing-en.png', 'tactical-hud.png', 'charge-full.png', 'charge-reduced.png', 'charge-impact.png', 'repair-full.png', 'repair-reduced.png', 'dash.png')
        for name in views:
            path = output / name
            if not path.is_file() or path.stat().st_size < 10000: raise ValueError('Rendered view missing: ' + name)
            header = path.read_bytes()[:24]
            if header[:8] != b'\x89PNG\r\n\x1a\n' or struct.unpack('>II', header[16:24]) != (args.width, args.height):
                raise ValueError('Rendered view dimensions mismatch: ' + name)
        report.update(passed=True, checks=list(expected), native=native)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        report['failure'] = str(error)
    report['seconds'] = time.monotonic() - started
    report['files'] = {str(p.relative_to(output)): dict(bytes=p.stat().st_size,
        sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in output.rglob('*') if p.is_file()}
    (output / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: report[k] for k in ('passed', 'failure', 'seconds') if k in report}))
    return 0 if report['passed'] else 2

if __name__ == '__main__':
    raise SystemExit(main())
