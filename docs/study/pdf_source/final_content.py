'10/19 최종 실행의 원본 요약을 읽는다. 숫자를 본문에 따로 복사하지 않는다.'
from build_study_pdfs import *


def final_result():
    return json.loads((ROOT/RF/'summary.json').read_text(encoding='utf-8'))


def final_guide():
    s = final_result(); r = s['result']; m = s['metrics']; match = s['match'][0]
    return [
Page('Redis를 켠 최종 1,500명 전투','35 / 2026-10-19 · 실행 조건과 관측',[
P('헤더 축소 전 8바이트 Release로 A/B/C 각 500명이 World에 입장한 뒤 240초 전투했습니다. 현재 4바이트 코드의 재측정값은 아닙니다. 원본 유닛·스킬과 사망 후 100초를 유지했고, Redis는 로컬 6379의 별도 battle_ Realm을 사용했습니다.'),
TableBlock(['조건','실제 실행값'],[['PC / 프로세스','Ryzen 7 7700X, 8코어/16스레드. Game 3 + World 1 + 대규모 더미 1. MSSQL/Redis도 같은 PC.'],['서버별 워커','네트워크 4, DB 6. World에 기록 1, Redis 1 추가. 더미는 8개 워커.'],['주기 / 검색','전투 20Hz, 기록 5Hz, Grid 셀 25 / 반경 25. Redis 갱신 1초.'],['시연 사본 변경','본진 HP 750,000. 재입장 관찰 시간을 확보하며 원본 파일은 유지.']],[1.2,3.8]),
TableBlock(['결과','실측'],[['준비 / 최대 동시 World / 로그아웃',f"{r['Ready']:,} / {r['PeakWorld']:,} / {r['LoggedOut']:,}"],['사망 / 사망 후 재입장',f"{r['Deaths']:,} / {r['DeathReentries']:,}"],['일반 유닛 공격 / 스킬 성공',f"{r['UnitAttacks']:,} / {r['Skills']:,}"],['정상 거부 응답 / 더미 오류',f"{r['Rejected']:,} / {r['Errors']} (거부 이유별 수량은 미분류)"]],[2.5,2.5]),
Note('완료와 시간의 의미','시간 제한 후 정상 종료한 server_shutdown 기록이며 승패 완료 경기는 아닙니다. 사망 대기자는 전장에서 빠집니다. 리플레이 최소 재입장 99.6초는 틱 기반 시간으로 생략 틱·200ms 기록 묶음의 영향을 받습니다. 실제 100초 제한은 steady_clock으로 검사하며 이 수치로 실시간 대기를 입증하지 않습니다.')
],[Ref(RF+'summary.json'),Ref(RF+'Server.ini'),Ref(RF+'result.txt'),Ref('Launch/StartBattle.ps1')]),
Page('전원 기록 대조와 지연 수치의 범위','36 / Redis · XML · MSSQL · 틱 비용',[
P(f"정상 종료 후 Redis 3개 Sorted Set의 전원 점수를 XML과 대조하고, XML의 ID·계정·서버·세 총계·종료 상태를 MSSQL {s['sql_rows']:,}행과 비교했습니다. 모든 행이 일치했습니다. Hash도 같은 {s['sql_rows']:,}명의 표시 정보를 보관합니다."),
TableBlock(['전원 합계','킬','죽음','진입'],[['Redis = XML = MSSQL',f"{match['kills']:,}",f"{match['deaths']:,}",f"{match['entries']:,}"]],[2,1,1,1]),
TableBlock(['World 로그 지표','관측값'],[['5초 구간 tick p95 중 최댓값',f"{m['max_interval_tick_p95_us']/1000:.3f} ms"],['개별 Tick 최댓값 / 생략 틱',f"{m['max_tick_us']/1000:.3f} ms / {m['skipped_ticks']:,}"],['개별 Snapshot 최댓값',f"{m['max_snapshot_us']/1000:.3f} ms"]],[3,2]),
TableBlock(['Redis 상위 10명 직접 조회','샘플 수','p95 / 최대 ms'],[[metric,str(q['samples']),f"{q['p95_ms']:.4f} / {q['max_ms']:.4f}"] for metric,q in s['redis_queries'].items()],[2.2,1,1.8]),
P('Redis 표는 1,500개 점수가 게시된 이후 5초마다 기존 TCP 연결로 보낸 ZREVRANGE 왕복입니다. 게임 Rank 패킷, 메인 요청 큐, HGET 표시 정보 수신을 합친 지연은 아닙니다. World 지표는 준비·정리도 포함하며, 구간 p95의 최댓값을 전체 실행 p95라고 부르지 않습니다.'),
Note('남은 한계','이 1,500명 시연에는 Redis OFF 대조군과 반복 시험이 없어 이전 버전 대비 개선율을 계산하지 않습니다. 고정 96명의 워커 반복 비교는 39-43쪽의 별도 시험입니다. 50ms 예산 초과와 진행 경기 강제 종료 복원은 남은 과제입니다. 소규모 랭킹 시험으로 승패 종료·빈 Redis 복구·SQL 멱등성도 다시 검증했습니다.')
],[Ref(RF+'summary.json'),Ref(RF+'redis-samples.csv'),Ref(RF+'sql-match-stats.csv'),Ref(RF+'ranking-release-test.txt')])]


