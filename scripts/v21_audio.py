"""Reconstruct issued Unreal music/SFX commands from original PCM; never claim loopback audio."""
import hashlib
import json
import math
from pathlib import Path
import re
import wave
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
RATE = 48000
STATES = {'briefing','relay1','relay2','extraction','upgrade','victory','defeat','paused','muted','restart','stopped'}
def require(ok, message):
    if not ok: raise ValueError(message)

def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def finite(value):
    return isinstance(value,(int,float)) and not isinstance(value,bool) and math.isfinite(value)

def validate_music(events, maximum):
    require(isinstance(events,list) and events, 'Missing native music commands')
    previous=-1
    for i,event in enumerate(events,1):
        t=event.get('videoSeconds')
        require(finite(t) and previous <= t <= maximum+.1 and t >= 0, 'Invalid music clock')
        previous=t
        require(event.get('sequence')==i and event.get('state') in STATES, 'Invalid music sequence/state')
        require(finite(event.get('worldSeconds')) and event['worldSeconds'] >= 0, 'Invalid music world clock')
        require(finite(event.get('volume')) and 0 <= event['volume'] <= .5, 'Invalid music gain')
        require(finite(event.get('fadeSeconds')) and 0 <= event['fadeSeconds'] <= 1, 'Invalid music fade')
        require(type(event.get('loop')) is bool and isinstance(event.get('reason'),str), 'Invalid music command scope')
        asset=event.get('asset')
        if asset:
            require(re.fullmatch(r'M_[A-Za-z0-9_]+',asset) and event.get('channel') in (0,1), 'Invalid music slot/name')
            require(event.get('assetPath')==f'/Game/Aegis/V21/Audio/{asset}.{asset}', 'Unbound music asset path')
        else:
            require(asset=='' and event.get('channel')==-1 and event['volume']==0 and event['loop'] is False,
                    'Invalid stop/mute command')

def read_sources(names):
    manifest_path=ROOT/'assets/v21_source_audio/manifest.json'
    manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
    entries={e['asset']:e for e in manifest['entries']}
    clips,records={},{}
    for name in sorted(names):
        require(name in entries,'Audio asset missing from original synthesis manifest: '+name)
        entry=entries[name]
        path=(ROOT/entry['source']).resolve()
        require(path.parent==(ROOT/'assets/v21_source_audio').resolve(),'Audio source outside expected directory')
        require(sha(path)==entry['sha256'],'Audio source changed: '+name)
        with wave.open(str(path),'rb') as w:
            require(w.getsampwidth()==2 and w.getframerate()==RATE and w.getnchannels() in (1,2),'Unexpected source format')
            channels=w.getnchannels()
            raw=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').reshape(-1,channels).astype(np.float32)/32768
        if channels==1: raw=np.repeat(raw,2,axis=1)*np.float32(.70710678)
        clips[name]=raw
        records[name]={'path':str(path),'bytes':path.stat().st_size,'sha256':entry['sha256'],
                       'sampleRate':RATE,'sourceChannels':channels,'loop':entry['loop']}
    return clips,records,{'path':str(manifest_path),'sha256':sha(manifest_path)}

