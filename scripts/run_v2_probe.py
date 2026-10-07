"""Strict Development v2 control fixture: frozen AI and disclosed wounds/placements, never natural play."""
import argparse
import datetime
import json
import os
from pathlib import Path
import re
import subprocess
import time

from run_unreal_functional import snapshot_inputs
from run_unreal_trial import OFFLINE_ARGUMENT
from run_decision_lab import sha, read_json, strict_json, validate_engine_messages
from run_portfolio_probe import binary_snapshot, number, required_fields

ROOT = Path(__file__).resolve().parents[1]
ASSERTIONS = (
    'begun_game_world_v2','briefing_defaults_route_energy','briefing_actions_have_no_cost',
    'v_toggles_route_in_briefing','enter_deploys_frozen_active_trial','active_v_cannot_change_route',
    'full_health_repair_has_no_cost','partial_charge_starts_without_spending','partial_charge_release_is_free',
    'charging_blocks_primary_fire','pause_cancels_charge_without_cost','paused_actions_preserve_time_energy',
    'small_repair_quote_is_8','small_repair_spends_8_heals_10','small_repair_cooldown_rejects_repeat',
    'restart_resets_route_energy_charge','large_repair_quote_is_40','large_repair_spends_40',
    'large_repair_actual_30_20','full_charge_reaches_ready_without_spending','charged_shot_spends_12',
    'charged_shot_hits_two_for_52','insufficient_energy_full_charge_release_is_free',
    'second_restart_resets_all_v2_state','outside_relay_overclock_rejected','relay_overclock_spends_35',
    'overclock_is_2x_and_used','overclock_repeat_has_no_cost','contested_relay_consumes_duration_without_progress',
    'overclock_pause_freezes_timer','overclock_restart_clears_used_route_and_energy',
    'out_of_range_ally_repair_rejected','covered_ally_repair_rejected','clear_ally_repair_quote_is_16',
    'clear_ally_repair_charges_16_heals_20','normal_menu_quit_available',
)
NOT_COVERED = ['real window focus loss','charge cancellation on natural stage transition',
               'upgrade penetration of three targets','V on first upgrade page','F rejection during extraction',
               'F expiration and next-relay eligibility','symbiosis healing']
BEGIN = 'AEGIS_V2_PROBE_BEGIN syntheticKeyboard=1 fixtureDamage=1 fixturePlacement=1 aiFrozen=1'
PASS = 'AEGIS_V2_PROBE_PASS checks=36'
REASON = 'Completed v2 control fixture; normal menu X requests quit'
DIRECT = re.compile(r'^(?:\[[^\]\r\n]*\])*LogTemp: Display: (AEGIS_V2_PROBE_[^\r\n]+)$')
EXIT = re.compile(r'^(?:\[[^\]\r\n]*\])*LogExit: Exiting\.$')


