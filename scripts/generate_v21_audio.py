"""Original deterministic PRISM FALL v2.1 score and weapon synthesis.

No samples, recordings, third-party music or model weights are used. The score,
voicings, motif, drum patterns and oscillators are authored in this source. Run
outside Unreal (Python + numpy), then import with unreal/import_v21_audio.py.
"""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import wave
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets' / 'v21_source_audio'
RATE = 48000
BPM = 100
BEAT = 60 / BPM
TAU = np.pi * 2


def hz(midi):
    return 440 * 2 ** ((midi - 69) / 12)


def clock(duration):
    return np.arange(round(duration * RATE), dtype=np.float64) / RATE


def env(t, duration, attack=.008, release=.1):
    return np.minimum(t / max(attack, 1/RATE), 1) * np.minimum(np.maximum(duration-t, 0) / max(release, 1/RATE), 1)


def add(buf, sound, at, gain=1, pan=0, wrap=False):
    sound = np.asarray(sound)
    if sound.ndim == 1:
        angle = (pan + 1) * np.pi / 4
        sound = np.column_stack((sound * np.cos(angle), sound * np.sin(angle)))
    pos = round(at * RATE)
    if wrap:
        indices = (np.arange(len(sound)) + pos) % len(buf)
        np.add.at(buf, indices, sound * gain)
    elif pos < len(buf):
        start = max(-pos, 0)
        count = min(len(sound)-start, len(buf)-max(pos, 0))
        if count > 0:
            buf[max(pos, 0):max(pos, 0)+count] += sound[start:start+count] * gain


def pad(note, duration, bright=False):
    t = clock(duration)
    f = hz(note)
    # Stereo detuning and slow phase modulation: no stock synth preset.
    left = np.sin(TAU*f*t + .07*np.sin(TAU*.23*t))
    right = np.sin(TAU*f*1.0015*t + .07*np.sin(TAU*.29*t))
    left += .22*np.sin(TAU*f*2*t) + .09*np.sin(TAU*f*3*t)
    right += .22*np.sin(TAU*f*2.002*t) + .09*np.sin(TAU*f*3.001*t)
    if bright:
        left += .035*np.sin(TAU*f*5*t)
        right += .035*np.sin(TAU*f*5.005*t)
    return np.column_stack((left, right)) * env(t, duration, .23, .58)[:, None] * .16


def bell(note, duration=.72, color=1):
    t = clock(duration)
    f = hz(note)
    x = (np.sin(TAU*f*t + .8*np.sin(TAU*f*2*t)*np.exp(-t*8)) * np.exp(-t*3.8)
         + .25*np.sin(TAU*f*3.003*t)*np.exp(-t*8)
         + .055*color*np.sin(TAU*f*6.1*t)*np.exp(-t*14))
    return x * env(t, duration, .003, .13) * .38


def bass(note, duration, drive=1):
    t = clock(duration)
    f = hz(note)
    x = np.sin(TAU*f*t) + .22*np.sin(TAU*2*f*t) + .055*drive*np.sin(TAU*3*f*t)
    return x * env(t, duration, .011, .065) * .36


def kick():
    t = clock(.32)
    phase = TAU*(47*t + 5.8*(1-np.exp(-t*35)))
    return np.sin(phase)*np.exp(-t*14)*env(t, .32, .001, .04)*.72


def snare(rng):
    t = clock(.23)
    n = rng.normal(0, 1, len(t))
    high = n - np.convolve(n, np.ones(11)/11, mode='same')
    return (high*.14*np.exp(-t*24) + .26*np.sin(TAU*178*t)*np.exp(-t*26)) * env(t,.23,.001,.03)


def hat(rng, open_hat=False):
    duration = .21 if open_hat else .075
    t = clock(duration)
    n = rng.normal(0, 1, len(t))
    high = n - np.convolve(n, np.ones(5)/5, mode='same')
    return high*.065*np.exp(-t*(24 if open_hat else 62))*env(t,duration,.001,.008)


CHORDS = [(50, [62,65,69,76]), (46, [62,65,70,72]),
          (41, [60,65,69,72]), (48, [60,64,67,74])]
