from build_study_pdfs import *
E='docs/results/2026-10-19/iocp/'

def current_pages(pages):
    '기존 학습 순서와 과거 측정 페이지는 보존하고 현재 구현 설명을 갱신한다.'
    changes={
        'HandlePackets':'HandleClientPackets',
        'IOCP, lock-free 큐, 전투 병렬 처리, 메모리 풀, TLS, World 진행 상태 복구.':'lock-free 큐, TLS, World 진행 상태 복구.',
        'IOCP·lock-free·전투 병렬 처리·결정적 리플레이는 미구현입니다. Grid는 Snapshot 후보 검색에 적용했습니다.':'IOCP·전투 계산 병렬화·I/O 풀은 구현했습니다. lock-free·결정적 재시뮬레이션·TLS는 미구현입니다. Grid는 시야와 범위 스킬 검색에 사용합니다.',
        '기본 공격의 대상 탐색은 m_players를 순회합니다. Snapshot의 반경 후보는 Linear/Grid로 선택하지만 공격·스킬의 대상 탐색은 그대로입니다.':'기본 공격·단일 스킬·Heal은 m_playersById로 대상을 찾습니다. 시야와 Plague·Storm 범위 후보는 Linear/Grid를 공유합니다.',
        'CanSee 내부의 은신 탐지 순회와 공격·스킬 범위 탐색은 이번 변경 대상이 아닙니다.':'CanSee 내부의 은신 탐지 순회는 남아 있습니다. 단일 대상은 ID 인덱스, 범위 스킬은 공간 인덱스로 개선했습니다.',
        'm_players는 메인에서만 접근하므로 전장 상태용 mutex가 없습니다.':'m_players는 메인만 수정하며, 전투 쓰레드(워커)의 읽기 작업이 끝날 때까지 메인이 기다리므로 전장 상태용 mutex가 없습니다.',
        '앞으로 다른 스레드가 stPlayer에 접근하면 이 전제가 깨집니다.':'전투 쓰레드(워커)는 완료 장벽 안에서만 stPlayer를 읽습니다. 이 장벽 밖에서 동시에 수정하면 전제가 깨집니다.',
        'S/TcpServer.cpp · Add, Run, Process, HandleClientPackets':'S/TcpServer.cpp · WSAPoll 비교 경로. 공통 파싱은 PacketDispatch.cpp.',
        'S/GameLogic.cpp · Submit, HandlePacket, Update, Tick':'S/GameLogic.cpp · 명령/틱. GameBattle.cpp · 이동. GameSnapshot.cpp · 시야.',
        '전장 병렬화 과제':'병렬 계산 범위·남은 직렬 구간',
        'Protobuf 구성·직렬화·송신 큐 적재 누계. 실제 소켓 전송 시간은 제외.':'Protobuf 구성·직렬화의 워커별 시간 합계. 송신 큐 적재와 소켓 전송 시간은 제외.',
        '검색 + CanSee + 정렬·최종 선택의 누계. 반대 방식 검증 시간은 제외.':'검색 + CanSee + 정렬의 워커별 시간 합. VerifySpatialSearch의 비교 비용도 포함.',
        '단일 게임 스레드는 전투 처리량의 상한이 될 수 있습니다.':'상태 확정의 직렬 구간과 완료 장벽은 남은 상한입니다.',
        'Move 한 번, Login 한 번, World 이동 한 번을 각각 “입력 → 큐 → 소유자 → 검증 → 상태 변경 → 응답” 순서로 소스에서 짚어보세요. 이 세 경로가 연결되면 나머지 클래스의 필요성도 설명할 수 있습니다.':'Move·Login·World 이동을 “입력 → 큐 → 소유자 → 검증 → 상태 변경 → 응답” 순서로 소스에서 짚어보세요.',
    }
    def convert(value):
        if isinstance(value,str):
            for old,new in changes.items():value=value.replace(old,new)
            return value
        if isinstance(value,list):return [convert(v) for v in value]
        if isinstance(value,tuple):return tuple(convert(v) for v in value)
        return value
    for page in pages:
        page.blocks=convert(page.blocks)
        refs=[]
        for path,needle in page.refs:
            if path==S+'TcpServer.cpp' and needle and 'HandlePackets' in needle:path,needle=S+'PacketDispatch.cpp','void HandleClientPackets'
            if path==S+'GameLogic.cpp' and needle and 'CGameLogic::BroadcastWorldState' in needle:path=S+'GameSnapshot.cpp'
            refs.append((path,needle))
        page.refs=refs
        if page.title=='CNetworkWorker: 접속을 받고 읽고 쓰기':page.title='WSAPoll 비교 경로의 CNetworkWorker'
        if page.title=='CWinsockSession·CSocket: 자원을 닫는 책임':page.label='03 / 공통 RAII와 WSAPoll 세션'
        if page.title=='멀티스레드의 핵심은 소유권 분리':
            page.blocks=[P('게임 상태를 확정하는 주인은 메인입니다. 전투 쓰레드(워커)는 한 전장 안의 독립 계산을 나누고 메인은 모든 결과를 받은 뒤 순서대로 반영합니다. 네트워크는 설정으로 IOCP/WSAPoll을 선택합니다.'),TableBlock(['실행 주체','역할'],[['메인 1','명령·DB 결과 반영, HP·사망·랭킹·승패 확정'],['네트워크 N','IOCP 완료 처리 또는 WSAPoll 송수신. stGameChannel과 명령 큐로 연결'],['전투 W','World의 이동/MP, Storm 피해, 수신자별 시야/직렬화 계산. W=1은 메인 실행'],['DB M','각자 ODBC 연결을 소유하고 작업/결과 큐 교환'],['기록 1 / Redis 1','활성화한 World의 파일 기록 / 순위 I/O'],['CChatRelay','Game 메인의 비차단 중계 연결. 추가 스레드 없음']],[1.3,3.7]),P('기본 World는 메인 1 + 네트워크 2 + DB 2 + 전투 4 + 기록 1 + Redis 1 = 명시적 실행 흐름 11개입니다. 전투 WorkerCount=1이면 별도 전투 스레드를 만들지 않아 7개입니다. 라이브러리 내부 스레드는 제외합니다.'),Note('읽기 단계의 소유권','전투 쓰레드(워커)가 실행 중일 때 메인은 Run에서 대기하므로 stPlayer 수명과 읽는 값이 유지됩니다. 결과 슬롯은 작업별로 다르고, stGameChannel의 응답 큐는 별도 mutex로 보호합니다.')]
    for page in pages:
        if page.title == '클래스·자료 구조 빠른 찾아보기':
            page.blocks[1] = P('Common.h: 표준 헤더와 using. Protocol: 패킷 도구. wire::*: Protobuf 생성 메시지.')
    return pages

