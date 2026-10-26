from build_study_pdfs import *

def guide():
    pages = [
Page('이 문서를 코드 옆에 두고 읽는 방법','START / 문서 사용법',[
P('이 프로젝트는 여러 게임 서버에서 한 개의 World 전장으로 이동하는 C++ 서버입니다. 게임 서버는 계정·유닛·서버 이동·채팅을 맡고, World는 유닛 이동·전투·승패를 판정합니다. 같은 실행 파일을 설정만 달리해 A/B/C/World 네 프로세스로 실행합니다.'),
P('현재 헤더는 길이 2+종류 2의 4바이트입니다(7쪽). 37-43쪽은 이전 8바이트 헤더의 측정 이력입니다. 44쪽부터 IOCP·병렬 계산·풀을 설명하고, 49-51쪽에 10/19 빌드의 반복 비교와 1,500명 시연을 담았습니다.'),
TableBlock(['페이지','읽는 주제'],[['3-5','전체 구조, 스레드 소유권, RAII'],['6-10','네트워크 쓰레드(워커), 패킷, 명령 큐, stPlayer'],['11-15','게임 루프, 전투, 스킬, 설정·CSV'],['16-21','ODBC, DB 쓰레드(워커), 저장 상태, 서버 이동, 채팅'],['22-26','리플레이, 더미 클라이언트, 자동 전투, 종료'],['27-29','과거 측정 해석, 구현 한계, 클래스 찾아보기'],['30-32','공통 로직과 Linear/Grid, 검증·측정, 새 지표'],['33-38','Redis 집계·조회·결과 저장과 과거 1,500명 시연'],['39-43','과거 워커 비교 조건·수치 표·그래프·한계'],['44-48','현재 IOCP·I/O 수명·메모리 풀·전투 계산 병렬화'],['49-51','10/19 빌드 15회 비교·그래프·1,500명 검증']],[1,4]),
H('아래쪽 코드 위치의 약어'),
Small('루트: D:/work/Josun_Server/git_ProJect\nS/ = MultiThreadServer/MultiThreadServer/src/\nD/ = DummyClient/src/ · R/ = docs/results/2026-10-16/\n예: S/GameLogic.cpp:641은 해당 파일의 641번째 줄입니다. 모든 경로는 위 루트 기준입니다.'),
Note('읽기 원칙','헤더에서 멤버·public/private 함수를 보고 같은 이름의 .cpp 구현으로 이동하세요. “이 값은 누가 바꾸나?”와 “이 작업이 늦어지면 어떻게 되나?”를 각 페이지에서 한 번씩 답해보면 구조가 연결됩니다.')
],[Ref('README.md','현재 구현 범위')]),

Page('프로세스 네 개 안에 무엇이 들어 있나','01 / 시스템 구성',[
Flow([('C++ 더미','Game 서버에 ID·비밀번호로 로그인'),('Game A / B / C','서버별 저장 유닛을 불러오고 World 이동권 발급'),('World','입장권·DB 상태 확인 후 전투 참가'),('복귀','사망·자발적 복귀·경기 종료 시 출발 Game으로 로그인')]),
TableBlock(['영역','실제 역할'],[['Game A/B/C','현재 CSV 기준 A=Zerg, B=Protoss, C=Terran. 서버 ID와 종족은 설정 데이터로 연결한다. 같은 종족 서버를 여러 개 둘 수 있다.'],['World','종족별 본진 3개, 유닛 이동·공격·스킬, 사망 복귀, 마지막 생존 건물의 진영 승리.'],['MSSQL project_db','계정 비밀번호 해시, 서버별 선택 유닛, 세션 소유권·임대 만료·재입장 대기. 종료 경기의 킬·죽음·진입 결과도 보관한다.'],['Redis','World가 확정한 총계를 저장하고 상위 10명·내 순위를 조회한다.'],['로컬 리플레이','전투 확정 상태와 사건을 파일에 기록한다. 오프라인 더미 뷰어가 읽는다.']],[1,3.1]),
P('Main.cpp는 인자 처리 후 설정·맵·CSV를 읽고 RunTcpServer를 호출합니다. RunTcpServer 안에서 CGameLogic을 만들고 네트워크 쓰레드(워커)를 시작합니다. 같은 exe여도 Game/World 역할에 따라 허용되는 명령과 전투 틱 실행 여부가 달라집니다.'),
Note('기본 실행과 서버 실행','MultiThreadServer.exe를 인자 없이 실행하면 기본 환경·DB 점검 모드입니다. 실제 수신 대기는 --server A 또는 --server World로 시작해야 합니다.')
],[Ref(S+'Main.cpp','int main('),Ref(S+'TcpServer.cpp','void RunTcpServer'),Ref('Data/GameServer.csv')]),

Page('멀티스레드의 핵심은 소유권 분리','02 / 실행 문맥',[
P('전투 데이터를 여러 스레드가 동시에 수정하는 구조는 아닙니다. 소켓 I/O와 DB·파일 작업은 별도 스레드에서 처리하고, 플레이어와 전장 상태 변경은 메인 스레드에 모읍니다. 이 구분이 현재 프로젝트의 가장 중요한 설명 지점입니다.'),
TableBlock(['실행 주체','소유·변경하는 것','다른 주체와 연결'],[['메인 스레드','accept, CGameLogic::Update, m_players, 건물·영역·승패','명령 큐 소비, DB 결과 반영, 송신 우편함 생산'],['CNetworkWorker N개','각자의 stClientSession·CSocket·수신/송신 버퍼','명령 Submit, stGameChannel 응답 소비'],['CDatabaseWorker M개','각 스레드 전용 ODBC 연결','값으로 받은 작업 실행, 결과 큐에 값 전달'],['Replay writer 1개','기록 파일 스트림','메인에서 직렬화한 바이트 큐 소비'],['CRedisWorker 1개','World의 hiredis 연결·순위 I/O','최신 총계 복사본, 조회/결과 큐'],['CChatRelay','추가 스레드 없음','메인 Update에서 nonblocking 중계 연결 진행']],[1.1,1.8,1.8]),
P('기본 설정은 네트워크 2, DB 2입니다. World에서 기록과 Redis를 켜면 메인 1 + 네트워크 2 + DB 2 + 기록 1 + Redis 1이 코드가 명시적으로 만드는 실행 흐름입니다. OS·ODBC 내부 스레드는 이 계산에서 제외합니다. 1,500명 시연은 네트워크 4, DB 6으로 별도 설정했습니다.'),
Note('“락이 없다”의 범위','m_players는 메인에서만 접근하므로 전장 상태용 mutex가 없습니다. 반면 명령 큐, 송신 우편함, DB 큐, 기록 큐는 스레드를 넘기 때문에 각각 동기화합니다. 단일 게임 스레드는 전투 처리량의 상한이 될 수 있습니다.')
],[Ref(S+'GameLogic.h','class CGameLogic'),Ref(S+'NetworkWorker.h','class CNetworkWorker'),Ref(S+'DatabaseWorker.h','class CDatabaseWorker'),Ref('Server.ini')]),

Page('CWinsockSession·CSocket: 자원을 닫는 책임','03 / 작은 RAII 클래스',[
TableBlock(['형식','담당 역할','소유권'],[['CWinsockSession','생성 시 WSAStartup, 소멸 시 WSACleanup','복사 금지. 서버 실행 범위의 가장 바깥에서 생존.'],['CSocket','SOCKET 핸들의 closesocket, Reset, Get','복사 금지·이동 허용. 이동한 원본은 INVALID_SOCKET.'],['stClientSession','소켓과 통신 버퍼, stGameChannel을 묶음','한 CNetworkWorker의 clients 벡터가 보유.'],['stHandlerGuard','콘솔 종료 핸들러 등록 해제','RunTcpServer의 지역 RAII 보조 구조체.']],[1,1.5,1.8]),
P('accept에서 만든 CSocket을 워커에 인계할 때 핸들을 복사하지 않고 이동합니다. 원래 지역 변수는 더 이상 소켓을 닫지 않으며, 인계가 모두 거절되면 지역 CSocket 소멸자가 연결을 닫습니다. 예외가 발생해도 같은 정리 규칙이 적용됩니다.'),
Code('CSocket client(accept(listener.Get(), nullptr, nullptr));\n// Add succeeds by moving ownership into the worker.\nif (workers[index]->AddClient(client)) { break; }'),
P('stClientSession의 channel은 shared_ptr입니다. 소켓이 닫힌 뒤에도 이미 큐에 들어간 명령이나 DB 결과가 우편함을 참조할 수 있으므로 메모리 수명을 늘립니다. 다만 연결이 끊겼다는 사실은 connected=false로 별도 표현하고, 메인은 이 값을 확인해 오래된 요청을 버립니다.'),
Note('공부할 C++ 포인트','shared_ptr는 객체가 살아 있는지를 관리합니다. 객체 내부의 vector나 플레이어 상태를 여러 스레드가 동시에 수정해도 안전하게 만들어 주지는 않습니다. “수명”과 “동시 접근”은 다른 문제입니다.')
],[Ref(S+'Network.h','class CWinsockSession'),Ref(S+'Network.h','class CSocket'),Ref(S+'NetworkWorker.h','struct stClientSession')]),

Page('CNetworkWorker: 접속을 받고 읽고 쓰기','04 / NetworkWorker.h와 NetworkWorker.cpp',[
Flow([('메인 accept','한 루프 최대 64개 수락, round-robin으로 AddClient 시도'),('AddClient','m_pendingClientsMutex 아래에서 소켓 인계, 워커 정원 확인'),('Run','m_pending을 clients로 옮기고 WSAPoll로 준비 상태 확인'),('ProcessSession','수신 누적 → HandlePackets / 우편함 → 부분 송신'),('종료','channel 연결 해제 표시, 세션 제거, CSocket 해제')]),
P('워커는 연결마다 스레드를 만들지 않습니다. 자신이 맡은 모든 소켓을 nonblocking으로 설정하고 WSAPoll에 전달합니다. 소켓을 기다리는 동안 메인 게임 루프가 DB나 recv에 묶이지 않도록 역할을 분리한 것입니다.'),
P('stClientSession의 recvBuffer는 아직 처리하지 못한 바이트, sendBuffer은 전송 대기 바이트, sendOffset는 이미 보낸 위치입니다. send가 일부만 성공하면 다음 루프에서 sendOffset부터 다시 보냅니다. WSAEWOULDBLOCK은 지금 준비되지 않았다는 뜻으로, 즉시 접속 오류로 처리하지 않습니다.'),
P('클라이언트 처리 예외는 해당 연결을 제거합니다. 워커 자체의 예외는 Failed 상태로 메인에 알려 서버 전체의 비정상 종료로 이어집니다. 구분 없이 예외를 삼키면 워커가 죽은 상태로 서버가 계속 정상처럼 보일 수 있습니다.'),
Note('현재 폴링의 대가','게임 스레드가 응답을 넣어도 WSAPoll을 즉시 깨우는 별도 장치는 없습니다. PollTimeoutMs로 최대 대기 시간을 제한합니다. 정원은 WorkerCount × MaxClientsPerWorker와 게임 로그인 정원을 각각 확인해야 합니다.')
],[Ref(S+'NetworkWorker.h','class CNetworkWorker'),Ref(S+'NetworkWorker.cpp','bool CNetworkWorker::ProcessSession'),Ref(S+'NetworkWorker.cpp','void CNetworkWorker::Run(stop_token')]),

Page('TCP 바이트를 패킷으로 나누는 방법','05 / Packet.h',[
P('TCP는 메시지 경계를 보존하지 않습니다. 한 패킷이 여러 recv로 나뉠 수 있고, 여러 패킷이 한 recv에 합쳐질 수 있습니다. Protocol::PacketSize는 최소 4바이트가 모였는지 확인하고 전체 길이를 검증합니다. 본문이 덜 왔으면 버퍼를 유지합니다.'),
TableBlock(['헤더 위치','크기','뜻'],[['0-1','2바이트','헤더 포함 전체 길이. 4~4096바이트.'],['2-3','2바이트','Protocol::Type 패킷 종류'],['4 이후','가변 길이','Game.proto 메시지를 직렬화한 본문']],[1,1,3]),
P('헤더의 다중 바이트 값은 big-endian으로 직접 읽고 씁니다. C++ 구조체 메모리를 그대로 보내지 않으므로 구조체 패딩과 x64 정렬에 의존하지 않습니다. 본문은 Protobuf 규칙을 따르며 헤더의 big-endian 규칙과 다릅니다.'),
H('SYS_ServerVersionCheck: 이름 test, 버전 5의 전송 예'),
Code('00 0C | 00 01 | 0A 04 74 65 73 74 10 05\nlen=12  type=1  protobuf body\n0A = field 1, wire type 2\n04 = UTF-8 byte length; 74 65 73 74 = "test"\n10 05 = field 2, varint value 5'),
P('HandlePackets는 완성된 패킷만 처리한 뒤 소비한 앞부분을 제거합니다. SYS_ServerVersionCheck와 SYS_Ping은 네트워크 쓰레드(워커)에서 바로 응답하고, 게임 명령은 CGameLogic::Submit으로 넘깁니다. 길이·본문이 잘못되거나 허용하지 않는 패킷이면 연결을 종료합니다.'),
Note('버전 확인은 접속 직후 한 번','헤더는 길이 2+종류 2이며 패딩이 없습니다. ServerVersionCheck 본문의 버전이 Protocol::Version=5와 같아야 다음 요청을 허용합니다. 응답에도 서버 버전을 보냅니다. 누락·불일치·중복 요청은 종료합니다. 매 패킷에 버전을 반복하지 않으며 본문 상한은 4092바이트입니다.')
],[Ref(S+'Packet.h','constexpr size_t HeaderSize'),Ref(S+'Packet.h','inline size_t PacketSize'),Ref('test-network.ps1','$versionCheckPacket')]),

Page('패킷 번호와 메시지 필드는 별개다','06 / Packet.h와 Game.proto 읽기',[
P('Packet.h의 Type은 SYS_ServerVersionCheck=1, CM_JoinGame=10 두 시작값만 지정합니다. 나머지는 선언 순서대로 1씩 증가하며 5~9는 비워 둡니다.'),
Small('SYS: 시스템 확인 / CM: 클라이언트 → 서버 / SM: 서버 → 클라이언트 / SS: 게임 서버 ↔ World'),
TableBlock(['번호','패킷 종류','번호','패킷 종류'],[
['1','SYS_ServerVersionCheck','19','CM_Attack'],
['2','SYS_ServerVersionCheckReply','20','CM_LeaveWorld'],
['3','SYS_Ping','21','SM_ActionReply'],
['4','SYS_Pong','22','SM_WorldState'],
['5-9','미사용 (시스템 예약)','23','SM_WorldEvent'],
['10','CM_JoinGame','24','CM_Skill'],
['11','SM_JoinGameReply','25','CM_Chat'],
['12','CM_RequestTicket','26','SM_ChatMessage'],
['13','SM_WorldTicket','27','SS_RelayConnect'],
['14','CM_ResumeGame','28','SS_RelayChallenge'],
['15','CM_Logout','29','SS_RelayAuth'],
['16','CM_EnterWorld','30','SS_RelayChat'],
['17','SM_WorldReply','31','CM_Rank'],
['18','CM_Move','32','SM_RankReply']],[.55,2.55,.55,2.1]),
Code('message JoinGameRequest {\n  string name = 1; string password = 2;\n}'),
P('CM_JoinGame은 헤더의 종류 10, 본문은 JoinGameRequest입니다. name=1과 password=2는 본문의 필드 태그입니다. SM_JoinGameReply(11)의 본문은 JoinGameReply입니다.'),
Small('새 게임 패킷은 enum 끝에 추가합니다. 기존 번호를 바꿨다면 통신 버전을 갱신하고 서버·더미를 함께 빌드합니다.')
],[Ref(S+'Packet.h','enum class Type'),Ref('Protocol/Game.proto','message JoinGameRequest'),Ref(S+'GameLogic.cpp','void CGameLogic::HandlePacket(')]),

Page('stGameChannel과 stCommand: 스레드 사이 우편함','07 / 동기화 경계',[
TableBlock(['멤버','누가 쓰고 읽나','필요한 이유'],[['connected / closeAfterSend','네트워크·메인에서 atomic 접근','연결 종료와 마지막 응답 후 닫기 의사 전달'],['pendingCommands','제출 시 증가, 메인 소비 시 감소','한 연결의 미처리 명령 제한'],['sendQueue / pendingSendBytes','메인이 생산, 네트워크가 소비\nsendMutex로 보호','응답 바이트와 누적 크기를 함께 관리'],['CGameLogic::m_commands','여러 워커가 생산, 메인이 소비\nm_commandsMutex로 보호','게임 상태 변경을 메인에 모음']],[1.5,2,2]),
P('stCommand는 shared_ptr<stGameChannel>, 패킷 종류, string payload를 소유합니다. Submit의 입력은 string_view지만 큐에는 string 복사본을 넣습니다. recv 버퍼가 다음 수신이나 erase로 바뀌어도 대기 중인 본문이 훼손되지 않습니다.'),
P('전역 명령 대기는 2048개, 한 채널은 32개로 제한합니다. Update는 한 번에 최대 128개를 로컬 work로 옮긴 뒤 mutex를 풀고 HandlePacket을 실행합니다. 큐 락을 잡고 전투·DB 제출·직렬화를 실행하지 않아 생산자의 대기를 줄입니다.'),
Flow([('워커','수신 바이트 검증 → Submit'),('공유 큐','짧은 잠금 안에서 복사본 저장'),('메인','최대 128개 꺼냄 → 잠금 해제 → HandlePacket'),('송신 경로','Send → sp_channel->sendQueue → 워커 send')]),
Note('순서 보장의 범위','한 TCP 연결의 바이트 순서는 유지됩니다. 서로 다른 클라이언트 요청의 전역적인 동시 순서는 보장하지 않습니다. 메인 큐에 들어온 순서로 처리하며, 공유 큐 자체가 lock-free 구조인 것도 아닙니다.')
],[Ref(S+'GameLogic.h','struct stGameChannel'),Ref(S+'GameLogic.cpp','void CGameLogic::Submit'),Ref(S+'GameLogic.cpp','void CGameLogic::Update')]),

Page('CGameLogic과 stPlayer: 전장의 실제 주인','08 / 중심 클래스와 내부 상태',[
P('CGameLogic은 한 서버 프로세스의 콘텐츠 상태를 관리합니다. GameLogic.cpp, GamePersistence.cpp, GameSkills.cpp, GameReplay.cpp, GameRanking.cpp는 별도 클래스가 아니라 CGameLogic 한 클래스의 멤버 함수를 역할별 파일로 나눈 것입니다.'),
TableBlock(['stPlayer 멤버 묶음','역할'],[['sp_channel / unit / p_definition','통신 우편함, 전송용 현재 상태, 읽기 전용 CSV 정의. definition은 stGameData 내부 항목을 가리킨다.'],['inWorld / moving / goalX,Y','전장 참가 여부와 이동 목표. 위치는 Tick이 갱신한다.'],['nextAttack / nextChat','steady_clock으로 공격·채팅 주기를 제한한다.'],['databaseToken / leaseUntil / renewAt','DB에서 인정하는 세션 소유권과 유효 시간.'],['databaseBusy / renewing','중복 DB 변경·갱신 작업을 조절한다.'],['p_stim / p_cloak / p_heal / skillCooldowns','현재 효과 정의와 스킬별 사용 가능 틱.'],['lockedUntil / plague / healTarget','상태 이상 만료, 주기 피해, 지속 치료 대상.'],['relayServerId / relayReady','일반 유저가 아닌 채팅 중계 연결의 인증 상태.']],[1.7,3]),
P('m_players는 stGameChannel*를 키로 사용하지만 stPlayer 안에 shared_ptr가 함께 있습니다. 키 포인터만 보고 소유권을 판단하면 안 됩니다. 로그아웃·연결 종료와 미처리 DB 작업을 정리한 뒤 맵에서 제거합니다.'),
P('m_config, m_map, m_data는 const 참조입니다. 시작 단계에서 생성한 객체들이 RunTcpServer 전체보다 오래 살아 있으므로 참조가 유효합니다. 런타임에 CSV를 다시 로드하거나 vector를 바꾸면 p_definition 포인터의 수명 규칙을 새로 설계해야 합니다.'),
Note('보여 주는 상태와 내부 상태','wire::UnitState에는 HP·좌표 등 전송할 값이 있습니다. DB 소유 토큰, 다음 공격 시각, 임시 큐 상태는 stPlayer에만 있습니다. 모든 내부 멤버를 클라이언트에 그대로 공개하지 않습니다.')
],[Ref(S+'GameLogic.h','struct stPlayer'),Ref(S+'GameLogic.h','const stServerConfig& m_config'),Ref(S+'GameData.h','struct stGameData')]),

Page('Update와 Tick은 같은 함수가 아니다','09 / 시간과 게임 루프',[
Flow([('Update 1','DB 완료 결과와 채팅 중계 상태 반영'),('Update 2','최대 128개 명령 처리, 끊긴 플레이어 정리'),('Tick','예정 시각이 되면 전투·이동·회복·승패 진행'),('기록·전송','기록 간격에 맞춰 Capture, 약 5Hz로 Snapshot')]),
P('Update는 accept 루프에서 반복 호출됩니다. Tick은 설정된 틱 주기에만 호출됩니다. 기본 20Hz는 목표 간격 50ms입니다. 매 Update가 50ms인 것도, 전투 요청마다 Tick을 한 번 도는 것도 아닙니다. 명령 처리는 Tick 밖의 Update 구간에도 발생합니다.'),
P('늦어졌을 때 최대 5번까지 따라잡는 틱을 수행합니다. 그래도 밀리면 m_missedTicks를 늘리고 다음 시각을 재설정합니다. 틱 생략이 0이어도 특정 틱의 계산 비용이 50ms를 넘을 수 있습니다. 따라서 평균·p95·최대와 생략 수를 함께 봐야 합니다.'),
TableBlock(['시간 표현','사용 예','의미'],[['steady_clock','다음 공격, 이동 루프 일정, 경기 재시작','로컬 경과 시간. 시스템 시각 조정의 영향을 줄임.'],['World m_tick','스킬 만료·주기 피해·MP 회복','시뮬레이션 진행 횟수 기준.'],['epoch 초','입장권 만료, DB lease, 재입장 시각','프로세스·DB 사이에 전달할 절대 시각.']],[1.2,1.8,2]),
P('Tick은 이동·스킬 상태를 갱신하고 생존 건물 수를 확인합니다. 한 진영의 건물이 남으면 승자, 모두 없어지면 무승부로 기록합니다. 전원 복귀 후 RoundRestartSeconds가 지나면 세 건물과 다음 경기 기록을 초기화합니다.'),
Note('측정 범위','m_tickMicros는 Tick 함수 실행 시간을 측정합니다. 그 앞에서 처리한 명령 큐와 DB 결과 반영 시간까지 포함한 전체 메인 루프 시간은 아닙니다.')
],[Ref(S+'GameLogic.cpp','void CGameLogic::Update'),Ref(S+'GameLogic.cpp','void CGameLogic::Tick'),Ref(S+'GameReplay.cpp','void CGameLogic::ReportMetrics')]),

Page('이동·공격·사망: 서버가 값을 확정한다','10 / 전투 요청 한 개 따라가기',[
P('클라이언트는 “이 좌표로 이동하고 싶다”, “이 대상을 공격하고 싶다”를 보냅니다. 위치·피해 결과를 직접 확정해서 보내지 않습니다. HandlePacket은 로그인/World 여부와 DB 처리 상태를 검사한 뒤 요청을 실행합니다.'),
TableBlock(['경로','검증과 상태 변경'],[['Move → Tick','좌표 유한값·맵 경계·건물 영역 검사 후 목표를 설정한다. Tick에서 속도/틱 비율만큼 직선 이동한다. 이동 선분과 건물을 함께 검사해 빠른 이동이 건물을 통과하는 일을 막는다.'],['Attack','대상 존재·적 진영·은신 탐지·사거리·공격 주기·상태 이상을 검사한다. 방어력·피해 타입·크기·보호막을 반영한다.'],['피해 후 HP=0','Return → SaveReturn으로 World 참가 상태를 내리고 DB 이동 처리를 제출한다. 복귀권은 저장 성공 뒤 응답한다.'],['LeaveWorld / back','자발적 복귀도 현재 정책상 100초 재입장 대기를 적용한다. 대기는 계정 DB 상태에도 저장한다.']],[1.1,3.6]),
P('기본 공격의 대상 탐색은 m_players를 순회합니다. Snapshot의 반경 후보는 Linear/Grid로 선택하지만 공격·스킬의 대상 탐색은 그대로입니다. 검색과 시야·정렬·전송 비용을 구별해 개선 범위를 설명합니다.'),
P('사용자가 직접 조작할 것이므로 범용 길찾기는 구현하지 않았습니다. 이동 명령은 직선 목표이며, 막히면 중단합니다. 유닛끼리의 충돌·밀어내기도 없어 같은 좌표에 여러 유닛이 겹칠 수 있습니다.'),
Note('A/B/C와 진영','아군 여부는 game_server 문자열이 아니라 race로 판정합니다. 서버 1과 2가 모두 Zerg이면 World에서 같은 진영이고 같은 Hive를 공유합니다. 현재 World는 서버마다 건물 하나가 아니라 종족별 건물 하나입니다.')
],[Ref(S+'GameLogic.cpp','void CGameLogic::HandlePacket'),Ref(S+'GameLogic.cpp','void CGameLogic::HandleAttack'),Ref(S+'GameLogic.cpp','void CGameLogic::ReturnToGame')]),

Page('스킬은 슬롯·데이터·상태로 연결된다','11 / GameSkills.cpp',[
P('SkillRequest.slot은 1~5입니다. 서버가 stPlayer.p_definition->skills[slot-1]에서 SkillIndex를 읽고 skillDefinitions에서 실제 정의를 찾습니다. 클라이언트가 임의의 스킬 ID를 보내 다른 유닛의 스킬을 사용하는 구조가 아닙니다.'),
TableBlock(['타입','핵심 처리'],[['Stim / Cloak / Burrow','스팀 효과 만료, 클로킹 시작·유지 비용, 버로우 전환 완료 틱을 관리한다.'],['Lockdown / Cleanse / Blind','기계 대상 정지, 상태 이상 제거, 시야 제한 등 대상 상태를 변경한다.'],['Heal','지속 치료 대상과 주기를 보관하고 실제 치료 틱마다 MP를 소비한다.'],['Storm / DarkSwarm','stAreaEffect에 위치·반경·만료 틱을 저장한다. 스톰 주기 피해와 원거리 공격 차단을 판정한다.'],['Plague','대상별 stPeriodicEffect로 주기 피해를 유지하고 MinTargetHP를 적용한다.']],[1.4,3.2]),
P('HandleSkill은 슬롯·쿨다운·HP/MP·대상 필터·진영·시야·거리·좌표·영역 수를 검증한 뒤 비용을 차감합니다. UpdateSkills가 지속 효과와 만료를 갱신합니다. SkillTicks는 밀리초를 틱으로 올림해 설정 시간보다 일찍 끝나지 않게 합니다.'),
P('MP는 기본 초당 1 회복하고 클로킹 유지 중에는 회복을 중단합니다. CanSee는 같은 진영, 은신 여부, 아군 탐지 유닛·본진 범위를 확인합니다. 보이지 않는 적을 Snapshot에서 빼고 공격·스킬 판정에서도 다시 확인합니다.'),
Note('데이터로 바꿀 수 있는 범위','CSV는 수치와 지원하는 타입을 선택합니다. 새로운 효과 로직을 CSV 한 행만으로 생성하지는 않습니다. 새 SkillType을 만들려면 로더 검증과 HandleSkill/UpdateSkills 분기를 함께 구현해야 합니다. 원작 전체 전투 공식의 완전 재현은 아닙니다.')
],[Ref(S+'GameSkills.cpp','void CGameLogic::HandleSkill'),Ref(S+'GameSkills.cpp','void CGameLogic::UpdateSkills'),Ref(S+'GameSkills.cpp','bool CGameLogic::CanSee'),Ref('Data/Skill.csv')]),

Page('CIniFile·stServerConfig: 설정을 런타임 값으로','12 / Config.h, Config.cpp',[
P('CIniFile은 section.key 형태로 문자열 값을 보관하고 Get/GetInt/GetNumber로 읽습니다. LoadServerConfig는 파일의 값을 범위가 확인된 stServerConfig로 변환합니다. 서버가 시작한 뒤 사용하는 값은 이 구성 스냅샷이며 자동 갱신하지 않습니다.'),
TableBlock(['항목','현재 의미'],[['Network.WorkerCount','송수신 워커 수. DB.WorkerCount와 별개.'],['MaxClientsPerWorker','로그인 전 연결·중계 연결도 포함하는 네트워크 상한.'],['MaxPendingSendBytes','느린 연결 때문에 대기 송신 데이터가 무한 증가하지 않게 제한.'],['Battle.MaxGamePlayers','Game 로그인 정원 기본값. [Server.A] MaxPlayers 등의 개별 값이 우선.'],['Battle.MaxWorldPlayers','World 참가 정원. 각 정원 설정의 허용값은 1~2048.'],['SnapshotRadius','양수이면 자신+근처 적 32+아군 31까지 전달. 0 전체 시야 모드는 작은 정원에만 허용.'],['SpatialSearch / SpatialCellSize','Linear/Grid 후보 검색, 셀 크기. VerifySpatialSearch=1은 결과 대조. 30-32쪽 참조.'],['Database.Realm / SessionLeaseSeconds','테스트 데이터 구분과 계정 세션 소유권 만료 기준.'],['Replay / Metrics','기록 간격·큐·파일 상한, 지표 출력 주기.']],[1.6,3.2]),
P('stGameServerDefinition은 CSV의 서버 ID·표시명·종족·주소·포트 한 행입니다. stMapConfig는 경계와 종족별 stSpawnPoint 세 개를 보관합니다. stSpawnPoint에는 입장 좌표뿐 아니라 본진 위치·반경·HP·보호막도 들어 있습니다.'),
Note('정원 예약도 인원이다','ValidateGameCapacity는 로그인 완료자뿐 아니라 DB 처리 중인 예약 인원도 셉니다. DB 제출 실패 시 token 등 예약 흔적을 정리합니다. 실제 소켓 수와 게임 로그인 수가 다른 이유를 함께 설명해야 합니다.')
],[Ref(S+'Config.h','class CIniFile'),Ref(S+'Config.cpp','stServerConfig LoadServerConfig'),Ref(S+'GamePersistence.cpp','void CGameLogic::ValidateGameCapacity'),Ref('Server.ini')]),

Page('stCsvTable·stGameData: 표를 게임 정의로 바꾸기','13 / 데이터와 참조 수명',[
TableBlock(['형식 / 파일','내용과 연결'],[['stCsvTable','컬럼명과 행 데이터를 읽고 Get/GetInt/GetNumber로 접근. 숫자·필수 컬럼·중복 ID 등을 시작 단계에서 검증.'],['stUnitDefinition / Unit.csv','유닛 기본 수치, 공격 방식·크기, 생체/기계, 탐지, Skill1~5.'],['stSkillDefinition / Skill.csv','SkillIndex를 키로 사용. Index는 독립 행 번호. 효과·대상·비용·주기·반경·중첩 정책.'],['stGameData','원본 stCsvTable과 형식화한 definitions 벡터·skillDefinitions 맵을 함께 보관.'],['StartPoint.csv','Index, MapIndex, Race, X, Y. MapIndex와 종족으로 선택.'],['Map.ini','0 이상인 맵 경계와 세 본진 수치. 시작점·건물도 경계 안인지 확인.']],[1.6,3.3]),
H('구체적 예: Ghost'),
P('UnitIndex=102의 Ghost는 Skill1=2, Skill2=3입니다. slot=1 요청은 SkillIndex=2 Cloaking, slot=2는 SkillIndex=3 Lockdown으로 해석됩니다. Skill3~5가 0이면 빈 슬롯입니다. Skill.csv의 행 배치 순서를 바꿔도 ID 연결은 유지됩니다.'),
P('유닛은 계정과 Game 서버별로 처음 배정해 DB에 보관합니다. 매 로그인마다 새로 무작위 선택하는 것이 아닙니다. 같은 서버로 돌아오면 저장한 UnitIndex로 정의를 다시 찾고 그 유닛으로 접속합니다.'),
P('데이터는 시작할 때 한 번 읽고 실행 중 수정하지 않습니다. 따라서 stPlayer가 stUnitDefinition과 stSkillDefinition을 const 포인터로 참조해도 정의 객체가 유지됩니다. 서버를 계속 켜 둔 채 CSV만 바꿔도 즉시 반영되지는 않습니다.'),
Note('일부 값은 포트폴리오용 임시 밸런스','이동 속도·지속 시간 등은 조정 가능한 시연 값입니다. 원작과 일치한다고 설명하지 말고, 데이터 검증·연결 방식과 서버 판정 구조를 보여 주세요.')
],[Ref(S+'CsvTable.h','struct stCsvTable'),Ref(S+'GameData.h','struct stUnitDefinition'),Ref(S+'GameData.cpp','stGameData LoadGameData'),Ref('Data/StartPoint.csv')]),
]
    pages.extend(storage_and_clients())
    from spatial_content import spatial_guide
    pages.extend(spatial_guide())
    from redis_content import redis_guide
    pages.extend(redis_guide())
    from final_content import final_guide
    pages.extend(final_guide())
    from worker_content import worker_guide
    pages.extend(worker_guide())
    return pages