# A short original five-note contour, varied rhythmically across the progression.
MOTIF = [[74,77,76,69,72], [74,77,81,77,72], [72,69,77,76,72], [74,76,79,74,69]]


def music(name, intensity, bars=16, ending=None):
    loop = ending is None
    duration = bars*4*BEAT if loop else 8*BEAT + 2.0
    buf = np.zeros((round(duration*RATE), 2), dtype=np.float64)
    rng = np.random.default_rng(2110+intensity)
    if ending:
        notes = [50,57,62,65,69,74] if ending == 'victory' else [38,45,50,53,60]
        for j,note in enumerate(notes):
            add(buf,pad(note,5.8,True),j*.035,.62 if ending=='victory' else .5)
        phrase = [74,77,81,86] if ending=='victory' else [65,62,57,50]
        for j,note in enumerate(phrase):
            add(buf,bell(note,2.7),.1+j*.48, .72, (j-1.5)*.17)
        add(buf,kick(),0,.7 if ending=='victory' else .3)
    else:
        for bar in range(bars):
            root, voicing = CHORDS[bar % 4]
            at = bar*4*BEAT
            for j,note in enumerate(voicing):
                add(buf,pad(note,4*BEAT+.62,intensity>=2),at,.54,wrap=True)
            # Pulse-less lower density in briefings/upgrade; same key and tempo.
            if intensity == 0:
                if bar % 2 == 0:
                    add(buf,bass(root-12,3.7*BEAT),at,.32,wrap=True)
                for j in range(2):
                    add(buf,bell(MOTIF[bar%4][j*2],1.8),at+(1+j*2)*BEAT,.3,(-1 if j%2 else 1)*.35,True)
            elif intensity == 1:
                for j in range(4):
                    add(buf,bell(voicing[j]+12,1.1),at+j*BEAT,.28,(-1 if j%2 else 1)*.3,True)
                add(buf,bass(root-12,3.3*BEAT),at,.48,wrap=True)
            else:
                # Four syncopated bass events, leaving space around snare attacks.
                for beat,length,offset in [(0,.7,0),(1.5,.35,0),(2.5,.35,12),(3.25,.5,0)]:
                    add(buf,bass(root-12+offset,length*BEAT,intensity),at+beat*BEAT,.83,wrap=True)
                for b in ([0,2.5] if intensity == 2 else [0,1.75,2.5,3.5]):
                    add(buf,kick(),at+b*BEAT,.64 if intensity<4 else .79,wrap=True)
                for b in [1,3]:
                    add(buf,snare(rng),at+b*BEAT,.8,wrap=True)
                for step in range(8):
                    add(buf,hat(rng,step==7 and intensity>=3),at+step*.5*BEAT,
                        .38 if step%2==0 else .57,(-1 if step%2 else 1)*.26,True)
                for j in range(8):
                    note = voicing[(j+bar)%4] + (12 if (j+bar)%3 else 0)
                    add(buf,bell(note,.52,1.2),at+(j*.5+.25)*BEAT,
                        .13 if intensity==2 else .20, np.sin(j*1.7)*.48,True)
                if bar%2 == 0 or intensity >= 3:
                    phrase = MOTIF[bar%4]
                    for j,b in enumerate([.5,1.5,2.25,3,3.5]):
                        note = phrase[j] + (12 if intensity==4 and bar>=8 and j==2 else 0)
                        add(buf,bell(note,1.1),at+b*BEAT,.48 if intensity<4 else .55,(j-2)*.13,True)
                if intensity >= 3 and bar%4==3:
                    # Small end-of-phrase tom/fill, not a repeated generic loop.
                    for j in range(4):
                        add(buf,bass(43-j*2,.15),at+(3+j*.25)*BEAT,.4, (j-1.5)*.2,True)
        # Modest stereo feedback echoes. Circular convolution keeps seamless loops
        # and carries the preceding bar's tail across the loop boundary.
        dry = buf.copy()
        for delay,gain in [(.75*BEAT,.13),(1.5*BEAT,.06),(2.25*BEAT,.028)]:
            buf += np.roll(dry[:,::-1],round(delay*RATE),axis=0)*gain
    # Soft saturation and exact loop boundary bridge avoid clicks without a gap.
    buf = np.tanh(buf * 1.15)
    if loop:
        count = 192
        u = np.linspace(0,1,count)[:,None]
        bridge = buf[-count].copy()*(1-u)+buf[0].copy()*u
        buf[-count:] = bridge
    else:
        buf *= env(clock(duration),duration,.005,.7)[:,None]
    return buf, loop


