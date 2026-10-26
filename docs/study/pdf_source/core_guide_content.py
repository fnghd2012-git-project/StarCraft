'학습 순서를 따라가는 18쪽짜리 핵심 가이드. at은 현재 소스에서 함수 줄을 찾는다.'


from pathlib import Path

SOURCE = Path(__file__).resolve().parents[3] / 'MultiThreadServer/MultiThreadServer/src'


def cut(file, scope, start, end=None, count=1):
    '함수 이름과 실제 문장으로 발췌 범위를 찾는다.'
    lines = (SOURCE / file).read_text('utf-8-sig').splitlines()
    begin = next(i for i,line in enumerate(lines) if scope in line)
    matches = [i for i in range(begin,len(lines)) if start in lines[i]]
    if not matches:
        raise ValueError(f"Missing excerpt: {file} / {scope} / {start}")
    first = matches[0]
    last = next(i for i in range(first,len(lines)) if end in lines[i]) if end else first+count-1
    return (first+1,last+1)


def pages(at):
    def ref(file, marker):
        return 'S/' + at(file, marker)

    pages = [
        {
            'tag':'START HERE / 읽을 함수부터 찾기',
            'title':'어느 클래스의 어느 함수를 봐야 할까',
            'intro':'파일 전체를 처음부터 읽기보다 아래 순서로 따라간다. 먼저 ‘누가 실행하고, 누구의 데이터를 바꾸는가’를 확인한다.',
            'kind':'table',
            'headers':['우선순위 / PDF 쪽','정확히 읽을 클래스·함수','이 부분을 읽고 설명할 것'],
            'widths':[143,407,326],
            'size':10.6,
            'rows':[
                ['1순위 · 2-4쪽','RunTcpServer() [자유 함수]\n'+ref('TcpServer.cpp','void RunTcpServer('),'메인 루프와 네트워크 쓰레드(워커)는 어디서 갈라지는가'],
                ['1순위 · 5-6쪽','CIocpServer::PostRecv() / HandleSessionCompletion()\nHandleClientPackets() [자유 함수]','수신 등록 → 완료 → 패킷 분리의 차이'],
                ['1순위 · 7-8쪽','CGameLogic::Submit / Update / HandleMove / Tick','네트워크에서 받은 명령이 메인에 전달되고 실제로 실행되는 지점'],
                ['1순위 · 9쪽','CGameLogic::SendPacket()\nCIocpServer::NotifySend() / PostSend()','응답 큐 → 워커 깨우기 → 송신 등록 → 완료'],
                ['2순위 · 10-12쪽','CBattleWorkers::Run() / Work()\nCGameLogic::MovePlayers()','잠금, 계산 분담, 완료 대기, 메인의 결과 반영'],
                ['2순위 · 13-15쪽','CIoBufferPool::Acquire() / Release()\nCIocpServer::Stop() / CDatabaseWorker::Run()','완료 전 버퍼 유지, 안전한 종료, DB 작업 분리'],
                ['확인 · 16-18쪽','CNetworkWorker::Run() / ProcessSession()\n설정 비교 / 디버거 중단점','WSAPoll 차이와 자신의 말로 검증할 순서'],
            ],
            'links':[4,5,7,9,11,13,18],
            'after':'프로젝트 루트: D:/work/Josun_Server/git_ProJect\nS/는 아래쪽의 소스 폴더 약어다. 현재 코드 줄 번호를 사용하며, PDF 책갈피와 위 표의 행으로 이동할 수 있다.',
            'takeaway':'코드 상자의 ...는 중간 생략, 줄 번호 없는 행은 긴 줄의 이어쓰기다. 표시된 줄 번호로 실제 원문을 대조하며 읽는다.',
        },
        {
            'tag':'전체 지도 / 역할과 실행 스레드',
            'title':'네트워크도 멀티스레드의 일부다',
            'intro':'네트워크는 역할, 스레드는 실행 주체다. 파일을 두 부류로 외우기보다 상태가 어느 경계를 넘어가는지 본다.',
            'kind':'diagram','diagram':'architecture',
            'takeaway':'핵심 소유권: CGameLogic의 stPlayer와 건물 상태는 메인이 확정한다. 워커가 모두 같은 상태를 고치는 구조가 아니다.',
        },
        {
            'tag':'한 패킷 추적 / CM_Move',
            'title':'이동 요청이 들어와 화면에 반영되기까지',
            'intro':'World 입장까지 끝난 클라이언트가 이동 요청을 보낸 상황이다. 4번의 접수 응답과 6번의 상태 전송을 구분한다.',
            'kind':'diagram','diagram':'move',
            'takeaway':'다음 읽기: 수신 5쪽 → 패킷 분리 6쪽 → 명령 실행 7-8쪽 → 송신 9쪽 → 병렬 계산 11-12쪽.',
        },
        {
            'tag':'MAIN + NETWORK / 시작 지점',
            'title':'서버를 시작하면 어디서 스레드가 생길까',
            'intro':'먼저 TcpServer.cpp의 RunTcpServer()를 연다. IocpServer.h에는 멤버·함수 선언, IocpServer.cpp에는 각 함수의 구현이 있다.',
            'role':'MAIN',
            'snippets':[
    ('TcpServer.cpp', [cut('TcpServer.cpp','void RunTcpServer','if (config.networkMode == "IOCP")',count=4),cut('TcpServer.cpp','void RunTcpServer','game.Update();',count=2)], 'RunTcpServer() / 메인에서 실행'),
    ('IocpServer.cpp', [cut('IocpServer.cpp','CIocpServer::CIocpServer','m_port = CreateIoCompletionPort'),cut('IocpServer.cpp','CIocpServer::CIocpServer','m_workers.emplace_back(',count=5)], 'CIocpServer 생성자 / 워커 생성')
],
            'notes':[
                ('먼저 열 곳',ref('TcpServer.cpp','void RunTcpServer(')+'\nCGameLogic 생성 → 네트워크 모드 선택 → while 루프 순서로 읽는다.'),
                ('스레드가 갈라지는 정확한 줄',ref('IocpServer.cpp','m_workers.emplace_back')+'\n생성자 자체는 메인에서 실행한다. 새 jthread가 호출하는 Run(i)는 네트워크 쓰레드(워커)에서 실행한다.'),
                ('Update() 두 개를 구분하기','CIocpServer::Update(): 연결 시간 초과·Accept 보충·통계 점검.\nCGameLogic::Update(): 게임 명령 처리와 틱 실행.\n둘 다 메인 루프에서 호출한다.'),
                ('헤더에서 바로 찾기','IocpServer.h의 private에 Run·HandleAccept·PostRecv·PostSend가 선언돼 있다. IocpServer.cpp의 CIocpServer::함수명으로 이동하면 실제 처리를 볼 수 있다.'),
            ],
            'takeaway':'질문: RunTcpServer의 1ms sleep이 게임 틱인가? 아니다. 실제 전장 틱 주기는 CGameLogic::Update()가 판단한다.',
        },
        {
            'tag':'NETWORK / IOCP 수신',
            'title':'PostRecv()는 등록, 완료 처리는 별도 함수',
            'intro':'WSARecv를 호출할 때 버퍼를 맡긴다. 그 작업의 완료 통지를 GetQueuedCompletionStatus로 꺼낸 뒤 바이트를 처리한다.',
            'role':'NETWORK',
            'snippets':[
    ('IocpServer.cpp', [cut('IocpServer.cpp','void CIocpServer::PostRecv','p_operation->sp_session = sp_session;'),cut('IocpServer.cpp','void CIocpServer::PostRecv','const int result =',end='nullptr);'),cut('IocpServer.cpp','void CIocpServer::Run','GetQueuedCompletionStatus('),cut('IocpServer.cpp','void CIocpServer::HandleSessionCompletion','sp_session->recvBuffer.append(',end='m_game);')], 'CIocpServer / 수신 등록과 완료 처리')
],
            'notes':[
                ('읽을 순서',ref('IocpServer.cpp','void CIocpServer::PostRecv(')+' → '+at('IocpServer.cpp','void CIocpServer::Run(int index)')+'\nPostRecv에서 요청을 등록한다. Run이 완료를 꺼내 HandleSessionCompletion으로 넘긴다.'),
                ('세 가지 핵심 변수','PostRecv의 p_operation->bytes: OS에 맡긴 수신 버퍼.\nbytes: 이번 완료에서 실제 받은 길이.\nsp_session->recvBuffer: 아직 소비하지 않은 TCP 누적 데이터.\nsp_는 shared_ptr, p_는 일반 포인터를 표시한다.'),
                ('완료 ≠ 패킷 한 개','완료 한 번에 패킷 일부나 여러 개가 올 수 있다. 받은 바이트만 append하고 HandleClientPackets에서 경계를 찾는다.'),
                ('실패와 다음 수신','WSA_IO_PENDING은 등록된 비동기 작업이 대기 중이라는 뜻이다. 그 외 즉시 오류는 반환·종료한다. 정상 완료 뒤 PostRecv를 다시 호출해 다음 수신을 건다.'),
            ],
            'takeaway':'HandleSessionCompletion의 sp_session->sessionMutex는 같은 세션의 수신·송신 완료가 서로 다른 IOCP 쓰레드(워커)에 배정될 수 있기 때문에 필요하다.',
        },
        {
            'tag':'NETWORK / 패킷 정의와 처리 등록',
            'title':'번호·본문·허용 목록·처리는 네 군데다',
            'intro':'HandleClientPackets는 클래스 멤버가 아닌 자유 함수다. TCP 프레임을 나누고 시스템 요청과 게임 요청을 구분한다.',
            'role':'NETWORK',
            'snippets':[
    ('PacketDispatch.cpp', [cut('PacketDispatch.cpp','void HandleClientPackets','const string_view remaining',end='break;',count=0),cut('PacketDispatch.cpp','void HandleClientPackets','const auto type',end='size - Protocol::HeaderSize);'),cut('PacketDispatch.cpp','void HandleClientPackets','game.Submit('),cut('PacketDispatch.cpp','void HandleClientPackets','recvBuffer.erase(')], 'HandleClientPackets() / 완성된 프레임만 전달'),
    ('Packet.h', [cut('Packet.h','namespace Protocol','constexpr size_t HeaderSize',count=2)], '길이 2B + 종류 2B')
],
            'notes':[
                ('① 패킷 번호: Packet.h','Protocol::Type에서 정한다. 현재 CM_Move는 18이다. 게임 패킷은 CM_JoinGame=10부터 증가한다. 번호가 밀리지 않게 새 항목은 끝에 추가한다.'),
                ('② 본문: Protocol/Game.proto / MoveRequest','MoveRequest { double x = 1; double y = 2; }\n필드 번호 1/2는 패킷 번호가 아니다. 본문은 Protobuf 규칙으로 직렬화한다.'),
                ('③ 허용 목록: PacketDispatch.cpp / HandleClientPackets','CM_Move처럼 수신 가능한 타입을 조건에 등록한다. 버전 검사 후에만 게임 명령을 Submit한다. 자동 핸들러 등록 테이블은 없다.'),
                ('④ 실제 처리: GameLogic.cpp / HandleWorldPacket → HandleMove','CGameLogic::HandlePacket의 해당 분기가 본문을 파싱하고 게임 규칙을 검사한다. SYS 버전 검사·Ping은 네트워크 경로에서 바로 응답한다.'),
            ],
            'takeaway':'PacketSize()는 헤더와 길이를 검사한다. 본문이 다 올 때까지 기다리는 판단은 HandleClientPackets가 수행한다.',
        },
        {
            'tag':'NETWORK → MAIN / 명령 큐',
            'title':'Submit()과 Update() 사이가 첫 번째 경계',
            'intro':'네트워크 쓰레드(워커)는 게임 상태를 직접 바꾸지 않고 명령을 넣는다. 메인은 큐에서 꺼낸 뒤 잠금을 풀고 처리한다.',
            'role':'MAIN',
            'snippets':[
    ('GameLogic.cpp', [cut('GameLogic.cpp','void CGameLogic::Submit','lock_guard lock(m_commandsMutex);'),cut('GameLogic.cpp','void CGameLogic::Submit','m_commands.push_back(',count=2),cut('GameLogic.cpp','void CGameLogic::Update','lock_guard lock(m_commandsMutex);',count=1),cut('GameLogic.cpp','void CGameLogic::Update','for (int i = 0;',end='m_commands.pop_front();'),cut('GameLogic.cpp','void CGameLogic::Update','HandlePacket(player,')], 'CGameLogic::Submit() / Update()')
],
            'notes':[
                ('정확히 열 함수',ref('GameLogic.cpp','void CGameLogic::Submit(')+'\n'+ref('GameLogic.cpp','void CGameLogic::Update()')),
                ('m_commandsMutex를 찾기','Submit: 여러 네트워크 쓰레드(워커)가 push.\nUpdate: 메인이 pop.\n같은 deque를 동시에 수정하므로 두 곳에서 같은 mutex를 사용한다.'),
                ('왜 string(payload)로 복사할까','payload는 수신 문자열을 바라보는 string_view다. 네트워크가 recvBuffer를 지우거나 변경해도 메인이 읽을 본문이 살아 있어야 한다.'),
                ('왜 잠금 밖에서 HandlePacket을 실행할까','Update는 최대 128개를 지역 work로 옮긴 뒤 잠금을 푼다. 게임 처리 중에도 네트워크가 새 명령을 넣을 수 있다. 전역 2,048개·연결별 32개 상한도 확인한다.'),
            ],
            'takeaway':'중요한 관찰: m_commandsMutex는 명령 큐를 보호한다. 모든 stPlayer 상태를 이 잠금으로 보호하는 구조가 아니다.',
        },
        {
            'tag':'MAIN / 명령 처리와 시간 진행',
            'title':'HandleMove()는 목표 설정, Tick()은 시간 진행',
            'intro':'이동 명령을 받으면 목적지를 저장한다. 실제 위치 갱신은 전장 틱에서 수행한다. 메인이 두 경로의 실행 순서를 정한다.',
            'role':'MAIN',
            'snippets':[
    ('GameLogic.cpp', [cut('GameLogic.cpp','void CGameLogic::HandleWorldPacket','HandleMove(player, Parse<wire::MoveRequest>'),cut('GameLogic.cpp','void CGameLogic::HandleMove','player.goalX = request.x();',count=3),cut('GameLogic.cpp','void CGameLogic::HandleMove','SendActionReply(player, true, "Move accepted");'),cut('GameLogic.cpp','void CGameLogic::Update','const auto interval'),cut('GameLogic.cpp','void CGameLogic::Update','for (int catchup'),cut('GameLogic.cpp','void CGameLogic::Update','Tick();'),cut('GameLogic.cpp','void CGameLogic::Tick','UpdateSkills();',count=2)], 'CGameLogic / 명령과 틱의 처리 지점')
],
            'notes':[
                ('HandlePacket()에서 자세히 볼 것',ref('GameLogic.cpp','void CGameLogic::HandlePacket(')+'\nHandleGamePacket/HandleWorldPacket 분기 → 입장 여부 → HandleMove에서 goalX/goalY/moving이 바뀌는 줄을 찾는다.'),
                ('Update()에서 자세히 볼 것','Update의 틱 판정: m_nextTick, interval, catchup.\n현재 TickRate=20이면 목표 간격 50ms다. 메인 반복마다 Tick이 무조건 실행되는 것은 아니다.'),
                ('Tick()에서 자세히 볼 것',ref('GameLogic.cpp','void CGameLogic::Tick()')+'\n스킬 갱신 → 이동·MP 계산 → 승패 → 리플레이 → BroadcastWorldState 순서를 읽는다.'),
                ('실제 시간과 목표 주기는 다르다','지연 시 최대 5회 따라잡고 더 밀리면 누락 틱을 집계한다. 현재 20Hz에서 Snapshot은 4틱마다, 즉 목표 5Hz로 전송한다. MP는 tickRate 틱마다 회복한다.'),
            ],
            'takeaway':'같은 CGameLogic 클래스가 GameBattle.cpp·GameSkills.cpp·GameSnapshot.cpp에도 나뉘어 구현돼 있다. 파일명과 클래스명은 다를 수 있다.',
        },
        {
            'tag':'MAIN → NETWORK / 응답 송신',
            'title':'SendPacket()은 큐 적재, PostSend()는 I/O 등록',
            'intro':'메인이 만든 응답을 stGameChannel로 넘긴다. IOCP 쓰레드(워커)를 깨워 실제 WSASend를 등록하고, 송신 완료 후 남은 데이터를 이어 보낸다.',
            'role':'NETWORK',
            'snippets':[
    ('GameLogic.cpp', [cut('GameLogic.cpp','void CGameLogic::SendPacket','sp_channel->pendingSendBytes += packet.size();',end='sp_channel->notifySend();')], 'CGameLogic::SendPacket() / 응답 적재'),
    ('IocpServer.cpp', [cut('IocpServer.cpp','void CIocpServer::PostSend','sp_session->sendPending = true;',count=2),cut('IocpServer.cpp','void CIocpServer::PostSend','const int result =',end='nullptr);'),cut('IocpServer.cpp','void CIocpServer::HandleSessionCompletion','m_sentBytes += bytes;',count=3)], 'CIocpServer::PostSend / HandleSessionCompletion')
],
            'notes':[
                ('읽을 함수 연결',ref('GameLogic.cpp','void CGameLogic::SendPacket(')+'\n'+ref('IocpServer.cpp','void CIocpServer::NotifySend(')+'\n'+ref('IocpServer.cpp','void CIocpServer::PostSend(')),
                ('송신 데이터가 머무는 세 곳','sp_channel->sendQueue: 메인 ↔ 네트워크 큐.\nsp_session->sendBuffer: 연결의 송신 대기 문자열.\nPostSend의 p_operation->bytes: 실제 I/O 버퍼.'),
                ('깨우기와 중복 송신 방지','notifySend는 NotifySend(id)를 호출한다. null OVERLAPPED와 세션 ID로 보낸 사용자 알림을 Run이 처리한다. sendPending은 진행 중인 송신이 있을 때 중복 등록을 막는다.'),
                ('부분 송신을 읽는 지점','HandleSessionCompletion의 송신 완료 분기는 실제 bytes만큼만 소비한다. 그 뒤 PostSend가 남은 문자열을 다시 등록한다. sendQueue과 sendBuffer 모두 송신 대기량 상한을 검사한다.'),
            ],
            'takeaway':'SendPacket 호출·WSASend 반환·송신 완료·상대의 게임 처리 완료는 서로 다른 시점이다.',
        },
    ]
    pages.extend(concurrency_pages(ref, at))
    return pages


