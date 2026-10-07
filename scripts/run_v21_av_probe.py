"""Sound-enabled native lifecycle evidence with actual Unreal master-submix WAVs."""
import argparse
import array
import datetime
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import wave

from run_unreal_functional import snapshot_inputs
from run_unreal_trial import OFFLINE_ARGUMENT
from run_decision_lab import sha, read_json, validate_engine_messages
from run_portfolio_probe import binary_snapshot

ROOT = Path(__file__).resolve().parents[1]
ASSERTIONS = ('seven_tracks_loaded', 'one_music_actor_two_channels_budget', 'briefing_voice_playing',
    'm_mutes_and_stops_voices', 'm_restores_music_playback', 'enter_switches_to_relay_score',
    'paused_menu_has_quiet_score', 'paused_mute_freezes_game_clock', 'paused_unmute_restores_one_voice',
    'resume_restores_relay_score', 'restart_creates_new_player', 'restart_restores_one_briefing_voice',
    'second_restart_has_no_music_actor_leak', 'three_master_mix_wavs_exported', 'normal_menu_quit_available')

def require(ok, message):
    if not ok:
        raise ValueError(message)

def wav_metrics(path):
    with wave.open(str(path), 'rb') as w:
        require(w.getsampwidth() == 2, 'Master submix must be PCM16')
        channels, rate, frames = w.getnchannels(), w.getframerate(), w.getnframes()
        require(1 <= channels <= 8 and rate >= 22050 and .5 <= frames/rate <= 4, 'Unexpected master export format/duration')
        values = array.array('h', w.readframes(frames))
    if sys.byteorder != 'little': values.byteswap()
    require(len(values) == frames*channels, 'Truncated master export')
    peak = max(abs(x) for x in values)/32768
    rms = math.sqrt(sum(float(x)*x for x in values)/len(values))/32768
    return dict(path=str(path), bytes=path.stat().st_size, sha256=sha(path), channels=channels,
                sampleRate=rate, frames=frames, duration=frames/rate, peak=peak, rms=rms)

def finite_number(value):
    return type(value) in (int, float) and math.isfinite(value)

def validate_report(report):
    require(isinstance(report, dict), 'Native report must be an object')
    require(type(report.get('schemaVersion')) is int and report['schemaVersion'] == 1, 'Wrong report schema')
    require(report.get('passed') is True and report.get('presentationVersion') == '2.1', 'Native AV probe failed')
    for key, value in {'engine':'unreal-runtime', 'soundEnabled':True, 'syntheticKeyboardInput':True,
                       'fixtureDamage':False, 'fixturePlacement':False, 'humanPlaytest':False,
                       'hardwareLoopback':False, 'quitPath':'normal menu X'}.items():
        require(type(report.get(key)) is type(value) and report[key] == value, 'Wrong scope: '+key)
    rows = report.get('assertions', [])
    require(isinstance(rows, list) and all(isinstance(x, dict) for x in rows), 'Assertions must be objects')
    require(tuple(x.get('name') for x in rows) == ASSERTIONS and all(x.get('passed') is True for x in rows),
            'Missing native assertions')
    prev = -1
    for row in rows:
        require(finite_number(row.get('wallSeconds')) and prev <= row['wallSeconds'] <= 35,
                'Invalid native wall clock')
        require(finite_number(row.get('gameSeconds')) and row['gameSeconds'] >= 0, 'Invalid native game clock')
        require(type(row.get('loadedTracks')) is int and row['loadedTracks'] == 7, 'Invalid track inventory')
        prev = row['wallSeconds']
        require(type(row.get('playingVoices')) is int and 0 <= row['playingVoices'] <= 2, 'Music voice budget exceeded')
        require(isinstance(row.get('gains'), list) and len(row['gains']) == 2 and
                all(finite_number(x) and 0 <= x <= .5 for x in row['gains']), 'Invalid gains')
        require(type(row.get('muted')) is bool and type(row.get('paused')) is bool, 'Invalid state booleans')

    # Stable stages have waited longer than their fades. The final P-menu checks
    # can still be in the .65-second crossfade: do not demand one voice there.
    states = ('briefing', 'briefing', 'briefing', 'muted', 'briefing', 'relay1',
              'paused', 'muted', 'paused', 'relay1', 'briefing', 'briefing', 'briefing')
    for index, row in enumerate(rows):
        muted = index in (3, 7)
        paused = index in (6, 7, 8, 13, 14)
        require(row['muted'] is muted and row['paused'] is paused, 'Contradictory native mute/pause state: '+row['name'])
        if index < len(states):
            require(row.get('musicState') == states[index], 'Wrong native music state: '+row['name'])
            require(row['playingVoices'] == (0 if muted else 1), 'Wrong native playing voices: '+row['name'])
        else:
            require(row.get('musicState') in ('briefing', 'paused') and row['playingVoices'] in (1, 2),
                    'Wrong final menu crossfade state')
        require(sum(row['gains']) == 0 if muted else sum(row['gains']) > 0,
                'Contradictory native gain: '+row['name'])
    require(sum(rows[6]['gains']) <= .161, 'Paused score exceeds native quiet gain assertion')
    # Only these three consecutive rows are inside the same uninterrupted pause.
    require(max(row['gameSeconds'] for row in rows[6:9]) - min(row['gameSeconds'] for row in rows[6:9]) < .001,
            'Game clock advanced during paused mute/unmute')
    for group in ((0, 1, 2), (10, 11), (13, 14)):
        for index in group[1:]:
            require(all(rows[index][key] == rows[group[0]][key] for key in
                        ('gameSeconds', 'paused', 'muted', 'musicState', 'playingVoices', 'loadedTracks', 'gains')),
                    'Consecutive checks in one native stage disagree')
    require(finite_number(report.get('wallSeconds')) and prev <= report['wallSeconds'] <= 35,
            'Invalid final native wall clock')