def shot(role, variant):
    t = clock(.31 if role != 'Enemy' else .37)
    rng = np.random.default_rng(21000+{'Player':1,'Ally':2,'Enemy':3}[role]*100+variant)
    noise = rng.normal(0,1,len(t))
    snap = (noise - np.convolve(noise,np.ones(7)/7,mode='same'))*np.exp(-t*90)*.13
    fundamental = {'Player':138,'Ally':204,'Enemy':83}[role] * [1,.973,1.032][variant-1]
    body = np.sin(TAU*(fundamental*t + 6.0*(1-np.exp(-t*33))))*np.exp(-t*23)*.61
    metallic = np.sin(TAU*fundamental*5.013*t)*np.exp(-t*39)*.15
    crack = np.sin(TAU*(2100*t-1000*t*t))*np.exp(-t*72)*.16
    tail = np.sin(TAU*fundamental*.502*t)*np.exp(-t*15)*.09
    x=(body+snap+metallic+crack+tail)*env(t,t[-1],.0008,.025)
    return np.tanh(x*1.4)


def effect(name):
    duration={'S_CrystalImpact':.28,'S_CrystalBreak':.95,'S_PulseV21':.9,
              'S_RepairV21':1.2,'S_UpgradeV21':1.4,'S_ChargeV21':.68,'S_OverclockV21':1.2,
              'S_ChargeReady':.27}[name]
    t=clock(duration)
    rng=np.random.default_rng(21991+sum(map(ord,name)))
    n=rng.normal(0,1,len(t))
    x=np.zeros(len(t))
    if name=='S_CrystalImpact':
        x=.2*n*np.exp(-t*85)+sum(.18*np.sin(TAU*f*t)*np.exp(-t*d) for f,d in [(420,30),(1452,35),(2491,40)])
    elif name=='S_CrystalBreak':
        for j,f in enumerate([670,1049,1691,2437,3211,4127]):
            dt=np.maximum(0,t-j*.014)
            x += .16*np.sin(TAU*f*dt)*np.exp(-dt*(8+j*3))*(t>=j*.014)
        x += .27*np.sin(TAU*(110*t-35*t*t))*np.exp(-t*12)+.10*n*np.exp(-t*25)
    elif name=='S_ChargeReady':
        # One short rising confirmation at the real 0.7s threshold; not a shot.
        x=.22*np.sin(TAU*(hz(81)*t+180*t*t))*np.exp(-t*14)
        dt=np.maximum(t-.07,0)
        x += .30*np.sin(TAU*hz(86)*dt)*np.exp(-dt*18)*np.minimum(dt/.004,1)*(t>=.07)
    elif name=='S_ChargeV21':
        x = .55*np.sin(TAU*(158*t+5.7*(1-np.exp(-t*22))))*np.exp(-t*15)
        x += .18*n*np.exp(-t*60)+.16*np.sin(TAU*(1300*t+400*t*t))*np.exp(-t*10)
        for j,note in enumerate([74,81,86]):
            dt=np.maximum(0,t-j*.025)
            x += .10*np.sin(TAU*hz(note)*dt)*np.exp(-dt*12)*(t>=j*.025)
    elif name=='S_PulseV21':
        x=.46*np.sin(TAU*(63*t+2*(1-np.exp(-t*9))))*np.exp(-t*5)
        x += .16*np.sin(TAU*(900*t-280*t*t))*np.exp(-t*7)+.05*n*np.exp(-t*15)
    else:
        notes={'S_RepairV21':[62,69,74], 'S_UpgradeV21':[62,65,69,74], 'S_OverclockV21':[50,62,69,76]}[name]
        for j,note in enumerate(notes):
            delay=j*.11
            dt=np.maximum(0,t-delay)
            x += .23*np.sin(TAU*hz(note)*dt)*np.exp(-dt*4)*np.minimum(dt/.006,1)*(t>=delay)
            x += .05*np.sin(TAU*hz(note)*2.006*dt)*np.exp(-dt*8)*(t>=delay)
        if name=='S_OverclockV21':
            x += .3*np.sin(TAU*(58*t+2*(1-np.exp(-t*25))))*np.exp(-t*8)
    return np.tanh(x*1.4)*env(t,duration,.0015,.07)


