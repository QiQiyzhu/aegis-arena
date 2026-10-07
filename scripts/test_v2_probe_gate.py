"""Offline adversarial validator tests; never launches Unreal."""
import copy
import json
from pathlib import Path
import tempfile
import unittest

from run_v2_probe import (ASSERTIONS,NOT_COVERED,BEGIN,PASS,REASON,validate_report,validate_sessions,build_command)


def report_fixture():
    report=dict(schemaVersion=1,passed=True,engine='unreal-runtime',syntheticKeyboardInput=True,fixtureDamage=True,
        fixturePlacement=True,aiFrozen=True,humanPlaytest=False,policyPerformanceTest=False,renderOffscreen=True,nullRHI=False,
        reason=REASON,quitPath='normal PlayerController X from paused menu',notCovered=NOT_COVERED.copy(),
        frozenControllerCount=16,wallSeconds=12,lastStage=35)
    rows=[]
    for index,name in enumerate(ASSERTIONS):
        rows.append(dict(name=name,passed=True,wallSeconds=index*.2,gameSeconds=index*.15,
                         energy=60,playerHealth=140,charging=False,actualPlayerShots=0,chargedShots=0,repairs=0,overclocks=0))
    changes={
        'small_repair_spends_8_heals_10':(52,140,0,1,0),
        'large_repair_actual_30_20':(20,125,0,1,0),
        'charged_shot_hits_two_for_52':(8,125,1,1,0),
        'insufficient_energy_full_charge_release_is_free':(8,125,1,1,0),
        'relay_overclock_spends_35':(25,140,0,0,1),
        'clear_ally_repair_charges_16_heals_20':(44,140,0,1,0),
    }
    for row in rows:
        if row['name'] in changes: row.update(zip(('energy','playerHealth','chargedShots','repairs','overclocks'),changes[row['name']]))
        if row['name']=='charged_shot_hits_two_for_52': row['actualPlayerShots']=1
    report['assertions']=rows
    report['interventions']=[dict(type='fixture_damage',actor='Actor',source='Hostile',requested=x,applied=x) for x in (10,45,35,20)]
    report['interventions'] += [dict(type='fixture_placement',actor='Actor',location=[-1200,-900,100]) for _ in range(9)]
    report['interventions'] += [dict(type='spawn_opaque_cover',actor='Cover')]
    log='\n'.join('LogTemp: Display: '+x for x in [BEGIN,*('AEGIS_V2_PROBE_ASSERT_PASS '+x for x in ASSERTIONS),PASS])
    return report,log+'\nLogExit: Exiting.'


def session_fixture(output):
    system=Path(output)/'system'; system.mkdir()
    plans=[dict(tx=[('repair',8)],player=10,ally=0,heal_self=10,heal_ally=0,charged=0,overclock=0,dealt=0),
           dict(tx=[('repair',40),('charged_shot',12)],player=45,ally=35,heal_self=30,heal_ally=20,charged=1,overclock=0,dealt=104),
           dict(tx=[('overclock',35)],player=0,ally=0,heal_self=0,heal_ally=0,charged=0,overclock=1,dealt=0),
           dict(tx=[('repair',16)],player=0,ally=20,heal_self=0,heal_ally=20,charged=0,overclock=0,dealt=0)]
    paths=[]
    for index,plan in enumerate(plans):
        run_id=f'{index+1:032X}'; trace=system/f'portfolio-{run_id}.jsonl'; paths.append(str(trace))
        events=[]; balance=60
        def append(kind,data):
            events.append(dict(runId=run_id,sequence=len(events)+1,event=kind,gameSeconds=len(events)*.1,energy=balance,data=data))
        append('session_started',{})
        for role,amount in (('AegisPlayerCharacter_0',plan['player']),('AegisAICharacter_0',plan['ally'])):
            if amount: append('damage',dict(actualDamage=amount,enemyVictim=False,victim=role))
        for reason,cost in plan['tx']:
            before=balance; balance-=cost
            append('energy_transaction',dict(reason=reason,before=before,after=balance,actualDelta=-cost,offeredDelta=-cost))
            if reason=='repair': append('repair_applied',dict(playerActualHealing=plan['heal_self'],companionActualHealing=plan['heal_ally']))
        if plan['dealt']:
            append('damage',dict(actualDamage=52,enemyVictim=True,victim='AegisAICharacter_2'))
            append('damage',dict(actualDamage=52,enemyVictim=True,victim='AegisAICharacter_3'))
        summary=dict(mode='aegis-prism-fall-v2.0',completed=False,ledgerBalanced=True,traceComplete=True,
            outcome='aborted' if index==3 else 'restarted',runId=run_id,tracePath=str(trace),eventCount=len(events)+1,
            energyInitial=60,energyFinal=balance,energyEarned=0,energySpent=60-balance,energyOverflow=0,pulsesUsed=0,
            enemiesRewarded=0,stagesRewarded=0,repairsUsed=1 if plan['heal_self']+plan['heal_ally'] else 0,
            repairPlayerActualHealing=plan['heal_self'],repairCompanionActualHealing=plan['heal_ally'],
            playerActualDamageTaken=plan['player'],companionActualDamageTaken=plan['ally'],chargedShots=plan['charged'],
            overclocks=plan['overclock'],alliedActualDamageDealt=plan['dealt'])
        append('session_finished',copy.deepcopy(summary))
        trace.write_text('\n'.join(json.dumps(event) for event in events)+'\n',encoding='utf-8')
        trace.with_suffix('.json').write_text(json.dumps(summary),encoding='utf-8')
    return dict(portfolioTracePaths=paths)


