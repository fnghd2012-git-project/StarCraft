from build_study_pdfs import *

RR = R2+'redis/'

def redis_guide():
    return [
Page('전투 기록과 순위의 소유자','31 / Redis 랭킹 · 전체 흐름',[
P('한 경기의 킬·죽음·진입 횟수는 World의 CGameLogic이 확정합니다. 클라이언트는 점수를 올리는 요청을 보낼 수 없습니다. Redis는 확정된 총계를 순위로 조회하는 저장소이고 MSSQL은 종료 결과를 보관합니다. 진행 중 전투를 Redis나 SQL에서 매 틱 읽지 않습니다.'),
Flow([('CountEntry / CountDeath','메인 스레드가 m_ranking[PlayerId] 총계 변경'),('PublishRanking','현재 경기의 값 복사본을 CRedisWorker에 전달'),('CRedisWorker::Run','전용 hiredis 연결로 ZADD·HSET, 순위 요청 처리'),('RankReply 51','메인이 결과를 stGameChannel 송신 우편함으로 전달'),('FinishMatch','최종 총계 → DB 쓰레드(워커) → XML 보관 + MSSQL 트랜잭션')]),
TableBlock(['자료 / 클래스','책임과 수명'],[['m_ranking','접속 stPlayer와 별개. 사망·복귀·재접속 후에도 같은 MatchId 안에서는 유지.'],['stMatchRanking','경기 ID, revision, RankEntry 값 목록. 공유하는 것은 불변 복사본.'],['CRedisWorker','World에만 1개. hiredis 연결·명령 실행은 워커가 독점.'],['stRankQuery / stRankResult','요청 ID로 결과와 채널을 연결. 소켓·stPlayer 포인터를 워커에 넘기지 않음.']],[1.4,3.6]),
Note('리플레이와 같은 경기 ID','StartMatch가 ID를 만들고 StartReplay도 이를 사용합니다. 리플레이를 꺼도 집계는 동작합니다. 새 경기에서 m_ranking을 비우며 MaxMatchParticipants는 한 경기의 서로 다른 누적 참가 계정 상한입니다.')
],[Ref(S+'GameRanking.cpp','void CGameLogic::StartMatch'),Ref(S+'RedisWorker.h','class CRedisWorker'),Ref(S+'GameReplay.cpp','header.set_format_version')]),

Page('어느 순간에 한 번 세는가','32 / 집계 규칙 · 일반 공격과 스톰',[
TableBlock(['사건','킬 / 죽음 / 진입'],[['World 입장 성공','진입 +1. DB Accept와 입장 검증 후 CountEntry 호출.'],['사망 후 재입장','원래 계정 총계를 유지하고 진입 +1. 실패·대기 중 거부는 증가하지 않음.'],['적 유닛 HP가 0이 됨','최종 공격자 킬 +1, 피해자 죽음 +1. 건물 파괴는 유닛 킬에 포함하지 않음.'],['스톰 아군 피해','피해자 죽음만 +1. 아군·자기 자신 처치는 킬로 인정하지 않음.'],['back / Logout / 연결 종료','죽음에 포함하지 않음. 기존 재입장 대기 규칙은 별도로 유지.'],['본진 파괴 / 경기 종료','복귀만 수행. 죽음을 올리지 않음.']],[1.5,3.5]),
H('ReturnToGame의 bool을 죽음으로 해석하면 안 됩니다'),
P('ReturnToGame의 applyCooldown은 사망뿐 아니라 자발적 복귀에도 true입니다. 재입장 대기를 적용한다는 뜻입니다. CountDeath는 실제 HP 0을 확인한 일반 공격과 스톰 경로에서만 호출하고, ReturnToGame으로 복귀하면서 inWorld=false로 바꿉니다.'),
P('stAreaEffect에 sourcePlayerId를 값으로 보관합니다. 시전자 stPlayer 포인터를 저장하면 로그아웃 후 수명이 끝날 수 있습니다. 스톰이 적을 죽일 때는 저장한 계정 ID와 시전 당시 진영으로 판정하므로 시전자가 먼저 복귀해도 정확한 계정에 킬이 들어갑니다.'),
Note('원본 CSV 기준 범위','현재 플래그는 MinTargetHP가 최소 1이므로 죽일 수 없고, 스팀도 자살할 만큼 HP를 쓰지 못합니다. 새 즉사·반사·소환 스킬을 추가한다면 마지막 피해자/공격자 집계 경로도 함께 연결해야 합니다.')
],[Ref(S+'GameRanking.cpp','void CGameLogic::CountDeath'),Ref(S+'GameLogic.cpp','void CGameLogic::ReturnToGame'),Ref(S+'GameSkills.cpp','sourcePlayerId'),Ref(S+'GameData.cpp','skill.minHP < 1')]),

Page('Redis 명령과 실패 복구를 읽기','33 / RedisWorker.cpp · 외부 I/O 경계',[
Code('portfolio:<Realm>:<MatchId>:kills    # Sorted Set\nportfolio:<Realm>:<MatchId>:deaths   # Sorted Set\nportfolio:<Realm>:<MatchId>:entries  # Sorted Set\nportfolio:<Realm>:<MatchId>:players  # Hash'),
P('세 Sorted Set의 member는 PlayerId, score는 해당 총계입니다. Hash는 PlayerId → Protobuf RankEntry 바이트로 이름·서버·세 총계를 보관합니다. ZREVRANGE 0 9로 상위 10명, ZREVRANK로 내 순위를 읽고 Hash에서 표시 정보를 가져옵니다. 동점은 PlayerId 역순의 개별 순위로 표시합니다.'),
P('쓰기에는 MULTI/EXEC와 파이프라인을 사용합니다. 한 번에 명령을 보내 왕복을 줄이고 EXEC의 내부 오류도 확인합니다. ZINCRBY 대신 ZADD 총계 덮어쓰기를 사용하므로 응답 유실 후 같은 총계의 재전송이 중복 점수를 만들지 않습니다. 최신 순서 보장은 이 World의 단일 Redis 워커가 담당합니다.'),
TableBlock(['상황','처리'],[['Redis 연결 실패','순위 요청은 unavailable. 전투 메인은 계속 진행. 다음 주기에 재접속.'],['Redis가 빈 상태로 재시작','메모리의 현재 경기 전체 목록을 다시 전송. 퇴장한 참가자도 포함.'],['연속 집계 변경','대기 스냅샷 하나를 최신 총계로 교체. 중간 상태는 생략할 수 있음.'],['순위 요청 폭주','1인 1초 간격·미완료 1건. 워커 전체 outstanding 기본 64건.'],['보관 만료','TTL 기본 7일. 쓰기 시 갱신하며 변화가 없어도 30초마다 재전송.']],[1.5,3.5]),
Note('지원 환경과 한계','기존 Windows Redis 3.2.100과 hiredis 1.3.0으로 검증했습니다. ZADD GT·ZRANGE REV는 사용하지 않습니다. 로컬 단일 인스턴스 전용이며 TLS·인증·Cluster·Sentinel·다중 World 공동 집계는 구현하지 않았습니다.')
],[Ref(S+'RedisWorker.cpp','void Save('),Ref(S+'RedisWorker.cpp','wire::RankReply Read('),Ref(S+'RedisWorker.cpp','void CRedisWorker::Run'),Ref('setup-redis.ps1')]),

Page('최종 결과 저장과 검증 범위','34 / MSSQL · 복구 파일 · 실제 검증',[
P('FinishMatch는 게임 메모리의 최종 총계로 XML 작업을 만듭니다. CDatabaseWorker는 MatchResults/<MatchId>.xml을 쓴 뒤 PortfolioSaveMatch를 호출합니다. ODBC ExecuteCommand가 값 바인딩을 공유하고, SQL은 경기 헤더와 모든 참가자 결과를 하나의 트랜잭션으로 저장합니다.'),
TableBlock(['저장소','보관 내용'],[['PortfolioMatch','Realm·MatchId·승리 진영·완료 여부·종료 사유·저장 시각'],['PortfolioMatchStats','경기별 PlayerId·AccountId·마지막 입장 서버·킬·죽음·진입'],['MatchResults/*.xml','해당 경기 재적재용 원본. 비밀번호·세션 토큰은 포함하지 않음.']],[1.5,3.5]),
P('같은 Realm/MatchId가 이미 커밋되어 있으면 최초 결과를 유지합니다. 연결 오류 후 재시도하거나 restore-match-result.ps1로 다시 읽어도 중복 행을 만들지 않습니다. 정상 서버 종료는 Complete=0, 경기 승패 확정은 Complete=1입니다.'),
TableBlock(['통합 시험의 완료 경기','킬','죽음','진입'],[['RankMarine','2','0','1'],['RankCaster','1','0','1'],['RankZerg','0','3','4'],['RankAlly','0','1','1']],[2.1,1,1,1]),
P('Debug/Release 각각 격리 포트/Realm에서 일반 공격·스톰 아군 피해·시전자 복귀·재입장·동점·잘못된 조회를 검사했습니다. 전용 Redis를 중단한 동안 공격과 이동을 진행하고 빈 Redis로 재시작한 뒤 총계 복구를 확인했습니다. 새 경기 초기화, SQL 결과 일치와 재적재 멱등성도 검사합니다.'),
Note('아직 보장하지 않는 것','강제 World 종료 시 진행 중 총계를 복원하는 WAL은 없습니다. DB 큐 포화·디스크 오류·종료 제한으로 최종 작업이 유실될 수 있습니다. Redis 장애가 경기 교체를 가로지르면 이전 경기 순위는 불완전할 수 있어 SQL/XML로 확인합니다. 10/19 Redis 활성 1,500명 실행과 틱 예산 초과는 37-38쪽에 따로 정리합니다.')
],[Ref(S+'GameRanking.cpp','void CGameLogic::FinishMatch'),Ref('Database/002_rankings.sql','CREATE OR ALTER'),Ref('restore-match-result.ps1'),Ref(RR+'manifest.json')]),
]

