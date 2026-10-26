"""최종 Release 전투를 실행하고 Redis를 읽기 전용으로 관측한다. Python 표준 라이브러리만 사용."""
from pathlib import Path
import configparser
import csv
import hashlib
import json
import re
import socket
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

sys.stdout.reconfigure(encoding='utf-8')
OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[3]
BIN = ROOT / 'Run64/Release'


class Redis:
    def __init__(self, host, port):
        self.socket = socket.create_connection((host, port), timeout=2)
        self.stream = self.socket.makefile('rb')

    def read(self):
        line = self.stream.readline()
        if not line:
            raise RuntimeError('Redis closed connection')
        kind, value = line[:1], line[1:-2]
        if kind == b'-':
            raise RuntimeError(value.decode())
        if kind == b'+':
            return value.decode()
        if kind == b':':
            return int(value)
        if kind == b'$':
            count = int(value)
            if count < 0:
                return None
            data = self.stream.read(count)
            if len(data) != count or self.stream.read(2) != b'\r\n':
                raise RuntimeError('Truncated Redis bulk response')
            return data.decode('utf-8')
        if kind == b'*':
            return [self.read() for _ in range(int(value))]
        raise RuntimeError('Unexpected Redis response')

    def command(self, *args):
        parts = [str(x).encode('utf-8') for x in args]
        request = b'*%d\r\n' % len(parts)
        request += b''.join(b'$%d\r\n' % len(p) + p + b'\r\n' for p in parts)
        self.socket.sendall(request)
        return self.read()

    def close(self):
        self.stream.close()
        self.socket.close()


def main():
    # 이 실행이 만든 설정 폴더를 런처 출력에서 읽는다. 기존 경기/Redis 키를 수정하지 않는다.
    stdout = OUT / 'simulation.out.txt'
    stderr = OUT / 'simulation.err.txt'
    started = time.monotonic()
    with stdout.open('wb') as log, stderr.open('wb') as errors:
        process = subprocess.Popen([
            'powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
            str(BIN / 'StartBattle.ps1'), '-PerServer', '500', '-Seconds', '240', '-NoReplay'
        ], cwd=BIN, stdout=log, stderr=errors, creationflags=subprocess.CREATE_NO_WINDOW)
        directory = None
        client = None
        observations = []
        try:
            while process.poll() is None:
                text = stdout.read_text(encoding='utf-8-sig', errors='replace')
                match = re.search(r'^\[DIRECTORY\] (.+)$', text, re.M)
                if match and directory is None:
                    directory = Path(match[1].strip())
                    config = configparser.ConfigParser()
                    config.read(directory / 'Server.ini', encoding='utf-8-sig')
                    assert config['Redis']['Enabled'] == '1'
                    assert config['Battle']['DeathReentrySeconds'] == '100'
                    client = Redis(config['Redis']['Host'], int(config['Redis']['Port']))
                    assert client.command('PING') == 'PONG'
                    (OUT / 'run-directory.txt').write_text(str(directory), encoding='utf-8')
                    print('RUN_DIRECTORY', directory, flush=True)
                if directory:
                    world = (directory / 'server-3.out.txt').read_text(encoding='utf-8', errors='replace')
                    matches = re.findall(r'\[MATCH\] Started ([0-9a-f]+)', world)
                    if matches:
                        match_id = matches[-1]
                        prefix = config['Redis']['KeyPrefix'] + ':' + config['Database']['Realm'] + ':' + match_id
                        observed_at = time.monotonic() - started
                        for metric in ('kills', 'deaths', 'entries'):
                            # 측정 구간은 TCP 연결 이후 ZREVRANGE 왕복만. 게임 Rank 패킷 RTT와 다르다.
                            before = time.perf_counter()
                            top = client.command('ZREVRANGE', prefix + ':' + metric, 0, 9, 'WITHSCORES')
                            latency = (time.perf_counter() - before) * 1000
                            count = client.command('ZCARD', prefix + ':' + metric)
                            observations.append(dict(elapsed_s=round(observed_at, 3), match_id=match_id,
                                metric=metric, players=count, top_score=int(top[1]) if top else 0,
                                query_ms=round(latency, 4)))
                        with (OUT / 'redis-samples.csv').open('w', newline='', encoding='utf-8') as f:
                            writer = csv.DictWriter(f, fieldnames=list(observations[0]))
                            writer.writeheader(); writer.writerows(observations)
                        print('SAMPLE', observations[-3:], flush=True)
                time.sleep(5)
            if process.returncode:
                raise RuntimeError(f'Battle launcher failed: {process.returncode}. See {stderr}')
            if directory is None or not observations:
                raise RuntimeError('No simulation evidence collected')
            # 정상 종료 뒤 Redis 3개 점수와 XML의 1,500명 총계를 전부 대조한다.
            verification = []
            for xml_file in sorted((directory / 'MatchResults').glob('*.xml')):
                match = ET.parse(xml_file).getroot()
                prefix = config['Redis']['KeyPrefix'] + ':' + match.attrib['realm'] + ':' + match.attrib['id']
                players = match.findall('player')
                summary = dict(match.attrib)
                summary['participants'] = len(players)
                for metric in ('kills', 'deaths', 'entries'):
                    values = client.command('ZREVRANGE', prefix + ':' + metric, 0, -1, 'WITHSCORES')
                    actual = dict(zip(values[0::2], map(int, values[1::2])))
                    expected = {p.attrib['id']: int(p.attrib[metric]) for p in players}
                    assert actual == expected, f'Redis/XML mismatch: {metric}'
                    summary[metric] = sum(actual.values())
                    summary[metric + '_top10'] = list(actual.items())[:10]
                assert client.command('HLEN', prefix + ':players') == len(players)
                summary['redis_xml_all_players_equal'] = True
                verification.append(summary)
            assert verification and sum(x['participants'] for x in verification) >= 1500
            (OUT / 'redis-xml-validation.json').write_text(json.dumps(verification, indent=2), encoding='utf-8')
            (OUT / 'simulation-binaries.json').write_text(json.dumps({name:hashlib.sha256((BIN/name).read_bytes()).hexdigest()
                for name in ('MultiThreadServer.exe', 'DummyClient.exe')}, indent=2), encoding='utf-8')
            print('PASS: simulation, Redis live queries, full Redis/XML comparison', flush=True)
        finally:
            if client:
                client.close()
            if process.poll() is None:
                # 관측 오류가 나도 런처의 제한 시간/정상 종료 경로를 기다린다.
                process.wait(timeout=1800)


if __name__ == '__main__':
    main()