def engine_guide():
    fault=json.loads((ROOT/E/'fault-release.json').read_text('utf-8'))
    return [
Page('IOCP: 준비 여부에서 완료 통지로','CURRENT / CIocpServer와 PacketDispatch',[
P('IocpServer.h에 Run·HandleAccept·PostRecv·PostSend를 private 함수로 선언하고 IocpServer.cpp에 직접 구현했습니다. RunTcpServer는 Network.Mode에 따라 CIocpServer 또는 CNetworkWorker를 만듭니다. 게임 명령 큐와 규칙은 공유합니다.'),
Flow([('AcceptEx','작업 객체에 OVERLAPPED와 주소 버퍼·accept 소켓 보관'),('완료 포트','GetQueuedCompletionStatus에서 수락/수신/송신 완료 받기'),('세션 잠금','같은 연결의 완료가 다른 워커에 와도 순서대로 처리'),('공통 파싱','HandleClientPackets가 프레임 누적·버전·종류 검사'),('게임 응답','stGameChannel에 적재 후 완료 포트 알림으로 송신 시작')]),
P('연결당 수신 한 개, 송신 한 개만 진행합니다. 부분 송신이면 완료된 바이트만 제거하고 남은 부분을 다시 WSASend합니다. notifySend는 짧은 완료 포트 알림만 게시하며 세션 잠금을 잡지 않습니다.'),
Note('왜 기존 경로를 남겼나','두 경로에서 같은 패킷 검사를 사용하는 상태로 방식만 바꿔 비교하기 위해서입니다. 채팅 중계와 더미의 WSAPoll도 그대로이며 모든 소켓이 IOCP로 바뀐 것은 아닙니다.')
],[Ref(S+'IocpServer.cpp','void CIocpServer::Run(int index)'),Ref(S+'PacketDispatch.cpp','void HandleClientPackets'),Ref(S+'TcpServer.cpp','void RunTcpServer')]),
Page('취소와 해제는 같은 시점이 아니다','CURRENT / I/O 수명과 종료',[
Flow([('Close','연결 해제 표시, 알림 제거, CancelIoEx와 closesocket'),('취소 완료','정상 완료와 마찬가지로 완료 포트에서 회수'),('stIoOperation 반환','버퍼 사용 종료 후 pool.Release, shared_ptr 참조 해제'),('서버 종료','새 작업 차단 → 모든 I/O 회수 → 종료 알림 → 워커 join')]),
P('stIoOperation은 OVERLAPPED·4096바이트 버퍼·세션 shared_ptr를 함께 보관합니다. 연결 목록에서 세션을 지워도 진행 중인 I/O가 참조를 갖습니다. “소켓을 닫았으니 버퍼도 바로 해제”하면 안 됩니다.'),
P('마지막 응답 후 closeAfterSend가 설정되는 시점도 별도로 점검합니다. 응답 완료가 더 빠르면 새 송신 알림이 없을 수 있어 100ms 유지 점검에서 종료 상태를 확인합니다. shutdown(SD_SEND) 후 상대 EOF 또는 최대 3초를 기다립니다.'),
Note('검증 로그','종료 로그의 pending_io=0과 pool_in_use=0을 함께 확인합니다. 취소 완료를 받기 전에 워커부터 종료하면 두 값이 남거나 수명 오류가 생깁니다. 이 구현은 IOCP 종료 대기에 별도 시간 제한을 두지 않습니다.')
],[Ref(S+'IocpServer.cpp','void CIocpServer::CloseSession('),Ref(S+'IocpServer.cpp','void CIocpServer::Stop()'),Ref(S+'IoBufferPool.h','struct stIoOperation')]),
Page('작고 범위가 분명한 메모리 풀','CURRENT / CIoBufferPool',[
P('UsePool=1이면 처음 필요한 만큼 stIoOperation을 생성하고 완료 뒤 m_free에 반환해 재사용합니다. mutex 하나로 목록과 계수를 보호합니다. 복잡한 lock-free 구조는 사용하지 않았습니다.'),
TableBlock(['구분','동작'],[['UsePool=0','Acquire마다 new, 완료 후 delete. 같은 작업 경로의 비교군'],['UsePool=1','반환 블록 우선 사용, 부족할 때 PoolCapacity까지만 생성'],['고갈','수신/송신을 걸 수 없는 연결은 종료. Accept 슬롯은 다음 점검에서 재시도'],['관리 대상','I/O 객체와 4096바이트 버퍼. stPlayer·Protobuf·문자열 전체를 풀링하지 않음']],[1.25,3.75]),
TableBlock(['장애 시험','할당','재사용','종료 사용 중'],[[f"pool={r['pool']} cap={r['capacity']}",str(r['metrics']['pool_allocations']),str(r['metrics']['pool_reused']),str(r['metrics']['pool_in_use'])] for r in fault],[2,1,1,1]),
Note('성능 주장의 범위','할당 횟수 감소는 직접 관측할 수 있습니다. mutex 경쟁·작업량에 따라 RTT는 달라집니다. 장애 시험의 할당 수로 같은 처리량의 개선율을 계산하지 않고 반복 비교 결과를 따로 사용합니다.')
],[Ref(S+'IoBufferPool.h','class CIoBufferPool'),Ref(E+'fault-release.json')]),
Page('한 전장의 전투 계산을 나누는 방법','CURRENT / CBattleWorkers',[
Flow([('입력 고정','메인이 대상 목록과 이동 입력을 준비'),('계산 분배','워커 i는 i, i+W, i+2W 인덱스 계산'),('완료 장벽','모든 워커가 끝날 때까지 메인 대기'),('검증 선택','VerifyParallel=1이면 직렬로도 계산하여 비교'),('순서대로 반영','메인이 위치·HP·사망·랭킹·복귀를 확정')]),
TableBlock(['병렬 계산','직렬로 유지'],[['이동/건물 충돌/MP 결과','클라이언트 명령 수락·기본 공격'],['같은 Storm의 대상별 피해','Storm 간 적용 순서, 사망·킬 집계'],['수신자별 시야 선택·Protobuf 직렬화','송신 큐 게시, DB 결과·승패·기록']],[1,1]),
P('서로 다른 Storm은 앞 결과를 적용한 뒤 다음 Storm을 계산합니다. 피해 간격을 공유하는 규칙과 마지막 타격 집계를 보존하기 위해서입니다. 전투 전체를 무조건 여러 스레드에서 수정하는 구조로 소개하지 않습니다.'),
Note('WorkerCount=1','추가 전투 스레드 없이 같은 함수를 메인에서 실행합니다. 네트워크·DB·기록·Redis는 별도이므로 서버 전체의 완전한 싱글스레드 모드는 아닙니다.')
],[Ref(S+'BattleWorkers.cpp','void CBattleWorkers::Run'),Ref(S+'GameBattle.cpp','void CGameLogic::MovePlayers'),Ref(S+'GameSkills.cpp','void CGameLogic::UpdateSkills')]),
Page('대상 검색과 결과 검증의 경계','CURRENT / 인덱스·확정 순서',[
TableBlock(['경로','현재 방법'],[['기본 공격 / 단일 스킬 / Heal','m_playersById로 ID 조회. 이미 World에서 나온 대상은 거절'],['Plague / Storm 반경','현재 좌표의 m_combatIndex로 Linear/Grid 후보 검색'],['대상 순서','player_id로 정렬한 목록 순서대로 상태 반영'],['시야','별도 스냅샷 인덱스 + 은신/진영 필터 + 가까운 적32/아군31'],['남은 선형 순회','CanSee의 아군 탐지 유닛 검색. 밀집 격자도 최악 비용 남음']],[1.5,3.5]),
P('인덱스의 stPlayer*는 소유권이 없습니다. stPlayer를 지우기 전에 ID 인덱스에서 제거하며, 입장·삭제·이동 후 공간 인덱스를 다시 만듭니다. 메인만 구조를 변경하고 계산 장벽 중에는 포인터의 수명이 고정됩니다.'),
P('--battle-check는 1,500개 고정 입력을 200회씩 워커 1/2/4에서 대조합니다. 이동 경계·건물 관통·MP 상한·치명 피해와 워커 예외 후 재사용도 검사합니다. 통합 시험은 실제 이동·스킬·사망·승패를 검증하고 VerifyParallel로 결과와 스냅샷 바이트를 대조합니다.'),
Note('결정적 엔진과 구분','검증은 같은 계산 입력에 대한 일치입니다. 네트워크 도착 시각·실시간 공격 쿨다운·DB 완료 시점까지 같게 재시뮬레이션하는 결정적 엔진이나 명령 리플레이를 구현한 것은 아닙니다.')
],[Ref(S+'GameBattle.cpp','void CGameLogic::BuildCombatIndex'),Ref(S+'BattleCalculation.cpp','int RunBattleChecks'),Ref('test-parallel.ps1')]),
    ] + engine_results()