def validate_report(report, log):
    required_fields(report, dict(schemaVersion=1,passed=True,engine='unreal-runtime',syntheticKeyboardInput=True,
        fixtureDamage=True,fixturePlacement=True,aiFrozen=True,humanPlaytest=False,policyPerformanceTest=False,
        renderOffscreen=True,nullRHI=False,reason=REASON,quitPath='normal PlayerController X from paused menu'))
    if report.get('notCovered') != NOT_COVERED: raise ValueError('Coverage limitations must remain explicit')
    number(report.get('frozenControllerCount'),2,1000,True)
    wall = number(report.get('wallSeconds'),0.1,45)
    if number(report.get('lastStage'),integer=True) != 35: raise ValueError('Wrong final native stage')
    rows=report.get('assertions')
    if (not isinstance(rows,list) or len(rows)!=36 or
            any(not isinstance(row,dict) or row.get('passed') is not True for row in rows) or
            tuple(row.get('name') for row in rows)!=ASSERTIONS):
        raise ValueError('All 36 distinct native assertions must pass in exact order')
    prev_wall=prev_game=-1
    for row in rows:
        w,g=number(row.get('wallSeconds'),0,wall),number(row.get('gameSeconds'))
        if w<prev_wall or g<prev_game: raise ValueError('Assertion clock regressed')
        prev_wall,prev_game=w,g
        number(row.get('energy'),0,100,True); number(row.get('playerHealth'),0,140)
        if type(row.get('charging')) is not bool: raise ValueError('Charging must be a native bool')
        for key in ('actualPlayerShots','chargedShots','repairs','overclocks'): number(row.get(key),0,100,True)
    snapshots={row['name']:row for row in rows}
    for name, expected in {
        'briefing_defaults_route_energy':(60,140,0,0,0),
        'small_repair_spends_8_heals_10':(52,140,0,1,0),
        'restart_resets_route_energy_charge':(60,140,0,0,0),
        'large_repair_actual_30_20':(20,125,0,1,0),
        'charged_shot_hits_two_for_52':(8,125,1,1,0),
        'insufficient_energy_full_charge_release_is_free':(8,125,1,1,0),
        'second_restart_resets_all_v2_state':(60,140,0,0,0),
        'relay_overclock_spends_35':(25,140,0,0,1),
        'overclock_restart_clears_used_route_and_energy':(60,140,0,0,0),
        'clear_ally_repair_charges_16_heals_20':(44,140,0,1,0),
    }.items():
        if tuple(snapshots[name][key] for key in ('energy','playerHealth','chargedShots','repairs','overclocks'))!=expected:
            raise ValueError('Numeric evidence contradicts a passing native check')
    if (snapshots['charging_blocks_primary_fire']['actualPlayerShots']!=0 or
            snapshots['charged_shot_hits_two_for_52']['actualPlayerShots']!=1 or
            snapshots['pause_cancels_charge_without_cost']['charging'] is not False):
        raise ValueError('Actual firing/cancellation counters contradict native checks')
    interventions=report.get('interventions')
    if not isinstance(interventions,list) or len(interventions)!=14 or any(not isinstance(x,dict) for x in interventions):
        raise ValueError('Missing disclosed fixture interventions')
    wounds=[x for x in interventions if x.get('type')=='fixture_damage']
    placements=[x for x in interventions if x.get('type')=='fixture_placement']
    covers=[x for x in interventions if x.get('type')=='spawn_opaque_cover']
    if len(wounds)!=4 or len(placements)!=9 or len(covers)!=1: raise ValueError('Unexpected fixture intervention set')
    if [x.get('requested') for x in wounds]!=[10,45,35,20]: raise ValueError('Wrong disclosed wounds')
    for row in wounds:
        if number(row.get('applied'))!=number(row.get('requested')) or not row.get('source') or not row.get('actor'):
            raise ValueError('Actual wound differs from explicit intervention')
    for row in placements:
        location=row.get('location')
        if not row.get('actor') or not isinstance(location,list) or len(location)!=3:
            raise ValueError('Missing fixture position')
        for value in location: number(value,-10000,10000)
    events=[m.group(1) for line in log.splitlines() if (m:=DIRECT.fullmatch(line))]
    if events!=[BEGIN,*('AEGIS_V2_PROBE_ASSERT_PASS '+name for name in ASSERTIONS),PASS]:
        raise ValueError('Missing, duplicate, replayed or failing original native markers')
    if sum(bool(EXIT.fullmatch(line)) for line in log.splitlines())!=1: raise ValueError('Normal menu exit not observed')
    if any(x in log for x in ('Handled ensure','Ensure condition failed','Assertion failed','Fatal error:')):
        raise ValueError('Engine failure in log')
    validate_engine_messages(log,rendered=True)


