"""동일 실행 파일/시험 데이터로 워커 설정만 비교한다. 기존 Redis/SQL 서비스는 중단하지 않는다."""
from pathlib import Path
import argparse
import configparser
import csv
import ctypes
from ctypes import wintypes
import hashlib
import json
import queue
import random
import re
import shutil
import socket
import subprocess
import threading
import time
import uuid

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[3]
BIN = ROOT/'Run64/Release'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Memory(ctypes.Structure):
    _fields_ = [('cb', wintypes.DWORD), ('PageFaultCount', wintypes.DWORD)] + [(name, ctypes.c_size_t) for name in
        ('PeakWorkingSetSize','WorkingSetSize','QuotaPeakPagedPoolUsage','QuotaPagedPoolUsage',
         'QuotaPeakNonPagedPoolUsage','QuotaNonPagedPoolUsage','PagefileUsage','PeakPagefileUsage')]


kernel = ctypes.WinDLL('kernel32', use_last_error=True)
psapi = ctypes.WinDLL('psapi', use_last_error=True)
kernel.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)]*4
psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(Memory), wintypes.DWORD]


def resource(process):
    created, exited, system, user = (wintypes.FILETIME() for _ in range(4))
    if not kernel.GetProcessTimes(int(process._handle), created, exited, system, user):
        raise ctypes.WinError(ctypes.get_last_error())
    ticks = lambda ft: (ft.dwHighDateTime << 32) + ft.dwLowDateTime
    memory = Memory(); memory.cb = ctypes.sizeof(memory)
    if not psapi.GetProcessMemoryInfo(int(process._handle), ctypes.byref(memory), memory.cb):
        # 이미 종료된 더미는 CPU 누계만 읽을 수 있다. 최대 메모리는 이전 표본을 사용한다.
        memory.WorkingSetSize = 0
    return dict(cpu_seconds=(ticks(system)+ticks(user))/1e7, working_set_mb=memory.WorkingSetSize/1048576)


class LoggedProcess:
    def __init__(self, command, cwd, directory, name, events):
        self.name = name
        self.process = subprocess.Popen([str(x) for x in command], cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            creationflags=subprocess.CREATE_NO_WINDOW)
        self.readers = []
        for stream_name, stream in (('out',self.process.stdout),('err',self.process.stderr)):
            def read(source=stream, kind=stream_name):
                with (directory/f'{name}.{kind}.txt').open('w',encoding='utf-8') as output:
                    for raw in iter(source.readline, b''):
                        line=raw.decode('utf-8',errors='replace').rstrip('\r\n')
                        output.write(line+'\n'); output.flush()
                        events.put((time.perf_counter(), name, kind, line))
                source.close()
            reader=threading.Thread(target=read,daemon=True); reader.start(); self.readers.append(reader)

    def finish_readers(self):
        for reader in self.readers: reader.join(timeout=5)


def fixture(directory, network, db, realm):
    (directory/'Data').mkdir(parents=True)
    shutil.copy2(BIN/'Cluster.local.ini',directory/'Cluster.local.ini')
    for name in ('Skill.csv','StartPoint.csv','Map.ini'):
        shutil.copy2(ROOT/'Data'/name,directory/'Data'/name)
    with (ROOT/'Data/Unit.csv').open(encoding='utf-8-sig',newline='') as file:
        reader=csv.DictReader(file); fields=reader.fieldnames
        units=[row for row in reader if row['UnitIndex'] in ('101','201','302')]
    assert len(units)==3
    for unit in units:
        unit.update(HP='100000',Shield='0',MoveSpeed='100',AttackRange='8')
        for slot in range(1,6): unit[f'Skill{slot}']='0'
    with (directory/'Data/Unit.csv').open('w',encoding='utf-8',newline='') as file:
        writer=csv.DictWriter(file,fieldnames=fields); writer.writeheader(); writer.writerows(units)
    listeners=[]
    for _ in range(4):
        listener=socket.socket(); listener.bind(('127.0.0.1',0)); listener.listen(); listeners.append(listener)
    ports=[listener.getsockname()[1] for listener in listeners]
    with (directory/'Data/GameServer.csv').open('w',encoding='utf-8',newline='') as file:
        writer=csv.writer(file); writer.writerow(['Index','Name','Race','Address','Port'])
        for name,race,port in zip(('A','B','C'),('Zerg','Protoss','Terran'),ports):
            writer.writerow([name,'Game '+name,race,'127.0.0.1',port])
    config=configparser.ConfigParser(interpolation=None); config.optionxform=str
    config.read(ROOT/'Server.ini',encoding='utf-8-sig')
    values={'Network':dict(WorkerCount=network,StopFile='stop.request',MaxPendingSendBytes=262144),
        'Database':dict(WorkerCount=db,Realm=realm,QueueCapacity=256,SessionLeaseSeconds=60),
        'Replay':dict(Enabled=1,FrameTicks=4,QueueMB=32,MaxFileMB=512),
        'Redis':dict(Enabled=1), 'Metrics':dict(IntervalSeconds=1),
        'World':dict(Port=ports[-1]),
        'Battle':dict(MaxWorldPlayers=128,SpatialSearch='Grid',SpatialCellSize=25,VerifySpatialSearch=0,SnapshotRadius=25)}
    for section,items in values.items():
        for key,value in items.items(): config[section][key]=str(value)
    with (directory/'Server.ini').open('w',encoding='utf-8') as file: config.write(file,space_around_delimiters=False)
    for listener in listeners: listener.close()
    return ports