def engine_results():
    summary=json.loads((ROOT/E/'summary.json').read_text('utf-8'))
    rows=summary['comparison'];stress=summary['stress']
    return [Page('10/19 측정: 한 요소씩 바꾼 반복 비교','MEASUREMENT / 현재 4바이트 헤더',[
P('같은 x64 Release 실행 파일, 96명, 워밍업 3초+측정 12초, 조건별 3회입니다. 네트워크/DB 쓰레드(워커)는 2/2, Grid·Redis·리플레이를 켰습니다. 사망으로 부하가 줄지 않게 사본 HP와 유닛 구성을 고정했습니다.'),
Small('환경: Ryzen 7 7700X (8코어/16스레드), Windows 11 Pro. 서버·더미·MSSQL·Redis가 같은 PC에서 loopback으로 통신합니다.'),
TableBlock(['조건','Ping p95(ms)','전투 p95(ms)','Snapshot 구간p95 최대(ms)'],[[r['label'],r['ping_p95'],r['combat_p95'],r['snapshot_p95']] for r in rows],[1.6,1.1,1.1,1.2]),
P('각 칸은 반복별 값의 중앙값입니다. 다음 쪽 그래프와 원본 CSV에는 반복 범위를 남겼습니다. 전투 RTT는 명령 응답 시간이며 틱 완료 시간과 다릅니다. 목표 요청률은 Ping 100/초/명, 전투 20/초/명인 closed-loop 상한입니다.'),
Note('해석',summary['interpretation']),
Small('같은 PC의 서버·더미·MSSQL·Redis가 CPU를 공유합니다. WAN, 최대 수용 인원, 장시간 안정성, 선형 확장을 입증하는 시험이 아닙니다. 과거 8바이트 헤더의 워커 18회 결과와 섞어 개선율을 계산하지 않습니다.')
],[Ref(E+'summary.json'),Ref(E+'matrix/plan.json'),Ref(E+'environment.json')]),
Page('반복 범위와 실제 비용을 함께 보기','MEASUREMENT / RTT·CPU·메모리',[
Picture(E+'comparison.png','같은 색의 점은 각 반복, 막대는 반복 중앙값입니다.',270),
TableBlock(['조건','World CPU(1코어=100%)','World 메모리 MiB','I/O 할당 / 재사용'],[[r['label'],r['cpu'],r['memory'],r['allocations']] for r in rows],[1.5,1.4,1.1,1.5]),
Small('CPU는 로그의 측정 시작/종료 구간 누계에서 계산해 관측 오차가 있습니다. 메모리는 주기적으로 읽은 Working Set 최대의 반복 중앙값입니다. I/O 계수는 World 프로세스 전체 실행의 중앙값으로, RTT 측정 구간만의 값이 아닙니다.')
],[Ref(E+'comparison.csv'),Ref(E+'summary.json')]),
Page('최종 1,500명 전투와 검증 범위','CURRENT / 실전 흐름 시연',[
TableBlock(['항목','관측값'],[[str(k),str(v)] for k,v in stress['display'].items()],[2,3]),
P('각 Game 서버 500명으로 전원 준비 후 전투를 시작했습니다. 사망하면 원래 Game으로 복귀하고 기본 100초 후 재입장합니다. 건물 HP는 전투를 오래 관찰하기 위한 실행 사본에서만 높였습니다.'),
Note('결과를 설명할 때',stress['note']),
H('새 코드에서 읽을 파일'),
TableBlock(['파일','읽는 순서'],[['CIocpServer / CIoBufferPool','PostAccept → Run → PostRecv/PostSend → CloseSession → Stop'],['PacketDispatch','프레임·버전 검사 → CGameLogic::Submit'],['CBattleWorkers / BattleCalculation','작업 게시 → 계산 → 완료 장벽'],['GameBattle / GameSnapshot','읽기 입력 → 개별 결과 → 검증 → 메인 반영']],[1.7,3.3])
],[Ref(E+'stress-validation.json'),Ref(E+'regression/completed.txt')])]

