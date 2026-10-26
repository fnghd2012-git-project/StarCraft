"""개인 로컬 서버만 실행하여 완료 전 해제, 풀 고갈, 느린 수신자를 검증한다."""
from pathlib import Path
import argparse, configparser, concurrent.futures, json, re, shutil, socket, struct, subprocess, time, uuid
ROOT=Path(__file__).resolve().parents[4]
VERSION=bytes([0,12,0,1,10,4,116,101,115,116,16,5])
PING=bytes([0,9,0,3,8,1,18,1,120])

def exact(s,n):
    data=b''
    while len(data)<n:
        more=s.recv(n-len(data))
        if not more: raise RuntimeError('Unexpected EOF')
        data+=more
    return data

def packet(s):
    size,kind=struct.unpack('!HH',exact(s,4))
    assert 4<=size<=4096
    return kind,exact(s,size-4)

def trial(binary, use_pool, capacity):
    folder=ROOT/'.deps'/('iocp-fault-'+uuid.uuid4().hex)
    folder.mkdir(); shutil.copytree(ROOT/'Data',folder/'Data')
    shutil.copy2(binary/'Cluster.local.ini',folder/'Cluster.local.ini')
    probe=socket.socket(); probe.bind(('127.0.0.1',0)); port=probe.getsockname()[1]; probe.close()
    config=configparser.ConfigParser(interpolation=None);config.optionxform=str;config.read(ROOT/'Server.ini',encoding='utf-8-sig')
    config['Network'].update(Mode='IOCP',WorkerCount='4',StopFile='stop.request')
    config['Memory'].update(UsePool=str(use_pool),PoolCapacity=str(capacity))
    config['World']['Port']=str(port);config['Replay']['Enabled']='0';config['Redis']['Enabled']='0'
    with (folder/'Server.ini').open('w',encoding='utf-8') as f: config.write(f,space_around_delimiters=False)
    connections=[]
    with (folder/'out.txt').open('w',encoding='utf-8') as out,(folder/'err.txt').open('w',encoding='utf-8') as err:
        process=subprocess.Popen([binary/'MultiThreadServer.exe','--server','World','--config',folder/'Server.ini','--run-seconds','60'],cwd=folder,stdout=out,stderr=err,creationflags=subprocess.CREATE_NO_WINDOW)
        def connect():
            s=socket.create_connection(('127.0.0.1',port),2);s.settimeout(3);return s
        def check():
            with connect() as s:
                s.sendall(VERSION[:2]);s.sendall(VERSION[2:]+PING*20)
                assert packet(s)[0]==2
                for _ in range(20): assert packet(s)[0]==4
        try:
            for _ in range(100):
                try:
                    with connect(): pass
                    break
                except OSError: time.sleep(.05)
            else: raise RuntimeError('Listen timeout')
            if capacity>32:
                # 병렬 재접속, 분할 헤더와 합쳐진 패킷, RST 종료를 섞는다.
                with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
                    list(executor.map(lambda _:check(),range(300)))
                for _ in range(100):
                    with connect() as s:
                        s.sendall(VERSION+PING*20)
                        s.setsockopt(socket.SOL_SOCKET,socket.SO_LINGER,struct.pack('HH',1,0))
                with connect() as slow:
                    slow.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,1024)
                    slow.sendall(VERSION); assert packet(slow)[0]==2
                    try:
                        for _ in range(100): slow.sendall(PING*10000)
                    except OSError: pass
                    time.sleep(.2)
                check() # 느린 연결이 다른 연결의 응답을 막으면 안 된다.
                for _ in range(64):
                    s=connect(); s.sendall(VERSION);packet(s);connections.append(s)
            else:
                # 미리 걸어 둔 Accept도 풀을 사용한다. 매우 작은 풀에서는 일부 접속이 거절된다.
                for _ in range(24): connections.append(connect())
                time.sleep(.5)
                for s in connections: s.close()
                connections.clear();time.sleep(.5)
                if process.poll() is not None: raise RuntimeError('Pool exhaustion stopped server')
                # 용량 회수 뒤 정상 요청이 다시 가능해야 한다.
                check()
            (folder/'stop.request').write_text('stop')
            process.wait(timeout=15)
            if process.returncode: raise RuntimeError('Server failed shutdown')
        finally:
            for s in connections: s.close()
            if process.poll() is None: process.kill();process.wait()
    log=(folder/'out.txt').read_text('utf-8'); errors=(folder/'err.txt').read_text('utf-8')
    stop=next(line for line in log.splitlines() if '[IOCP STOP]' in line)
    values={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',stop)}
    assert values['pending_io']==values['pool_in_use']==0
    if use_pool: assert values['pool_allocations']<=capacity and values['pool_reused']>0
    else: assert values['pool_reused']==0
    if capacity<=32: assert values['pool_exhausted']>0
    else: assert values['pool_exhausted']==0 and 'Client send queue full' in errors
    return dict(pool=use_pool,capacity=capacity,runtime=str(folder),metrics=values,passed=True)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--configuration',default='Release');args=parser.parse_args()
    results=[]
    for pool,capacity in [(0,8192),(1,8192),(1,16)]:
        result=trial(ROOT/'Run64'/args.configuration,pool,capacity);results.append(result)
        print('[OK]',pool,capacity,result['metrics'],flush=True)
    output=Path(__file__).parent/('fault-'+args.configuration.lower()+'.json')
    output.write_text(json.dumps(results,indent=2),encoding='utf-8')

if __name__=='__main__':main()