def trial(target, network, db, repeat, pilot):
    label=f'r{repeat}-n{network}-d{db}'
    result_dir=target/label; result_dir.mkdir()
    realm='workers_'+uuid.uuid4().hex
    runtime=ROOT/'.deps/worker-benchmark'/realm
    ports=fixture(runtime,network,db,realm)
    shutil.copy2(runtime/'Server.ini',result_dir/'Server.ini')
    shutil.copytree(runtime/'Data',result_dir/'Data')
    events=queue.Queue(); servers=[]; client=None
    records=[]; measurements={}; resources=[]; phase_intervals=[]
    def drain():
        batch=[]
        while True:
            try: item=events.get_nowait()
            except queue.Empty: break
            records.append(item); batch.append(item)
        return batch
    def run_phase(name, args, timeout=180):
        nonlocal client
        client=LoggedProcess([BIN/'DummyClient.exe',*args],runtime,result_dir,name,events)
        roles={s.name:s.process for s in servers}; roles['Dummy']=client.process
        before=None; after=None; started=None; finished=None; peak={role:0 for role in roles}
        launched=time.perf_counter()
        while True:
            now=time.perf_counter()
            for stamp,source,kind,line in drain():
                if source!=name: continue
                if line=='[LOAD PHASE] measure_begin':
                    started=stamp; before={role:resource(p) for role,p in roles.items()}
                elif line=='[LOAD PHASE] measure_end':
                    finished=stamp; after={role:resource(p) for role,p in roles.items()}
            if before is not None and after is None:
                for role,process in roles.items():
                    current=resource(process); peak[role]=max(peak[role],current['working_set_mb'])
                    resources.append(dict(phase=name,monotonic=now,role=role,**current))
            if client.process.poll() is not None:
                client.finish_readers()
                # 종료 직전 출력도 처리한다. 로그 파이프 관측 오차는 CPU 지표에만 영향을 준다.
                for stamp,source,kind,line in drain():
                    if source==name and line=='[LOAD PHASE] measure_end':
                        finished=stamp; after={role:resource(p) for role,p in roles.items()}
                break
            if any(s.process.poll() is not None for s in servers): raise RuntimeError('Server stopped during '+name)
            if now-launched>timeout: raise TimeoutError(name)
            time.sleep(.05)
        if client.process.returncode!=0: raise RuntimeError(f'{label}/{name} failed; see its log')
        if before is None or after is None or finished<=started: raise RuntimeError('Missing measurement markers: '+name)
        elapsed=finished-started
        measurements[name]={'marker_seconds':elapsed,'resources':{role:dict(
            cpu_one_core_percent=100*(after[role]['cpu_seconds']-before[role]['cpu_seconds'])/elapsed,
            sampled_peak_working_set_mb=max(peak[role],before[role]['working_set_mb'],after[role]['working_set_mb'])) for role in roles}}
        phase_intervals.append((name,started,finished))
        client=None
        print(f'[PHASE] {label} {name} finished',flush=True)
    try:
        for index,name in enumerate(('A','B','C','World')):
            process=LoggedProcess([BIN/'MultiThreadServer.exe','--server',name,'--config',runtime/'Server.ini','--run-seconds','600'],runtime,result_dir,name,events)
            servers.append(process)
            for _ in range(200):
                if process.process.poll() is not None: raise RuntimeError('Server startup failed: '+name)
                try:
                    with socket.create_connection(('127.0.0.1',ports[index]),.1): break
                except OSError: time.sleep(.05)
            else: raise TimeoutError('Listen '+name)
        count=12 if pilot else 96; concurrency=4 if pilot else 32
        base=['--session-load','--config',runtime/'Server.ini','--clients',str(count),'--concurrency',str(concurrency),'--prefix','WorkerSession']
        run_phase('seed',[*base,'--phase','seed','--samples',result_dir/'seed.csv'])
        # 별도 계정으로 전체 경로를 먼저 거친다. 본 측정 계정에는 World 재입장 대기를 만들지 않는다.
        warm=['--session-load','--config',runtime/'Server.ini','--clients','12','--concurrency','4','--prefix','WarmSession']
        run_phase('warm-seed',[*warm,'--phase','seed'])
        run_phase('warm-session',[*warm,'--phase','measure'])
        run_phase('session',[*base,'--phase','measure','--samples',result_dir/'session.csv'])
        scenarios=[('ping',100),('fixed-combat',20)]
        if repeat%2==0: scenarios.reverse()
        for mode,rate in scenarios:
            run_phase(mode,['--load','--config',runtime/'Server.ini','--mode',mode,'--clients',str(count),
                '--seconds',str(3 if pilot else 20),'--rate',str(rate),'--warmup',str(1 if pilot else 3),
                '--csv',result_dir/f'{mode}.csv','--samples',result_dir/f'{mode}-samples.csv'])
        (runtime/'stop.request').write_text('benchmark complete',encoding='utf-8')
        for server in servers:
            server.process.wait(timeout=30); server.finish_readers()
            if server.process.returncode: raise RuntimeError('Nonzero server exit: '+server.name)
        drain()
        failures=[line for _,_,_,line in records if re.search(r'\[(?:REDIS|REPLAY|WORKER|MATCH DB).*FAIL|\[FAIL\]|uncommitted jobs',line)]
        if failures: raise RuntimeError(str(failures[:5]))
        metrics=[]
        for stamp,source,kind,line in records:
            if not line.startswith('[METRICS]'): continue
            # 마지막 1초 구간 전체가 해당 측정 안에 든 표본만 사용한다. 경계 구간은 제외한다.
            phase=next((name for name,a,b in phase_intervals if a+1.1<=stamp<=b),'outside')
            metrics.append(dict(phase=phase,source=source,monotonic=stamp,**{k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)}))
        def write_csv(path,rows):
            if not rows: return
            with path.open('w',encoding='utf-8',newline='') as file:
                writer=csv.DictWriter(file,fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
        write_csv(result_dir/'resources.csv',resources); write_csv(result_dir/'metrics.csv',metrics)
        with (result_dir/'events.csv').open('w',encoding='utf-8',newline='') as file:
            writer=csv.writer(file); writer.writerow(['Monotonic','Source','Stream','Line']); writer.writerows(records)
        # 설정과 원본 표본은 공개 결과 폴더, 비밀 공유 키/대용량 기록은 무시되는 실행 폴더에 둔다.
        for file in (runtime/'MatchResults').glob('*.xml'): shutil.copy2(file,result_dir/file.name)
        result=dict(network=network,db=db,repeat=repeat,realm=realm,runtime=str(runtime),measurements=measurements,
            intervals=phase_intervals,healthy_exit=True)
        (result_dir/'run.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        print(f'[OK] {label}',flush=True)
        return result
    finally:
        (runtime/'stop.request').write_text('cleanup',encoding='utf-8')
        if client is not None:
            if client.process.poll() is None: client.process.kill()
            client.process.wait(timeout=10); client.finish_readers()
        for server in servers:
            if server.process.poll() is None:
                try: server.process.wait(timeout=15)
                except subprocess.TimeoutExpired: server.process.kill(); server.process.wait(timeout=10)
            server.finish_readers()


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--pilot',action='store_true')
    parser.add_argument('--output',type=Path); args=parser.parse_args()
    target=args.output.resolve() if args.output else OUT/('pilot' if args.pilot else 'matrix')
    target.mkdir(parents=True,exist_ok=False)
    configurations=[(1,1),(2,2),(4,6),(1,2),(4,2),(4,1)]
    schedule=[]; rng=random.Random(20261019)
    for repeat in range(1,4):
        order=configurations.copy(); rng.shuffle(order)
        schedule.extend((n,d,repeat) for n,d in order)
    if args.pilot: schedule=[(1,1,0)]
    sources=['Server.ini','Protocol/Game.proto','DummyClient/DummyClient.vcxproj']
    sources += [p.relative_to(ROOT).as_posix() for base in ('MultiThreadServer/MultiThreadServer/src','DummyClient/src')
        for p in (ROOT/base).glob('*') if p.suffix in ('.h','.cpp')]
    plan=dict(date=time.strftime('%Y-%m-%d'),schedule=schedule,build='x64 Release',pilot=args.pilot,
        clients=12 if args.pilot else 96,concurrency=4 if args.pilot else 32,
        measure_seconds=3 if args.pilot else 20,warmup_seconds=1 if args.pilot else 3,ping_rate_per_client=100,combat_rate_per_client=20,
        redis=True,replay=True,spatial='Grid',query_samples='raw RTT; summaries use nearest-rank p95/p99',
        limitations=['Same PC and loopback; not an Internet capacity claim','Closed-loop offered rate cap; achieved rate may be lower',
            'Session login includes PBKDF2 and SQL, not SQL alone','Raw process CPU deltas follow log markers with ~50ms observation delay',
            'Metrics intervals overlapping phase boundaries are excluded','Warmup/cache/OS background state cannot be identical'],
        source_sha256={p:digest(ROOT/p) for p in sources},
        binaries={p:digest(BIN/p) for p in ('MultiThreadServer.exe','DummyClient.exe')})
    (target/'plan.json').write_text(json.dumps(plan,indent=2),encoding='utf-8')
    results=[]
    for n,d,repeat in schedule:
        print(f'[START] repeat={repeat} network={n} db={d}',flush=True)
        results.append(trial(target,n,d,repeat,args.pilot))
    (target/'completed.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print(f'[COMPLETE] trials={len(results)} path={target}',flush=True)


if __name__=='__main__': main()
