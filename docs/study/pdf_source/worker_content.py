'워커 비교의 실제 원본 표본 요약을 PDF에 연결한다. 미측정 효과를 추정해서 쓰지 않는다.'
from build_study_pdfs import *

RW='docs/results/2026-10-19/workers/'


def results():
    return json.loads((ROOT/RW/'summary.json').read_text(encoding='utf-8'))


def item(s,n,d):
    return next(x for x in s['configurations'] if (x['network'],x['db'])==(n,d))


def number(x,decimals=2):
    return f"{x['median']:,.{decimals}f}"


def interval(x,decimals=2):
    return f"{number(x,decimals)}\n({x['minimum']:,.{decimals}f}~{x['maximum']:,.{decimals}f})"


def worker_guide():
    s=results(); overall=[item(s,n,d) for n,d in ((1,1),(2,2),(4,6))]
    db=[item(s,4,d) for d in (1,2,6)]; net=[item(s,n,2) for n in (1,2,4)]
    gain=db[-1]['session']['throughput']['median']/db[0]['session']['throughput']['median']
    rejected=sum(sum(x['fixed-combat']['rejected']['runs']) for x in s['configurations'])
    return [
Page('워커 수의 효과를 분리해서 측정','37 / 6개 설정 × 3회 · 전장 스레드 분할과 구별',[
P('이 비교는 헤더 축소 전 8바이트 실행 파일의 결과입니다. Network/Database WorkerCount만 바꾸고 Grid를 고정했습니다. 현재 4바이트 코드의 재측정값은 아닙니다. World 전투는 메인이 소유하며, 1/1도 메인·기록·Redis가 별도로 있는 멀티스레드 구성입니다.'),
TableBlock(['비교','실행 설정: 네트워크 / DB'],[['전체 구성','1/1 → 2/2 → 4/6'],['네트워크 분리','1/2 → 2/2 → 4/2. DB=2 고정.'],['DB 분리','4/1 → 4/2 → 4/6. 네트워크=4 고정.'],['반복·순서','중복 조합을 합쳐 6개, 각 3회 = 18회. 회차 내 순서를 미리 섞고 2회차는 전투/Ping 순서도 교대.']],[1.2,3.8]),
TableBlock(['구간','동일하게 유지한 부하'],[['세션','기존 계정 96개, 동시 작업 32개. Game A 로그인 → World 이동 → 로그아웃. 먼저 계정을 만들고 별도 계정으로 경로를 예열.'],['Ping','96연결, 연결당 목표 상한 100회/초, 워밍업 3초 + 측정 20초.'],['이동·공격','96명, 연결당 목표 상한 20회/초, 워밍업 3초 + 측정 20초. 이동/고정 상대 공격을 교대.']],[1.2,3.8]),
Note('조건과 표본','Release·동일 PC·같은 서버 exe. 원본 게임 로직은 유지하고 더미 계측만 보완했습니다. HP=100000·속도=100·사거리=8, 종족당 한 유닛·스킬 없음인 시험 사본으로 인원 감소를 막습니다. Redis/리플레이/20Hz/Grid는 모든 설정에서 동일합니다. 요청은 응답을 기다리는 closed-loop이며 실제 전송률을 함께 기록합니다.')
],[Ref(RW+'matrix/plan.json'),Ref(RW+'run_benchmark.py'),Ref(D+'LoadClient.cpp','fixed-combat'),Ref(D+'SessionLoad.cpp','int RunSessionLoad')]),
Page('세 가지 구성의 결과를 한 표로 읽기','38 / 워커 전체 구성 · 반복 범위',[
P(f"총 {s['trials']}회, 세션 흐름 {s['sessions']:,}건과 Ping/이동·공격 응답 {s['requests']:,}개를 확인했습니다. 더미 오류 {s['errors']}이며, 워밍업은 응답 지연 분포에서 제외했습니다. 아래는 실행별 지표 3개의 중앙값이며 괄호는 최소~최대입니다."),
TableBlock(['N / DB','세션 완료/초','로그인 p95 ms','전투 p99 ms'],[[f"{x['network']} / {x['db']}",interval(x['session']['throughput']),interval(x['session']['p95_ms']),interval(x['fixed-combat']['p99_ms'])] for x in overall],[.6,1.3,1.6,1.5]),
Picture(RW+'overall-workers.png','같은 목표 입력에서도 실제 처리량과 지연은 서로 다른 지표입니다. 점 3개는 각 반복 결과입니다.',210),
Note('무엇을 결론낼 수 있나','이 표는 두 워커 수를 동시에 바꾼 전체 구성 비교입니다. 원인 설명은 다음 두 페이지의 고정 조건 비교를 사용합니다. 워커 증가 배수와 성능 증가 배수가 같다고 가정하지 않습니다. 반복 p95를 평균내 전체 요청의 p95라고 부르지 않으며, 여기서는 실행별 p95의 중앙값을 표시합니다.')
],[Ref(RW+'summary.json'),Ref(RW+'comparison.csv'),Ref(RW+'trials.csv')]),
Page('DB 쓰레드(워커)를 늘렸을 때의 실제 효과','39 / 네트워크 4 고정 · 로그인과 이동',[
Picture(RW+'db-workers.png','왼쪽은 로그인→World 이동→로그아웃 완료 흐름/초, 오른쪽은 JoinGame 요청 왕복의 실행별 p95입니다.',220),
TableBlock(['DB 쓰레드(워커)','완료 흐름/초','로그인 p95 ms','이동 p95 ms'],[[str(x['db']),interval(x['session']['throughput']),interval(x['session']['p95_ms']),interval(x['session']['transfer_p95_ms'])] for x in db],[.7,1.3,1.5,1.5]),
P(f"네트워크 4 고정에서 DB 1→6의 완료 처리량 중앙값 비율은 {gain:.2f}배입니다. 이 시험은 로그인당 PBKDF2-SHA256 600,000회 계산과 ODBC 작업을 같은 DB 쓰레드(워커)가 수행합니다. 따라서 SQL 엔진만 {gain:.2f}배 빨라졌다는 뜻이 아닙니다."),
Note('큐와 CPU를 함께 보기','많은 로그인 작업이 들어오면 한 워커가 해시 계산을 하는 동안 다음 작업이 대기합니다. 워커를 늘리면 이 작업들이 겹쳐 수행될 수 있습니다. 지연·처리량과 프로세스 CPU를 함께 확인하되, 큐의 대기 시간을 직접 계측한 결과와 혼동하지 않습니다. SessionLoad의 LoginMs와 TransferMs는 서로 다른 경계입니다.')
],[Ref(S+'DatabaseWorker.cpp','BCryptDeriveKeyPBKDF2'),Ref(D+'SessionLoad.cpp','loginStarted'),Ref(RW+'summary.json')]),
Page('네트워크 쓰레드(워커)와 전투 응답을 분리','40 / DB 2 고정 · Ping과 메인 경유 요청',[
Picture(RW+'network-workers.png','Ping은 네트워크 쓰레드(워커)에서 응답하며, 이동·공격은 메인 게임 큐와 판정을 거칩니다.',220),
TableBlock(['N 워커','Ping 응답/초','Ping p95 ms','전투 p95 ms'],[[str(x['network']),interval(x['ping']['throughput'],0),interval(x['ping']['p95_ms'],3),interval(x['fixed-combat']['p95_ms'])] for x in net],[.7,1.3,1.5,1.5]),
P(f'연결당 목표 상한은 Ping 100회/초, 전투 20회/초입니다. 응답 대기·Sleep·OS 스케줄링 때문에 실측 요청률은 상한과 다릅니다. 전투 RTT에는 허용과 거부 응답을 모두 포함합니다. 이동은 전부 허용됐고 공격 거부는 총 {rejected:,}건입니다. 거부 이유별 건수는 수집하지 않았으며 통신 오류와 구분합니다.'),
Note('해석 범위','메인 큐·게임 판정·Snapshot 비용은 그대로입니다. CPU가 낮아도 대기는 남을 수 있으며 구간별 대기를 직접 재지 않아 병목은 확정하지 않습니다. 반복 범위가 겹치는 전투 수치로 일정한 개선을 주장하지 않습니다. 전장 병렬화나 최대 수용 인원 시험도 아닙니다.')
],[Ref(S+'TcpServer.cpp','void RunTcpServer'),Ref(S+'GameLogic.cpp','void CGameLogic::Update'),Ref(RW+'summary.json')]),
Page('CPU·틱·정합성으로 결과를 확인','41 / 프로세스 계측과 남은 한계',[
P('CPU는 프로세스 누적 CPU 시간 차이 ÷ 측정 시간입니다. 100%는 논리 CPU 한 개를 계속 쓴 양이며, 이 PC 전체 비율은 16으로 나눕니다. 아래 CPU 값은 실행 3회의 중앙값입니다. 열마다 측정 단계가 다르므로 서로 더하지 않습니다.'),
TableBlock(['N/DB','A CPU %\n세션 단계','World CPU %\n전투 단계','더미 CPU %\n전투 단계','Tick 최대 ms\n전투 3회 중'],[[f"{x['network']}/{x['db']}",number(x['session']['resources']['game_a_cpu_one_core_pct'],1),number(x['fixed-combat']['resources']['world_cpu_one_core_pct'],1),number(x['fixed-combat']['resources']['dummy_cpu_one_core_pct'],1),f"{x['fixed-combat']['resources']['tick_max_ms']['maximum']:.3f}"] for x in s['configurations']],[.6,1.05,1.15,1.1,1.1]),
TableBlock(['확인한 것','검증 범위'],[['동일 조건','워커 수·Realm·임시 포트를 제외한 설정 동일. Unit/Skill/Map/StartPoint 파일 해시 동일.'],['원본 표본','응답 수·허용 횟수·클라이언트 번호·순서·워밍업 제외를 집계와 대조.'],['정상 종료와 저장','18회 정상 종료·리플레이 검증. Redis/XML/SQL 3,672명 점수 일치. 사망 0·각 진입 1. 예열 12+세션 96+전투 96명씩.'],['재현 파일','원본 표본, 실행별 CSV, 설정, 로그, exe/소스 해시를 workers 폴더에 보관.']],[1.2,3.8]),
Note('측정 오차와 다음 과제','CPU 경계는 로그 마커 관측에 약 50ms 오차가 있고 메모리는 주기 표본의 최대입니다. 틱·큐는 단계 내부의 1초 구간만 사용합니다. SQL/Redis 프로세스 CPU는 제외했습니다. 동일 PC 경쟁·closed-loop·3회 반복 한계가 남습니다. 로그인과 전투의 동시 폭주, 별도 부하 PC, 장시간·개방형 요청·전장 병렬화는 이후 과제입니다.')
],[Ref(RW+'resources-summary.csv'),Ref(RW+'sql-validation.csv'),Ref(RW+'redis-replay-validation.json'),Ref(RW+'environment.json')])]


