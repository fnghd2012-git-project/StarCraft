from build_study_pdfs import *


def convention_guide():
    return [Page('익숙한 이름으로 코드 흐름 읽기', 'READABILITY / 2026-10-21', [
        P('클래스와 구조체, 멤버와 지역 변수를 이름으로 구분합니다. 파일 이름은 기능별로 유지했습니다. GameLogic.h에서 CGameLogic 선언을 보고, GameLogic.cpp와 GameBattle.cpp 등에서 실제 함수를 찾습니다.'),
        TableBlock(['대상', '현재 규칙과 예'], [
            ['클래스 / 구조체', 'CGameLogic, CIocpServer / stPlayer, stGameChannel'],
            ['멤버 / 전역', 'm_players, m_commands / g_stopRequested'],
            ['일반 / 공유 포인터', 'p_operation: T* / sp_session: shared_ptr'],
            ['단독 / 약한 참조', 'up_connection: unique_ptr / wp_: weak_ptr (현재 사용 없음)'],
            ['포인터 멤버 / 일반 참조', 'm_up_redis, m_sp_latest / 일반 참조 player는 접두사 없음'],
            ['지역 변수 / 데이터 필드', 'player, packet, sendQueue, pendingSendBytes'],
            ['처리 / 검증 / 조회', 'HandleMove / ValidateGameCapacity / FindPlayer'],
            ['잠금 이름', 'sessionMutex: 세션 상태 / m_battleMutex: 전투 작업 동기화'],
            ['송신 단계', 'SendMessage: 직렬화 → SendPacket: 큐 → PostSend: I/O 등록'],
        ], [1.2, 3.8]),
        H('읽을 함수의 순서'),
        Flow([
            ('IOCP 완료', 'CIocpServer::Run → HandleSessionCompletion'),
            ('명령 전달', 'HandleClientPackets → CGameLogic::Submit → m_commands'),
            ('게임 요청', 'Update → HandlePacket → HandleGamePacket / HandleWorldPacket'),
            ('이동 목표', 'HandleMove에서 goalX / goalY 설정. 다음 Tick에서 실제 이동'),
        ]),
        Note('같이 유지하는 규칙', 'CM_·SM_·SS_·SYS_와 번호, Protobuf 본문은 유지합니다. 락은 공유 큐의 확인·수정을 묶는 범위만 잡습니다. DB 저장이 필요한 요청은 DB 결과 확인 뒤 성공을 응답합니다.'),
    ], [Ref('docs/CodeConventions.md'), Ref(S+'GameLogic.cpp','void CGameLogic::HandleMove'),
        Ref(S+'IocpServer.cpp','void CIocpServer::HandleSessionCompletion')])]


def convention_practice():
    return [Page('이름을 읽고 실제 처리 위치 확인하기', 'READABILITY / 코드 설명 연습', [
        P('아래 질문은 현재 소스의 함수와 변수명을 직접 찾아 답합니다. 같은 파일의 클래스 선언 → cpp 정의 → 호출부 순서로 따라가세요.'),
        TableBlock(['찾을 곳', '설명할 내용'], [
            ['stGameChannel::pendingSendBytes', 'sendQueue 안의 대기 바이트 수다. 실제 전송 누계와 다르다. 큐를 비울 때 0이 되는 코드를 찾는다.'],
            ['CGameLogic::HandleMove', '목표 좌표만 바꾸는 이유, 현재 좌표를 반영하는 MovePlayers와의 관계를 설명한다.'],
            ['CIocpServer::PostRecv', '수신 버퍼를 등록하는 시점과 완료 통지를 받는 시점, 메모리 풀 반환 시점을 구분한다.'],
            ['CIocpServer::HandleSessionCompletion', 'sp_session은 shared_ptr, p_operation은 일반 포인터다. 수신·송신 완료와 p_operation이 없는 송신 알림을 각각 설명한다.'],
            ['CGameLogic::Update', 'm_commandsMutex를 잡고 꺼낸 뒤 락 없이 HandlePacket을 실행하는 이유를 설명한다.'],
            ['GamePersistence.cpp', 'DB 요청 등록과 결과 수신, 성공 응답의 순서를 확인한다.'],
        ], [2, 3]),
        Note('실제 기능은 기존 시험으로 확인', '이름 변경 전후의 코드 비교와 Debug/Release 빌드, IOCP/WSAPoll 통신, 계정·스킬·복귀·채팅·전투 1·2·4 쓰레드·Redis·리플레이를 확인합니다. 결과: docs/results/2026-10-21/mutex-roles. 기존 성능 그래프는 이전 빌드의 측정 기록입니다.'),
    ], [Ref('docs/CodeConventions.md'), Ref(S+'GameLogic.h','class CGameLogic'),
        Ref(S+'IocpServer.h','class CIocpServer')])]
