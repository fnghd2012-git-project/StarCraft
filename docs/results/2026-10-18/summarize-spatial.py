"""원본 CSV/로그에서 비교표와 PDF가 공유할 요약을 만든다."""
from pathlib import Path
import csv, hashlib, json, math, re, statistics

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
rows = list(csv.DictReader((HERE/'spatial-benchmark.csv').open(encoding='utf-8')))
assert len(rows) == 80
summary = {'date':'2026-10-18', 'scope':'coordinate build + radius search only; not server RTT', 'benchmark':{}, 'live':{}}
for scenario in ('Uniform','Clustered'):
    summary['benchmark'][scenario] = {}
    for mode in ('Linear','Grid'):
        samples = [r for r in rows if r['Scenario']==scenario and r['Mode']==mode]
        assert len(samples)==20
        values=sorted(float(r['TotalUs']) for r in samples)
        counts={int(r['CandidateChecks']) for r in samples}
        assert len(counts)==1
        summary['benchmark'][scenario][mode] = {
            'runs':20, 'total_median_us':statistics.median(values),
            'total_p95_us':values[math.ceil(len(values)*.95)-1],
            'candidate_checks':counts.pop(),
            'build_median_us':statistics.median(float(r['BuildUs']) for r in samples),
            'query_median_us':statistics.median(float(r['QueryUs']) for r in samples)}
    for pass_id in range(1,21):
        pair=[r for r in rows if r['Scenario']==scenario and int(r['Pass'])==pass_id]
        assert len(pair)==2
        assert pair[0]['Matches']==pair[1]['Matches'] and pair[0]['Checksum']==pair[1]['Checksum']
for mode in ('Linear','Grid'):
    metrics=(HERE/(mode+'-world.txt')).read_text(encoding='utf-8-sig')
    queries=sum(map(int,re.findall(r'spatial_queries=(\d+)',metrics)))
    verified=sum(map(int,re.findall(r'spatial_verified=(\d+)',metrics)))
    assert queries==verified and verified>0
    report=dict(line.split('=',1) for line in (HERE/(mode+'-battle-result.txt')).read_text(encoding='utf-8-sig').splitlines() if '=' in line)
    assert report['Errors']=='0' and report['Ready']=='120' and report['LoggedOut']=='120'
    assert int(report['Deaths'])>0 and int(report['DeathReentries'])>0 and int(report['UnitAttacks'])>0
    summary['live'][mode]={'verified':verified,'report':report}
summary['environment']=json.loads((HERE/'environment.json').read_text(encoding='utf-8-sig'))
source_paths=['Server.ini','MultiThreadServer/MultiThreadServer/src/SpatialIndex.h','MultiThreadServer/MultiThreadServer/src/SpatialIndex.cpp',
    'MultiThreadServer/MultiThreadServer/src/SpatialCheck.cpp','MultiThreadServer/MultiThreadServer/src/GameLogic.cpp',
    'MultiThreadServer/MultiThreadServer/src/GameReplay.cpp','MultiThreadServer/MultiThreadServer/src/Config.cpp',
    'Run64/Release/MultiThreadServer.exe','Run64/Release/DummyClient.exe']