def storage_and_clients():
    return [
Page('ODBC 클래스는 핸들과 쿼리 경계를 맡는다','14 / Database.cpp',[
TableBlock(['형식','역할'],[['stDatabaseConfig','MTS_DB_* 환경 변수와 기본 로컬 설정을 묶는다. DB 주소·이름·연결 제한 시간 등.'],['COdbcEnvironment','SQLHENV 생성·ODBC 버전 지정·해제. 복사 금지.'],['CDatabaseConnection','환경·SQLHDBC 소유. 생성/소멸 시 연결·해제. 공개 동작은 ExecuteSession, CheckConnection.'],['COdbcStatement','Database.h에 선언한 SQLHSTMT RAII. 구현은 Database.cpp. 문장 핸들을 함수 범위에 묶는다.'],['stDatabaseCheckResult','DB 점검 조회에서 얻은 서버·버전·DB명·전송·인증 정보를 담는 값.']],[1.4,3]),
P('CDatabaseConnection은 SQLDriverConnectW로 MSSQL에 연결합니다. 기본은 127.0.0.1의 project_db에 Windows 인증을 사용하는 로컬 개발 환경입니다. 로그인·쿼리 제한 시간을 두고 실패 시 SQLSTATE·서버 오류 번호·진단 메시지를 함께 확인할 수 있게 합니다.'),
P('ExecuteSession과 SaveMatch는 ExecuteCommand의 바인딩을 공유합니다. 세션은 16개 매개변수·5개 결과, 경기 저장은 XML 매개변수 한 개를 사용합니다. 계정명이나 비밀번호를 SQL 문장 문자열에 이어 붙이지 않습니다. DB 작업 스레드가 이 결과를 stDatabaseResult로 변환하고 메인에 전달합니다.'),
P('CheckConnection은 DB 이름·정수·한글 조회 등 연결과 인코딩 기본 경로를 검사합니다. DB 연결이 한 번 성공한 것과 계정/세션의 동시성 처리가 올바른 것은 별도 검증 대상입니다.'),
Note('설정의 목적을 설명하기','개발용 TrustServerCertificate 설정과 게임 TCP 구간의 암호화는 다른 문제입니다. ODBC 연결 옵션을 사용한다고 클라이언트와 게임 서버 사이의 비밀번호 전송까지 암호화되는 것은 아닙니다.')
],[Ref(S+'Database.h','struct stDatabaseConfig'),Ref(S+'Database.h','class COdbcStatement'),Ref(S+'Database.cpp','CDatabaseConnection::ExecuteSession'),Ref(S+'Database.cpp','CDatabaseConnection::CheckConnection')]),

Page('CDatabaseWorker: 느린 일을 값으로 넘긴다','15 / 작업 큐와 결과 큐',[
Flow([('메인 Submit','stDatabaseJob을 큐에 넣음. 정원 초과면 즉시 실패'),('DB 쓰레드(워커) Run','큐에서 꺼낸 뒤 잠금 밖에서 해시·ODBC 실행'),('결과 게시','stDatabaseResult에 ID·성공 여부·값·오류 저장'),('메인 TakeResults','완료 값 회수 후 현재 플레이어에 적용할지 판단')]),
P('각 DB 쓰레드(워커)는 자신의 CDatabaseConnection을 지연 생성해 재사용합니다. 여러 스레드가 하나의 ODBC 연결 핸들을 동시에 호출하지 않습니다. 작업 대기는 condition_variable로 잠들고 새 작업이나 종료 통지로 깨어납니다.'),
TableBlock(['데이터','핵심 내용'],[['stDatabaseJob','작업 ID·action·realm·account·server·소유 token·nonce·destination·password 등 값. stPlayer*를 넘기지 않는다.'],['stDatabaseResult','요청 ID, ok/error, playerId·unitIndex·race·reentryAt·leaseUntil 등 결과 값.'],['m_outstanding','대기+실행 중+메인이 아직 회수하지 않은 결과를 포함한다. 결과만 쌓이는 경우도 큐 제한에 걸린다.']],[1.2,3.6]),
P('로그인 비밀번호는 랜덤 salt와 PBKDF2-HMAC-SHA256 600,000회로 검증·저장합니다. 비싼 해시 작업도 DB 쓰레드(워커) 쪽이 맡으므로 전투 메인 스레드를 직접 막지 않습니다. 처리한 job의 password를 지우지만, 프로그램 내 모든 임시 문자열까지 안전하게 지웠다고 주장할 수는 없습니다.'),
Note('재시도에는 같은 식별자를 쓴다','연결 오류·시간 초과·교착 상태 계열에 한 번 재시도합니다. 같은 token/nonce로 DB 프로시저의 중복 처리 분기를 사용합니다. 모든 SQL이 자동으로 안전한 재시도가 되는 것은 아닙니다.')
],[Ref(S+'DatabaseWorker.h','struct stDatabaseJob'),Ref(S+'DatabaseWorker.cpp','bool CDatabaseWorker::Submit'),Ref(S+'DatabaseWorker.cpp','void CDatabaseWorker::Run')]),

Page('GamePersistence: DB 완료 후 응답하기','16 / 비동기 결과와 현재 세션',[
P('Login은 정원을 검사하고 ID·비밀번호 형식을 확인한 다음 임시 유닛 후보와 소유 token을 준비합니다. SubmitDatabase가 job ID와 stPendingDatabase를 연결합니다. 플레이어 상태는 메인에 남고, DB 쓰레드(워커)에는 값만 갑니다.'),
TableBlock(['함수','중요한 책임'],[['MakeDatabaseJob','현재 세션의 계정·서버·소유권 정보를 DB 작업 값으로 만든다.'],['SubmitDatabase','databaseBusy·m_pendingDatabase를 관리한다. 큐 제출 실패 시 예약을 되돌린다.'],['UpdateDatabase','완료 ID를 pending에 대조하고 채널·token·현재 상태를 확인한 뒤 반영한다.'],['RequestTransfer / SaveReturn','전장 이동 또는 복귀 상태를 DB에 먼저 저장하고 성공 뒤 티켓을 보낸다.'],['Logout / lease 갱신','token에 해당하는 세션을 닫거나 유효 기간을 연장한다. 만료하면 현재 연결을 유지하지 않는다.']],[1.6,3.2]),
H('왜 DB 결과를 곧바로 stPlayer*에 쓰지 않나'),
P('DB가 처리하는 동안 사용자는 연결을 끊거나 이동할 수 있습니다. 작업 ID는 어떤 요청의 결과인지, 소유 token은 어느 로그인 세대인지 구분합니다. stPendingDatabase의 shared_ptr는 우편함 수명을 유지하지만 “아직 유효한 로그인”까지 보증하지 않으므로 별도 검사를 합니다.'),
P('World 입장 시에는 DB Accept 성공 뒤에도 현재 경기와 본진·정원을 다시 확인합니다. 그 사이 본진이 파괴됐거나 정원이 찼으면 입장을 거절하고 복귀 경로를 제공합니다. 처음 검사가 성공했다고 늦게 온 결과를 무조건 적용하지 않습니다.'),
Note('장애 시 행동','DB 실패를 성공으로 바꿔 응답하지 않습니다. 실패 응답·연결 종료와 lease 만료 복구 경로가 있습니다. DB 장애 중에도 모든 기능을 완전히 유지하는 고가용성 설계는 현재 범위가 아닙니다.')
],[Ref(S+'GamePersistence.cpp','void CGameLogic::Login'),Ref(S+'GamePersistence.cpp','void CGameLogic::SubmitDatabase'),Ref(S+'GamePersistence.cpp','void CGameLogic::UpdateDatabase')]),

Page('계정 SQL 테이블과 세션 상태 전이','17 / Database/001_accounts.sql',[
TableBlock(['테이블','저장하는 것','기준 키'],[['PortfolioAccount','계정·고정 player ID·salt/hash·재입장 시각','Realm + AccountId'],['PortfolioUnit','Game 서버별 처음 배정한 UnitIndex·Race','Realm + AccountId + ServerId'],['PortfolioSession','소유 token·Owner·Location·lease·nonce·목적지','Realm + AccountId']],[1.4,2.1,1.6]),
P('PortfolioSessionCommand는 계정 단위 트랜잭션 안에서 sp_getapplock의 Exclusive 잠금을 얻어 로그인·이동·갱신·로그아웃을 조정합니다. 서버 프로세스마다 별개의 mutex를 두는 것으로는 같은 DB 계정의 중복 로그인을 막을 수 없기 때문입니다.'),
Flow([('Offline → Game','비밀번호 검증, 유효한 기존 lease가 없을 때 Login'),('Game → Transfer','소유 token 확인, 목적지 World와 nonce 저장'),('Transfer → World','Accept에서 목적지·nonce·만료·계정 정보 확인'),('World → Transfer → Game','사망/복귀권 이동, 새 소유 token으로 수락'),('Game/World → Offline','일치하는 token·owner의 Logout 또는 만료 후 회수')]),
P('늦게 도착한 이전 Logout은 현재 token·owner와 일치하지 않으면 새 로그인을 지우지 않습니다. Transfer/Accept의 재시도도 동일 token·nonce 조건으로 판단합니다. “프로세스가 죽으면 DB 행을 즉시 삭제한다”는 방식 대신 lease로 소유권을 일정 시간 뒤 회수할 수 있게 합니다.'),
Note('복구되는 것과 사라지는 것','계정·서버별 유닛·재입장 대기와 세션 소유권은 저장합니다. World의 진행 중 위치·HP·건물 상태를 복구해 같은 경기를 이어 하는 기능은 없습니다.')
],[Ref('Database/001_accounts.sql','CREATE TABLE dbo.PortfolioAccount'),Ref('Database/001_accounts.sql','sp_getapplock'),Ref(S+'GamePersistence.cpp','void CGameLogic::UpdateDatabase')]),

Page('CEntryTicket: 출발 Logout과 도착 Login','18 / 서버 이동 시퀀스',[
Flow([('Game 로그인','DB에서 유닛·소유권을 확보한 상태'),('RequestTicket','Game이 목적지 World 티켓 생성 → DB Transfer 성공 대기'),('Game 응답·닫기','티켓 전송, Game 콘텐츠 세션 해제, 송신 후 연결 종료'),('World EnterWorld','HMAC·만료·목적지·데이터 검증 → DB Accept'),('World 참가','새 token 소유권 확정, 시작점 배치, 상태 전송')]),
P('CEntryTicket::Issue는 계정·player ID·출발 Game·UnitIndex·만료·nonce·목적지·재입장 시각을 EntryClaims로 직렬화하고, 그 원본 바이트에 HMAC-SHA256을 붙입니다. 서버 공유 키는 Cluster.local.ini에서 읽고 클라이언트에 주지 않습니다.'),
P('Verify는 크기·서명·유효 시간을 검사합니다. 목적지 서버와 실제 CSV 종족·유닛 연결, 한 번 사용 여부는 콘텐츠·DB 단계에서도 확인합니다. 티켓만 복사했다고 동시에 여러 World 세션을 획득할 수 없도록 서명 검증과 DB 상태 전이를 함께 사용합니다.'),
P('이동은 소켓을 서버 간에 넘기는 것이 아닙니다. 더미가 티켓을 받아 World에 새 TCP 연결을 만들고 ServerVersionCheck 후 EnterWorld를 보냅니다. 복귀는 WorldTicket을 받아 출발 Game에 새 연결을 만들고 ResumeGame을 보내는 대칭 경로입니다.'),
Note('보안 범위','HMAC은 claims가 바뀌었는지 확인하는 서명입니다. 내용 암호화가 아니며 게임 TCP에 TLS도 없습니다. 공개망 서비스를 주장하려면 전송 암호화와 운영 키 관리·인증 방어를 추가로 설계해야 합니다.')
],[Ref(S+'EntryTicket.cpp','wire::WorldTicket CEntryTicket::Issue'),Ref(S+'EntryTicket.cpp','wire::EntryClaims CEntryTicket::Verify'),Ref(S+'GamePersistence.cpp','void CGameLogic::RequestTransfer')]),

Page('CChatRelay: 두 종류의 채팅 범위','19 / 인증된 서버 간 연결',[
TableBlock(['사용 위치 / 채널','전달 대상'],[['Game 일반 채팅','현재 Game 서버에 로그인한 사용자'],['Game 월드 채팅','중계 인증을 마친 Game 서버들의 로그인 사용자'],['World 일반 채팅','현재 World 전장에 참가한 사용자'],['World 월드 채팅','허용하지 않음. Game 월드 채팅과 World 참가자 채팅은 범위가 다름.']],[1.4,3.1]),
P('Game 프로세스의 CChatRelay는 World에 별도 TCP 연결을 유지합니다. World는 RelayConnect/Challenge/Auth를 통해 중계 서버를 인증하고 RelayChat을 다른 Game 중계 연결에 전달합니다. 받는 Game은 자신의 사용자에게 ChatMessage를 보냅니다.'),
P('중계 인증은 client nonce와 server nonce, 역할·서버 ID를 함께 서명합니다. 한쪽의 고정 문자열 서명을 계속 재사용하지 않습니다. 일반 사용자 연결이 중계 서버 역할을 흉내 내지 못하도록 HandleRelay와 로그인 경로를 구분합니다.'),
Flow([('CChatRelay::Update','메인 스레드에서 연결·ServerVersionCheck·인증·송수신 상태 진행'),('SendChat','Ready일 때 제한된 송신 큐에 요청 추가'),('World 중계','검증 후 연결된 Game 중계 채널에 전달'),('TakeMessages','Game 메인이 메시지를 가져와 사용자에게 배포')]),
Note('실패를 설명할 때','연결 실패 시 재연결을 시도하고 대기 메시지는 시간 초과를 처리합니다. 이 구조는 영구 메시지 로그·오프라인 보관·exactly-once 전달 시스템이 아닙니다. 접속 장애 중 미전달 메시지는 별도로 성공했다고 간주하지 않습니다.')
],[Ref(S+'ChatRelay.h','class CChatRelay'),Ref(S+'ChatRelay.cpp','void CChatRelay::Update'),Ref(S+'GameLogic.cpp','void CGameLogic::HandleChat'),Ref(S+'GameLogic.cpp','void CGameLogic::HandleRelay')]),

Page('Replay::CRecorder: 전투를 파일로 남기기','20 / GameReplay.cpp와 Replay.cpp',[
P('GameReplay.cpp의 StartReplay/CaptureReplay는 World 상태를 리플레이 모듈에 연결합니다. 기록에는 관전자용 전체 유닛 상태를 넣습니다. 클라이언트별 근처 시야 필터로 잘라 보낸 네트워크 Snapshot을 그대로 저장하는 구조는 아닙니다.'),
TableBlock(['기록 항목','의미'],[['Header','매치 ID·시작 시각·틱 설정·맵·사용 CSV/설정 사본.'],['Frame','현재 tick·전체 또는 변경 상태·제거 ID·전투 명령/사건.'],['주기 전체 스냅샷','시점 이동 시 처음부터 전부 재생하지 않게 만드는 기준 상태.'],['Footer','정상 완료 여부·종료 이유·요약 정보. 경기 종료와 서버 종료를 구분.']],[1.3,3.4]),
P('CRecorder::Capture는 메인에서 이전 상태와 비교하고 Protobuf 기록을 직렬화합니다. Enqueue로 넘긴 바이트는 별도 Run 스레드가 파일에 씁니다. 파일 I/O를 분리했지만 상태 복사·비교·직렬화 비용까지 모두 기록 스레드로 옮긴 것은 아닙니다.'),
P('파일은 MTSRPL01 식별자 뒤에 길이·FNV-1a 64비트 체크섬·Protobuf 레코드를 저장합니다. 쓰는 동안 .part, 정상 Footer 기록 후 .replay로 마감합니다. FrameTicks=1이면 매 틱, 대규모 시연은 4로 설정해 20Hz 시뮬레이션 중 5Hz 상태를 저장했습니다.'),
Note('기록 실패 정책','큐·파일 크기 제한이나 쓰기 오류가 나면 해당 기록을 실패 처리하고 전투는 계속합니다. 체크섬은 손상 검출용이며 악의적 변조 방지를 위한 암호학적 인증이 아닙니다. 비밀번호·티켓·Cluster 키·채팅 본문은 기록하지 않습니다.')
],[Ref(S+'GameReplay.cpp','void CGameLogic::StartReplay'),Ref(S+'Replay.cpp','void Replay::CRecorder::Capture'),Ref(S+'Replay.cpp','void Replay::CRecorder::Run')]),

Page('stArchive와 CReplayWindow: 저장한 상태 재생','21 / 오프라인 리플레이',[
Flow([('Replay::Load','헤더·레코드 길이·체크섬·순서·Footer 검증'),('stArchive::StateAt','목표 이전의 전체 스냅샷에서 변경분 적용'),('Replay::Apply','변경 유닛 교체, 제거 ID 반영, 건물·영역 갱신'),('CReplayWindow','재생 시계에 따라 프레임 진행, GDI+로 그리기')]),
P('stArchive는 Header/Footer와 프레임, 전체 스냅샷 인덱스를 담는 자료 구조입니다. FindTime은 시간에 대응하는 프레임 위치를 찾고 StateAt은 그 상태를 복원합니다. CReplayWindow는 DummyClient의 Win32/GDI+ 뷰어 클래스이며 서버와 DB가 없어도 파일만으로 동작합니다.'),
P('기본은 1배속 일시정지입니다. Play를 누르면 계속 진행하고 Pause 후 다시 Play해도 선택한 배속을 유지합니다. 0.25~16 배속, 시간 탐색, 첫 전투 이동, 유닛 선택을 지원합니다. 콘솔 모드의 status/events는 숫자와 사건을 확인할 때 사용합니다.'),
P('유닛이 100명보다 많으면 화면에서 같은 작은 구역에 겹친 같은 종족을 대표 아이콘과 인원으로 묶습니다. 그림의 겹침을 줄이는 표시 기능이며 기록 좌표를 이동하거나 실제 인원을 줄인 것이 아닙니다.'),
Note('무엇을 재현하는가','확정된 상태와 사건을 재생합니다. 입력만 저장한 뒤 같은 전투 코드를 다시 돌려 결과를 만드는 결정적 시뮬레이션은 아닙니다. .part는 기본 거부하고 allow-partial로 정상 앞부분만 읽을 수 있지만 중간 손상은 허용하지 않습니다.'),
Small('JPG/GIF 아이콘은 Assets/Replay에 보관하며 출처는 SOURCES.txt에 있습니다. 실행 중 내려받지 않습니다. 소스·포트폴리오를 외부 공개할 때 게임 이미지의 이용 조건을 별도로 확인할 필요가 있습니다.')
],[Ref(S+'Replay.h','struct stArchive'),Ref(S+'Replay.cpp','Replay::stArchive Replay::Load'),Ref(D+'ReplayWindow.h','class CReplayWindow'),Ref('Assets/Replay/SOURCES.txt')]),

Page('CClientConnection: 응답과 상태 수신을 나누기','22 / 대화형 더미 클라이언트',[
TableBlock(['멤버 / 함수','역할'],[['생성자','주소 연결과 ServerVersionCheck를 수행한 뒤 수신 jthread 시작.'],['Request','패킷 전송 후 기대하는 응답 종류를 기다린다. 타임아웃·연결 종료를 검사.'],['Receive','소켓 수신·패킷 조립. Reply, WorldState, 복귀권 등 종류별로 분리.'],['Snapshot / ReturnTicket','mutex 아래에서 현재 상태나 받은 티켓의 복사본 반환.'],['inWorld 상태','연결 존재와 실제 전장 참가 여부를 구별한다.']],[1.4,3.3]),
P('대화형 PlayClient는 사용자의 입력과 자동 전투 갱신을 조정합니다. world 명령은 Game 티켓 요청, 기존 연결 정리, World 연결·입장으로 이어집니다. 서버가 사망 복귀권을 보내면 원래 Game에 ResumeGame으로 로그인합니다. 사용자가 게임 서버에 남아 있는 것처럼 보이더라도 네트워크 연결은 바뀝니다.'),
P('Game에서 허용한 명령은 world, status, server, quit, chat 계열입니다. World에서는 move/attack/skill/back과 전장 상태를 사용합니다. Game 서버가 직접 World 좌표를 수정하지 않습니다.'),
P('이 연결 도우미는 한 연결에서 요청을 차례로 보내는 사용을 전제로 합니다. 일반 요청별 correlation ID를 두고 여러 미완료 요청을 자유롭게 섞는 RPC 시스템은 아닙니다. 응답 종류가 같은 요청을 병렬로 추가하려면 식별자와 대기 맵부터 설계해야 합니다.'),
Note('상태 분할 수신','WorldState는 한 번에 유닛 최대 4개씩 나뉘어 올 수 있습니다. 같은 tick의 조각을 모으고 complete=true인 마지막 조각까지 받은 상태를 사용합니다. 개별 TCP recv 단위나 중간 조각을 완성된 전장으로 취급하지 않습니다.')
],[Ref(D+'ClientConnection.h','class CClientConnection'),Ref(D+'ClientConnection.cpp','string CClientConnection::Request'),Ref(D+'ClientConnection.cpp','void CClientConnection::Receive'),Ref(D+'PlayClient.cpp')]),

Page('자동 전투·대규모 더미·부하 측정의 차이','23 / 더미 안의 역할 분리',[
TableBlock(['클래스 / 파일','목적'],[['stBattleOrder / CBattleDecision','받은 상태에서 이동·공격·스킬 주문을 선택하는 판단 데이터와 함수. 소켓이나 서버 HP를 소유하지 않는다.'],['CAutoBattle','대화형 CClientConnection에서 약 250ms마다 판단·요청. auto buildings는 건물 공략만 한다.'],['CBattleWorker / stBattleBot','BattleWorker.h/.cpp. 한 워커가 여러 stBattleBot 소켓을 WSAPoll로 관리한다.'],['stBattleCounters','대규모 참가·사망·재입장·공격·오류 등을 atomic으로 합산한다.'],['LoadClient.cpp의 Result','Ping/이동/혼합 요청의 RTT 샘플과 성공·거부·오류를 저장한다.']],[1.6,3.1]),
P('CBattleDecision은 가까운 적 유닛을 우선 공격하고 없으면 건물 목표를 선택합니다. 치료·스톰·플레이그 등 지원 행동도 고릅니다. 클라이언트 판단은 요청 후보일 뿐, 사거리·쿨다운·MP 등 최종 허용 여부는 서버가 다시 검사합니다.'),
P('1,500명 시연은 기본 8개 CBattleWorker와 각 워커의 stBattleBot 상태 머신을 사용합니다. Waiting/Game/World/Done/Failed 상태, 처리 중 패킷 종류, 송수신 버퍼를 개별 stBattleBot에 둡니다. 전원 입장 준비가 끝난 뒤 전투를 시작하고 서버가 준 재입장 대기를 따릅니다.'),
P('LoadClient는 연결별 순차 요청의 RTT를 모읍니다. 응답을 기다리는 closed-loop이라 느릴수록 실측 요청률이 떨어집니다. --clients는 1~128입니다. 추가한 fixed-combat은 이동/지정 상대 공격, --warmup/--samples는 예열 제외와 원본 표본 저장입니다. SessionLoad는 기존 계정 로그인·World 이동·로그아웃을 따로 잽니다.'),
Note('서로 다른 시험의 역할','1,500명 동작 시연, 64명 과거 RTT, 96명 워커 비교는 도구·데이터·목적이 다릅니다. 이번 비교는 39-43쪽에서 확인하세요. 대규모 더미의 오류 0을 1,500명 RTT 보증으로 바꾸지 않습니다.')
],[Ref(D+'BattleDecision.h','class CBattleDecision'),Ref(D+'AutoBattle.h','class CAutoBattle'),Ref(D+'BattleWorker.h','class CBattleWorker'),Ref(D+'LoadClient.cpp','int RunLoadClient')]),

Page('종료와 실패 경로도 설계의 일부다','24 / 수명 정리',[
Flow([('종료 요청','콘솔 핸들러는 atomic 플래그만 설정'),('접속 중단','메인 루프 탈출, listener.Reset'),('워커 합류','workers.clear → stop 요청·join → 각 소켓 정리'),('CGameLogic 정리','마지막 기록 마감, 가능한 DB Logout 제출'),('DB·Winsock 정리','DB 쓰레드(워커) 합류와 핸들 해제 후 Winsock 종료')]),
P('jthread가 있다고 모든 종료 문제가 저절로 해결되지는 않습니다. 네트워크 쓰레드(워커)는 bounded poll로 반복 확인하며, DB 쓰레드(워커)는 condition_variable을 깨워 종료를 처리합니다. CGameLogic보다 네트워크 쓰레드(워커)를 먼저 끝내야 워커가 이미 파괴된 game 참조를 사용하지 않습니다.'),
P('복귀권·로그아웃 응답 후 연결은 closeAfterSend로 정리합니다. 송신 큐가 비면 shutdown(SD_SEND)으로 송신 방향을 닫고, 수신을 계속 비우며 상대 EOF 또는 3초 제한까지 기다립니다. 읽지 않은 수신 데이터를 둔 채 급히 닫아 마지막 응답이 RST에 영향을 받는 경로를 줄입니다.'),
TableBlock(['실패 지점','현재 처리'],[['느린 수신자 / 송신 큐 상한','연결을 유지하며 무한 적재하지 않고 종료 경로로 전환.'],['DB 큐 가득 참','제출 실패, 로그인 예약 정리, 실패 응답.'],['DB 쓰레드(워커) 종료','대기 작업에 5초 drain 정책. 실행 중 ODBC 호출의 즉시 강제 취소 보장은 아님.'],['기록 큐·파일 오류','기록 실패 표시, .part 유지, 전투 계속.'],['강제 프로세스 종료','소멸자 실행 보장 없음. lease 회수와 부분 파일 검사가 별도 대응.']],[1.5,3.2]),
Note('말로 설명할 순서','“그냥 소멸자에서 닫습니다”에서 끝내지 말고, 새 일 차단 → 생산자 종료 → 소비자 마감 → 참조 대상 파괴 순서를 실제 지역 변수와 멤버 선언 순서로 확인하세요.')
],[Ref(S+'TcpServer.cpp','void RunTcpServer'),Ref(S+'GamePersistence.cpp','CGameLogic::~CGameLogic'),Ref(S+'DatabaseWorker.cpp','CDatabaseWorker::~CDatabaseWorker'),Ref(S+'Replay.cpp','void Replay::CRecorder::Finish')]),

Page('과거 검증 숫자를 읽는 방법','25 / 10/16 기록 · Redis 추가 전',[
TableBlock(['기존 실행','확인한 값','해석'],[['64명 Ping / 8초','5,120 요청, 오류 0\np95 0.480ms','네트워크 직접 응답 경로. 게임 틱 성능 아님.'],['64명 이동 / 10초','6,400 요청, 오류 0\np95 31.150ms','게임 명령 응답 RTT. 인터넷 지연 아님.'],['64명 혼합 / 60초','38,400 요청\n22,428 허용·15,972 거부','오류 0. 사거리·쿨다운 등 정상 거부와 구분.'],['1,500명 전투 / 240초','최대 World 1,500\n사망 2,954·재입장 1,551','전원 준비 후 전투. 계속 1,500명 생존 상태는 아님.']],[1.3,1.7,2]),
P('1,500명 실행은 같은 PC의 x64 Release·로컬 DB·loopback입니다. 서버별 네트워크 4/DB 6, 더미 WSAPoll 8을 사용했습니다. 유닛 수치는 유지하고 실행 사본의 건물 HP만 750,000으로 높였습니다. 유닛 공격 성공 6,684, 스킬 성공 1,237, 건물 공격 0, 접속/응답 오류 0이 기록됐습니다.'),
P('5초 지표 구간별 tick p95 중 최대가 78.966ms, 개별 Tick 최대가 104.199ms였습니다. 20Hz의 목표 50ms를 넘은 틱이 있습니다. skipped_ticks=0은 “모든 틱이 50ms 이내”라는 뜻이 아닙니다. 당시 다른 Debug 빌드·기능 검사도 함께 돌아 정밀 성능 비교로 쓰기 어렵습니다.'),
Note('완료 파일과 경기 승리는 다르다','1,470프레임·294 전체 스냅샷, 기록 293.95초, 65,174,921바이트의 리플레이는 Footer 검증을 통과했습니다. reason=server_shutdown, winner=0입니다. 정해진 시연 시간에 종료한 기록이며 승자가 나온 한 경기라고 소개하면 안 됩니다.'),
Small('이 페이지는 10/16의 과거 결과이며 Redis 활성 최종 실행은 37-38쪽입니다. 64명 RTT 시험은 사본 HP=100000, MP/InitialMP=1000, MoveSpeed=100으로 사망을 막았습니다. 원본 밸런스 경기와 동일 조건이 아니며, 장시간·반복 측정과 1,500명 RTT는 추가 과제입니다.')
],[Ref(R+'latency.csv'),Ref(R+'battle-1500-validation.txt'),Ref(R+'battle-1500-result.txt')]),

Page('현재 병목과 아직 구현하지 않은 것','26 / 사실과 개선안을 구별하기',[
H('근처 상태 전송은 전송량을 줄인다'),
P('SnapshotRadius>0이면 자신과 가까운 적 32명·아군 31명까지 보냅니다. 대규모 아군이 모여도 적 정보가 전부 밀려나지 않게 목록을 따로 제한합니다. 네트워크 표시를 줄일 뿐 서버 전투와 리플레이 참가자를 줄이지 않습니다.'),
P('Linear는 모든 수신자가 전체 좌표를 검색해 O(N²)입니다. Grid는 주변 셀로 후보를 줄이지만 밀집 시 최악 O(N²)가 남습니다. CanSee 내부의 은신 탐지 순회와 공격·스킬 범위 탐색은 이번 변경 대상이 아닙니다. “격자를 적용해서 전체 서버가 O(N)”이라고 말하지 않습니다.'),
TableBlock(['구분','현재 상태'],[['구현됨','IOCP, I/O 풀, 전투 계산 병렬화, 제한 큐, 비동기 DB·기록·Redis, signed ticket·lease. 공통 규칙 + Linear/Grid 후보 선택과 실행 중 목록 대조.'],['검증 근거','과거 RTT·검색·워커 비교는 30-43쪽. 현재 IOCP·풀·전투 쓰레드(워커) 비교 15회와 1,500명 시연은 49-51쪽.'],['미구현','IOCP, lock-free 큐, 전투 병렬 처리, 메모리 풀, TLS, World 진행 상태 복구.'],['다음 확인','Linear/Grid 서버 전체 반복 RTT, 밀집/은신 비용, 대규모 장시간 부하. 검색 함수·Redis 직접 조회 지연을 전체 서비스 지표로 쓰지 않는다.']],[1,3.8]),
P('CGameLogic은 단순하게 따라가기 쉽지만 전투·세션·중계·기록 연결을 함께 가진 큰 클래스입니다. 파일 분리는 탐색을 돕지만 클래스 책임이 완전히 분리된 것은 아닙니다. 실제 변경 이유가 생겼을 때 세션 전이와 전투 상태 등의 경계를 나누는 편이 설명하기 좋습니다.'),
Note('첨부 포트폴리오와의 관계','참고 PDF의 “문제 → 구조 → 실행 흐름 → 근거” 설명 순서만 참고했습니다. 그 자료의 IOCP·객체별 TaskQueue·SendArena·MySQL 설계를 이 프로젝트의 구현으로 가져오지 않았습니다.')
],[Ref(S+'GameLogic.cpp','void CGameLogic::BroadcastWorldState'),Ref(S+'GameSkills.cpp','bool CGameLogic::CanSee'),Ref(R+'battle-1500-validation.txt')]),

Page('클래스·자료 구조 빠른 찾아보기','27 / 코드 탐색 색인',[
TableBlock(['찾는 대상','파일 / 먼저 볼 곳'],[['CWinsockSession, CSocket','S/Network.h · 자원 생성·이동·해제'],['stClientSession, CNetworkWorker','S/NetworkWorker.h/.cpp · AddClient, Run, ProcessSession. PacketDispatch.cpp · 파싱'],['stGameChannel, CGameLogic::stPlayer','S/GameLogic.h · 공유 경계와 게임 상태 멤버'],['CGameLogic','S/GameLogic.cpp · Submit, HandlePacket, Update, Tick'],['CSpatialIndex / SpatialCheck','S/SpatialIndex.h · 좌표 검색, SpatialCheck.cpp · 검증'],['CGameLogic 저장·스킬·기록 함수','S/GamePersistence.cpp / GameSkills.cpp / GameReplay.cpp'],['CIniFile, stServerConfig, stMapConfig, stSpawnPoint','S/Config.h · LoadServerConfig, LoadMapConfig'],['stCsvTable, stGameData, stUnitDefinition/stSkillDefinition','S/CsvTable.h, GameData.h · LoadGameData'],['COdbcEnvironment, CDatabaseConnection','S/Database.h/.cpp · ExecuteSession, COdbcStatement'],['CDatabaseWorker, Job, Result','S/DatabaseWorker.h · Submit, Run, TakeResults'],['CEntryTicket, CChatRelay','S/EntryTicket.h / ChatRelay.h · Issue/Verify, Update'],['Replay::CRecorder, Replay::stArchive','S/Replay.h · Capture/Finish, StateAt/FindTime'],['CClientConnection, Reply','D/ClientConnection.h · Request, Receive'],['CAutoBattle, CBattleDecision, stBattleOrder','D/AutoBattle.h / BattleDecision.h · Update, Decide'],['CBattleWorker, stBattleBot, stBattleCounters','D/BattleWorker.h/.cpp · 대규모 소켓 상태 머신'],['CReplayWindow, stReplayAttackLine','D/ReplayWindow.h/.cpp · Win32/GDI+ 재생·그리기']],[2,3]),
P('Common.h는 표준 헤더와 using 선언을 관리합니다. Protocol은 Packet.h의 namespace, wire::*는 Protobuf 메시지입니다. Redis 경로는 33-36쪽에서 읽습니다.'),
Note('스스로 설명할 최종 과제','Move 한 번, Login 한 번, World 이동 한 번을 각각 “입력 → 큐 → 소유자 → 검증 → 상태 변경 → 응답” 순서로 소스에서 짚어보세요. 이 세 경로가 연결되면 나머지 클래스의 필요성도 설명할 수 있습니다.')
],[Ref(S+'Common.h'),Ref(D+'FlowCheck.cpp'),Ref(D+'AccountSkillCheck.cpp'),Ref(D+'LoadClient.cpp')]),
]