def redis_questions():
    from qa_content import Pair
    return [
Pair('21 / Q41-Q42','Redis를 넣은 이유와 데이터 구조',
'Q41. MSSQL이 있는데 Redis는 왜 사용했나요?',
'전투의 누적 총계는 World 메모리에서 계산하고, 실시간 상위 목록과 내 순위 조회는 Redis Sorted Set으로 처리했습니다. 최종 경기 결과는 MSSQL에 저장합니다. Redis가 반드시 더 빠르다는 실측 결론을 내린 것은 아니며, 전투와 외부 I/O의 책임 분리 및 장애 시 동작을 보여 주는 구현입니다.',
'“SQL ORDER BY로도 되지 않나?” 가능합니다. 규모·조회 빈도에 따라 SQL만으로도 충분합니다. 자료 구조 선택과 추가 운영 비용을 함께 설명합니다.',
'Q42. 킬·죽음·진입 횟수는 어떻게 조회하나요?',
'Realm과 MatchId로 분리한 Sorted Set 세 개에 PlayerId와 총계를 저장합니다. ZREVRANGE로 상위 10명, ZREVRANK로 자기 순위를 읽습니다. 이름과 세 총계는 Hash의 Protobuf RankEntry를 읽습니다. 점수가 같은 사람은 PlayerId 역순으로 표시하며 공동 순위는 구현하지 않았습니다.',
'“패킷 번호와 본문은?” 요청 50은 RankRequest.metric, 응답 51은 RankReply의 성공·경기 ID·top·self입니다.',
'RankReply가 메모리에서 임의로 정렬한 대체 응답이 아니라 Redis 조회 결과인지 Read와 RequestRanking을 연결해 확인하세요.',
[Ref(S+'RedisWorker.cpp','wire::RankReply Read('),Ref('Protocol/Game.proto','message RankReply')]),
Pair('22 / Q43-Q44','재시도 중복과 일관성',
'Q43. Redis 응답이 유실되어 재시도하면 킬이 두 번 오르나요?',
'킬 이벤트를 ZINCRBY로 재전송하지 않고 World가 가진 누적 총계를 ZADD로 덮어씁니다. 같은 총계를 반복해도 값은 같습니다. 단일 워커가 순서대로 쓰고 대기 복사본은 최신 값으로 교체합니다. 재연결하면 현재 경기의 전체 총계를 보내 Redis가 비어 있어도 복구합니다.',
'“여러 World가 동시에 쓴다면?” 지금 보장은 단일 작성자 조건입니다. 공동 집계에는 버전 검증·소유권·이벤트 중복 제거 등 추가 설계가 필요합니다.',
'Q44. MULTI/EXEC면 모든 실패가 롤백되나요?',
'명령 묶음은 다른 명령이 끼어들지 않고 실행되지만 실행 중 오류의 자동 롤백은 아닙니다. EXEC 배열 내부 오류까지 확인하고 성공으로 표시하지 않습니다. 통신 실패 시 연결을 버리고 전체 총계를 다시 보냅니다. 잘못된 키 타입 같은 지속 오류는 로그로 드러내고 원인 수정이 필요합니다.',
'“순위와 Hash가 다른 순간 값이면?” 이 프로젝트의 한 워커만 쓰고 조회 중에 다른 쓰기 작업을 실행하지 않습니다. 외부 작성자가 같은 키를 수정하는 상황은 보장 범위 밖입니다.',
'현재 PC Redis는 3.2.100입니다. 이 버전에서 없는 ZADD GT를 썼다고 답하면 안 됩니다.',
[Ref(S+'RedisWorker.cpp','void CheckReply'),Ref(S+'RedisWorker.cpp','void Save(')]),
Pair('23 / Q45-Q46','사망 집계와 느린 Redis',
'Q45. back을 눌렀는데 왜 죽음이 늘지 않나요?',
'ReturnToGame의 applyCooldown은 재입장 대기 여부이고 사망 여부가 아닙니다. 죽음은 공격이나 스톰이 HP를 0으로 만든 위치에서 CountDeath로 기록합니다. 스톰 시전자는 ID와 진영을 영역에 값으로 남겨 복귀 후에도 킬을 귀속하고, 아군 사망은 킬을 주지 않습니다.',
'“같은 유닛을 두 번 죽이면?” 첫 처리 직후 inWorld가 false가 되어 후속 공격 대상과 스킬 피해 순회에서 빠집니다.',
'Q46. Redis가 느려지면 게임 틱도 느려지나요?',
'Redis 호출은 전용 워커 하나에서 수행합니다. 메인은 복사본과 결과 큐만 다루고 소켓 응답을 기다리지 않습니다. 조회 큐 상한과 타임아웃, 1인 조회 제한을 두었습니다. 1,500명 전투도 실행했지만 총계 복사 비용을 분리하거나 Redis OFF와 대조하지 않았으므로 틱에 영향이 없다고 단정하지 않습니다.',
'“장애를 실제로 시험했나?” 별도 Redis 프로세스를 중단하고 공격·이동·틱 진행, 조회 실패, 빈 Redis 재시작 후 누적 총계 복구를 확인했습니다.',
'test-ranking.ps1은 기존 6379 서비스를 중단하지 않고 자신이 시작한 프로세스만 종료합니다.',
[Ref(S+'GameRanking.cpp','void CGameLogic::CountDeath'),Ref(S+'RedisWorker.cpp','bool CRedisWorker::Query'),Ref('test-ranking.ps1')]),
Pair('24 / Q47-Q48','영속성과 포트폴리오의 범위',
'Q47. 경기 결과는 Redis만 믿고 저장하나요?',
'아닙니다. 승패가 확정되면 World 메모리의 최종 총계를 DB 쓰레드(워커)에 넘깁니다. 워커가 XML 원본을 남기고 SQL 트랜잭션으로 경기와 전원 결과를 저장합니다. Realm/MatchId가 이미 커밋되었으면 그대로 두므로 동일 결과 재시도는 멱등입니다.',
'“DB가 꺼져 있었다면?” XML이 정상 저장된 경우 복구 스크립트로 재적재합니다. 큐 미수락·파일 실패·프로세스 강제 종료까지 무손실을 보장하는 구조는 아닙니다.',
'Q48. Redis까지 붙였으니 완성된 서비스인가요?',
'로컬 단일 World·Redis 구현입니다. 최종 1,500명 시연에서 전원 점수를 Redis·XML·SQL로 대조했고 장애 복구도 검증했습니다. Tick 최대 39.143ms, 생략 틱 0이지만 짧은 로컬 시연입니다. Redis 인증·TLS·장애 조치·다중 World 집계·진행 경기 강제 종료 복원은 미구현이므로 실서비스 완성이나 수용량 보증으로 소개하지 않습니다.',
'“다음 개선 우선순위는?” 전체 Rank 패킷 RTT와 총계 복사·쓰기 비용을 나눠 반복 측정합니다. 직접 ZREVRANGE 지연만으로 사용자 응답 시간을 대신하지 않습니다. 무손실 요구에는 durable outbox를 검토합니다.',
'어떤 저장소가 무엇의 원본인지, 언제 성공 응답을 보내는지, 무엇이 유실될 수 있는지를 한 문장씩 분명하게 답하세요.',
[Ref('Database/002_rankings.sql'),Ref(S+'GameRanking.cpp','void CGameLogic::FinishMatch'),Ref('docs/results/2026-10-19/iocp/summary.json')]),
]

