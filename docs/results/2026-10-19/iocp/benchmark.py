"""같은 최종 빌드로 네트워크 방식, I/O 풀, 전투 워커를 한 요소씩 비교한다."""
from pathlib import Path
import argparse, configparser, csv, hashlib, importlib.util, json, queue, random, re, shutil, socket, subprocess, time, uuid
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]; BIN=ROOT/'Run64/Release'
spec=importlib.util.spec_from_file_location('base',HERE.parent/'workers/run_benchmark.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
CONFIGS=[('poll-1','WSAPoll',0,1),('iocp-1-heap','IOCP',0,1),('iocp-1-pool','IOCP',1,1),('iocp-2-pool','IOCP',1,2),('iocp-4-pool','IOCP',1,4)]

def write_csv(path,rows):
    if not rows:return
    with path.open('w',encoding='utf-8',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)

def trial(target,setting,repeat,seconds):
    label,mode,pool,workers=setting
    directory=target/f'r{repeat}-{label}';directory.mkdir()
    realm='engine_'+uuid.uuid4().hex; runtime=ROOT/'.deps/engine-benchmark'/realm
    ports=base.fixture(runtime,2,2,realm)
    config=configparser.ConfigParser(interpolation=None);config.optionxform=str;config.read(runtime/'Server.ini',encoding='utf-8')
    config['Network']['Mode']=mode;config['Memory'].update(UsePool=str(pool),PoolCapacity='8192')
    config['Battle'].update(WorkerCount=str(workers),VerifyParallel='0')
    with (runtime/'Server.ini').open('w',encoding='utf-8') as f:config.write(f,space_around_delimiters=False)
    shutil.copy2(runtime/'Server.ini',directory/'Server.ini');shutil.copytree(runtime/'Data',directory/'Data')
    events=queue.Queue();servers=[];client=None;records=[];measurements={};resources=[];intervals=[]
    def drain():
        result=[]
        while True:
            try:row=events.get_nowait()
            except queue.Empty:break
            records.append(row);result.append(row)
        return result
    try:
        for i,name in enumerate(('A','B','C','World')):
            server=base.LoggedProcess([BIN/'MultiThreadServer.exe','--server',name,'--config',runtime/'Server.ini','--run-seconds','300'],runtime,directory,name,events);servers.append(server)
            for _ in range(200):
                if server.process.poll() is not None:raise RuntimeError('Server stopped '+name)
                try:
                    with socket.create_connection(('127.0.0.1',ports[i]),.1):break
                except OSError:time.sleep(.05)
            else:raise RuntimeError('Startup timeout')
        phases=[('ping',100),('fixed-combat',20)]
        if repeat%2==0:phases.reverse()
        for phase,rate in phases:
            client=base.LoggedProcess([BIN/'DummyClient.exe','--load','--config',runtime/'Server.ini','--mode',phase,'--clients','96','--seconds',str(seconds),'--rate',str(rate),'--warmup','3','--csv',directory/f'{phase}.csv','--samples',directory/f'{phase}-samples.csv'],runtime,directory,phase,events)
            roles={s.name:s.process for s in servers};roles['Dummy']=client.process
            started=finished=before=after=None;peak={r:0 for r in roles};launched=time.perf_counter()
            while True:
                for stamp,source,kind,line in drain():
                    if source==phase and line=='[LOAD PHASE] measure_begin':started=stamp;before={r:base.resource(p) for r,p in roles.items()}
                    if source==phase and line=='[LOAD PHASE] measure_end':finished=stamp;after={r:base.resource(p) for r,p in roles.items()}
                if started and not finished:
                    for role,p in roles.items():
                        sample=base.resource(p);peak[role]=max(peak[role],sample['working_set_mb'])
                        resources.append(dict(phase=phase,role=role,monotonic=time.perf_counter(),**sample))
                if client.process.poll() is not None:
                    client.finish_readers()
                    for stamp,source,kind,line in drain():
                        if source==phase and line=='[LOAD PHASE] measure_end':finished=stamp;after={r:base.resource(p) for r,p in roles.items()}
                    if client.process.returncode:raise RuntimeError('Client failed '+phase)
                    break
                if any(s.process.poll() is not None for s in servers):raise RuntimeError('Server exited')
                if time.perf_counter()-launched>180:raise TimeoutError(phase)
                time.sleep(.05)
            if not started or not finished:raise RuntimeError('Missing measurement markers')
            span=finished-started
            measurements[phase]={r:dict(cpu_one_core_percent=100*(after[r]['cpu_seconds']-before[r]['cpu_seconds'])/span,peak_mb=peak[r]) for r in roles}
            intervals.append((phase,started,finished));client=None
        (runtime/'stop.request').write_text('stop')
        for server in servers:
            server.process.wait(timeout=30);server.finish_readers()
            if server.process.returncode:raise RuntimeError('Shutdown failed')
        drain()
        failure=[line for _,_,_,line in records if re.search(r'\[(?:REDIS|REPLAY|WORKER|MATCH DB).*FAIL|\[FAIL\]|uncommitted jobs',line)]
        if failure:raise RuntimeError(str(failure[:3]))
        metrics=[];io=[]
        for stamp,role,kind,line in records:
            values={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)}
            if line.startswith('[METRICS]'):
                phase=next((p for p,a,b in intervals if a+1.1<=stamp<=b),'outside')
                metrics.append(dict(phase=phase,role=role,**values))
            if line.startswith('[IOCP STOP]'):
                if values['pending_io'] or values['pool_in_use'] or values['pool_exhausted']:raise RuntimeError('IOCP lifetime/capacity failure')
                io.append(dict(role=role,**values))
        write_csv(directory/'metrics.csv',metrics);write_csv(directory/'io.csv',io);write_csv(directory/'resources.csv',resources)
        result=dict(label=label,mode=mode,pool=pool,battle_workers=workers,repeat=repeat,realm=realm,runtime=str(runtime),measurements=measurements,healthy_exit=True)
        (directory/'run.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        for path in (runtime/'MatchResults').glob('*.xml'):shutil.copy2(path,directory/path.name)
        print('[OK]',directory.name,flush=True);return result
    finally:
        (runtime/'stop.request').write_text('stop')
        if client and client.process.poll() is None:client.process.kill();client.process.wait()
        for server in servers:
            if server.process.poll() is None:
                try:server.process.wait(timeout=15)
                except subprocess.TimeoutExpired:server.process.kill();server.process.wait()
            server.finish_readers()

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--pilot',action='store_true');parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    target=args.output.resolve();target.mkdir(parents=True,exist_ok=False)
    rng=random.Random(20261019);schedule=[]
    for repeat in range(1,4):
        settings=CONFIGS.copy();rng.shuffle(settings);schedule.extend((s,repeat) for s in settings)
    if args.pilot:schedule=[(CONFIGS[-1],0)]
    sources=[p for p in (ROOT/'MultiThreadServer/MultiThreadServer/src').glob('*') if p.suffix in ('.h','.cpp')]+[ROOT/'Server.ini',ROOT/'Protocol/Game.proto']
    plan=dict(schedule=schedule,clients=96,seconds=3 if args.pilot else 12,warmup=3,network_workers=2,db_workers=2,redis=True,replay=True,header_bytes=4,protocol=5,
        source_sha256={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
        binaries={name:hashlib.sha256((BIN/name).read_bytes()).hexdigest() for name in ('MultiThreadServer.exe','DummyClient.exe')})
    (target/'plan.json').write_text(json.dumps(plan,indent=2),encoding='utf-8')
    results=[]
    for setting,repeat in schedule:
        print('[START]',repeat,setting,flush=True);results.append(trial(target,setting,repeat,plan['seconds']))
    (target/'completed.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print('[COMPLETE]',len(results),flush=True)

if __name__=='__main__':main()