class V2ProbeGateTests(unittest.TestCase):
    def test_exact_declared_native_contract_passes(self): validate_report(*report_fixture())

    def test_missing_reordered_failed_or_duplicate_assertion_rejected(self):
        for mode in ('missing','reordered','failed','duplicate'):
            report,log=report_fixture()
            if mode=='missing': report['assertions'].pop()
            elif mode=='reordered': report['assertions'].reverse()
            elif mode=='failed': report['assertions'][3]['passed']=False
            else: report['assertions'][3]=copy.deepcopy(report['assertions'][2])
            with self.subTest(mode=mode),self.assertRaises(ValueError): validate_report(report,log)

    def test_fixture_flags_and_limitations_cannot_be_omitted_or_truthy(self):
        for key in ('fixtureDamage','fixturePlacement','aiFrozen','humanPlaytest','policyPerformanceTest','passed'):
            report,log=report_fixture(); report[key]=int(report[key])
            with self.subTest(key=key),self.assertRaises(ValueError): validate_report(report,log)
        report,log=report_fixture(); report['notCovered']=[]
        with self.assertRaises(ValueError): validate_report(report,log)

    def test_numeric_success_contradictions_rejected(self):
        for key,value in (('energy',99),('chargedShots',99),('actualPlayerShots',0)):
            report,log=report_fixture(); report['assertions'][21][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError): validate_report(report,log)

    def test_invalid_numeric_types_nan_and_regressed_clocks_rejected(self):
        for value in (True,float('nan'),float('inf'),-1):
            report,log=report_fixture(); report['assertions'][10]['energy']=value
            with self.subTest(value=value),self.assertRaises(ValueError): validate_report(report,log)
        report,log=report_fixture(); report['assertions'][-1]['gameSeconds']=0
        with self.assertRaises(ValueError): validate_report(report,log)

    def test_all_fixture_wounds_and_placements_required(self):
        report,log=report_fixture(); report['interventions'][0]['applied']=0
        with self.assertRaises(ValueError): validate_report(report,log)
        report,log=report_fixture(); report['interventions'].pop()
        with self.assertRaises(ValueError): validate_report(report,log)

    def test_direct_unique_markers_and_exit_required(self):
        report,log=report_fixture()
        for bad in (log.replace('LogTemp: Display:','LogAutomationController: Display:'),log+'\nLogTemp: Display: '+PASS,
                    log.replace('LogExit: Exiting.',''),log.replace(PASS,'missing')):
            with self.subTest(log=bad[-60:]),self.assertRaises(ValueError): validate_report(report,bad)

    def test_engine_warning_error_or_ensure_rejected(self):
        report,log=report_fixture()
        for extra in ('Handled ensure','LogTemp: Warning: unexpected','LogTemp: Error: failure'):
            with self.subTest(extra=extra),self.assertRaises(ValueError): validate_report(report,log+'\n'+extra)

    def test_four_native_sessions_reconcile(self):
        with tempfile.TemporaryDirectory() as directory:
            report=session_fixture(directory)
            self.assertEqual(len(validate_sessions(report,directory)),4)

    def test_corrupt_transaction_or_actual_damage_rejected(self):
        for kind in ('energy_transaction','damage','repair_applied'):
            with tempfile.TemporaryDirectory() as directory:
                report=session_fixture(directory); path=Path(report['portfolioTracePaths'][0])
                rows=[json.loads(line) for line in path.read_text().splitlines()]
                row=next(x for x in rows if x['event']==kind)
                field={'energy_transaction':'actualDelta','damage':'actualDamage','repair_applied':'playerActualHealing'}[kind]
                row['data'][field]=0
                path.write_text('\n'.join(json.dumps(x) for x in rows)+'\n')
                with self.subTest(kind=kind),self.assertRaises(ValueError): validate_sessions(report,directory)

    def test_missing_duplicate_outside_and_extra_sessions_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            report=session_fixture(directory)
            for paths in (report['portfolioTracePaths'][:-1],[report['portfolioTracePaths'][0]]*4):
                with self.assertRaises(ValueError): validate_sessions(dict(portfolioTracePaths=paths),directory)
            (Path(directory)/'system'/'portfolio-unreported.jsonl').write_text('{}')
            with self.assertRaises(ValueError): validate_sessions(report,directory)

    def test_command_discloses_only_new_probe_and_renderer(self):
        command=build_command('C:/engine/UnrealEditor-Cmd.exe','C:/reports/v2-new',True)
        for token in ('-game','-AegisV2','-AegisV2Probe','-AegisInputProbe','-RenderOffscreen'): self.assertIn(token,command)
        self.assertNotIn('-AegisPortfolioProbe',command)
        self.assertNotIn('-AegisGuardLegacy',command)


if __name__=='__main__': unittest.main()
