from build_study_pdfs import *

def practice():
    pages = [
Page('학습과 검증을 결과물로 남기는 순서','START / 실행 가능한 준비 계획',[
P('현재 코드는 4바이트 헤더, 시스템 1~4·예약 5~9·게임 10~32를 사용합니다. 접두어와 번호 규칙은 가이드 8쪽에서 확인하세요. 이 책의 기존 성능 표는 과거 기록입니다. 공간 검색은 13쪽, Redis는 14-17쪽, 과거 워커 비교는 18-19쪽입니다. 새 IOCP·전투 쓰레드(워커) 실습은 20쪽, 최종 수치는 코드 가이드 49-51쪽, 설정별 동작은 별도 04 설명서입니다.'),
TableBlock(['단계','할 일','남길 결과'],[['1','코드 학습 가이드 3-10쪽\nMain → TCP → 명령 큐','스레드 소유권 그림 한 장'],['2','Login → DB → World 이동 디버깅','호출 스택·상태 전이 표'],['3','Move/Attack → Tick → Snapshot','요청/판정/상태 구분 메모'],['4','실패 경로·리플레이 검사','재현 명령·PASS 기준·로그'],['5','조건 고정 스트레스 측정','설정·해시·CSV·지표·한계'],['6','발표 구성과 Q&A 연습','10장 요약 자료·10분 설명']],[.65,2,2]),
H('이 자료에서 사용하는 경로'),
Small('루트: D:/work/Josun_Server/git_ProJect\nS/ = MultiThreadServer/MultiThreadServer/src/\nD/ = DummyClient/src/ · R/ = docs/results/2026-10-16/\n테스트 스크립트는 루트에서, exe·StartBattle·StartReplay 실행은 Run64/Release 또는 Debug에서 합니다.'),
Note('학습 완료 기준','함수 이름을 암기하는 대신 데이터의 주인을 설명하세요. 코드 없이 그린 흐름도가 실제 호출 순서와 일치하고, 실패 상황 하나를 재현하며, 수치의 측정 조건을 말할 수 있으면 다음 단계로 넘어갑니다.')
],[Ref('README.md'),Ref('test-all.ps1')]),

Page('실습 1: 로그인에서 World까지 멈춰 보기','01 / Debug 빌드의 중단점',[
P('Visual Studio에서 Debug x64 빌드로 진행합니다. 먼저 Game A와 World를 실행하고 디버거를 필요한 서버 프로세스에 연결합니다. 긴 중단은 다른 프로세스의 티켓·lease를 만료시킬 수 있으므로, 한 번에 한 경계씩 짧게 관찰하거나 로그포인트로 값을 남기세요.'),
TableBlock(['중단점','관찰할 값','확인할 질문'],[['CGameLogic::Login','account, p_definition 후보, databaseToken','DB 저장값이 오기 전 무엇을 임시로 잡나?'],['CDatabaseWorker::Submit / Run','job.id, action, token','메인과 DB 쓰레드(워커)의 thread ID가 다른가?'],['CGameLogic::UpdateDatabase','result.id, pending.kind, token','어떤 조건을 통과한 뒤 로그인 응답을 보내나?'],['RequestTransfer','목적지, nonce, pending ticket','DB Transfer 전에 티켓이 응답되는가?'],['AcceptTransfer / Enter','claims, destination, unitIndex','World는 무엇을 다시 검증하나?']],[1.7,1.5,2]),
H('직접 작성할 상태 표'),
P('“Game 로그인 → Transfer 저장 → 티켓 응답 → World 연결 → Accept 저장 → 전장 참가” 여섯 행을 적고 각 행에 실행 프로세스, 스레드, DB Location, token 변경 여부를 채우세요. 비밀번호·공유 키·실제 티켓 서명은 학습 기록에 복사하지 않습니다.'),
Note('관찰 성공 기준','원래 Game의 콘텐츠 로그아웃과 World의 새 로그인 사이에 DB의 Transfer 상태가 있음을 설명할 수 있어야 합니다. 소켓 연결 유무와 콘텐츠 로그인 상태를 같은 값으로 생각하지 않는 것이 핵심입니다.'),
Small('발전 과제: DB 결과가 돌아오기 전에 더미 연결이 끊겼다고 가정하고 UpdateDatabase/연결 정리 코드가 어느 token을 대상으로 처리하는지 종이에 추적하세요. 장애 재현은 이후 test-recovery의 격리 환경에서 수행합니다.')
],[Ref(S+'GamePersistence.cpp','void CGameLogic::Login'),Ref(S+'GamePersistence.cpp','void CGameLogic::AcceptTransfer'),Ref(S+'DatabaseWorker.cpp','void CDatabaseWorker::Run'),Ref('test-recovery.ps1')]),

Page('실습 2: Move와 Attack의 생애 추적','02 / 요청과 확정 상태',[
P('대화형 더미로 World에 들어간 뒤 auto off로 자동 전투를 끄고 status를 확인합니다. 현재 맵의 건물 밖 좌표 하나를 골라 move X Y를 보냅니다. 예를 들어 중심 근처 60 60은 현재 데이터에서 목표로 사용할 수 있습니다.'),
Flow([('더미 MoveTo','MoveRequest 직렬화, Type::CM_Move=18'),('워커 HandlePackets','수신 프레임 파싱, Submit에 본문 복사'),('메인 HandlePacket','IsValidPosition 후 goalX/goalY 설정, 즉시 ActionReply'),('메인 Tick','속도에 맞춰 실제 x/y 갱신'),('Snapshot / CaptureReplay','시야 전송과 전체 기록이 각 간격에 맞춰 진행')]),
H('세 개의 값을 비교하세요'),
P('① 요청한 목표 좌표 ② ActionReply를 받은 순간의 서버 좌표 ③ 다음 Snapshot 좌표를 따로 적습니다. “Move accepted”는 목표 수락이며 도착 완료가 아닙니다. 공격도 요청·허용·피해 후 상태·사망 복귀 순서가 연결됩니다.'),
TableBlock(['추가 입력','예상 결과'],[['move -1 10','서버 경계 검사에서 거부. 음수 좌표를 정상 상태로 저장하지 않음.'],['사거리 밖 대상 attack','ActionReply 거부. HP 감소 없음.'],['동일 진영 대상 attack','자기 진영 공격 거부.'],['배정되지 않은 skill 슬롯','빈 슬롯 또는 범위 오류. 임의 스킬 실행 안 됨.']],[1.5,3.4]),
Note('관찰 성공 기준','Move 응답과 WorldState를 구별하고, 네트워크 쓰레드(워커)가 stPlayer 좌표를 직접 수정하지 않는다는 것을 중단점의 스레드 ID로 확인하세요. 디버거 일시정지 때문에 생긴 타임아웃을 실제 성능 수치에 넣지 않습니다.')
],[Ref(D+'PlayClient.cpp','wire::ActionReply MoveTo'),Ref(S+'TcpServer.cpp','void HandlePackets'),Ref(S+'GameLogic.cpp','void CGameLogic::Tick')]),

Page('실습 3: 실패해도 남는 상태 확인','03 / 정원·중복·종료',[
TableBlock(['상황','관찰할 불변 조건','사용할 근거'],[['정원 초과 동시 로그인','허용 인원은 정원 이하. 거부된 예약이 새 로그인 슬롯을 계속 점유하지 않음.','test-capacity.ps1'],['동일 계정 중복 로그인','유효한 소유 세션은 하나. 늦은 Logout이 새 token의 세션을 닫지 않음.','계정/복구 검사와 SQL 조건'],['World 정원 초과','실패 World 세션에 남지 않고 복귀권으로 Game 재로그인 가능.','test-capacity.ps1'],['중계 서버 연결 단절','인증 없는 중계 불가. 재연결 뒤 인증을 다시 진행.','test-chat-relay.ps1'],['기록 쓰기 실패','성공 파일로 위장하지 않고 실패 지표/.part. 전투 응답 경로는 계속.','test-load -ReplayFailure'],['정상 종료','새 접속 차단 → 워커 합류 → 기록·DB 정리.','TcpServer/소멸자']],[1.2,2.4,1.6]),
P('시험은 자기 실행의 임시 포트·Realm·설정 사본을 사용하는 스크립트부터 읽고 실행하세요. 기존 test 스크립트가 남기는 출력 경로를 기록합니다. 개인 project_db에 만들어진 시험 계정은 조사용으로 남을 수 있으므로 정상 개발 계정과 Realm을 구별합니다.'),
Note('한 실패당 네 줄','① 재현 조건 ② 기대 결과 ③ 실제 로그/상태 ④ 통과 또는 남은 문제. “실행해 보니 되었다”를 이 형식으로 바꾸면 포트폴리오의 검증 페이지가 됩니다.'),
Small('시험 중 서비스 프로세스 전체를 이름으로 일괄 종료하지 마세요. 기존 스크립트는 자신이 시작한 PID를 관리합니다. 디버거 실습과 성능 실행도 별도 시간에 수행하세요.')
],[Ref('test-capacity.ps1'),Ref('test-recovery.ps1'),Ref('test-chat-relay.ps1'),Ref('test-load.ps1')]),

Page('현재 코드로 실행할 수 있는 명령','04 / 빌드·회귀·대화형 접속',[
P('아래 빌드·테스트 명령은 프로젝트 루트에서 실행합니다. PowerShell에서 .ps1 실행이 제한되는 환경에서는 기존 README의 powershell -NoProfile -ExecutionPolicy Bypass -File 형식으로 같은 스크립트를 실행할 수 있습니다. DB 연동 시험은 로컬 project_db 스키마 설치가 필요합니다.'),
Code('.\\build.ps1 -Configuration Debug -RunChecks\n.\\test-all.ps1 -Configuration Debug\n.\\test-all.ps1 -Configuration Release'),
Small('test-all은 내부에서 빌드와 기능 검사를 수행합니다. 위 세 줄을 매번 모두 돌릴 필요는 없습니다. 코드 읽기에는 Debug, 성능 비교에는 별도로 빌드한 Release를 사용합니다.'),
H('수동 접속은 서버 창과 더미 창을 나눕니다'),
P('다음 네 서버 명령을 각각 다른 콘솔에서 Run64/Debug를 작업 폴더로 실행합니다. A만 시험할 때는 A와 World부터 켜도 됩니다. 더미의 서버 선택은 해당 서버가 실제 수신 대기 중이어야 합니다.'),
Code('.\\MultiThreadServer.exe --server A\n.\\MultiThreadServer.exe --server B\n.\\MultiThreadServer.exe --server C\n.\\MultiThreadServer.exe --server World'),
P('다섯 번째 콘솔도 Run64/Debug에서 실행합니다. ID·비밀번호를 입력하고 A/B/C를 선택한 뒤 world, auto off, status 순서로 관찰합니다.'),
Code('.\\DummyClient.exe --play'),
Note('기존 연결 오류를 구별하기','10061은 수신 중인 서버가 없거나 해당 주소·포트에서 연결이 거절된 경우입니다. 서버 콘솔에 [LISTEN]이 유지되는지 확인합니다. 같은 콘솔에서 서버를 먼저 종료한 뒤 더미를 실행하면 접속할 수 없습니다.')
],[Ref('build.ps1'),Ref('test-all.ps1'),Ref(D+'Main.cpp','--play'),Ref(S+'Main.cpp')]),

Page('한 경기와 대규모 전투, 리플레이 실행','05 / 시연 목적에 맞게 선택',[
P('이 페이지의 명령은 Run64/Release에서 실행합니다. StartMatch10.cmd는 기존 30명 건물 공략 한 경기, StartBattle1500.cmd는 유닛 전투·사망 재입장을 포함한 시간 제한 시연입니다. 둘의 완료 조건이 다릅니다.'),
Code('.\\StartMatch10.cmd\n.\\StartBattle1500.cmd'),
TableBlock(['실행','주요 특징'],[['StartMatch10','Game 서버마다 10명, auto buildings, 마지막 본진 승리와 전원 복귀 검증.'],['StartBattle1500','서버마다 500명, 유닛 우선 전투, 사망 후 100초, 전투 240초. 종료 이유는 보통 server_shutdown.'],['작은 전투 점검','PerServer 10으로 시작해 흐름 확인. TestCooldownSeconds 3은 실행 사본의 시험용 대기.']],[1.2,3.6]),
Code('.\\StartBattle.ps1 -PerServer 10 -Seconds 55 `\n  -TestCooldownSeconds 3 -NoReplay\n.\\StartBattle.ps1 -PerServer 500 -Seconds 240'),
H('저장한 리플레이만 열기'),
Code('.\\StartReplay.cmd\n.\\DummyClient.exe --replay-list'),
P('목록에서 파일을 선택하고 Play를 누르면 1배속으로 계속 재생합니다. 처음 입장 준비 구간에는 유닛이 적거나 없을 수 있어 “첫 전투로” 이동을 사용합니다. 수동 파일 검증은 아래 예시의 파일명을 실제 목록 이름으로 바꿔 실행합니다.'),
Code('.\\DummyClient.exe --replay .\\Replays\\FILE.replay --verify'),
Note('실행 흔적','BattleRuns/<실행 시각-ID>에는 설정 사본·로그·원본 기록이 남습니다. Replays에는 완료 기록 사본이 생깁니다. 이전 파일을 새 실행 결과와 섞지 않도록 실행 ID를 기록하세요.')
],[Ref('Launch/StartMatch.ps1'),Ref('Launch/StartBattle.ps1'),Ref(D+'ReplayClient.cpp')]),

Page('그림 한 장에도 근거 설명을 붙이기','06 / Redis 포함 최종 1,500명 시연',[
Picture(RF+'battle-1500.png','2026-10-19 Redis 활성 최종 시뮬레이션의 기록 70.00초를 뷰어 그리기 함수로 내보낸 PNG입니다. Windows 창 캡처는 아닙니다.',330),
P('이 장면에는 전장 인원 1,185명(테란 390·프로토스 454·저그 341)이 표시됩니다. 최대 입장 1,500명과 특정 시점의 생존 인원은 다릅니다. ×인원 표시는 겹친 같은 진영 유닛을 묶은 표현이며 실제 전투 인원을 줄이지 않습니다.'),
TableBlock(['함께 적을 정보','현재 근거'],[['실행 조건','A/B/C 각 500명, 전투 240초, 사망 대기 설정 100초'],['검증 결과','사망 3,075회, 사망 재입장 1,692회, 오류 0'],['제한','건물 HP는 사본만 750,000. 건물 공격 340회. 시간 제한 종료로 winner=0.']],[1.1,3.5]),
Note('포트폴리오 캡션 예시','“1,500명 입장 이후 유닛 전투·사망 재입장 동작을 검증하고, 확정 상태를 오프라인 뷰어로 재생했다. 이 장면은 기록 70초이며, 성능 보증이나 경기 승리 장면이 아니다.”')
],[Ref(RF+'replay-verification.txt'),Ref(RF+'result.txt'),Ref('Assets/Replay/SOURCES.txt')]),

Page('스트레스 시험을 단계별로 나누기','07 / 아직 수행하지 않은 추가 계획',[
P('96명 고정 인원의 워커 반복 시험은 완료했으며 18-19쪽에서 재현합니다. 아래는 그 이후의 추가 계획입니다. 같은 PC에서는 더미·MSSQL·서버가 자원을 나눠 쓰므로 가능하면 부하 생성기를 별도 PC로 분리한 비교도 남깁니다.'),
TableBlock(['단계','조건과 목적','필수 지표'],[['기준선','기존 test-load Release를 다른 작업 없이 3회. Ping/이동/혼합을 분리.','실측 요청률, 허용/거부/오류, RTT p50/p95/p99'],['동시 인원 증가','30 → 150 → 300 → 750 → 1,500 전투 시연. 각 단계 준비 완료 후 관찰.','현재 World 인원, deaths/reentries, 큐 최대, 틱 비용'],['일정 인원 유지','96명 고정 시험을 출발점으로 더 큰 인원의 계측을 확장. 사망 재입장 시연과 구분.','실제 활성 인원과 요청률 유지 여부'],['장시간','작은 안정 조건부터 30~60분 이상 반복. 기존 실행기 시간 제한을 먼저 확인.','메모리 추세, 핸들·스레드 수, 기록 용량, 오류율'],['장애','연결 끊김·DB 지연·기록 실패를 각각 격리 재현.','복구 시간, 데이터 불변 조건, 미완료 처리']],[1.1,2.4,1.7]),
P('현재 LoadClient의 --clients는 최대 128이고 StartBattle의 --seconds는 최대 1800입니다. 기존 옵션에 1500 또는 3600을 넣으면 되는 것처럼 안내하지 않습니다. 더 큰 고정 인원 RTT 시험이나 긴 단일 실행은 도구·시나리오를 확장한 뒤 측정해야 합니다.'),
Note('예시 목표는 목표로 표시','“20Hz 목표 간격 50ms, 틱 비용 상위 구간과 큐가 지속 악화하지 않음” 같은 조건을 사전에 정합니다. 현재 1,500명 기록에는 50ms 초과가 있으므로 이 목표를 이미 달성했다고 표시하지 않습니다.')
],[Ref(D+'LoadClient.cpp','--clients'),Ref('Launch/StartBattle.ps1','ValidateRange(10,1800)'),Ref(R+'battle-1500-validation.txt')]),

Page('수치를 재현할 측정표와 개선 실험','08 / 성능 증거 작성',[
TableBlock(['매 실행 기록할 항목','작성 내용'],[['소스·실행물','기준 작업 트리/커밋, exe SHA-256, Debug/Release, 빌드 시각'],['환경','CPU·코어, 메모리, OS, DB 위치, 더미 위치, 실행 중 다른 작업'],['부하','현재 인원, 초당 목표/실제 요청, 성공·거부·오류 정의, 워밍업·측정 시간'],['설정','네트워크/DB 쓰레드(워커), TickRate, SnapshotRadius, 기록 주기·활성 여부'],['데이터','Unit/Skill/Map 사본, 임시 HP 변경, 로그 Realm, 실행 ID'],['출력','RTT CSV, 구간별 틱, 전체 루프·Snapshot 비용, CPU·메모리, 큐·파일 크기']],[1.2,3.8]),
P('기존 test-load.ps1은 측정 중 사망을 피하려고 사본의 HP=100000, MP/InitialMP=1000, MoveSpeed=100을 사용합니다. 기존 64명 숫자를 원본 전투 밸런스 결과라고 쓰지 않습니다. 대규모 시연은 원본 유닛 수치를 유지하고 건물 HP만 높였으므로 두 실행을 그대로 전후 비교할 수 없습니다.'),
H('완료한 변경과 다음 실험: Snapshot 후보 검색'),
Flow([('구현','SpatialSearch=Linear/Grid로 공통 규칙의 검색만 교체'),('계측','구축·선정·직렬화/큐 적재 시간과 후보 수 집계'),('검색 검증','같은 좌표 결과 대조, 120명 전투 목록 대조, 1,500점 검색 측정'),('다음 비교','Linear/Grid 각각의 서버 전체 RTT·틱·밀집/은신 비용')]),
Note('백분위 계산 주의','각 실행의 p95를 단순 평균해서 전체 샘플의 p95라고 부르지 않습니다. 원본 샘플을 합치거나 실행별 분포를 별도로 보고합니다. 현재 Tick 지표는 구간별 값이고 RTT는 응답 시간입니다.')
],[Ref('test-load.ps1'),Ref(D+'LoadClient.cpp','double Percentile'),Ref(S+'GameReplay.cpp','void CGameLogic::ReportMetrics'),Ref(R+'benchmark-binaries.txt')]),

Page('첨부 자료처럼 정리할 포트폴리오 구성','09 / 10장 발표 설계안',[
P('참고 PDF의 장점은 기능 나열보다 구조·수명·실행 경로·검증 증거를 연결하는 구성입니다. 아래는 현재 프로젝트용 발표 설계안입니다. 참고 자료의 기술명이나 벤치마크를 옮기지 않고 실제 소스와 로그로 각 장을 채웁니다.'),
TableBlock(['장','제목','보여 줄 핵심 증거'],[['1','프로젝트 개요와 범위','Game → World → 복귀, C++20/Protobuf/MSSQL/Redis'],['2','서버·스레드 구조','4프로세스와 메인/네트워크/DB/기록/Redis 소유권'],['3','TCP와 Protobuf','4바이트 헤더, 분할/병합 수신, 부분 송신, 패킷 예'],['4','명령 큐와 수명','stGameChannel, 짧은 mutex 구간, stPlayer 단일 소유'],['5','계정과 서버 이동','DB 커밋 뒤 응답, HMAC ticket, token/nonce/lease'],['6','서버 판정 전투','목표 이동, 스킬 검증, 사망 100초, 진영 승패'],['7','리플레이 구조','전체+변경 상태, writer 큐, seek, 정상/부분 파일'],['8','Redis와 장애 검증','Sorted Set·총계 재전송, 빈 Redis 복구, SQL 최종 결과'],['9','수치와 한계','96명 워커 18회 표·그래프, 1,500명 정합성·틱 예산 초과'],['10','개선과 향후 연동','Linear/Grid 결과 대조·밀집 한계·전장 병렬화 과제']],[.45,1.35,3]),
P('각 장은 “문제 한 줄 → 해결 흐름 그림 → 짧은 코드/결과 → 한계 한 줄”로 맞춥니다. 큰 소스 파일 전체 캡처보다 10~20줄의 관련 구간과 호출 방향이 읽기 쉽습니다. 전투 장면은 흥미를 주고, 소유권·실패 처리·수치가 서버 구현의 근거가 됩니다.'),
Note('이번 산출물의 위치','현재 세 PDF는 학습·답변·준비 자료입니다. 실제 제출용 포트폴리오는 실습과 통제 측정 뒤 본인의 설명과 증거를 골라 다시 구성하는 단계로 남겨 두었습니다.')
],[Ref('README.md'),Ref(R+'battle-1500-validation.txt'),Ref(S+'GameLogic.h')]),

Page('10분 발표와 증거 폴더 체크리스트','10 / 준비 완료 기준',[
TableBlock(['시간','설명','손으로 짚을 자료'],[['0-1분','무엇을 만들었고 어느 범위까지 했나','구조 한 장·현재 기능'],['1-3분','상태 소유권과 스레드 간 전달','stGameChannel → Submit → Update'],['3-5분','로그인·이동의 실패와 복구','pending ID·token·SQL 상태 전이'],['5-7분','전투·스킬·리플레이','서버 판정 한 경로·짧은 재생'],['7-9분','측정 조건·수치·발견한 한계','96명 워커 비교와 1,500명 시연 구분'],['9-10분','다음 개선의 근거','워커 비교의 범위·장시간·밀집 비용']],[.7,1.8,2.5]),
H('증거 폴더에 남길 파일'),
P('실행별 폴더에 설정/데이터 사본, 실행 명령, exe 해시, 요약 result, 원본 서버/더미 로그, 지연 CSV, 지표 CSV, 리플레이 검증 결과를 함께 둡니다. 화면은 어느 실행의 몇 초인지 캡션을 붙입니다. 비밀번호·Cluster 공유 키·세션 티켓은 제출본에서 제외합니다.'),
H('스스로 통과시킬 다섯 가지'),
P('① 그림 없이 스레드별 데이터 주인을 말할 수 있다.\n② Move 한 요청을 파일·함수 이름으로 끝까지 추적한다.\n③ DB 결과 지연과 재시도 시 안전 조건을 설명한다.\n④ 정상 거부·통신 오류·틱 지연·기록 실패를 구별한다.\n⑤ 측정하지 않은 수치를 말하지 않고 다음 실험을 제안한다.'),
Note('권장 읽기 순서','코드 학습 가이드로 한 경로를 읽고 → 이 문서로 직접 멈춰 확인하고 → 예상 질문 PDF의 관련 두 질문을 소리 내어 답하세요. 이해되지 않은 답변은 문장을 외우기보다 소스로 다시 돌아갑니다.'),
Small('재생성 소스: docs/study/pdf_source/build_study_pdfs.py\n문서 기준·근거 소스 SHA-256: output/pdf/build-manifest.json\n코드나 시험 결과가 바뀌면 문서 날짜와 인용 위치를 함께 갱신하세요.')
],[Ref(R+'battle-1500-validation.txt'),Ref(R+'benchmark-binaries.txt'),Ref(S+'GamePersistence.cpp')]),
]

    from spatial_content import spatial_practice
    pages.append(spatial_practice())
    from redis_content import redis_practice
    pages.extend(redis_practice())
    from final_content import final_practice
    pages.extend(final_practice())
    from worker_content import worker_practice
    pages.extend(worker_practice())
    return pages