def validate_sessions(report, output):
    system=Path(output).resolve()/'system'
    names=report.get('portfolioTracePaths')
    if not isinstance(names,list) or len(names)!=4 or len(set(names))!=4:
        raise ValueError('Expected four sessions across three normal R restarts')
    # Actual fixture transactions: repair8; repair40+charged12; overclock35; ally repair16.
    expected_sessions=[(52,8,1,10,0,10,0,0,0,0),(8,52,1,30,20,45,35,1,0,104),
                       (25,35,0,0,0,0,0,0,1,0),(44,16,1,0,20,0,20,0,0,0)]
    bound=[]
    for index,name in enumerate(names):
        if not isinstance(name,str) or not Path(name).is_absolute(): raise ValueError('Trace path must be absolute')
        trace=Path(name).resolve(strict=True)
        if trace.parent!=system or not re.fullmatch(r'portfolio-[0-9A-Fa-f]{32}\.jsonl',trace.name):
            raise ValueError('Trace outside this run')
        summary=read_json(trace.with_suffix('.json'))
        required_fields(summary,dict(mode='aegis-prism-fall-v2.0',completed=False,ledgerBalanced=True,traceComplete=True,
                                     outcome='aborted' if index==3 else 'restarted'))
        run_id=trace.stem.removeprefix('portfolio-')
        if summary.get('runId')!=run_id or Path(summary.get('tracePath','')).resolve()!=trace:
            raise ValueError('Session identity mismatch')
        events=[strict_json(line) for line in trace.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
        if (len(events)<3 or any(not isinstance(event,dict) for event in events) or
                number(summary.get('eventCount'),integer=True)!=len(events) or
                events[0].get('event')!='session_started' or events[-1].get('event')!='session_finished'):
            raise ValueError('Incomplete original session trace')
        balance,spent,previous=60,0,-1
        dealt=player_damage=ally_damage=self_heal=ally_heal=0
        transactions=[]
        for sequence,event in enumerate(events,1):
            if not isinstance(event,dict) or event.get('runId')!=run_id or number(event.get('sequence'),integer=True)!=sequence:
                raise ValueError('Event identity/sequence mismatch')
            stamp=number(event.get('gameSeconds'))
            if stamp<previous: raise ValueError('Session clock regressed')
            previous=stamp
            data=event.get('data')
            if not isinstance(data,dict): raise ValueError('Missing native event data')
            if event.get('event')=='energy_transaction':
                before=number(data.get('before'),0,100,True)
                delta=number(data.get('actualDelta'),-40,-8,True)
                if before!=balance or data.get('after')!=before+delta or data.get('offeredDelta')!=delta:
                    raise ValueError('V2 transaction does not reconcile')
                transactions.append((data.get('reason'),-delta)); balance+=delta; spent-=delta
            elif event.get('event')=='damage':
                applied=number(data.get('actualDamage'),0.00001)
                if data.get('enemyVictim') is True: dealt+=applied
                elif re.fullmatch(r'AegisPlayerCharacter(?:_\d+)?',str(data.get('victim',''))): player_damage+=applied
                elif re.fullmatch(r'AegisAICharacter(?:_\d+)?',str(data.get('victim',''))): ally_damage+=applied
                else: raise ValueError('Unknown fixture damage recipient')
            elif event.get('event')=='repair_applied':
                self_heal+=number(data.get('playerActualHealing'),0,30)
                ally_heal+=number(data.get('companionActualHealing'),0,20)
            if number(event.get('energy'),0,100,True)!=balance: raise ValueError('Undisclosed energy change')
        expected_transactions=[[('repair',8)],[('repair',40),('charged_shot',12)],[('overclock',35)],[('repair',16)]][index]
        if transactions!=expected_transactions: raise ValueError('Unexpected v2 fixture transactions')
        keys=('energyFinal','energySpent','repairsUsed','repairPlayerActualHealing','repairCompanionActualHealing',
              'playerActualDamageTaken','companionActualDamageTaken','chargedShots','overclocks','alliedActualDamageDealt')
        expected=dict(zip(keys,expected_sessions[index]))
        expected.update(energyInitial=60,energyEarned=0,energyOverflow=0,pulsesUsed=0,enemiesRewarded=0,stagesRewarded=0)
        for key,value in expected.items():
            if number(summary.get(key))!=value or events[-1]['data'].get(key)!=summary.get(key):
                raise ValueError('Session result contradicts fixture: '+key)
        if summary['energyFinal']!=balance or summary['energySpent']!=spent: raise ValueError('Final ledger mismatch')
        actual=dict(alliedActualDamageDealt=dealt,playerActualDamageTaken=player_damage,
                    companionActualDamageTaken=ally_damage,repairPlayerActualHealing=self_heal,
                    repairCompanionActualHealing=ally_heal)
        if any(value!=summary[key] for key,value in actual.items()):
            raise ValueError('Actual wound, shot or healing events do not reconcile with summary')
        bound.append(dict(path=str(trace),sha256=sha(trace),summarySha256=sha(trace.with_suffix('.json'))))
    if {p.resolve() for p in system.glob('portfolio-*.jsonl')}!={Path(x).resolve() for x in names}:
        raise ValueError('Unreported extra session')
    return bound


def build_command(exe,output,editor=False):
    exe,output=Path(exe).resolve(),Path(output).resolve()
    command=[str(exe)]+([str(ROOT/'AegisArena.uproject'),'/Game/Aegis/Maps/AegisArena','-game'] if editor else [])
    return command+['-AegisV2','-AegisPortfolio','-AegisV2Probe','-AegisInputProbe','-RenderOffscreen','-d3d11',
        '-ForceRes','-ResX=1280','-ResY=720','-windowed','-unattended','-nop4','-nosound','-nosplash',OFFLINE_ARGUMENT,
        '-stdout','-FullStdOutLogOutput',f'-AegisV2ProbeOutput={output}',f'-AegisPortfolioOutput={output/"system"}',
        f'-abslog={output/"engine.log"}']


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,required=True); parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--editor',action='store_true'); parser.add_argument('--cache-root',type=Path)
    args=parser.parse_args(); exe=args.exe.resolve(); output=args.output.resolve()
    if not exe.is_file(): parser.error('Executable missing')
    if args.editor!=exe.name.casefold().startswith('unrealeditor'): parser.error('Editor mode does not match executable')
    output.mkdir(parents=True,exist_ok=False)
    command=build_command(exe,output,args.editor)
    record=dict(schemaVersion=1,passed=False,engine='unreal-runtime',launch='editor-game' if args.editor else 'packaged-development',
        command=command,expectedAssertions=list(ASSERTIONS),notCovered=NOT_COVERED,fixtureDamage=True,fixturePlacement=True,
        aiFrozen=True,syntheticKeyboardInput=True,humanPlaytest=False,policyPerformanceTest=False,
        actorDeadlineSeconds=45,processTimeoutSeconds=180,startedAtUtc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    environment=os.environ.copy()
    if args.cache_root:
        cache=args.cache_root.resolve(); (cache/'Temp').mkdir(parents=True,exist_ok=True)
        environment.update(TEMP=str(cache/'Temp'),TMP=str(cache/'Temp'))
        environment['UE-LocalDataCachePath']=str(cache/'DerivedDataCache')
    wrappers=('run_v2_probe.py','run_portfolio_probe.py','run_decision_lab.py','run_unreal_functional.py',
              'run_unreal_trial.py','run_unreal_input_probe.py')
    started=time.monotonic()
    try:
        record['inputsBefore']=snapshot_inputs(ROOT); record['binariesBefore']=binary_snapshot(exe,args.editor)
        record['wrappersBefore']={name:sha(ROOT/'scripts'/name) for name in wrappers}
        with (output/'stdout.log').open('w',encoding='utf-8') as stream:
            result=subprocess.run(command,cwd=ROOT if args.editor else exe.parent,env=environment,
                stdout=stream,stderr=subprocess.STDOUT,timeout=180,check=False)
        record['exitCode']=result.returncode
        if result.returncode: raise ValueError(f'Native fixture exit code {result.returncode}')
        report=read_json(output/'v2-probe.json'); log=(output/'engine.log').read_text(encoding='utf-8-sig')
        validate_report(report,log); record['sessions']=validate_sessions(report,output); record['nativeAssertions']=36
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError) as error: record['failure']=str(error)
    finally:
        try:
            record['inputsAfter']=snapshot_inputs(ROOT); record['binariesAfter']=binary_snapshot(exe,args.editor)
            record['wrappersAfter']={name:sha(ROOT/'scripts'/name) for name in wrappers}
            for label in ('inputs','binaries','wrappers'):
                if record.get(label+'Before')!=record.get(label+'After'):
                    record['failure']=record.get('failure','')+f' | {label} changed or unbound'
        except (OSError,ValueError) as error: record['failure']=record.get('failure','')+' | final provenance: '+str(error)
        record['passed']='failure' not in record and record.get('nativeAssertions')==36
        record['wallSeconds']=time.monotonic()-started
        record['files']={str(p.relative_to(output)):{'bytes':p.stat().st_size,'sha256':sha(p)} for p in sorted(output.rglob('*'))
                         if p.is_file() and p.name!='provenance.json'}
        (output/'provenance.json').write_text(json.dumps(record,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps({k:record[k] for k in ('passed','nativeAssertions','failure','wallSeconds') if k in record}))
    return 0 if record['passed'] else 2


if __name__=='__main__': raise SystemExit(main())