summary['sha256']={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in source_paths}
(HERE/'spatial-summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
table=['| 배치 | 방식 | 구축+조회 중앙값 | p95 | 거리 검사 횟수 |','| --- | --- | ---: | ---: | ---: |']
for scenario, modes in summary['benchmark'].items():
    for mode,r in modes.items():
        table.append(f"| {scenario} | {mode} | {r['total_median_us']/1000:.3f} ms | {r['total_p95_us']/1000:.3f} ms | {r['candidate_checks']:,} |")
live='\n'.join(f"- {m}: {v['verified']:,}개 목록 대조, 사망 {v['report']['Deaths']}, 재입장 {v['report']['DeathReentries']}, 오류 0." for m,v in summary['live'].items())
text='''# Linear / Grid 공간 검색 비교 - 2026-10-18

게임 규칙은 공통으로 유지하고 Snapshot의 후보 검색만 선택하도록 변경했다.
Server.ini의 [Battle] SpatialSearch=Linear 또는 Grid. 기본 배포 설정은 Grid다.
시작 시 읽으므로 재시작이 필요하며, 예전 INI에 옵션이 없으면 Linear다.

## 정확성 검증

- Debug/Release 빌드 및 공간 검색·설정 검사 통과. 경계/중복/삭제/빈 전장과 고정 배치 9,000개 쿼리를 독립 전체 검사에 대조했다.
- Debug/Release의 네트워크·전투·리플레이·정원·계정/스킬·자동 전투·채팅/복구 회귀 통과.
- Debug 첫 실행은 Windows PowerShell 5.1에서 예상 실패 stderr를 예외로 처리한 기존 capacity 테스트에서 중단했다. 테스트를 종료 코드 판정으로 수정한 뒤 해당 검사부터 재개했고 통과했다. debug-regression.txt / debug-regression-continued.txt / debug-final-checks.txt에 남겼다.
- Release 전체 실행은 release-regression.txt에 기록했다.
- 실제 전투 대조는 각 방식별 120명, 35초, 시험용 사망 대기 3초다. VerifySpatialSearch=1에서 같은 순간의 두 최종 수신 목록과 순서를 비교했다.
'''+live+'''
- 위 대조 횟수는 지표가 출력된 구간의 합이며 종료 전 미출력 구간을 포함하지 않는다. 실행마다 난수 배정과 전투 경로가 달라 두 전투 실행을 성능 비교로 쓰지 않는다.
- 계정·유닛·세션은 로컬 127.0.0.1/project_db의 기존 테이블에 저장한다. 공간 인덱스는 메모리, 비용은 CSV/로그에 남겨 신규 스키마는 추가하지 않았다.

## 검색 전용 비용

x64 Release, 고정 시드 20261018, 1,500개 점과 동일한 1,500개 질의 중심, 반경/셀 크기 25.
Uniform은 X/Y 0~120, Clustered는 55~65다. 2회 예열 후 20쌍, 매 회 실행 순서를 교대했다.
좌표 복사·검증·인덱스 구축 + 정확한 반경 조회 + 동일한 결과 합계 계산을 측정했다.
구축/조회 원본은 spatial-benchmark.csv. p95는 20개 시간의 nearest-rank 19번째 값이다.
빌드와 회귀·전투 대조가 끝난 뒤 별도 실행했다. OS와 다른 일반 앱의 모든 부하는 통제한 실험실 환경이 아니다.

'''+ '\n'.join(table)+'''

분산 배치에서 후보를 줄일 수 있지만 밀집 배치는 후보 수가 줄지 않는다. Grid는 밀집 시 최악 O(N²)가 남으며 구축/해시 비용이 추가된다.
이 측정에는 CanSee·진영 정렬·Protobuf·소켓·DB·실제 전투가 없다. 서버 전체 개선율이나 RTT 개선이라고 소개하지 않는다.
10/16의 1,500명 시연·64명 RTT를 이번 표와 개선 전후로 직접 비교하지 않는다.

## 지표와 재현

- spatial_build_us: 좌표 수집 및 인덱스 구축의 구간 누계.
- spatial_select_us: 선택한 방식의 검색·시야·진영 정렬 누계. 검증용 반대 검색 제외.
- snapshot_send_us: 메시지 구성·직렬화·송신 큐 적재 누계. 소켓 전송 제외.
- snapshot_p95_us/max_us: Snapshot 호출 전체. 검증 옵션이 켜지면 추가 대조 비용 포함.
- spatial_candidates: 선택한 방식의 거리 검사 횟수. 자기 자신 포함.
- spatial_verified: 성공한 최종 목록 대조 횟수. 성능 측정 시 VerifySpatialSearch=0.

```powershell
.\\test-spatial.ps1 -Configuration Release
.\\test-spatial-live.ps1 -Configuration Release
.\\Run64\\Release\\MultiThreadServer.exe --spatial-check --csv .\\spatial-benchmark.csv
```

다음 측정은 동일 설정·데이터에서 검증 옵션을 끄고 서버 전체 RTT·틱·큐·CPU·메모리를 반복 비교하는 것이다.
공격/스킬의 대상 검색, CanSee의 탐지 순회와 전투 스레드 소유권은 이번 변경에서 유지했다.
'''
(HERE/'spatial-comparison.md').write_text(text,encoding='utf-8')
print(json.dumps({'benchmark':summary['benchmark'],'live':summary['live']},ensure_ascii=False,indent=2))