def engine_questions():
    from qa_content import Pair
    return [Pair('CURRENT / Q53-Q54','완료 통지와 세션 수명',
'Q53. closesocket 뒤 왜 세션이 남아 있나요?',
'취소 요청만으로 진행 중인 I/O가 모두 끝난 것은 아닙니다. stIoOperation이 세션 shared_ptr와 버퍼를 보유하고 완료 포트에서 통지를 회수한 뒤 풀에 반환합니다. 서버 종료도 pending_io가 0이 된 다음 워커를 종료합니다.',
'“수신과 송신 완료가 다른 스레드로 오면?” 세션 mutex로 직렬화하고 각 방향 한 작업만 진행합니다.',
'Q54. 풀은 어디에 적용했고 무엇을 측정했나요?',
'IOCP 작업 객체와 4096바이트 버퍼에만 적용했습니다. mutex로 free list를 보호하며, 끄면 동일 경로에서 new/delete합니다. 할당·재사용·고갈·동시 사용 최고치를 기록하고 종료 사용량 0을 검사했습니다.',
'“고갈 시 서버 전체가 죽나요?” 해당 연결을 정리하고 Accept는 여유가 생기면 다시 보충합니다.',
'test-iocp가 반복 접속, 강제 단절, 느린 수신, 풀 고갈·회복, 대기 I/O 종료를 검사합니다.',[Ref(S+'IocpServer.cpp'),Ref(S+'IoBufferPool.h'),Ref(E+'fault-release.json')]),
Pair('CURRENT / Q55-Q56','병렬 계산과 충돌하는 피해',
'Q55. 한 전장 안에서 실제로 무엇이 병렬인가요?',
'이동·충돌·MP 계산, 같은 Storm의 대상별 피해, 수신자별 시야 선택과 Protobuf 직렬화입니다. 메인은 입력을 고정하고 결과를 기다린 뒤 반영합니다. 기본 공격·죽음·킬 집계·승패는 메인에서 처리합니다.',
'“전투 전체가 네 배 빨라지나요?” 직렬 구간과 작업 게시·대기 비용이 있으므로 반복 측정으로 판단합니다.',
'Q56. 두 스톰이 같은 유닛을 때리면 어떻게 하나요?',
'한 스톰의 대상들을 병렬 계산한 뒤 ID 순서대로 적용합니다. 다음 스톰은 갱신된 HP와 nextStormDamage를 읽습니다. 중복 피해 간격과 킬 집계를 메인에서 확정하므로 같은 피해를 동시에 더하지 않습니다.',
'“동일 결과를 무엇으로 확인했나요?” 고정 계산 입력과 VerifyParallel의 직렬 결과·스냅샷 바이트를 비교하고 통합 승패 테스트를 실행했습니다.',
'결정적 명령 재생 전체를 검증한 것과 같은 계산 결과 대조는 구분합니다.',[Ref(S+'GameSkills.cpp','void CGameLogic::UpdateSkills'),Ref(S+'BattleWorkers.cpp'),Ref('test-parallel.ps1')]),
Pair('CURRENT / Q57-Q58','설정 비교와 성능 해석',
'Q57. IOCP·풀·전투 쓰레드(워커) 효과를 어떻게 분리했나요?',
'10/19의 같은 실행 파일에서 WSAPoll/IOCP, IOCP의 풀 OFF/ON, 전투 쓰레드(워커) 1/2/4를 비교했습니다. 네트워크·DB 쓰레드(워커)와 부하를 고정하고 조건별 세 번 반복했습니다. RTT·Snapshot·CPU·메모리·할당 수를 함께 봅니다.',
'“풀과 워커 수를 한꺼번에 바꾸면?” 어느 변경의 효과인지 분리할 수 없어 한 요소씩 바꿉니다.',
'Q58. WorkerCount=1이면 싱글스레드인가요?',
'Battle.WorkerCount=1은 전투 계산을 메인에서 실행한다는 뜻입니다. Network와 Database에도 별도 WorkerCount가 있고 기록·Redis도 별도입니다. 설정 섹션과 실제 생성 스레드를 함께 설명해야 합니다.',
'“테스트 때 VerifyParallel을 켜나요?” 기능 검증에서는 켜고, 성능 비교에서는 직렬 재계산 비용을 피하도록 끕니다.',
'설정별 동작과 실행 예는 별도 04 설정 설명서에서 확인합니다.',[Ref('Server.ini'),Ref(E+'summary.json'),Ref(E+'matrix/plan.json')])]