def worker_questions():
    from qa_content import Pair
    s=results(); one=item(s,4,1); many=item(s,4,6)
    gain=many['session']['throughput']['median']/one['session']['throughput']['median']
    return [
Pair('25 / Q49-Q50','멀티스레드 효과를 수치로 설명',
'Q49. 워커 수를 늘린 효과를 실제로 측정했나요?',
f"네트워크/DB 1/1·2/2·4/6과 원인 분리용 설정을 합쳐 6개를 각 3회 실행했습니다. 같은 exe와 데이터, 96명 요청 패턴을 유지했습니다. 네트워크 4 고정에서 DB 1→6의 세션 처리량 중앙값은 {number(one['session']['throughput'])}→{number(many['session']['throughput'])}건/초로 약 {gain:.2f}배였습니다. 이 결과를 전체 게임 처리량 개선으로 확대하지 않습니다.",
'“정말 DB만 빨라졌나?” 로그인 워커는 비밀번호 해시와 ODBC를 함께 실행합니다. 순수 SQL 비교가 아닌 로그인·이동 흐름 비교입니다.',
'Q50. 1/1과 4/6은 싱글스레드 대 멀티스레드인가요?',
'아닙니다. 1/1도 메인·기록·Redis 스레드가 별도이고 프로세스도 네 개입니다. 비교 대상은 네트워크·DB 쓰레드(워커) 수입니다. Grid는 공간 후보 검색 방식이고 전투 스레드 분할 옵션이 아닙니다. World의 전투 상태는 현재 메인 한 스레드가 소유합니다.',
'“전장 병렬화 효과는?” 이번 결과로 답할 수 없습니다. 구역 경계 전투·객체 소유권·이동·동기화 정책을 먼저 설계하고 별도 검증해야 합니다.',
'원본 세션 표본 LoginMs/TransferMs와 CDatabaseWorker::Run을 함께 보면 측정 경계와 작업 내용을 설명할 수 있습니다.',
[Ref(RW+'summary.json'),Ref(D+'SessionLoad.cpp'),Ref(S+'DatabaseWorker.cpp','void CDatabaseWorker::Run')]),
Pair('26 / Q51-Q52','공정한 비교와 해석의 한계',
'Q51. 우연히 빨랐던 한 번을 골랐나요?',
'설정 6개를 3회씩 실행하고 회차 내 순서를 미리 섞었습니다. 원본 응답 표본으로 실행별 p95/p99를 다시 계산하고, 세 실행의 중앙값과 최소~최대를 모두 표시했습니다. 가장 좋은 실행 하나를 대표값으로 선택하지 않았습니다. 같은 PC의 일반 앱 부하와 SQL 캐시를 완전히 통제한 실험실 시험은 아닙니다.',
'“p95 세 개의 평균이 전체 p95인가?” 아닙니다. 문서 표는 실행별 p95의 중앙값이고, 전체 표본의 p95와 구별합니다.',
'Q52. 96명 시험과 1,500명 시연을 어떻게 연결하나요?',
'96명 시험은 같은 인원을 유지하려고 고정 유닛·높은 HP·스킬 없음 사본을 사용한 워커 비교입니다. 1,500명 시연은 원본 유닛·스킬과 사망·100초 재입장을 포함합니다. 부하와 목적이 달라 두 결과를 그대로 개선 전후로 묶지 않습니다. 전자는 워커 효과, 후자는 대규모 기능과 저장 정합성의 근거입니다.',
'“오류 0이면 안정성 보증인가?” 관측한 입력과 시간에서 오류가 없었다는 뜻입니다. 긴 실행, 다른 PC, 입력 폭주, 실제 인터넷 응답까지 보장하지 않습니다.',
'CPU 100%는 논리 CPU 한 개입니다. 표는 헤더 축소 전 8바이트 버전의 결과이며, 현재 4바이트 버전이나 헤더 축소의 개선율을 측정한 값이 아닙니다.',
[Ref(RW+'matrix/plan.json'),Ref(RW+'comparison.csv'),Ref(RF+'summary.json')])]


