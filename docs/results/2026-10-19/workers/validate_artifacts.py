"""워커 비교 종료 후 Redis 점수와 기록 파일을 읽기 전용으로 검증한다."""
from pathlib import Path
import argparse
import configparser
import csv
import hashlib
import json
import re
import socket
import subprocess
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[4]
OUT=Path(__file__).resolve().parent


def reply(stream):
    line=stream.readline()
    if not line: raise RuntimeError('Redis connection closed')
    kind,value=line[:1],line[1:-2]
    if kind==b'-': raise RuntimeError(value.decode())
    if kind==b':': return int(value)
    if kind==b'+': return value
    if kind==b'*': return [reply(stream) for _ in range(int(value))]
    if kind==b'$':
        size=int(value)
        if size<0: return None
        result=stream.read(size); assert len(result)==size and stream.read(2)==b'\r\n'
        return result
    raise RuntimeError('Invalid Redis reply')


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--matrix',type=Path,default=OUT/'matrix')
    parser.add_argument('--report',type=Path,default=OUT); args=parser.parse_args()
    runs=json.loads((args.matrix/'completed.json').read_text()); assert len(runs)==18
    result=[]
    for run in runs:
        directory=args.matrix/f"r{run['repeat']}-n{run['network']}-d{run['db']}"
        files=list(directory.glob('*.xml')); assert len(files)==1
        match=ET.parse(files[0]).getroot(); players=match.findall('player'); assert len(players)==204
        expected={p.attrib['id']:p.attrib for p in players}
        config=configparser.ConfigParser(); config.read(directory/'Server.ini')
        key=f"{config['Redis']['KeyPrefix']}:{run['realm']}:{match.attrib['id']}"
        with socket.create_connection((config['Redis']['Host'],int(config['Redis']['Port'])),timeout=5) as sock:
            stream=sock.makefile('rb')
            def command(*args):
                parts=[str(x).encode() for x in args]
                sock.sendall(b'*'+str(len(parts)).encode()+b'\r\n'+b''.join(b'$'+str(len(p)).encode()+b'\r\n'+p+b'\r\n' for p in parts))
                return reply(stream)
            for metric in ('kills','deaths','entries'):
                data=command('ZRANGE',key+':'+metric,0,-1,'WITHSCORES')
                actual={data[i].decode():int(data[i+1]) for i in range(0,len(data),2)}
                assert actual=={pid:int(p[metric]) for pid,p in expected.items()}, (run['realm'],metric)
            assert {x.decode() for x in command('HKEYS',key+':players')}==set(expected)
            stream.close()
        runtime=Path(run['runtime']).resolve()
        assert runtime.is_relative_to(ROOT/'.deps/worker-benchmark')
        replays=list((runtime/'Replays').glob('*.replay')); assert len(replays)==1
        check=subprocess.run([str(ROOT/'Run64/Release/DummyClient.exe'),'--replay',str(replays[0]),'--verify'],
            capture_output=True,text=True,encoding='utf-8',timeout=30,creationflags=subprocess.CREATE_NO_WINDOW)
        (directory/'replay-verification.txt').write_text(check.stdout+check.stderr,encoding='utf-8')
        assert check.returncode==0 and '[OK] Replay verified' in check.stdout
        assert 'match='+match.attrib['id'] in check.stdout and 'complete=1 reason=server_shutdown' in check.stdout
        result.append(dict(repeat=run['repeat'],network=run['network'],db=run['db'],realm=run['realm'],
            match=match.attrib['id'],players=204,redis_scores_equal=True,redis_hash_ids_equal=True,
            replay_verified=True,replay=str(replays[0]),replay_sha256=hashlib.sha256(replays[0].read_bytes()).hexdigest()))
    (args.report/'redis-replay-validation.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print('[OK] Redis/XML 3672 participants: all three scores and hash IDs match. 18 replays verified.')


if __name__=='__main__': main()
