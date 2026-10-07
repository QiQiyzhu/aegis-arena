"""Synthetic mixer boundaries, separate from native audio evidence."""
import unittest
import numpy as np
from v21_audio import render_music,validate_music

def event(t,asset,slot,volume=1,fade=0,loop=True):
    return dict(videoSeconds=t,asset=asset,channel=slot,volume=volume,fadeSeconds=fade,loop=loop)
class MixTests(unittest.TestCase):
    def setUp(self): self.clips={'a':np.ones((10,2),np.float32),'b':np.ones((10,2),np.float32)*2,'c':np.ones((10,2),np.float32)*4}
    def test_loop_and_nonloop(self):
        x=render_music([event(0,'a',0,loop=False)],self.clips,30,rate=10)
        self.assertTrue(np.all(x[:10]==1));self.assertTrue(np.all(x[10:]==0))
        y=render_music([event(0,'a',0)],self.clips,30,rate=10)
        self.assertTrue(np.all(y==1))
    def test_crossfade(self):
        x=render_music([event(0,'a',0),event(1,'b',1,fade=1)],self.clips,30,rate=10)
        np.testing.assert_allclose(x[10:20,0],np.linspace(1,1.9,10),rtol=1e-6)
        self.assertTrue(np.all(x[20:]==2))
    def test_rapid_reuse_stops_replaced_slot(self):
        x=render_music([event(0,'a',0),event(.5,'b',1,fade=1),event(.7,'c',0,fade=1)],self.clips,30,rate=10)
        self.assertAlmostEqual(float(x[7,0]),.4,places=5)
        self.assertTrue(np.all(x[17:]==4))
    def test_mute_then_restore(self):
        x=render_music([event(0,'a',0),event(.5,'',-1,volume=0,fade=.2,loop=False),event(1,'b',1)],self.clips,20,rate=10)
        self.assertTrue(np.all(x[7:10]==0));self.assertTrue(np.all(x[10:]==2))
    def test_reject_unsafe_music_path(self):
        e=dict(sequence=1,state='briefing',asset='M_Briefing',assetPath='../escape.wav',videoSeconds=0,
               worldSeconds=0,volume=.3,fadeSeconds=.65,loop=True,reason='public_phase',channel=0)
        with self.assertRaises(ValueError): validate_music([e],10)

if __name__=='__main__':unittest.main()
