"""Read-only v2.1 audiovisual release gate; historical AI evidence stays historical."""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import json
from review_v2_release import (Audit, bound_file, child, exact, file_record, require,
                               review_video, visual_review, read_json, sha)
import run_v2_probe as control_gate
import run_decision_lab_input as lab_gate
import run_v21_av_probe as av_gate
from v21_audio import validate_music

ROOT = Path(__file__).resolve().parents[1]

def vfx(capture):
    previous = None
    rows = capture['samples']
    require(isinstance(rows,list) and rows, 'Missing VFX samples')
    fields = {'shots','chargedShots','renderedShots','traceImpacts','damageImpacts','worldImpacts',
              'blockedCharacterImpacts','fatalImpacts','deathsObserved','renderedDeathBursts',
              'poolComponents','peakActiveComponents','reusedActiveSlots'}
    for row in rows:
        fx = row.get('playerShotVfx')
        require(isinstance(fx, dict), 'Missing native player VFX snapshot')
        require(set(fx)==fields, 'Incomplete VFX snapshot')
        for key, value in fx.items():
            require(type(value) is int and value >= 0, 'Invalid VFX counter: '+key)
        require(fx['shots'] == row['shots'] == fx['renderedShots'], 'Actual shots differ from rendered shots')
        require(fx['chargedShots'] <= fx['shots'], 'Invalid charged-shot count')
        require(fx['traceImpacts'] == sum(fx[k] for k in ('damageImpacts','worldImpacts','blockedCharacterImpacts')),
                'Impact classes do not partition actual traces')
        require(fx['fatalImpacts'] <= fx['damageImpacts'], 'Fatal impacts exceed actual damage impacts')
        require(fx['renderedDeathBursts'] <= fx['deathsObserved'], 'Death bursts exceed observed actor deaths')
        require(0 <= fx['peakActiveComponents'] <= fx['poolComponents'] == 27, 'VFX component budget differs from 24 particles + 3 portfolio tracers')
        if previous:
            require(all(fx[k] >= previous[k] for k in fx), 'VFX counter regressed')
        previous = fx
    require(previous['shots'] > 10 and previous['chargedShots'] > 0 and previous['damageImpacts'] > 0,
            'Demonstration did not exercise ranged, charged and impact effects')
    dash_after_shots = [r for r in rows if r.get('shots',0)>=4 and r.get('speed',0)>900]
    require(dash_after_shots, 'Missing high-speed dash after repeated shots regression')
    return {'lastPlayerSnapshot': previous, 'samplesChecked': len(rows),
            'dashAfterRepeatedShotsSamples':len(dash_after_shots),
            'scope':'Actual player shot/impact counters and per-player pool; visual quality reviewed separately. Not GPU performance.'}