def render_music(events, clips, samples, rate=RATE, origin=0.0, intro=0.0):
    """Two channel reuse and linear envelopes; timestamps are issued commands, not PCM measurements."""
    result=np.zeros((samples,2),dtype=np.float32)
    channels=[None,None]
    def gain(channel, positions):
        if channel['fade'] == 0:
            return np.full_like(positions,channel['target'],dtype=np.float64)
        alpha=np.clip((positions-channel['at'])/channel['fade'],0,1)
        return channel['from']+(channel['target']-channel['from'])*alpha
    def draw(begin,end):
        if end<=begin: return
        positions=np.arange(begin,end,dtype=np.int64)
        for channel in channels:
            if channel is None: continue
            clip=clips[channel['asset']]
            phase=positions-channel['start']
            valid=(phase>=0) & (channel['loop'] | (phase<len(clip)))
            if not np.any(valid): continue
            amplitude=gain(channel,positions)
            result[begin:end][valid] += clip[phase[valid]%len(clip)]*amplitude[valid,None]
    cursor=0
    for event in events:
        at=max(0,min(samples,round((intro+max(0,event['videoSeconds']-origin))*rate)))
        require(at>=cursor,'Music clock regressed after quantization')
        draw(cursor,at); cursor=at
        for channel in channels:
            if channel is None: continue
            current=float(gain(channel,np.array([at]))[0])
            channel.update({'from':current,'target':0.,'at':at,'fade':round(event['fadeSeconds']*rate)})
        if event['asset']:
            slot=event['channel']
            channels[slot]={'asset':event['asset'],'start':at,'at':at,'from':0.,'target':event['volume'],
                            'fade':round(event['fadeSeconds']*rate),'loop':event['loop']}
    draw(cursor,samples)
    return result

def mix_audio(events, music_events, destination, duration, capture_start, native_seconds, intro=5):
    validate_music(music_events,capture_start+native_seconds)
    names={e['asset'] for e in events}|{e['asset'] for e in music_events if e['asset']}
    clips,assets,manifest=read_sources(names)
    for e in music_events:
        if e['asset']: require(e['loop']==assets[e['asset']]['loop'],'Loop flag differs from imported source')
    samples=round(duration*RATE)
    sfx=np.zeros((samples,2),dtype=np.float32)
    for event in events:
        name=event['asset']
        require(re.fullmatch(r'S_[A-Za-z0-9_]+',name) is not None,'Invalid SFX name')
        require(event.get('assetPath')==f'/Game/Aegis/V21/Audio/{name}.{name}','Unbound SFX asset path')
        require(event.get('pitch')==1,'Only actual unit-pitch commands are supported')
        require(finite(event.get('volume')) and 0<=event['volume']<=4,'Invalid SFX volume')
        start=round((intro+max(0,event['videoSeconds']-capture_start))*RATE)
        clip=clips[name]; count=min(len(clip),samples-start)
        require(count>0,'Sound outside film')
        sfx[start:start+count]+=clip[:count]*np.float32(event['volume'])
    music=render_music(music_events,clips,samples,origin=capture_start,intro=intro)
    combined=sfx+music
    # Native audio is bounded by the captured game window. Editorial cards add
    # no invented engine music. A 20ms editorial edge fade avoids a cut click.
    end=min(samples,round((intro+native_seconds)*RATE)); edge=min(round(.02*RATE),end)
    combined[end:]=0
    combined[end-edge:end]*=np.linspace(1,0,edge,dtype=np.float32)[:,None]
    require(np.isfinite(combined).all(),'Nonfinite mix')
    peak=float(np.max(np.abs(combined))); normalization=min(1.,.90/peak) if peak else 1.
    final=combined*normalization
    with wave.open(str(destination),'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(RATE)
        w.writeframes(np.rint(final*32767).astype('<i2').tobytes())
    return {'eventCount':len(events),'musicEventCount':len(music_events),'assets':assets,'sourceManifest':manifest,
            'sourceGain':1,'musicChannelLimit':2,'musicStates':list(dict.fromkeys(e['state'] for e in music_events)),
            'peakBeforeNormalization':peak,'normalizationGain':normalization,'peakAfterNormalization':float(np.max(np.abs(final))),
            'rms':float(np.sqrt(np.mean(final.astype(np.float64)**2))),'sampleRate':RATE,'channels':2,
            'captureBoundaryFadeSeconds':.02,'sha256':sha(destination),
            'method':'Real engine-issued SFX/music commands reconstructed from original PCM with channel reuse and fades; not loopback recording',
            'stereoMethod':'Authored source stereo; no invented spatial attenuation',
            'timingLimit':'Command/frame-clock reconstruction with continuous offline ramps; not bit-identical to hardware-clock audio rendering'}