def concurrency_pages(ref, at):
    return [
        {
            'tag':'동시성 핵심 / 잠금과 소유권',
            'title':'mutex·atomic·shared_ptr는 역할이 다르다',
            'intro':'스레드 이름만 외우지 말고 ‘어떤 데이터를 누가 함께 쓰는가’를 본다. 아래 이름을 코드에서 검색하며 잠금 구간을 확인한다.',
            'kind':'table','size':10.7,
            'headers':['찾을 멤버 / 위치','공유하는 실행 주체','보호하거나 유지하는 것'],
            'widths':[285,235,356],
            'rows':[
                ['m_commandsMutex\nCGameLogic::Submit / Update','네트워크 쓰레드(워커) + 메인','m_commands에 넣고 꺼내는 작업. HandlePacket 실행 동안은 잡고 있지 않는다.'],
                ['stGameChannel::sendMutex\nCGameLogic::SendPacket / CIocpServer::PostSend','메인 + 네트워크 쓰레드(워커)','sendQueue, pendingSendBytes, notifySend, sendNotifyPending을 함께 다룬다.'],
                ['stIocpSession::sessionMutex\nCIocpServer::HandleSessionCompletion / Update / Stop','여러 IOCP 쓰레드(워커) + 메인','한 연결의 recvBuffer, sendBuffer, sendOffset, sendPending, 종료 상태를 직렬화한다.'],
                ['CIocpServer::m_sessionsMutex\nHandleAccept / Run / CloseSession','여러 IOCP 쓰레드(워커) + 메인','세션 목록과 ID를 보호한다. 목록에서 shared_ptr를 복사한 뒤 세션 처리를 한다.'],
                ['CBattleWorkers::m_battleMutex\nRun / Work','메인 + 전투 쓰레드(워커)','작업 세대·함수·남은 워커 수를 동기화한다. 실제 계산은 잠금 밖에서 한다.'],
                ['atomic<bool>, atomic<size_t>\nstGameChannel / CIocpServer','서로 다른 스레드','connected, stopping, outstanding 등 개별 상태의 안전한 접근. 여러 필드의 일괄 갱신을 보장하지는 않는다.'],
                ['shared_ptr<stIocpSession>\nstIoOperation::sp_session','진행 중인 I/O + 완료 처리','세션 객체의 생존 기간을 늘린다. 객체 내부 데이터를 동시에 수정해도 안전하게 만드는 기능은 아니다.'],
            ],
            'takeaway':'설명 연습: mutex는 데이터 접근, atomic은 개별 값의 접근, shared_ptr는 객체 수명이라는 세 문장으로 구분한다.',
        },
        {
            'tag':'MAIN + BATTLE / 작업 분배와 완료 대기',
            'title':'CBattleWorkers::Run()과 Work()를 같이 읽기',
            'intro':'Run은 메인이 호출하는 작업 제출·대기 함수다. Work는 생성해 둔 전투 스레드에서 반복 실행되는 함수다.',
            'role':'BATTLE',
            'snippets':[
    ('BattleWorkers.cpp', [cut('BattleWorkers.cpp','void CBattleWorkers::Run','m_remaining = m_count;',end='m_calculate = {};'),cut('BattleWorkers.cpp','void CBattleWorkers::Work','lock.unlock();'),cut('BattleWorkers.cpp','void CBattleWorkers::Work','for (size_t i = index;',end='++processed;'),cut('BattleWorkers.cpp','void CBattleWorkers::Work','if (--m_remaining == 0)',end='m_done.notify_one();')], 'CBattleWorkers::Run() / Work()')
],
            'notes':[
                ('먼저 볼 클래스와 함수',ref('BattleWorkers.cpp','void CBattleWorkers::Run(')+'\n'+ref('BattleWorkers.cpp','void CBattleWorkers::Work(')+'\n생성자의 m_threads.emplace_back도 확인한다.'),
                ('m_generation과 m_remaining','m_generation: 새 작업 묶음이 시작됐는지 구분한다.\nm_remaining: 아직 끝나지 않은 워커 수다. 마지막 워커가 0으로 만들고 메인을 깨운다.'),
                ('4개 워커라면','Work(0): 결과 0,4,8,...\nWork(1): 결과 1,5,9,...\n나머지도 같은 방식이다. 각 워커가 서로 다른 results[i]만 쓴다.'),
                ('wait가 잠금을 잡고 막는가','condition_variable::wait는 기다리는 동안 mutex를 놓고, 깨어나면 다시 잡는다. 그래서 Work가 m_remaining을 줄일 수 있다. m_count=1이면 새 스레드 없이 메인에서 같은 함수를 수행한다.'),
            ],
            'takeaway':'핵심: 매 틱 스레드를 새로 만드는 구조가 아니다. 기존 워커를 깨우고, 모두 끝날 때까지 메인이 기다린다.',
        },
        {
            'tag':'BATTLE → MAIN / 실제 병렬 계산',
            'title':'MovePlayers()에서 계산과 반영을 나눠 보기',
            'intro':'독립 입력 만들기 → 워커별 결과 계산 → 완료 대기 → 메인의 상태 반영. 이 네 단계가 전투 병렬화의 기본 형태다.',
            'role':'BATTLE',
            'snippets':[
    ('GameBattle.cpp', [cut('GameBattle.cpp','void CGameLogic::MovePlayers','vector<stMovementResult> results'),cut('GameBattle.cpp','void CGameLogic::MovePlayers','m_battleWorkers.Run(',end='});'),cut('GameBattle.cpp','void CGameLogic::MovePlayers','players[i]->unit.set_x(',count=4)], 'CGameLogic::MovePlayers() / 계산 후 메인 반영')
],
            'notes':[
                ('읽을 순서',ref('GameBattle.cpp','void CGameLogic::MovePlayers()')+'\n→ '+ref('BattleCalculation.cpp','stMovementResult CalculateMovement(')+'\n→ CBattleWorkers::Run / Work'),
                ('안전하게 병렬 계산하는 조건','메인이 입력·결과 배열 크기를 먼저 정한다. 워커는 각자 다른 결과 칸에 쓰고 메인은 기다린다. 계산 중 입력·건물·stPlayer를 변경하지 않으므로 안정된 상태를 읽는다.'),
                ('같은 패턴이 쓰이는 곳','GameSkills.cpp의 UpdateSkills: 스톰 대상별 피해 계산.\nGameSnapshot.cpp의 BroadcastWorldState: 수신자별 시야 선택·Protobuf 직렬화.\n둘 다 완료 후 메인이 결과를 반영하거나 송신 큐에 넣는다.'),
                ('병렬화 범위를 정확히 말하기','일반 공격, 사망, 킬 집계, 승패 확정은 메인 처리다. 전투 전체가 병렬 실행되는 것은 아니다. VerifyParallel=1은 같은 입력을 직렬로 다시 계산해 결과를 대조한다.'),
            ],
            'takeaway':'면접 핵심: 독립 계산만 나누고 게임 상태의 반영 순서는 메인에서 유지했다. 안전성 근거는 읽기/쓰기 범위와 완료 대기다.',
        },
        {
            'tag':'NETWORK / I/O 객체 수명',
            'title':'CIoBufferPool은 어떤 메모리를 재사용할까',
            'intro':'stPlayer나 모든 Protobuf 메모리를 관리하는 풀이 아니다. Accept·Receive·Send에 사용하는 stIoOperation을 재사용한다.',
            'role':'NETWORK',
            'snippets':[
    ('IoBufferPool.h', [cut('IoBufferPool.h','struct stIoOperation','OVERLAPPED overlapped',end='DWORD length')], 'stIoOperation / 버퍼와 완료 정보'),
    ('IoBufferPool.cpp', [cut('IoBufferPool.cpp','stIoOperation* CIoBufferPool::Acquire','if (m_enabled && !m_free.empty())',end='++m_reused;'),cut('IoBufferPool.cpp','void CIoBufferPool::Release','p_item->sp_session.reset();',count=4)], 'CIoBufferPool::Acquire() / Release()')
],
            'notes':[
                ('이 파일 하나부터 열기',ref('IoBufferPool.cpp','stIoOperation* CIoBufferPool::Acquire(')+'\n'+ref('IoBufferPool.cpp','void CIoBufferPool::Release(')+'\n헤더에는 함수 목록이 있고, 실제 할당·반환 처리는 IoBufferPool.cpp에 있다.'),
                ('bytes[4096] + OVERLAPPED','각 operation에 버퍼와 작업 식별 정보가 붙어 있다. OS에 I/O를 맡긴 뒤에는 완료를 회수할 때까지 객체와 버퍼의 주소·수명이 유지돼야 한다.'),
                ('Acquire와 Release의 시점','PostRecv/PostSend/PostAccept가 획득한다. 완료 통지를 처리한 Run이 CIocpServer::ReleaseOperation을 통해 반환한다. I/O가 등록되지 않은 즉시 실패도 반환 경로다.'),
                ('재사용과 상한','UsePool=1: m_free에서 재사용하고 필요할 때 상한까지 생성한다. 처음부터 전부 만들지 않는다.\n0: 같은 호출 지점에서 new/delete한다.\n풀 목록은 mutex로 보호한다.'),
            ],
            'takeaway':'풀을 쓴다는 것보다 더 중요한 질문: OS가 아직 쓰는 버퍼를 언제까지 보관하고, 어떤 시점에 재사용하는가?',
        },
        {
            'tag':'NETWORK + MAIN / 종료 순서',
            'title':'CloseSession() 후에도 완료 처리 워커가 필요하다',
            'intro':'연결을 닫거나 I/O를 취소했다고 모든 완료 처리가 끝난 것은 아니다. 객체 수명은 마지막 완료를 회수할 때까지 이어진다.',
            'role':'NETWORK',
            'snippets':[
    ('IocpServer.cpp', [cut('IocpServer.cpp','void CIocpServer::CloseSession','CancelIoEx(',end='m_sessions.erase('),cut('IocpServer.cpp','void CIocpServer::Stop','while (m_outstanding.load()',count=4),cut('IocpServer.cpp','void CIocpServer::Stop','PostQueuedCompletionStatus(m_port, 0, 0, nullptr);'),cut('IocpServer.cpp','void CIocpServer::Stop','if (worker.joinable())',count=4)], 'CIocpServer::CloseSession() / Stop()')
],
            'notes':[
                ('읽을 함수',ref('IocpServer.cpp','void CIocpServer::CloseSession(')+'\n'+ref('IocpServer.cpp','void CIocpServer::Stop()')+'\nRun 끝의 ReleaseOperation(p_operation)도 이어서 확인한다.'),
                ('1. 신규 작업을 막고 닫는다','stopping을 켜고 listener·세션 소켓을 닫는다. Close는 연결 종료를 표시하고 취소를 요청한다. 세션 목록에서는 제거해도 진행 중인 operation이 shared_ptr로 세션을 보관한다.'),
                ('2. outstanding을 0으로 만든다','등록한 I/O마다 증가하고 완료 회수 시 감소한다. 이 동안 네트워크 쓰레드(워커)는 계속 살아 있어야 한다. 취소 완료도 Run으로 들어와 반환돼야 한다.'),
                ('3. 워커를 깨워 종료하고 합류','모든 I/O를 회수한 후 null OVERLAPPED·key=0 알림을 보내 Run을 끝내고 join한다. jthread의 자동 join만으로 대기 중인 OS 완료 큐가 자동으로 깨지는 것은 아니다.'),
            ],
            'takeaway':'종료 순서: 신규 I/O 차단 → 취소·소켓 닫기 → 완료 회수 → 워커 종료 알림 → join → 포트 해제.',
        },
        {
            'tag':'MAIN ↔ DATABASE / 비동기 저장',
            'title':'CDatabaseWorker::Run()에는 SQL 실행이 있다',
            'intro':'게임 메인은 DB 요청을 넣고 다음 일을 진행한다. DB 쓰레드(워커)가 결과를 돌려주면 메인이 현재 플레이어 상태와 대조해 적용한다.',
            'role':'DATABASE',
            'snippets':[
    ('DatabaseWorker.cpp', [cut('DatabaseWorker.cpp','void CDatabaseWorker::Run','m_ready.wait(',end='});'),cut('DatabaseWorker.cpp','void CDatabaseWorker::Run','job = move(m_jobs.front());',count=2),cut('DatabaseWorker.cpp','void CDatabaseWorker::Run','const auto values = up_connection->ExecuteSession('),cut('DatabaseWorker.cpp','vector<stDatabaseResult> CDatabaseWorker::TakeResults','lock_guard lock(m_databaseMutex);',end='return result;')], 'CDatabaseWorker::Run() / TakeResults()')
],
            'notes':[
                ('네 함수만 순서대로',ref('GamePersistence.cpp','void CGameLogic::SubmitDatabase(')+'\n'+ref('DatabaseWorker.cpp','bool CDatabaseWorker::Submit(')+'\n'+ref('DatabaseWorker.cpp','void CDatabaseWorker::Run()')+'\n'+ref('GamePersistence.cpp','void CGameLogic::UpdateDatabase()')),
                ('SQL 중에는 큐 잠금을 풀기','Run의 지역 unique_ptr<CDatabaseConnection>은 워커별 연결이다. m_jobs에서 꺼낸 뒤 잠금 범위를 벗어나 SQL을 실행한다. 느린 SQL이 큐 잠금을 계속 잡지 않는다.'),
                ('DB 쓰레드(워커)도 stPlayer를 바꾸지 않는다','SQL 결과를 m_results에 넣는다. CGameLogic::UpdateDatabase가 TakeResults로 가져와 요청 ID와 세션 token을 검사한 뒤 반영한다. 늦게 도착한 이전 요청 결과를 무조건 적용하지 않는다.'),
                ('같은 패턴을 나중에 더 보기','CRedisWorker::Run(): Redis 통신.\nReplay::CRecorder::Run(): 파일 기록.\n지금은 DB까지 큐의 생산자·소비자·결과 적용 주체를 설명할 수 있으면 된다.'),
            ],
            'takeaway':'네트워크/DB 스레드를 늘려도 CGameLogic의 최종 상태 갱신 주체는 메인이다. DB 분리는 SQL 대기를 게임 루프에서 떼어낸다.',
        },
        {
            'tag':'비교 경로 / WSAPoll',
            'title':'WSAPoll은 어디가 달라지는가',
            'intro':'현재 기본값은 IOCP다. WSAPoll은 같은 게임 명령 큐를 사용하는 대체 네트워크 구현이므로 IOCP 경로를 먼저 이해한 뒤 비교한다.',
            'kind':'table','size':11,
            'headers':['비교 지점','WSAPoll 경로','IOCP 경로'],
            'widths':[146,365,365],
            'rows':[
                ['먼저 열 함수',ref('NetworkWorker.cpp','void CNetworkWorker::Run(stop_token stop)')+'\nCNetworkWorker::Run',ref('IocpServer.cpp','void CIocpServer::Run(int index)')+'\nCIocpServer::Run'],
                ['이벤트의 의미','WSAPoll이 소켓의 준비 상태를 알려준다. 이후 ProcessSession이 recv/send를 호출한다.','PostRecv/PostSend로 비동기 I/O를 먼저 등록한다. Run이 GQCS로 그 작업의 완료를 받는다.'],
                ['실제 송수신',ref('NetworkWorker.cpp','bool CNetworkWorker::ProcessSession(')+'\nCNetworkWorker::ProcessSession의 recv / send 및 부분 송신 처리',ref('IocpServer.cpp','void CIocpServer::PostRecv(')+'\n'+ref('IocpServer.cpp','void CIocpServer::PostSend(')+'\nWSARecv / WSASend'],
                ['현재 세션 분배','RunTcpServer가 수락한 연결을 AddClient로 넘긴다. 각 CNetworkWorker가 자신의 m_clients를 처리한다.','여러 IOCP 쓰레드(워커)가 완료를 꺼낸다. 같은 연결에 접근할 수 있어 sp_session->sessionMutex로 보호한다.'],
                ['게임 처리와 연결','ProcessSession → HandleClientPackets → CGameLogic::Submit → 메인 Update','Run → HandleClientPackets → CGameLogic::Submit → 메인 Update'],
                ['읽을 때 주의','준비 상태는 작업 완료가 아니다. recv/send 결과 및 WSAEWOULDBLOCK 처리를 확인한다.','완료는 애플리케이션 패킷 완성이 아니다. 버퍼 수명과 TCP 프레임 누적은 여전히 필요하다.'],
            ],
            'takeaway':'이 프로젝트의 비교 지점은 네트워크 처리 방식이다. 게임 규칙과 명령 큐는 공유한다. 모드 이름만으로 성능 우열을 단정하지 않는다.',
        },
        {
            'tag':'SERVER.INI / 학습에 필요한 설정',
            'title':'어떤 설정이 어느 함수의 동작을 바꾸나',
            'intro':'프로젝트 루트 Server.ini의 현재 값이다. 실행 파일 옆 설정으로 실행한다면 그 복사본을 확인하고, 변경 후 서버를 다시 시작한다.',
            'kind':'table','size':10.7,
            'headers':['섹션.키 / 현재 값','연결해서 읽을 코드','값을 바꾸면 달라지는 것'],
            'widths':[241,290,345],
            'rows':[
                ['[Network] Mode=IOCP','RunTcpServer\nTcpServer.cpp / RunTcpServer','IOCP / WSAPoll 네트워크 구현 선택. 게임 규칙과 명령 큐는 같다.'],
                ['[Network] WorkerCount=2','CIocpServer 생성자\nCNetworkWorker 생성 반복','실제 네트워크 쓰레드(워커) 수. IOCP의 포트 동시 실행 제한에도 같은 값을 사용한다.'],
                ['[Battle] WorkerCount=4','CBattleWorkers 생성자 / Run\nCGameLogic 생성자','1: 메인 계산, 전투 쓰레드(워커) 추가 없음.\n2-16: 지정한 수의 전투 쓰레드(워커). World에만 적용한다.'],
                ['[Battle] VerifyParallel=0','MovePlayers / UpdateSkills / BroadcastWorldState','1: 병렬 결과와 같은 입력의 직렬 계산을 대조한다. 학습·정확성 확인용이며 속도 측정은 0.'],
                ['[Battle] TickRate=20','CGameLogic::Update의 interval\nTick / MovePlayers','20: 목표 50ms 틱, 1: 목표 1초 틱. 수신 루프 속도와 별개다. 이동·MP 계산에도 쓰인다.'],
                ['[Memory] UsePool=1\nPoolCapacity=8192','CIoBufferPool::Acquire / Release','1: I/O 객체 재사용, 0: new/delete.\n상한은 활성+캐시 블록 수. 세션 수나 바이트 수가 아니다.'],
                ['[Database] WorkerCount=2','CDatabaseWorker 생성자 / Run','DB 작업 워커 수. 각 워커가 자기 연결을 가진다. 네트워크·전투 쓰레드(워커) 수와 별도다.'],
            ],
            'after':'학습 비교는 한 번에 한 설정만: IOCP↔WSAPoll / Battle 1↔4 / UsePool 0↔1. 명령·로그·테스트 사용법 전체는 04_Server_Ini_Manual.pdf에서 확인한다.',
            'takeaway':'Battle WorkerCount=1은 ‘서버 전체가 싱글스레드’라는 뜻이 아니다. 네트워크·DB·기록·Redis 워커는 별도로 존재한다.',
        },
        {
            'tag':'직접 확인 / 디버거와 설명 연습',
            'title':'중단점 여섯 개로 한 요청을 끝까지 따라가기',
            'intro':'Debug 빌드에서 World 입장 후 이동 요청 한 건을 보낸다. Visual Studio의 호출 스택과 스레드 창을 함께 보며 실행 주체를 확인한다.',
            'kind':'table','size':10.7,
            'headers':['중단점 / 관찰 순서','변수·호출 스택에서 확인할 것','스스로 답할 질문'],
            'widths':[330,290,256],
            'rows':[
                ['1. CIocpServer::HandleSessionCompletion\n'+at('IocpServer.cpp','sp_session->recvBuffer.append('),'수신 완료 분기, bytes,\nsp_session->recvBuffer, 현재 스레드 ID','이번 완료 바이트만으로 패킷 하나가 완성됐는가?'],
                ['2. CGameLogic::Submit\n'+at('GameLogic.cpp','m_commands.push_back('),'type=CM_Move, payload,\nm_commands와 pendingCommands','왜 본문을 복사하며, 아직 위치는 왜 안 바뀌는가?'],
                ['3. CGameLogic::HandleMove\n'+at('GameLogic.cpp','player.goalX = request.x();'),'메인 스레드인지, goalX / goalY,\nplayer.unit.x/y와 moving','목적지와 현재 위치는 각각 언제 바뀌는가?'],
                ['4. CBattleWorkers::Work\n'+at('BattleWorkers.cpp','for (size_t i = index;'),'워커 index, i += m_count,\nMovePlayers → Run의 대기','각 워커가 다른 결과 칸을 쓴다는 것을 확인했는가?'],
                ['5. CGameLogic::MovePlayers\n'+at('GameBattle.cpp','players[i]->unit.set_x('),'Run 반환 후, results[i],\nstPlayer의 최종 x/y 및 MP','왜 워커 완료 전에는 이 값을 반영하면 안 되는가?'],
                ['6. CIocpServer::PostSend / HandleSessionCompletion\n'+at('IocpServer.cpp','WSASend(')+' / '+at('IocpServer.cpp','m_sentBytes += bytes;'),'sendPending, p_operation->bytes,\n송신 완료의 bytes와 남은 sendBuffer','WSASend 호출 직후 버퍼를 반환해도 되는가?'],
            ],
            'after':'stGameChannel의 sendQueue은 접수 응답과 상태 패킷 둘 다 담는다. 먼저 SM_ActionReply가 보일 수 있다.\n검증용 소수 클라이언트로 읽는다. 중단점은 타임아웃·측정에 영향을 주므로 이 상태의 지연을 성능 수치로 사용하지 않는다.',
            'takeaway':'완료 기준: 수신 등록/완료, 명령 큐, 메인 상태 갱신, 독립 계산, 응답 송신, 종료 시 버퍼 회수를 자신의 말로 연결한다.',
        },
    ]
