# 코드 작성 규칙

이 프로젝트는 면접에서 코드 흐름과 선택 이유를 직접 설명할 수 있는 C++ 서버를 목표로 한다.
한 번에 읽히는 함수, 익숙한 이름, 명확한 데이터 소유권을 우선한다.

## 이름

| 대상 | 규칙 | 실제 예 |
|---|---|---|
| 클래스 | `C` + PascalCase | `CGameLogic`, `CIocpServer`, `CDatabaseWorker` |
| 구조체 | `st` + PascalCase | `stGameChannel`, `stPlayer`, `stServerConfig` |
| 클래스 멤버 | `m_` + camelCase | `m_players`, `m_commands`, `m_config` |
| 전역 변수 | `g_` + camelCase | `g_stopRequested` |
| 구조체 필드·지역 변수·일반 인자 | camelCase, 역할을 나타내는 명사 | `player`, `packet`, `sendQueue`, `pendingSendBytes` |
| 일반 포인터 `T*` | `p_` + camelCase | `stIoOperation* p_operation` |
| 공유 포인터 `shared_ptr` | `sp_` + camelCase | `const shared_ptr<stGameChannel>& sp_channel` |
| 단독 소유 포인터 `unique_ptr` | `up_` + camelCase | `unique_ptr<CClientConnection> up_world` |
| 약한 참조 `weak_ptr` | `wp_` + camelCase | `wp_session` (향후 사용 규칙, 현재 사용처 없음) |
| 포인터인 클래스 멤버 | `m_` + 종류별 접두사 | `m_up_redis`, `m_up_replay`, `m_sp_latest` |
| 함수 | PascalCase, 동작을 나타내는 동사 | `HandleMove`, `ValidateGameCapacity`, `FindPlayer`, `SendPacket` |
| 상수·열거형 | PascalCase | `MaxPendingCommands`, `IoKind::Receive` |

- 포인터 종류별 접두사는 매개변수·지역 변수·구조체 필드·클래스 멤버에 적용한다. `const`나 `&`로 스마트 포인터를 받아도 동일하다. 일반 참조인 `stPlayer& player`, 값 인자, 포인터를 담은 컨테이너 `m_sessions`·`m_bots`에는 붙이지 않는다.
- 클래스 멤버는 `m_`와 조합하고 공개 구조체 필드는 `sp_channel`, `p_definition`처럼 쓴다. C++ 진입점의 `argc`·`argv`, Windows API 핸들·콜백은 익숙한 API 표기를 유지한다.
- `sp_session.get()`을 별도 변수로 받으면 그 변수의 타입은 일반 포인터이므로 `p_session`이다. `.get()`은 소유권을 넘기지 않는다. 이름 변경을 위해 타입·소유권·수명을 바꾸지 않는다.
- `p_`만으로 소유권을 단정하지 않는다. 예를 들어 `p_operation`의 수명은 I/O 완료와 메모리 풀 반환 규칙으로 관리한다. `sp_`도 객체 내부 데이터의 쓰레드 안전성을 보장하는 표시는 아니다.
- 그 밖의 지역 변수에 자료형 접두사를 반복하지 않는다. `recv`, `send`, `db`, `id`, `hp`, `mp`처럼 익숙한 약어만 쓴다.
- 단위를 구분해야 하는 값은 `Bytes`, `Ms`, `Seconds`, `Ticks` 등을 붙인다.
- 구조체는 전달할 데이터 묶음이므로 공개 필드에는 `m_`를 붙이지 않는다. 클래스의 상태와 구분한다.
- 파일은 기능별 이름을 유지한다. `GameLogic.h` 안의 클래스는 `CGameLogic`이고 전투 구현은 `GameBattle.cpp`에 있다.
- Protobuf가 생성한 `wire::MoveRequest`, 외부 라이브러리, Windows API의 이름은 원래 규칙을 따른다.

## 함수와 코드 배치

- `Handle`은 요청·완료 처리, `Validate`는 입력·상태 검증, `Is`/`Can`은 참·거짓 판정에 쓴다.
- `Find`/`Get`은 조회, `Send`는 응답 전송 경로, `PostRecv`/`PostSend`는 비동기 I/O 등록을 뜻한다.
- DB 큐에 넣는 함수는 `SubmitDatabase`, 실제 저장은 `CDatabaseConnection::SaveMatch`처럼 책임을 구분한다. 큐 등록만으로 저장 성공을 응답하지 않는다.
- `CGameLogic::HandlePacket`은 공통 상태 확인 뒤 `HandleGamePacket`/`HandleWorldPacket`으로 넘긴다. 이동 목표 설정은 `HandleMove`에서 읽는다.
- `CIocpServer::Run`은 완료 통지 분배, `HandleAccept`는 새 연결 생성, `HandleSessionCompletion`은 기존 연결의 완료 처리를 담당한다.
- 불필요한 중첩은 조기 반환으로 줄인다. 클래스 함수는 헤더에 선언하고 cpp에 구현한다. 단순한 Get 함수와 짧은 RAII 함수는 헤더에 둘 수 있다.
- 들여쓰기 4칸, 중괄호는 다음 줄, 줄 길이 110자를 기준으로 `.clang-format`을 사용한다. 데이터 멤버는 가급적 한 줄에 하나씩 선언한다.
- 표준 헤더와 필요한 `using std::...`는 `Common.h`에서 관리한다.

