#pragma once
#include "Network.h"
#include "GameLogic.h"
#include "IocpSession.h"
#include "IoBufferPool.h"
#include <mswsock.h>

// IOCP 네트워크 서버. 게임 상태는 CGameLogic이 소유하고 이 클래스는 통신만 담당한다.
// 선언은 이 헤더, 실제 처리는 IocpServer.cpp의 같은 이름 함수에서 확인한다.
class CIocpServer final
{
public:
    CIocpServer(const stServerConfig& config, CGameLogic& game, const string& serverInfo, CSocket listener);
    ~CIocpServer();
    CIocpServer(const CIocpServer&) = delete;
    CIocpServer& operator=(const CIocpServer&) = delete;

    // 메인 스레드: 연결 시간 초과 점검, 종료 요청, 장애 상태 확인.
    void Update();
    void Stop();
    bool Failed() const;

private:
    // 네트워크 쓰레드(워커)의 진입점. 완료 통지를 꺼내 수락/수신/송신 처리를 수행한다.
    void Run(int index);
    void HandleSessionCompletion(const shared_ptr<stIocpSession>& sp_session,
                                 stIoOperation* p_operation,
                                 bool success,
                                 DWORD bytes);

    // 접속 수락: AcceptEx 등록과 수락 완료 처리.
    void PostAccept();
    void HandleAccept(stIoOperation* p_operation, bool success);

    // 호출자는 sp_session->sessionMutex를 소유한다. 완료 전까지 I/O 버퍼를 유지한다.
    void PostRecv(const shared_ptr<stIocpSession>& sp_session);
    void PostSend(const shared_ptr<stIocpSession>& sp_session);
    void CloseSession(const shared_ptr<stIocpSession>& sp_session);

    // 메인에서 응답이 도착하면 IOCP 쓰레드(워커)를 깨운다. ReleaseOperation은 I/O 완료 후 호출한다.
    void NotifySend(uintptr_t id);
    void ReleaseOperation(stIoOperation* p_operation);
    void LogMetrics(const char* p_name);

    const stServerConfig& m_config;
    CGameLogic& m_game;
    const string& m_serverInfo;
    CSocket m_listener;
    HANDLE m_port = nullptr;
    LPFN_ACCEPTEX m_acceptEx = nullptr;
    CIoBufferPool m_pool;

    // 각 잠금은 접속 등록, 세션 목록, 로그 출력만 보호한다.
    mutex m_acceptMutex;
    mutex m_sessionsMutex;
    mutex m_logMutex;
    unordered_map<uintptr_t, shared_ptr<stIocpSession>> m_sessions;
    vector<jthread> m_workers;

    atomic<bool> m_stopping = false;
    atomic<bool> m_failed = false;
    atomic<size_t> m_outstanding = 0;
    array<atomic<unsigned long long>, 32> m_completions{};
    atomic<unsigned long long> m_receivedBytes = 0;
    atomic<unsigned long long> m_sentBytes = 0;
    atomic<unsigned long long> m_dropped = 0;
    uintptr_t m_nextId = 1;
    int m_acceptPending = 0;
    chrono::steady_clock::time_point m_maintenanceAt{};
    chrono::steady_clock::time_point m_metricsAt = chrono::steady_clock::now();
};