def document(path, qa_path, source):
    path = Path(path).resolve(); doc = read_json(path)
    exact(doc.get('schemaVersion'), 2, 'Unexpected document build schema')
    exact(doc.get('pageCount'), 11, 'Expected eleven document pages')
    require(doc.get('sourceSha256') == source, 'Document runtime differs')
    require(doc.get('inputHashes'), 'Document input hashes absent')
    for name, digest in doc['inputHashes'].items(): bound_file(name, digest)
    for name in ('source','manifest'): bound_file(doc[name]['path'], doc[name])
    manifest = read_json(doc['manifest']['path'])
    exact(manifest.get('reviewed'), True, 'Document manifest unreviewed')
    require(manifest.get('sourceSha256') == source and manifest.get('schemaVersion') == 3,
            'Wrong v2.1 document manifest')
    outputs = doc.get('outputs', [])
    require(len(outputs)==2 and {Path(x['path']).suffix for x in outputs}=={'.pdf','.docx'}, 'Missing PDF/DOCX')
    files = [bound_file(x['path'],x) for x in outputs]
    pages = doc.get('pageImages', [])
    require(len(pages)==len(set(pages))==11, 'Incomplete rendered pages')
    qa=visual_review(qa_path, [(Path(p),sha(p)) for p in pages])
    require(len(doc.get('nativeImages',[]))==7, 'Missing seven native illustrations')
    for row in doc.get('nativeImages',[])+doc.get('evidenceFiles',[])+doc.get('designDiagrams',[]):
        bound_file(row['path'],row)
    return {'record':file_record(path),'pageCount':11,'outputs':files,'visualReview':qa}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('control','av','lab','capture','video','video-qa','document','document-qa','package-exe','history','output'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--expected-source',required=True);p.add_argument('--expected-content',required=True)
    a=p.parse_args(); require(not a.output.exists(),'Use a fresh acceptance path')
    audit=Audit(a.expected_source,a.expected_content,a.package_exe)
    control=audit.native(a.control,36)
    c=read_json(a.control/'v2-probe.json')
    control_gate.validate_report(c,(a.control/'engine.log').read_text(encoding='utf-8-sig'))
    control_gate.validate_sessions(c,a.control)
    av=audit.native(a.av,15); validated_audio=av_gate.validate(a.av)
    require(av.get('masterSubmixWavs')==validated_audio['masterSubmixWavs'],'Audio PCM evidence changed')
    lab=audit.native(a.lab,35,lab=True)
    lab_gate.validate(read_json(a.lab/'lab-input.json'),(a.lab/'engine.log').read_text(encoding='utf-8-sig'),a.lab)
    full, proof, captured=audit.capture(a.capture)
    exact(captured.get('presentationVersion'),'2.1','Capture is not v2.1')
    require(full['seed']==1101 and full['resolution']==[1920,1080] and full['outcome'] in ('won','lost'),
            'Expected complete native development demonstration')
    fx=vfx(captured)
    video=review_video(a.video,a.capture,captured,a.video_qa)
    raw=read_json(a.video)
    music=captured.get('musicEvents',[])
    validate_music(music,captured['frameTimes'][-1]['videoSeconds']+1/30)
    bound_file(a.video.parent/'music-events.json',raw.get('musicEventsSha256'))
    require(read_json(a.video.parent/'music-events.json')==music,'Music commands changed')
    bound_file(raw['mixerScript']['path'],raw['mixerScript'])
    bound_file(raw['script']['path'],raw['script'])
    require(raw['audio']['musicEventCount']==len(music),'Wrong music event count')
    require(raw['audio']['musicStates']==list(dict.fromkeys(e['state'] for e in music)), 'Music summary differs from actual commands')
    bound_file(raw['audio']['sourceManifest']['path'],raw['audio']['sourceManifest'])
    expected_music={'briefing', 'victory' if full['outcome']=='won' else 'defeat'}
    for sample in captured['samples']:
        if sample['wave'] in (1,2,3):
            expected_music.add({1:'relay1',2:'relay2',3:'extraction'}[sample['wave']])
        if sample['phase']==2: expected_music.add('upgrade')
    require(expected_music <= set(raw['audio']['musicStates']),
            'Music does not cover the phases actually entered and the natural result')
    doc=document(a.document,a.document_qa,a.expected_source)
    history=read_json(a.history)
    exact(history.get('deliveryReady'),True,'Historical AI release did not pass')
    # Read-only historical binding: never reinterpret old source as this release.
    historical={'acceptance':file_record(a.history),'scope':'Preserved v2.0 Guard case and paired outcomes; no v2.1 AI reevaluation or training claim'}
    failures=[]
    for path in sorted(a.capture.parent.glob('*/provenance.json')):
        failed=read_json(path)
        if failed.get('passed') is False:
            files=[file_record(f) for f in sorted(path.parent.rglob('*')) if f.is_file() and f.suffix in ('.json','.jsonl','.log')]
            failures.append({'path':str(path.parent),'failure':failed.get('failure'),'files':files})
    require(any(Path(r['path']).name=='capture-01' for r in failures), 'Original VFX crash record missing')
    regressions=[]
    for folder in sorted(a.capture.parent.glob('regression-*-*')):
        proof_path=folder/'provenance.json'
        if not proof_path.is_file(): continue
        regression=read_json(proof_path)
        require(regression.get('passed') is True, 'Regression wrapper did not pass')
        ledger_files=list((folder/'system').glob('*.jsonl'))
        require(len(ledger_files)==1, 'Regression ledger missing')
        ledger=[__import__('json').loads(line) for line in ledger_files[0].read_text(encoding='utf-8-sig').splitlines() if line.strip()]
        endings=[row['data'] for row in ledger if row.get('event')=='session_finished']
        require(len(endings)==1 and endings[0].get('outcome') in ('won','lost'), 'Regression natural result missing')
        regressions.append({'directory':str(folder), 'technicalPassed':True, 'result':endings[0],
                            'files':[file_record(f) for f in sorted(folder.rglob('*')) if f.is_file() and f.suffix in ('.json','.jsonl','.log')],
                            'scope':'Separate no-frame regression execution; not a paired AI comparison or the final video run'})
    require(any(r['result']['outcome']=='lost' for r in regressions), 'Preserved natural regression loss missing')
    result={'schemaVersion':1,'presentationVersion':'2.1','deliveryReady':True,
            'reviewedAtUtc':datetime.now(timezone.utc).isoformat(),'inputs':audit.inputs,'package':audit.package,
            'nativeControl':file_record(a.control/'provenance.json'),'nativeAudio':validated_audio,
            'nativeAudioProvenance':file_record(a.av/'provenance.json'),'nativeLab':file_record(a.lab/'provenance.json'),
            'capture':full,'vfx':fx,'video':video,'document':doc,'historicalAI':historical,
            'preservedDevelopmentFailures':failures,
            'preservedCompletedRegressions':regressions,
            'training':False,'humanPlaytest':False,'performanceBenchmark':False,
            'musicScope':'Native master-submix checks cover briefing/mute/restore, lifecycle covers relay1/pause/restart; video reconstructs issued music/SFX commands.'}
    a.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'deliveryReady':True,'output':str(a.output)},ensure_ascii=True))

if __name__=='__main__':main()