## 패킷

- `SYS_`, `CM_`, `SM_`, `SS_` 이름과 기존 번호를 유지한다. 게임 패킷은 `CM_JoinGame=10`부터 순서대로다.
- 번호는 `Packet.h`, 본문 필드는 `Protocol/Game.proto`, 수신 허용은 `HandleClientPackets`, 게임 처리는 `CGameLogic`에서 확인한다.
- 패킷을 추가할 때 네 곳과 더미 클라이언트를 함께 확인한다. 본문 크기·역직렬화·접속 상태·게임 규칙을 검사한다.
- 헤더는 전체 길이 2바이트 + 종류 2바이트다. C++ 구조체 메모리를 그대로 전송하지 않는다.
- `SendMessage`는 Protobuf 직렬화, `SendPacket`은 송신 큐 등록, `PostSend`는 `WSASend` 등록이다. 서로 다른 완료 시점이다.
- 로그인·서버 이동의 성공 응답은 DB 결과를 메인이 확인한 뒤 보낸다. Redis 순위는 별도 비동기 경로다.

## 쓰레드와 주석

- 게임 상태의 최종 변경은 메인 쓰레드가 담당한다. 전투 쓰레드(워커)는 읽기 전용 입력과 서로 다른 결과 칸으로 계산한다.
- mutex 변수는 `sessionMutex`, `m_commandsMutex`, `m_battleMutex`, `m_databaseMutex`처럼 보호하는 대상·역할을 이름에 넣는다. `mutex`·`m_mutex`라는 변수명은 쓰지 않는다.
- 공유 큐에는 같은 mutex를 사용한다. 큐에서 꺼낸 뒤 실제 게임·DB 작업을 수행한다.
- `stGameChannel::sendMutex`는 `sendQueue`, `pendingSendBytes`, 송신 알림 상태를 함께 보호한다. `pendingSendBytes`는 누적 전송량이 아니라 현재 큐에서 대기하는 양이다.
- 세션 수명은 진행 중인 I/O의 `shared_ptr`로 보장한다. 수명 보장과 동시 접근 보호는 별개다.
- 주석은 한글로 실행 쓰레드, 락 범위, 데이터 수명, 검증 이유를 설명한다. 코드 문법을 반복하지 않는다.
- 설명에서는 IOCP 쓰레드(워커), 전투 쓰레드(워커), DB 쓰레드(워커)처럼 역할을 함께 쓴다.

| 잠금 변수 | 보호하는 대상 |
|---|---|
| `stIocpSession::sessionMutex` | 한 연결의 수신·송신 버퍼, I/O 진행·종료 상태 |
| `CBattleWorkers::m_battleMutex` | 전투 작업 세대, 계산 함수, 남은 쓰레드 수 |
| `CDatabaseWorker::m_databaseMutex` | DB 작업·결과 큐, 대기 수, 종료 상태 |
| `CRedisWorker::m_rankingMutex` | 최신 순위, 조회·결과 큐, 종료 상태 |
| `CIoBufferPool::m_poolMutex` | 풀 반환 목록, 할당·사용 통계 |
| `Replay::CRecorder::m_recordMutex` | 기록 큐, 대기 바이트, 종료 상태 |
| `CClientConnection::m_receiveMutex` | 수신 응답, 스냅샷, 복귀 티켓, 오류 |
| `CNetworkWorker::m_pendingClientsMutex` | 배정 대기 중인 연결 목록 |
| `playbackMutex` | 콘솔 리플레이의 커서, 재생·속도·시간 상태 |

## 기존 프로젝트에 적용할 때

기존 회사 코드는 인접 구현과 팀 규칙을 먼저 따른다. 개인 포트폴리오의 이름 변경을 회사 코드에 일괄 적용하지 않는다.
회사 서버의 Gate/Game/GameDB 경로나 SQL 행 크기 기준은 해당 프로젝트의 별도 규칙이다.
이번 정리는 개인 서버의 명명·함수 분리·주석 정리이며, 성능 개선 수치를 새로 주장하는 변경이 아니다.