def redis_practice():
    return [
Page('Redis 연결과 순위 화면을 직접 확인','12 / 설치·빌드·접속 순서',[
P('PC에는 Redis 3.2.100이 이미 있고 127.0.0.1:6379가 PONG을 반환했습니다. 기존 서비스를 재설치하지 않았습니다. 새 C++ 의존성은 공식 hiredis 1.3.0 소스를 .deps에서 x64 Debug/Release 정적 라이브러리로 빌드했습니다. 시스템 PATH는 바꾸지 않았습니다.'),
Code('& "C:\\Program Files\\Redis\\redis-cli.exe" PING\n.\\init-database.ps1\n.\\build.ps1 -Configuration Debug\ncd .\\Run64\\Debug'),
P('A/B/C/World는 기존 실행 스크립트나 --server 인자로 켭니다. DummyClient.exe --play에서 서버·ID·비밀번호를 입력하고 world로 입장합니다. 자동 전투 중에도 순위를 볼 수 있고, 천천히 관찰하려면 auto off를 입력합니다.'),
Code('rank kills\nrank deaths\nrank entries'),
P('각 명령은 1초 이상 간격을 둡니다. 상위 10명과 [MY] 자신의 순위·킬·죽음·진입을 출력합니다. Game 서버 상태에서는 World에 입장하라는 안내를 받습니다. Redis가 없으면 순위를 0점 성공처럼 표시하지 않고 unavailable을 출력합니다.'),
TableBlock(['Server.ini [Redis]','기본값 / 역할'],[['Enabled / Host / Port','1 / 127.0.0.1 / 6379'],['KeyPrefix','portfolio. 키는 prefix:Realm:MatchId:metric'],['TimeoutMs / FlushIntervalMs','200 / 1000. I/O 제한과 갱신 주기'],['RetentionSeconds / QueryCapacity','604800(7일) / 64(미소비 결과 포함)']],[2,3]),
Note('설정 사본 확인','빌드하면 루트 Server.ini가 Run64에 복사됩니다. 실행 중에는 설정을 다시 읽지 않습니다. 연결 주소·주기·Enabled 변경 후 World를 재시작하세요. Redis CLI KEYS *나 FLUSHALL 없이 정확한 경기 키만 조회하면 됩니다.')
],[Ref('setup-redis.ps1'),Ref('Server.ini','[Redis]'),Ref(D+'PlayClient.cpp','command == "rank"')]),
Page('장애 재현부터 면접 시연까지','13 / 검증 명령·결과·한계',[
Code('.\\test-ranking.ps1 -Configuration Debug\n.\\test-ranking.ps1 -Configuration Release'),
P('테스트는 별도 Realm·포트·설정 사본과 전용 Redis를 사용합니다. 원본 밸런스를 바꾸지 않고 사본 HP·사거리·스톰 피해·재입장 시간을 조정합니다. 공격·입장 패킷으로 집계하고 SQL 결과까지 검사합니다. 시작한 프로세스는 finally에서 정리합니다.'),
Flow([('첫 경기','0점/진입 1 → 일반 처치 → 사망/자발 복귀 후 재입장'),('스톰','시전자 복귀 후 적·아군 사망. 킬/죽음 귀속 대조'),('Redis 중단','전투 진행·조회 오류 확인 후 빈 Redis 재시작'),('라운드 종료','기존 총계 복구 → 건물 파괴 → SQL 최종값 대조'),('새 경기','점수 초기화 → 정상 종료 미완료 결과 → XML 중복 재적재')]),
Code('.\\restore-match-result.ps1 `\n  -Path .\\Run64\\Debug\\MatchResults\\<MatchId>.xml'),
P('위 복구 명령은 XML이 실제로 남은 DB 장애에 사용합니다. 이미 확정된 경기에는 행을 추가하거나 점수를 더하지 않습니다. 파일이 없는 강제 종료나 큐 미수락을 복구하는 명령은 아닙니다.'),
Note('발표용 3분 구성','1분: GameRanking → CRedisWorker → RankReply 흐름. 1분: 처치/진입 순위와 SQL 결과. 1분: 장애 시 전투 지속·복구 테스트, 메모리 유실 한계. 10/19 최종 1,500명 시뮬레이션과 짧은 장애 시험은 별도 실행입니다. 최종 증거 위치는 다음 16-17쪽을 보세요.')
],[Ref('test-ranking.ps1'),Ref(D+'RankingCheck.cpp','void RunRankingCheck'),Ref('restore-match-result.ps1'),Ref(RR+'manifest.json')])
]