def save(name, signal, loop=False, metadata=None):
    signal=np.asarray(signal)
    peak=float(np.max(np.abs(signal)))
    # Retain dynamic differences within a cue, set consistent headroom per source.
    signal=signal*(.78 if name.startswith('S_') else .70)/max(peak,1e-9)
    pcm=np.round(signal*32767).astype('<i2')
    path=OUT/(name+'.wav')
    with wave.open(str(path),'wb') as file:
        file.setnchannels(1 if pcm.ndim==1 else pcm.shape[1]); file.setsampwidth(2)
        file.setframerate(RATE); file.writeframes(pcm.tobytes())
    return {'asset':name,'source':str(path.relative_to(ROOT)).replace('\\','/'),
            'unrealPath':f'/Game/Aegis/V21/Audio/{name}.{name}',
            'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'loop':loop,
            'sampleRate':RATE,'channels':1 if pcm.ndim==1 else pcm.shape[1],
            'durationSeconds':len(pcm)/RATE,'peakDbfs':float(20*np.log10(np.max(np.abs(signal)))),
            'rmsDbfs':float(20*np.log10(np.sqrt(np.mean(signal**2)))),
            'clippedSamples':int(np.sum(np.abs(pcm.astype(np.int32))>=32767)),
            'loopBoundaryDelta':float(np.max(np.abs(signal[-1]-signal[0]))) if loop else None,
            **(metadata or {})}


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    entries=[]
    for name,intensity,bars,ending in [('M_Briefing',0,8,None),('M_Relay1',2,16,None),
        ('M_Relay2',3,16,None),('M_Extraction',4,16,None),('M_Upgrade',1,8,None),
        ('M_Victory',1,4,'victory'),('M_Defeat',0,4,'defeat')]:
        signal,loop=music(name,intensity,bars,ending)
        entries.append(save(name,signal,loop,{'bpm':BPM,'key':'D minor',
                      'layers':['original chord pad','original crystal motif','sub bass']+
                               (['kick','snare','hi hat','arpeggio','phrase variations'] if intensity>=2 else [])}))
    for role in ['Player','Ally','Enemy']:
        for variant in range(1,4):
            entries.append(save(f'S_{role}Shot{variant:02d}',shot(role,variant),metadata={'role':role,'variant':variant}))
    for name in ['S_CrystalImpact','S_CrystalBreak','S_PulseV21','S_RepairV21','S_UpgradeV21','S_ChargeV21','S_OverclockV21','S_ChargeReady']:
        entries.append(save(name,effect(name)))
    assert len(entries)==24 and all(e['clippedSamples']==0 for e in entries)
    manifest={'schema':1,'presentationVersion':'2.1','generator':'scripts/generate_v21_audio.py',
              'generatorSha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'origin':'Original procedural composition and oscillator/noise synthesis authored for Aegis Arena with AI coding assistance.',
              'thirdPartySamples':False,'thirdPartyCompositions':False,'trainedModel':False,
              'usage':'Project-owned source code and generated audio. No third-party attribution or sample license is required by this generator.',
              'mix':{'musicRuntimeGain':[.16,.48],'sfxMaximumVoices':16,'musicMaximumVoices':2,
                     'transitionSeconds':.65,'muteTransitionSeconds':.15,'pitch':1.0},
              'entries':entries}
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'passed':True,'assets':len(entries),'bytes':sum((ROOT/e['source']).stat().st_size for e in entries),
                      'manifest':str(OUT/'manifest.json')},ensure_ascii=True))


if __name__=='__main__': main()
