"""Offline timing, source-boundary, accounting and audio gates; no Unreal or encoder."""
from copy import deepcopy
import json
from pathlib import Path
import tempfile
import unittest
import wave

import numpy as np
import build_portfolio_video as video


def capture(interval=1/30):
    rows = [{'file':f'frame-{i:06d}.png','videoSeconds':(i+1)*interval,
             'worldSeconds':1+(i+1)*interval} for i in range(61)]
    return {'validRun':True,'scriptedPlayer':True,'fixtureDamage':False,'humanPlaytest':False,
            'outcome':'won','frames':61,'completedFrames':61,'frameTimes':rows,
            'samples':[{'videoSeconds':interval,'phase':1,'wave':1,'diagnostics':False},
                       {'videoSeconds':30*interval,'phase':2,'wave':1,'diagnostics':False},
                       {'videoSeconds':45*interval,'phase':1,'wave':2,'diagnostics':True}],
            'audioEvents':[{'asset':'S_Shot','videoSeconds':interval,'worldSeconds':1+interval,
                            'volume':.45,'location':[1,2,3]}]}


def ledger():
    return {'ledgerBalanced':True,'traceComplete':True,'completed':True,'outcome':'won',
            'energyInitial':60,'energyFinal':20,'energyEarned':135,'energySpent':175,
            'energyOverflow':0,'pulsesUsed':5,'repairsUsed':0,'enemiesRewarded':5,'stagesRewarded':3,
            'eventCount':30,'upgradeMask':6,'trialElapsedSeconds':90.5,'repairPlayerActualHealing':0,
            'repairCompanionActualHealing':0,'alliedActualDamageDealt':500,'playerActualDamageTaken':20,
            'companionActualDamageTaken':20}


class PortfolioVideoTests(unittest.TestCase):
    def test_native_fixed_frame_cadence(self):
        self.assertAlmostEqual(video.validate_capture(capture()),1/30)

    def test_sparse_is_never_final(self):
        value=capture(.5)
        with self.assertRaisesRegex(ValueError,'native 1/30'):
            video.validate_capture(value)
        self.assertAlmostEqual(video.validate_capture(value,True),.5)

    def test_dropped_irregular_frame_rejected(self):
        value=capture(); value['frameTimes'][10]['videoSeconds']+=.001
        with self.assertRaisesRegex(ValueError,'irregular'):
            video.validate_capture(value)

    def test_path_traversal_rejected(self):
        value=capture(); value['frameTimes'][0]['file']='../frame-000000.png'
        with self.assertRaisesRegex(ValueError,'canonical'):
            video.validate_capture(value)

    def test_paused_world_allowed_but_sped_world_rejected(self):
        value=capture()
        for row in value['frameTimes']: row['worldSeconds']=1
        video.validate_capture(value)
        value['frameTimes'][-1]['worldSeconds']=2
        with self.assertRaisesRegex(ValueError,'exceeds capture cadence'):
            video.validate_capture(value)

    def test_missing_audio_never_final(self):
        value=capture(); value['audioEvents']=[]
        with self.assertRaisesRegex(ValueError,'audioEvents'):
            video.validate_capture(value)
        video.validate_capture(value,True)

    def test_unsafe_or_nonfinite_sound_rejected(self):
        for change in ({'asset':'../../S_Shot'},{'volume':float('nan')},{'videoSeconds':9}):
            value=capture(); value['audioEvents'][0].update(change)
            with self.assertRaises(ValueError): video.validate_capture(value)

    def test_chapters_preserve_whole_source_and_actual_changes(self):
        value=capture(); result=video.chapters(value,1/30)
        self.assertEqual([x['key'] for x in result],['stage1','upgrade1','diagnostics'])
        self.assertEqual([x['firstSourceFrame'] for x in result],[0,29,44])
        self.assertAlmostEqual(sum(x['duration'] for x in result),61/30)

    def test_energy_accounting_cannot_use_flag_alone(self):
        source=ledger(); video.validate_ledger(source,capture())
        for field in ('energySpent','energyFinal','pulsesUsed','stagesRewarded','energyEarned'):
            changed=deepcopy(source); changed[field]+=1
            with self.assertRaises(ValueError): video.validate_ledger(changed,capture())

    def test_strict_json_rejects_duplicates_and_nan(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)/'data.json'
            for text in ('{"a":1,"a":2}','{"a":NaN}'):
                p.write_text(text)
                with self.assertRaises(ValueError): video.strict_json(p)

    def test_real_pcm_mixer_keeps_silence_before_event_and_peak_bound(self):
        with tempfile.TemporaryDirectory() as directory:
            folder=Path(directory); asset=folder/'S_Shot.wav'
            with wave.open(str(asset),'wb') as w:
                w.setnchannels(1); w.setsampwidth(2); w.setframerate(24000)
                w.writeframes(np.full(2400,30000,dtype='<i2').tobytes())
            event={'asset':'S_Shot','videoSeconds':.1,'worldSeconds':1.1,'volume':4,'location':[0,0,0]}
            mix=folder/'mix.wav'
            result=video.mix_audio([event]*3,folder,mix,6,.1)
            with wave.open(str(mix),'rb') as w:
                self.assertEqual((w.getnchannels(),w.getframerate()),(2,48000))
                pcm=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').reshape(-1,2)
            self.assertEqual(np.count_nonzero(pcm[:5*48000]),0)
            self.assertGreater(np.count_nonzero(pcm[5*48000:]),0)
            self.assertLessEqual(np.max(np.abs(pcm.astype(np.int32))),round(.82*32767))
            self.assertLess(result['normalizationGain'],1)
            self.assertEqual(result['eventCount'],3)


if __name__=='__main__': unittest.main()