def worker_practice():
    return [
Page('워커 비교를 다시 실행하고 표 읽기','16 / 반복 측정의 재현 절차',[
P('가이드 39-43쪽의 수치는 헤더 축소 전 8바이트 버전입니다. 아래 명령으로 현재 코드를 빌드하면 4바이트 버전의 새 측정이 됩니다. 원본 Server.ini 대신 실행 사본을 만들며, 결과 폴더의 설정에서 WorkerCount를 확인합니다.'),
Code('.\\build.ps1 -Configuration Release -RunDbCheck\n.\\test-workers.ps1 -Pilot\n.\\test-workers.ps1'),
P('기본 실행은 6개 설정×3회입니다. 스크립트는 새 결과 폴더를 사용하고 이전 결과를 덮어쓰지 않습니다. 같은 프로젝트의 다른 서버·부하 시험을 멈춘 상태에서 시작하고, 측정 중 빌드를 겹치지 마세요. 기존 로컬 Redis와 project_db에 격리 Realm으로 기록합니다.'),
TableBlock(['파일','확인할 값'],[['matrix/plan.json','미리 정한 순서·exe 해시·부하 조건'],['comparison.csv','구성/구간별 중앙값·최소·최대'],['trials.csv','각 반복의 p95/p99·완료 처리량·허용/거부'],['resources-summary.csv','서버/더미 CPU, 관측 틱·큐·메모리'],['구성 폴더의 원본 표본','*-samples.csv 또는 .csv.gz, session.csv'],['sql-validation.csv','18회 종료 결과와 인원 유지 여부']],[2,3]),
Note('기존 테스트와 구별','--load combat은 주변 상태에 따라 판단하는 기존 동작 시험입니다. 이번 fixed-combat은 이동과 지정 상대 공격을 번갈아 보냅니다. SessionLoad는 기존 계정 로그인·이동을 따로 재며, seed와 워밍업은 최종 표에서 제외합니다.')
],[Ref('test-workers.ps1'),Ref(RW+'run_benchmark.py'),Ref(D+'LoadClient.cpp'),Ref(D+'SessionLoad.cpp')]),
Page('측정 표를 내 설명으로 바꾸기','17 / 수치·근거·한계를 연결하는 실습',[
Flow([('1. 전체 구성','1/1·2/2·4/6의 세션 처리량과 전투 p99를 읽는다.'),('2. 원인 분리','DB=2 또는 네트워크=4로 고정한 표만 따로 읽는다.'),('3. 반복 범위','중앙값과 세 실행의 점을 보고 차이가 일관적인지 확인한다.'),('4. 코드 경계','PBKDF2·ODBC와 네트워크 응답/메인 큐의 위치를 짚는다.'),('5. 남은 검증','고정 인원, closed-loop, 같은 PC의 제한을 말한다.')]),
TableBlock(['피할 설명','근거에 맞는 설명'],[['“DB가 배수만큼 빨라졌다”','로그인 해시+ODBC+이동 흐름의 완료 처리량 변화다.'],['“1/1은 싱글스레드다”','네트워크·DB가 각 1개이며 다른 스레드는 별도로 있다.'],['“Grid가 전투를 병렬 처리한다”','주변 후보 탐색 방식만 바꾸며 전투는 메인이 소유한다.'],['“목표 전송률이 곧 처리량이다”','closed-loop이므로 지연에 따라 실측 요청률이 내려간다.'],['“p95가 작으니 모든 요청이 빠르다”','p99·최댓값·반복 범위와 오류/거부도 같이 본다.']],[1.8,3.2]),
P('면접 Q49~52를 소리 내어 설명한 뒤, 비교 표의 숫자 하나를 trials.csv와 원본 표본까지 거슬러 확인하세요. 표의 근거를 직접 찾는 연습이 완성된 문장을 외우는 것보다 도움이 됩니다.'),
Note('최종 학습 순서','가이드에서 소유권과 호출 흐름 → 이 워크북으로 실제 실행/CSV 확인 → 면접 답변 연습. 1,500명 시연과 96명 비교는 서로 다른 증거로 발표합니다.')
],[Ref(RW+'comparison.csv'),Ref(RW+'analyze.py'),Ref(S+'DatabaseWorker.cpp','PasswordHash')])]