def final_practice():
    return [
Page('최종 실행을 다시 열고 근거 확인하기','14 / 실행 폴더와 최종 결과',[
P('코드를 바꾸기 전에 이번 기록을 먼저 확인하세요. 최종 실행은 10/19 Redis 활성 Release, A/B/C 각 500명, 준비 완료 후 240초 전투입니다. 리플레이는 Run64/Release/Replays에 Battle-1500- 접두사로 남았습니다.'),
Code('cd .\\Run64\\Release\n.\\DummyClient.exe --replay-list\n.\\StartReplay.cmd\n# New run: isolated Realm and ports\n.\\StartBattle.ps1 -PerServer 500 -Seconds 240 -NoReplay'),
TableBlock(['증거 파일 (최종 결과 폴더)','직접 확인할 것'],[['result.txt','준비·최대 인원·사망·재입장·오류·로그아웃.'],['Server.ini / Data/Map.ini','Redis 활성, Grid, 100초 대기와 시연용 본진 HP.'],['redis-samples.csv','누적 참가 1,500명 이후 점수 변화와 직접 조회 지연.'],['redis-xml-validation.json / sql-match-stats.csv','Redis·XML·SQL의 전원 최종 점수가 일치하는가.'],['world-metrics.csv / summary.json','50ms 예산 초과와 구간 p95의 의미.'],['manifest.json / replay-verification.txt','실행 파일·소스 해시와 정상 마감·행동 기록.']],[2.7,2.3]),
Small('결과 폴더: docs/results/2026-10-19/redis-final/\nrun-directory.txt가 원본 실행 폴더를 가리킵니다. 실행마다 Realm을 새로 만들어 시연끼리 기록을 분리합니다. test-ranking.ps1의 짧은 격리 시험은 대규모 시뮬레이션과 별도로 실행합니다.'),
Note('숫자 맞추기 실습','더미 Entered와 SQL Entries 합계가 같은지, Deaths와 SQL Deaths가 같은지 확인하세요. 아군 스톰 사망은 죽음만 늘 수 있으므로 킬 합계와 죽음 합계가 반드시 같지는 않습니다.')
],[Ref(RF+'summary.json'),Ref(RF+'manifest.json'),Ref(RF+'run-directory.txt'),Ref('Launch/StartBattle.ps1')]),
Page('Redis Insight와 코드 학습 마무리','15 / GUI 관찰 → 코드 추적 → 말하기',[
P('Redis Insight 3.4.2를 설치해 Portfolio Redis 이름으로 127.0.0.1:6379에 연결했습니다. Browse에서 아래 검색어를 입력하고 키를 선택하면 Member는 PlayerId, Score는 해당 횟수로 표시됩니다. Score 열을 내림차순으로 정렬하세요.'),
Code('portfolio:*:kills\nportfolio:*:deaths\nportfolio:*:entries'),
P('여러 경기 결과가 함께 있으므로 이번 Server.ini의 Database.Realm과 summary.json의 경기 ID를 대조합니다. :players 값은 Protobuf 바이너리입니다. 계정 이름과 세 총계를 함께 보려면 World의 rank 명령을 사용하거나 SQL 결과 CSV를 읽으세요. TTL 기본값은 마지막 갱신부터 7일입니다.'),
Flow([('1. 통신과 소유권','가이드 3-9쪽 → ServerVersionCheck/Move를 패킷부터 메인까지 추적'),('2. 계정과 이동','가이드 16-20쪽 → DB 완료와 Logout/Login 경계 관찰'),('3. 전투와 기록','가이드 10-13, 22-25쪽 → 죽음/복귀/리플레이 연결'),('4. 순위와 검증','가이드 33-43쪽 → Redis 집계와 워커 비교의 수치'),('5. 자신의 설명','면접 Q02·Q07·Q15·Q34·Q43·Q48~52를 코드 없이 말하기')]),
Note('학습 완료 기준','정상 경로 한 개와 실패 경로 한 개를 실제 함수 이름으로 설명하고, 이번 수치가 무엇을 측정하지 않았는지 말할 수 있으면 됩니다. 숫자를 외우기보다 데이터를 확정하는 주체와 스레드 사이 전달 값을 확인하세요.')
],[Ref(S+'GameRanking.cpp','void CGameLogic::RequestRanking'),Ref(S+'RedisWorker.cpp','wire::RankReply Read('),Ref(RF+'summary.json')])]