def engine_practice():
    return [Page('IOCP와 전투 쓰레드(워커)를 직접 바꿔 보기','CURRENT / 설정 실습',[
P('04 설정 설명서의 비교 A-E를 따라 실행 사본의 Server.ini를 바꿉니다. 실행 파일과 Unit/Skill/Map은 유지하고 설정 하나씩 바꿉니다. 설정은 시작 시 읽으므로 모든 대상 서버를 다시 실행합니다.'),
Code('.\\test-iocp.ps1 -Configuration Release\n.\\test-parallel.ps1 -Configuration Release\n.\\test-network.ps1 -Configuration Release -NetworkMode WSAPoll'),
TableBlock(['관찰','설명할 내용'],[['IOCP STOP','pending_io=0, pool_in_use=0. 취소 통지를 왜 기다리는가?'],['pool_allocations/reused','풀 켜기 전후 할당 횟수와 캐시 보관량'],['battle_items','워커별 실행된 항목 수. 작업 시간의 균등 분배 보장은 아님'],['parallel_verified','직렬 재계산과 동일한 결과를 확인한 항목 수'],['snapshot_p95_us','워커 작업 게시·계산·대기·큐 게시를 포함한 전체 지연']],[1.5,3.5]),
Note('성능 표를 읽는 연습','성능 표는 10/19 빌드의 기록입니다. 10/20에는 클래스 분리·함수 배치·서식을 정리한 뒤 통신·전투·랭킹·리플레이와 IOCP 종료/풀 고갈 시험을 다시 통과했습니다. 성능 개선을 새로 측정한 결과는 아닙니다.')
],[Ref('test-iocp.ps1'),Ref('test-parallel.ps1'),Ref(E+'comparison.csv'),Ref('docs/results/2026-10-20/readability/validation.md')])]
