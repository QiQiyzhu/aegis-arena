"""Validate delivered PCM independently of synthesis. This is not a listening test."""
import argparse
import hashlib
import json
from pathlib import Path
import wave
import numpy as np

parser=argparse.ArgumentParser()
parser.add_argument('--output',required=True)
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
output=Path(args.output)
if output.exists(): raise SystemExit('Refusing to overwrite existing evidence')
manifest_path=root/'assets/v21_source_audio/manifest.json'
manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
rows=[]
errors=[]
for entry in manifest['entries']:
    path=root/entry['source']
    digest=hashlib.sha256(path.read_bytes()).hexdigest()
    if digest!=entry['sha256']: errors.append(entry['asset']+': hash mismatch')
    with wave.open(str(path),'rb') as stream:
        channels=stream.getnchannels(); width=stream.getsampwidth(); rate=stream.getframerate()
        count=stream.getnframes(); pcm=np.frombuffer(stream.readframes(count),dtype='<i2').reshape(-1,channels)
    samples=pcm.astype(np.float64)/32768
    clips=int(np.sum(np.abs(pcm.astype(np.int32))>=32767))
    peak=float(np.max(np.abs(samples))); rms=float(np.sqrt(np.mean(samples*samples)))
    duration=count/rate
    if (rate,width,channels)!=(48000,2,entry['channels']): errors.append(entry['asset']+': format mismatch')
    if abs(duration-entry['durationSeconds'])>1/rate: errors.append(entry['asset']+': duration mismatch')
    if clips or not(.015<rms<.5) or not(0<peak<.9): errors.append(entry['asset']+': invalid level')
    boundary=float(np.max(np.abs(samples[-1]-samples[0])))
    if entry['loop'] and boundary>2/32768: errors.append(entry['asset']+': loop discontinuity')
    # One second of signal is enough to show multiple frequency bands actually
    # exist, rather than trusting the manifest's list of authored instruments.
    mono=np.mean(samples,axis=1)
    segment=mono[:min(len(mono),48000)]
    power=np.abs(np.fft.rfft(segment*np.hanning(len(segment))))**2
    freq=np.fft.rfftfreq(len(segment),1/rate)
    total=max(float(np.sum(power)),1e-16)
    bands={f'{lo}-{hi}Hz':float(np.sum(power[(freq>=lo)&(freq<hi)])/total)
           for lo,hi in [(20,150),(150,500),(500,3000),(3000,16000)]}
    rows.append({'asset':entry['asset'],'sha256':digest,'sampleRate':rate,'channels':channels,
                 'durationSeconds':duration,'peakDbfs':20*np.log10(peak),
                 'rmsDbfs':20*np.log10(rms),'clippedSamples':clips,'loop':entry['loop'],
                 'loopBoundaryDelta':boundary if entry['loop'] else None,'firstSecondBandPowerFraction':bands})
music=[r for r in rows if r['asset'].startswith('M_')]
sfx=[r for r in rows if r['asset'].startswith('S_')]
if (len(rows),len(music),len(sfx),sum(r['loop'] for r in rows))!=(24,7,17,5): errors.append('Inventory mismatch')
report={'passed':not errors,'verificationType':'PCM format, hash, levels, and loop boundary; not perceptual listening or native playback',
        'sourceManifest':str(manifest_path),'sourceManifestSha256':hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
        'musicCount':len(music),'sfxCount':len(sfx),'errors':errors,'assets':rows}
output.parent.mkdir(parents=True,exist_ok=True)
output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'passed':not errors,'music':len(music),'sfx':len(sfx),'errors':errors,'output':str(output)},ensure_ascii=True))
raise SystemExit(0 if not errors else 1)