def validate(output):
    output = Path(output)
    report = read_json(output/'av-probe.json')
    validate_report(report)
    log = (output/'engine.log').read_text(encoding='utf-8-sig')
    # UE5.8 WASAPI explicitly enables AUTOCONVERTPCM for a 44.1 kHz device.
    # Bind this exact paired diagnostic; other audio warnings remain failures.
    device_messages = ('Sample rate mismatch. Engine sample rate: 48000 Device sample rate: 44100',
                       'Device level sample rate conversion will be used.')
    device_rows = []
    remaining = []
    device_pattern = re.compile(r'^(?:\[[^\]\r\n]*\])*LogAudioMixerWasapi: Warning: (.+)$')
    for line in log.splitlines():
        found_device = device_pattern.fullmatch(line)
        if found_device and found_device.group(1) in device_messages:
            device_rows.append(found_device.group(1))
        else:
            remaining.append(line)
    require(not device_rows or tuple(device_rows) == device_messages, 'Incomplete/repeated device conversion diagnostic')
    validate_engine_messages('\n'.join(remaining), rendered=True)
    direct = re.compile(r'^(?:\[[^\]\r\n]*\])*LogTemp: Display: (AEGIS_AV_PROBE_[^\r\n]+)$')
    found = [m.group(1) for line in log.splitlines() if (m := direct.fullmatch(line))]
    require(found == ['AEGIS_AV_PROBE_BEGIN keyboard=1 fixtureDamage=0 fixturePlacement=0 soundEnabled=1',
                     *('AEGIS_AV_PROBE_ASSERT_PASS '+x for x in ASSERTIONS), 'AEGIS_AV_PROBE_PASS checks=15'],
            'Missing/duplicate native event sequence')
    require(sum(bool(re.fullmatch(r'(?:\[[^\]\r\n]*\])*LogExit: Exiting\.', x)) for x in log.splitlines()) == 1,
            'Normal engine shutdown not recorded')
    clips = {n:wav_metrics(output/'audio'/f'bgm-{n}.wav') for n in ('on','muted','restored')}
    for n in ('on','restored'):
        require(clips[n]['rms'] > .0001 and clips[n]['peak'] < .995, 'Missing or clipped rendered music: '+n)
    require(clips['muted']['rms'] <= max(clips['on']['rms'], clips['restored']['rms'])*.025+.00005,
            'Muted master mix is not substantially quieter')
    return {'nativeAssertions':15, 'masterSubmixWavs':clips, 'deviceConversionDiagnostics':device_rows,
            'scope':'Actual UE master-mix samples and ordinary keyboard lifecycle. Not physical speaker proof or a human listening test.'}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, required=True); p.add_argument('--output', type=Path, required=True)
    p.add_argument('--editor', action='store_true'); p.add_argument('--cache-root', type=Path)
    a = p.parse_args(); exe=a.exe.resolve(); out=a.output.resolve()
    require(exe.is_file(), 'Executable missing'); out.mkdir(parents=True, exist_ok=False)
    command=[str(exe)]
    if a.editor: command += [str(ROOT/'AegisArena.uproject'), '/Game/Aegis/Maps/AegisArena','-game']
    command += ['-AegisV2','-AegisAVProbe','-AegisInputProbe','-RenderOffscreen','-d3d11','-AudioMixer',
                '-windowed','-ForceRes','-ResX=1280','-ResY=720','-unattended','-nop4','-nosplash','-stdout',
                '-FullStdOutLogOutput','-ExecCmds=au.NeverDisableSubmixes 1',
                '-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1',OFFLINE_ARGUMENT,f'-AegisAVProbeOutput={out}',
                f'-AegisPortfolioOutput={out/"system"}',f'-abslog={out/"engine.log"}']
    environment=os.environ.copy()
    if a.cache_root:
        cache=a.cache_root.resolve(); (cache/'Temp').mkdir(parents=True,exist_ok=True)
        environment.update(TEMP=str(cache/'Temp'),TMP=str(cache/'Temp'))
        environment['UE-LocalDataCachePath']=str(cache/'DerivedDataCache')
    wrappers=('run_v21_av_probe.py','run_unreal_functional.py','run_unreal_trial.py','run_decision_lab.py','run_portfolio_probe.py')
    record={'schemaVersion':1,'passed':False,'presentationVersion':'2.1','engine':'unreal-runtime',
            'command':command,'humanPlaytest':False,'soundEnabled':True,'hardwareLoopback':False,
            'launch':'editor-game' if a.editor else 'packaged-development',
            'audioProbeOverride':'UnfocusedVolumeMultiplier=1 permits offscreen audio; au.NeverDisableSubmixes=1 renders actual silent buffers during mute. Default runtime may mute unfocused windows and suspend silent submixes.',
            'startedAtUtc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
    started=time.monotonic()
    try:
        record['inputsBefore']=snapshot_inputs(ROOT); record['binariesBefore']=binary_snapshot(exe,a.editor)
        record['wrappersBefore']={n:sha(ROOT/'scripts'/n) for n in wrappers}
        with (out/'stdout.log').open('w',encoding='utf-8') as stream:
            result=subprocess.run(command,cwd=ROOT if a.editor else exe.parent,env=environment,
                                  stdout=stream,stderr=subprocess.STDOUT,timeout=180)
        record['exitCode']=result.returncode
        require(result.returncode==0, 'Native AV process failed: '+str(result.returncode))
        record.update(validate(out))
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError,wave.Error) as error:
        record['failure']=str(error)
    finally:
        try:
            record['inputsAfter']=snapshot_inputs(ROOT); record['binariesAfter']=binary_snapshot(exe,a.editor)
            record['wrappersAfter']={n:sha(ROOT/'scripts'/n) for n in wrappers}
            for name in ('inputs','binaries','wrappers'):
                if record.get(name+'Before') != record.get(name+'After'):
                    record['failure']=record.get('failure','')+' | '+name+' changed/unbound'
        except (OSError,ValueError) as error:
            record['failure']=record.get('failure','')+' | final provenance: '+str(error)
        record['passed']='failure' not in record and record.get('nativeAssertions')==15
        record['wallSeconds']=time.monotonic()-started
        record['files']={str(f.relative_to(out)):{'bytes':f.stat().st_size,'sha256':sha(f)}
                         for f in sorted(out.rglob('*')) if f.is_file() and f.name!='provenance.json'}
        (out/'provenance.json').write_text(json.dumps(record,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps({k:record[k] for k in ('passed','nativeAssertions','failure','wallSeconds') if k in record}))
    return 0 if record['passed'] else 2

if __name__ == '__main__': raise SystemExit(main())
