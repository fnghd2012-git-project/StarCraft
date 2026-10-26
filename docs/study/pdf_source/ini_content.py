from build_study_pdfs import *

def manual():
    return [
Page('어느 Server.ini를 바꾸면 되나요?','START / 적용 파일과 재시작',[
P('루트의 Server.ini는 빌드 시 Run64/Debug와 Run64/Release로 복사하는 원본입니다. Run64에서 실행하면 그곳의 설정을 읽습니다. 실행 중 파일을 고쳐도 자동 적용되지 않으므로 서버를 종료하고 다시 시작합니다.'),
Code('cd D:\\work\\Josun_Server\\git_ProJect\\Run64\\Release\n.\\MultiThreadServer.exe --config-check World\n.\\MultiThreadServer.exe --server A\n.\\MultiThreadServer.exe --server B\n.\\MultiThreadServer.exe --server C\n.\\MultiThreadServer.exe --server World'),
P('실제 서버는 각각 별도 터미널에서 실행합니다. 다른 설정 파일을 쓰려면 --config 경로를 명시합니다. Data·Replay·Cluster.local.ini·StopFile의 상대 경로는 설정 파일이 있는 폴더 기준입니다.'),
Note('시연 실행의 사본','StartBattle.ps1은 BattleRuns 하위에 설정·CSV를 복사하고 임시 포트/Realm을 사용합니다. 네트워크4·DB6, 큐/기록 설정과 건물 HP도 시연용으로 바꿉니다. 원본 파일의 값과 실행 사본의 값을 혼동하지 마세요.'),
Small('여기서 A/B/C는 GameServer.csv의 서버 ID입니다. 설정 비교 A-E와는 별개입니다. 신규 옵션을 생략한 이전 설정은 Network.Mode=WSAPoll, Battle.WorkerCount=1, Memory.UsePool=0을 기본값으로 사용합니다.')
],[Ref('Server.ini'),Ref(S+'Config.cpp','stServerConfig LoadServerConfig'),Ref('prepare-runtime.ps1'),Ref('Launch/StartBattle.ps1')]),
Page('설정 세 개로 비교 버전을 선택하기','01 / 동일 게임 로직의 비교표',[
TableBlock(['비교','Network.Mode','Memory.UsePool','Battle.WorkerCount'],[['A','WSAPoll','0','1'],['B','IOCP','0','1'],['C','IOCP','1','1'],['D','IOCP','1','2'],['E','IOCP','1','4']],[.55,1.2,1.45,1.8]),
P('A→B는 네트워크 방식, B→C는 I/O 풀, C→D→E는 한 전장의 전투 계산 워커 수를 비교합니다. Network.WorkerCount=2와 Database.WorkerCount=2, CSV·인원·요청률·기록·Redis 설정을 고정합니다.'),
Code('[Network]\nMode=IOCP\nWorkerCount=2\n\n[Memory]\nUsePool=1\nPoolCapacity=8192\n\n[Battle]\nWorkerCount=4\nVerifyParallel=0'),
Note('붙여넣기 주의','위 코드는 변경할 항목만 보여 줍니다. 완전한 Server.ini가 아니므로 기존 파일을 이 내용으로 덮어쓰지 말고 해당 섹션의 값을 수정하세요. 성능 측정은 VerifyParallel=0, VerifySpatialSearch=0입니다.')
],[Ref('Server.ini'),Ref('docs/results/2026-10-19/iocp/matrix/plan.json')]),
Page('네트워크 설정: 실제 연결과 송수신','02 / [Network]',[
TableBlock(['키 / 현재값','허용값','동작'],[['Mode=IOCP','IOCP / WSAPoll','완료 통지 / 준비 여부 폴링'],['WorkerCount=2','1~32','각 프로세스의 네트워크 쓰레드(워커) 수'],['MaxClientsPerWorker=1024','1~1024','총 연결 상한의 배수. 로그인 전·중계도 포함'],['MaxPendingSendBytes=65536','4096~1048576','네트워크 송신과 게임 우편함 각각의 상한'],['PollTimeoutMs=5','1~50','WSAPoll 대기 상한(ms). IOCP에는 영향 없음'],['StopFile (선택)','파일 경로','해당 파일 존재 시 정상 종료. 기본은 미지정']],[1.9,1.1,2]),
P('IOCP의 총 네트워크 정원은 WorkerCount × MaxClientsPerWorker입니다. 워커마다 세션을 고정 배정하는 구조는 아니며 여러 워커가 완료를 받습니다. WSAPoll은 각 워커가 자기 소켓 목록을 소유합니다.'),
Note('1,500명과 워커 1개','현재 MaxClientsPerWorker 상한이1024이므로 네트워크 쓰레드(워커)1로 World에1500명을 연결할 수 없습니다. 전투 WorkerCount=1과 네트워크 WorkerCount=1은 다릅니다. 비교 시 연결 정원도 확인하세요.'),
P('65536은 프로세스 전체 메모리 상한이 아닙니다. 세션 문자열·Protobuf·게임 상태·기록 큐·I/O 풀도 따로 존재합니다. 느린 수신자의 대기 데이터가 상한을 넘으면 해당 연결을 종료합니다.')
],[Ref(S+'Config.cpp','config.workerCount'),Ref(S+'IocpServer.cpp','void CIocpServer::PostSend('),Ref(S+'NetworkWorker.h','class CNetworkWorker')]),
Page('메모리 풀 설정과 용량 선택','03 / [Memory]',[
TableBlock(['키','값','결과'],[['UsePool','0','I/O마다 new/delete. 풀 효과 비교용'],['UsePool','1','완료한 블록 재사용. IOCP에서만 동작'],['PoolCapacity','1~65536 / 현재8192','동시에 생성해 보관할 수 있는 최대 블록 수']],[1,1.6,2.4]),
P('블록 하나는 OVERLAPPED·세션 참조·accept 소켓·4096바이트 버퍼를 포함합니다. 이번 x64 빌드의 sizeof(stIoOperation)은4168바이트입니다. 8192블록을 모두 생성하면 객체만 약32.56MiB이며 목록·힙 관리 비용은 별도입니다. 시작 시8192개를 전부 생성하지 않습니다.'),
P('미리 걸어 둔 AcceptEx도 최대8블록을 사용합니다. 연결은 수신과 송신이 겹칠 수 있고 완료 처리 중 재등록이 있어 순간 사용량이 늘어납니다. 연결 수만큼만 설정하면 충분하다고 단정할 수 없습니다.'),
TableBlock(['로그','의미'],[['pool_allocations','실제 생성 횟수. 풀 OFF에서는 누적 new 횟수'],['pool_reused / pool_exhausted','반환 블록 재사용 횟수 / 고갈 횟수'],['pool_peak / pool_cached','사용 중 최고치 / 현재 반환되어 보관된 수'],['pool_in_use / pending_io','현재 사용 블록 / 완료 회수가 남은 I/O. 종료 때 둘 다0']],[1.9,3.1]),
Note('너무 작게 설정하면','신규/기존 연결이 끊길 수 있습니다. 서버 전체를 바로 종료하지 않고 반환 블록을 기다려 Accept를 보충합니다. test-iocp의16블록 시험은 고갈 경로 확인용이며 실제1,500명 설정이 아닙니다.')
],[Ref(S+'IoBufferPool.h'),Ref('docs/results/2026-10-19/iocp/fault-release.json')]),
Page('전투 쓰레드(워커)와 검증 스위치','04 / [Battle] WorkerCount',[
TableBlock(['설정','어떻게 실행되나요?'],[['WorkerCount=1','추가 전투 쓰레드(워커) 없이 메인에서 같은 계산 함수 실행'],['WorkerCount=2','전투 쓰레드(워커)2개가 작업 인덱스를 나누어 계산'],['WorkerCount=4 (현재)','전투 쓰레드(워커)4개. 메인이 완료 장벽에서 기다렸다가 적용'],['WorkerCount=5~16','허용 범위. 더 많다고 빠르다는 보장은 없음'],['VerifyParallel=0 (현재)','일반 실행·성능 측정'],['VerifyParallel=1','같은 입력을 직렬로 재계산해 결과와 스냅샷 바이트 비교']],[2,3]),
P('World에만 전투 쓰레드(워커)가 생깁니다. Game A/B/C의 Battle.WorkerCount 값은 전투 스레드를 만들지 않습니다. 병렬 대상은 이동/MP, Storm 피해, 시야·직렬화이고 기본 공격·킬·사망·승패 확정은 메인입니다.'),
Note('검증 실패','불일치가 발견되면 예외를 발생시켜 성공처럼 계속 진행하지 않습니다. 검증을 켠 실행은 같은 계산을 두 번 수행하므로 성능 비교 표에 섞지 않습니다.'),
Code('.\\test-parallel.ps1 -Configuration Release\n.\\Run64\\Release\\MultiThreadServer.exe --battle-check')
],[Ref(S+'BattleWorkers.cpp'),Ref(S+'GameBattle.cpp'),Ref(S+'GameSnapshot.cpp'),Ref('test-parallel.ps1')]),
Page('틱·전장 규칙과 정원 설정','05 / [Battle] 게임 규칙',[
TableBlock(['키 / 현재값','허용 / 의미'],[['TickRate=20','1~1000 중 1000을 나누어떨어뜨리는 값.\n20 → 50ms / 10 → 100ms / 1 → 1000ms'],['MpRegenPerSecond=1','0~1000. 초당 MP 회복량'],['PauseMpRegenWhileCloaked=1','0/1. 클로킹 중 MP 회복 중지'],['DeathReentrySeconds=100','0~86400. 사망·자발적 복귀 후 재입장 대기'],['RoundRestartSeconds=300','1~86400. 종료 후 건물 복구까지 대기'],['MaxWorldPlayers=1500','1~2048. World 동시 입장 상한'],['MaxGamePlayers=500','1~2048. Game 로그인 기본 상한'],['MaxMatchParticipants=10000','World 정원~100000. 한 경기 누적 고유 계정 상한'],['[Server.A] MaxPlayers=500','서버별 상한이 Game 기본값보다 우선. B/C도 동일']],[2.2,2.8]),
P('TickRate=1이면1초에 한 번 틱을 수행합니다. 스냅샷도 틱이 실행될 때만 만들어져1Hz가 됩니다. 이동 입력에 대한 즉시 응답과 실제 위치가 변하는 틱을 구분하세요. 기본20Hz에서는 스냅샷이약5Hz입니다.'),
Note('TickRate 변경 시 같이 확인','Replay.FrameTicks는1~TickRate이므로 TickRate=1이면 FrameTicks도1이어야 합니다. 스킬 시간은 틱으로 올림되어 낮은 TickRate에서 해상도가 거칠어집니다. 서버는 무제한 따라잡지 않고 최대5틱 처리 후 밀린 틱을 건너뜁니다.')
],[Ref(S+'Config.cpp','config.tickRate'),Ref(S+'GameLogic.cpp','void CGameLogic::Update'),Ref(S+'GameSkills.cpp','SkillTicks')]),
Page('시야·공간 검색과 데이터 파일','06 / [Battle] 검색 · [Data]',[
TableBlock(['설정','동작'],[['SnapshotRadius=25','반경 안 자신+가까운 적32/아군31까지 표시'],['SnapshotRadius=0','전체 시야. MaxWorldPlayers≤64일 때만 허용'],['SpatialSearch=Grid','격자 후보 검색. Linear는 전체 좌표 검사 비교군'],['SpatialCellSize=25','1~1000000.작아지면 셀 수가 늘고 커지면 후보 수가 늘 수 있음'],['VerifySpatialSearch=0/1','최종 시야와 범위 후보를 대조하는 검증 설정'],['BuildingDetectionRange=10','본진의 은신 탐지 거리.0이면 본진 탐지 없음']],[2.1,2.9]),
TableBlock(['[Data] 키','현재 경로 / 역할'],[['ServerFile','Data/GameServer.csv / ID·종족·주소·포트'],['UnitFile / SkillFile','Data/Unit.csv / Data/Skill.csv'],['MapFile','Data/Map.ini / 0이상 경계·건물'],['StartPointFile','Data/StartPoint.csv / MapIndex+Race별 시작 좌표']],[1.7,3.3]),
P('시야 제한은 네트워크에 보여 주는 유닛 수만 줄입니다. 전투·순위·리플레이 참가자를 제거하지 않습니다. 맵/유닛/스킬 데이터는 시작 시 읽고 실행 중 수정하지 않습니다.')
],[Ref(S+'Config.cpp','config.snapshotRadius'),Ref(S+'GameBattle.cpp','FindTargetsInRange'),Ref(S+'GameSnapshot.cpp'),Ref('Data/GameServer.csv')]),
Page('MSSQL 워커와 서버 이동 상태','07 / [Database] · [World]',[
TableBlock(['키 / 현재값','허용 / 동작'],[['Realm=local','소문자·숫자·_·- 1~64자.계정 데이터 격리 키'],['WorkerCount=2','1~8.DB 쓰레드(워커)와 각자의 ODBC 연결'],['QueueCapacity=256','16~4096.대기 작업 상한.가득 차면 제출 거절'],['SessionLeaseSeconds=15','10~60.비정상 종료 뒤 세션 점유를 회수하는 임대 시간'],['World.Address=127.0.0.1','Game이 안내하는 World 주소 및 World 수신 주소'],['World.Port=7100','1~65535.World 포트. Game별 포트는 CSV']],[2,3]),
P('DB 주소/인증은 MTS_DB_* 환경 변수와 Database 설정 로더에서 관리합니다. 개인 테스트는127.0.0.1의project_db를 사용합니다. Realm을 바꿔도 DB 서버나 데이터베이스 자체가 바뀌지는 않습니다.'),
P('World의 ServerId/ServerType/Race 항목은 현재 로더가 실행 인수 World를 기준으로 결정합니다. Game의 ID와 종족은 CSV에 지정합니다. 문서용 값을 고쳐 역할이 바뀐다고 생각하면 안 됩니다.'),
Note('워커 수와 접속 수','Database.WorkerCount를 늘려도 MSSQL의 잠금·비밀번호 해시·메인 완료 처리 대기가 남을 수 있습니다. QueueCapacity를 무조건 늘리는 것은 처리 능력 증가가 아니라 대기 작업 증가일 수 있습니다.')
],[Ref(S+'Database.cpp','stDatabaseConfig LoadDatabaseConfig'),Ref(S+'Config.cpp','stServerConfig LoadServerConfig'),Ref(S+'GamePersistence.cpp')]),
Page('Redis 랭킹 갱신·조회 설정','08 / [Redis]',[
TableBlock(['키 / 현재값','허용 / 의미'],[['Enabled=1','0/1.World 전용 순위 워커 활성화'],['Host=127.0.0.1','현재 로컬 주소만 허용'],['Port=6379','1~65535'],['KeyPrefix=portfolio','소문자·숫자·_·- 1~32자.키의 접두어'],['TimeoutMs=200','50~2000.Redis I/O 제한 시간'],['FlushIntervalMs=1000','100~5000.총계 게시 간격(ms)'],['RetentionSeconds=604800','60~2592000.경기 키 보관 TTL(현재7일)'],['QueryCapacity=64','1~1024.순위 조회 대기 수 상한']],[2.1,2.9]),
P('Enabled=0이면 전투 집계는 메모리에 남지만 Redis 순위 조회는 비활성 응답입니다. 경기 최종 결과의 MSSQL 저장은 별도입니다. Enabled=1에서도 Redis가 끊기면 전투는 계속하고 조회는 실패 응답을 줍니다.'),
P('FlushIntervalMs를 줄이면 반영 지연을 줄일 수 있지만 쓰기 빈도가 늘어납니다. 순위 요청 시에도 최신 총계를 게시하며, 재연결하면 전체 총계를 다시 보내 빈 Redis를 복구합니다.'),
Note('지원 범위','로컬 단일 Redis입니다. 외부 인증·TLS·Cluster·Sentinel·여러 World의 공동 점수 쓰기는 현재 범위에 포함되지 않습니다.')
],[Ref(S+'RedisWorker.cpp'),Ref(S+'GameRanking.cpp','void CGameLogic::PublishRanking'),Ref(S+'Config.cpp','config.redisEnabled')]),
Page('리플레이와 지표의 설정·단위','09 / [Replay] · [Metrics]',[
TableBlock(['키 / 현재값','허용 / 동작'],[['Enabled=1','0/1.전투 상태 기록 활성화'],['Directory=Replays','설정 파일 기준 출력 폴더'],['QueueMB=8','1~32.메인에서 기록 워커로 넘기는 대기 바이트'],['MaxFileMB=128','1~1024.한 기록 파일 크기 상한'],['SnapshotSeconds=1','1~10.전체 상태 기준점 간격'],['FrameTicks=1','1~TickRate.1=매틱,20Hz에서4=5Hz 기록'],['Metrics.IntervalSeconds=5','1~60.지표 로그 간격(초)']],[2,3]),
P('기록이 실패하면 해당 기록을 실패 표시하고 전투는 계속합니다. MaxFileMB를 늘려도 디스크 속도와 큐 상한은 별도입니다. 네트워크 시야가 제한되어도 리플레이는 전체 전투 상태를 저장합니다.'),
TableBlock(['지표','읽는 방법'],[['tick_p95_us / tick_max_us','로그 구간의 Tick 처리 시간.20Hz 예산은50000us'],['snapshot_p95_us','Snapshot 전체 벽시계 시간'],['spatial_select_us / snapshot_send_us','워커들의 검색 시간 합 / 구성·직렬화 시간 합. 소켓 지연 아님'],['battle_calculate_us / battle_apply_us','이동·Storm 계산 대기 포함 누계 / 메인 반영 누계'],['skipped_ticks','밀려서 생략한 틱 수.0이어도 느린 틱이 없다는 뜻은 아님']],[2.2,2.8])
],[Ref(S+'GameReplay.cpp','void CGameLogic::ReportMetrics'),Ref(S+'GameSnapshot.cpp'),Ref(S+'Replay.cpp')]),
Page('권장 확인 순서와 잘못된 설정 예','10 / 실행 전후 점검',[
Flow([('1. 설정 검사','--config-check World로 범위·경로·CSV 검사'),('2. 기능 검증','test-network / test-gameplay / test-parallel 실행'),('3. 장애 검증','test-iocp에서 고갈·단절·종료 회수 확인'),('4. 반복 비교','검증 스위치를 끄고 한 값씩 바꿔 같은 부하 실행'),('5. 결과 보관','실행 사본 INI·CSV, exe 해시, 원본 RTT·로그 보관')]),
TableBlock(['설정 예','결과'],[['TickRate=60','1000ms를 나누어떨어뜨리지 않아 시작 실패'],['TickRate=1, FrameTicks=4','FrameTicks가 틱 수보다 커서 시작 실패'],['MaxWorldPlayers=1500, SnapshotRadius=0','전체 시야 인원 제한 때문에 시작 실패'],['Mode=Iocp','대소문자 구분. IOCP로 입력해야 함'],['UsePool=1, Mode=WSAPoll','설정은 읽지만 WSAPoll에는 풀을 적용하지 않음'],['Game CSV 포트 중복','중복 endpoint 검증으로 시작 실패']],[2.3,2.7]),
Note('이 설정이 최적값이라는 뜻은 아닙니다','현재값은 학습·로컬 시연을 위한 기본값입니다. 별도 부하 PC·실서비스 맵·긴 실행 시간에서는 다시 측정해야 합니다. C++ 더미와 서버를 같은 PC에서 돌린 수치를 인터넷 수용량으로 소개하지 않습니다.')
],[Ref(S+'Config.cpp'),Ref('test-iocp.ps1'),Ref('test-parallel.ps1')])]
