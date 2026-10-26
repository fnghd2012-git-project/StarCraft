#include "IocpServer.h"
#include "PacketDispatch.h"

CIocpServer::CIocpServer(const stServerConfig& settings,
                         CGameLogic& logic,
                         const string& info,
                         CSocket socket)
    : m_config(settings), m_game(logic), m_serverInfo(info), m_listener(move(socket)),
      m_pool(settings.useMemoryPool, settings.poolCapacity)
{
    try
    {
        // 완료 통지를 모을 IOCP 생성. 실제 워커 스레드는 아래에서 별도로 만든다.
        m_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, m_config.workerCount);
        if (!m_port)
        {
            throw runtime_error("CreateIoCompletionPort failed");
        }
        if (!CreateIoCompletionPort(reinterpret_cast<HANDLE>(m_listener.Get()), m_port, 0, 0))
        {
            throw runtime_error("Associate listener with IOCP failed");
        }
        GUID id = WSAID_ACCEPTEX;
        DWORD bytes = 0;
        SocketCheck(WSAIoctl(m_listener.Get(),
                             SIO_GET_EXTENSION_FUNCTION_POINTER,
                             &id,
                             sizeof(id),
                             &m_acceptEx,
                             sizeof(m_acceptEx),
                             &bytes,
                             nullptr,
                             nullptr) == 0,
                    "Get AcceptEx");
        // 네트워크 쓰레드 할당
        // Server.ini의 [Network] WorkerCount만큼 생성하며, 각 워커는 Run에서 완료를 기다린다.
        for (int i = 0; i < m_config.workerCount; ++i)
        {
            m_workers.emplace_back(
                [this, i]
                {
                    Run(i);
                });
        }
        // 여러 접속을 미리 받아 둘 수 있도록 소수의 AcceptEx를 걸어 둔다.
        const int accepts = m_config.useMemoryPool ? min(8, m_config.poolCapacity) : 8;
        for (int i = 0; i < accepts; ++i)
        {
            PostAccept();
        }
    }
    catch (...)
    {
        Stop();
        if (m_port)
        {
            CloseHandle(m_port);
            m_port = nullptr;
        }
        throw;
    }
}

CIocpServer::~CIocpServer()
{
    Stop();
    if (m_port)
    {
        CloseHandle(m_port);
    }
}

void CIocpServer::NotifySend(uintptr_t id)
{
    // OVERLAPPED가 없는 완료는 게임 응답 알림이다. ID로 조회하므로 늦은 알림도 안전하다.
    if (!m_stopping.load() && !PostQueuedCompletionStatus(m_port, 0, id, nullptr))
    {
        m_failed.store(true);
    }
}

void CIocpServer::ReleaseOperation(stIoOperation* p_operation)
{
    m_pool.Release(p_operation);
    --m_outstanding;
}

void CIocpServer::PostAccept()
{
    lock_guard lock(m_acceptMutex);
    if (m_stopping.load() || m_acceptPending >= 8)
    {
        return;
    }
    auto* p_operation = m_pool.Acquire(IoKind::Accept);
    // 풀 고갈은 프로세스 장애가 아니다. 다음 유지 점검에서 비어 있는 Accept를 보충한다.
    if (!p_operation)
    {
        return;
    }
    p_operation->accepted =
        CSocket(WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED));
    if (p_operation->accepted.Get() == INVALID_SOCKET)
    {
        m_pool.Release(p_operation);
        m_failed.store(true);
        return;
    }
    ++m_outstanding;
    ++m_acceptPending;
    DWORD bytes = 0;
    // 비동기 접속 요청 등록. 접속이 완료되면 Run -> Accepted에서 세션을 만든다.
    const BOOL ok = m_acceptEx(m_listener.Get(),
                               p_operation->accepted.Get(),
                               p_operation->bytes.data(),
                               0,
                               sizeof(sockaddr_in) + 16,
                               sizeof(sockaddr_in) + 16,
                               &bytes,
                               &p_operation->overlapped);
    if (!ok && WSAGetLastError() != WSA_IO_PENDING)
    {
        --m_acceptPending;
        ReleaseOperation(p_operation);
        m_failed.store(true);
    }
}

void CIocpServer::CloseSession(const shared_ptr<stIocpSession>& sp_session)
{
    // 호출자는 sp_session->sessionMutex를 소유한다. 취소 통지를 처리할 워커는 아직 종료하지 않는다.
    if (sp_session->closed)
    {
        return;
    }
    sp_session->closed = true;
    sp_session->sp_channel->connected.store(false);
    {
        lock_guard lock(sp_session->sp_channel->sendMutex);
        sp_session->sp_channel->notifySend = {};
        sp_session->sp_channel->sendQueue.clear();
        sp_session->sp_channel->pendingSendBytes = 0;
    }
    CancelIoEx(reinterpret_cast<HANDLE>(sp_session->socket.Get()), nullptr);
    sp_session->socket.Reset();
    lock_guard lock(m_sessionsMutex);
    m_sessions.erase(sp_session->id);
    // 진행 중인 각 IoOperation의 shared_ptr가 마지막 완료까지 세션을 보관한다.
}

void CIocpServer::PostRecv(const shared_ptr<stIocpSession>& sp_session)
{
    if (sp_session->closed || m_stopping.load())
    {
        return;
    }
    auto* p_operation = m_pool.Acquire(IoKind::Receive);
    if (!p_operation)
    {
        ++m_dropped;
        CloseSession(sp_session);
        return;
    }
    p_operation->sp_session = sp_session;
    WSABUF buffer{static_cast<ULONG>(p_operation->bytes.size()), p_operation->bytes.data()};
    DWORD flags = 0, bytes = 0;
    ++m_outstanding;
    // 비동기 수신 요청 등록. 받은 바이트는 HandleSessionCompletion의 수신 완료 분기에서 처리한다.
    const int result =
        WSARecv(sp_session->socket.Get(), &buffer, 1, &bytes, &flags, &p_operation->overlapped, nullptr);
    // WSA_IO_PENDING은 실패가 아니라 아직 수신 완료를 기다리는 상태다.
    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
    {
        ReleaseOperation(p_operation);
        CloseSession(sp_session);
    }
}

void CIocpServer::PostSend(const shared_ptr<stIocpSession>& sp_session)
{
    if (sp_session->closed || m_stopping.load())
    {
        return;
    }
    if (!sp_session->sp_channel->connected.load())
    {
        CloseSession(sp_session);
        return;
    }
    // 응답 큐에 쌓인 패킷을 이 연결의 송신 버퍼로 옮긴다.
    {
        lock_guard lock(sp_session->sp_channel->sendMutex);
        sp_session->sp_channel->sendNotifyPending = false;
        for (const auto& packet : sp_session->sp_channel->sendQueue)
        {
            if (sp_session->sendBuffer.size() - sp_session->sendOffset + packet.size() >
                static_cast<size_t>(m_config.maxPendingSendBytes))
            {
                throw runtime_error("Client send queue full");
            }
            sp_session->sendBuffer += packet;
        }
        sp_session->sp_channel->sendQueue.clear();
        sp_session->sp_channel->pendingSendBytes = 0;
    }
    // 연결당 송신 요청은 하나씩 진행해 응답 순서를 유지한다.
    if (sp_session->sendPending)
    {
        return;
    }
    if (sp_session->sendOffset == sp_session->sendBuffer.size())
    {
        sp_session->sendBuffer.clear();
        sp_session->sendOffset = 0;
        if (sp_session->sp_channel->closeAfterSend.load() && !sp_session->closing)
        {
            SocketCheck(shutdown(sp_session->socket.Get(), SD_SEND) == 0, "IOCP shutdown send");
            sp_session->closing = true;
            sp_session->closeDeadline = chrono::steady_clock::now() + chrono::seconds(3);
            sp_session->recvBuffer.clear();
        }
        return;
    }
    auto* p_operation = m_pool.Acquire(IoKind::Send);
    if (!p_operation)
    {
        ++m_dropped;
        CloseSession(sp_session);
        return;
    }
    p_operation->sp_session = sp_session;
    p_operation->length = static_cast<DWORD>(
        min(p_operation->bytes.size(), sp_session->sendBuffer.size() - sp_session->sendOffset));
    memcpy(p_operation->bytes.data(),
           sp_session->sendBuffer.data() + sp_session->sendOffset,
           p_operation->length);
    WSABUF buffer{p_operation->length, p_operation->bytes.data()};
    DWORD bytes = 0;
    sp_session->sendPending = true;
    ++m_outstanding;
    // 실제 비동기 송신 등록. operation의 버퍼는 송신 완료가 올 때까지 유지한다.
    const int result =
        WSASend(sp_session->socket.Get(), &buffer, 1, &bytes, 0, &p_operation->overlapped, nullptr);
    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
    {
        sp_session->sendPending = false;
        ReleaseOperation(p_operation);
        CloseSession(sp_session);
    }
}

void CIocpServer::HandleAccept(stIoOperation* p_operation, bool success)
{
    lock_guard acceptLock(m_acceptMutex);
    --m_acceptPending;
    if (!success || m_stopping.load())
    {
        return;
    }
    const SOCKET listening = m_listener.Get();
    SocketCheck(setsockopt(p_operation->accepted.Get(),
                           SOL_SOCKET,
                           SO_UPDATE_ACCEPT_CONTEXT,
                           reinterpret_cast<const char*>(&listening),
                           sizeof(listening)) == 0,
                "Accept context");
    const BOOL noDelay = TRUE;
    SocketCheck(setsockopt(p_operation->accepted.Get(),
                           IPPROTO_TCP,
                           TCP_NODELAY,
                           reinterpret_cast<const char*>(&noDelay),
                           sizeof(noDelay)) == 0,
                "TCP_NODELAY");
    // 새 연결의 소켓, 수신 버퍼, 응답 큐를 보관할 세션 생성.
    auto sp_session = make_shared<stIocpSession>();
    sp_session->socket = move(p_operation->accepted);
    {
        lock_guard lock(m_sessionsMutex);
        if (m_sessions.size() >= static_cast<size_t>(m_config.workerCount * m_config.maxClientsPerWorker))
        {
            ++m_dropped;
            return;
        }
        sp_session->id = m_nextId++;
        // 이 소켓의 수신/송신 완료도 같은 IOCP로 전달되도록 연결한다.
        if (!CreateIoCompletionPort(
                reinterpret_cast<HANDLE>(sp_session->socket.Get()), m_port, sp_session->id, 0))
        {
            throw runtime_error("Associate client with IOCP failed");
        }
        sp_session->sp_channel->notifySend = [this, id = sp_session->id]
        {
            NotifySend(id);
        };
        m_sessions.emplace(sp_session->id, sp_session);
    }
    lock_guard lock(sp_session->sessionMutex);
    // 접속 직후 첫 수신 등록. 이후에는 수신 완료를 처리할 때마다 다시 등록한다.
    PostRecv(sp_session);
}

// IOCP 쓰레드(워커): 한 연결의 수신/송신 완료와 송신 알림을 처리한다.
// 이 함수의 락은 해당 세션만 보호한다. 다른 세션은 다른 쓰레드가 계속 처리할 수 있다.
void CIocpServer::HandleSessionCompletion(const shared_ptr<stIocpSession>& sp_session,
                                          stIoOperation* p_operation,
                                          bool success,
                                          DWORD bytes)
{
    lock_guard lock(sp_session->sessionMutex);
    if (sp_session->closed)
    {
        return;
    }
    if (p_operation && (!success || bytes == 0))
    {
        CloseSession(sp_session);
        return;
    }

    if (p_operation && p_operation->kind == IoKind::Receive)
    {
        m_receivedBytes += bytes;
        if (!sp_session->sp_channel->closeAfterSend.load())
        {
            // 받은 바이트를 누적하고 완성된 패킷만 게임 명령 큐로 넘긴다.
            sp_session->recvBuffer.append(p_operation->bytes.data(), bytes);
            HandleClientPackets(sp_session->recvBuffer,
                                sp_session->versionChecked,
                                sp_session->sp_channel,
                                m_config,
                                m_serverInfo,
                                m_game);
        }
        PostRecv(sp_session);
    }
    else if (p_operation && p_operation->kind == IoKind::Send)
    {
        if (bytes > p_operation->length)
        {
            throw runtime_error("Invalid send completion size");
        }
        m_sentBytes += bytes;
        sp_session->sendOffset += bytes;
        sp_session->sendPending = false;
        // 완료된 바이트만 제거한다. 부분 송신이면 남은 데이터가 다음 요청에 포함된다.
        sp_session->sendBuffer.erase(0, sp_session->sendOffset);
        sp_session->sendOffset = 0;
    }

    // p_operation == nullptr는 새 송신 데이터 알림이다. 수신 완료와 구분한다.
    PostSend(sp_session);
}

void CIocpServer::Run(int index)
{
    while (true)
    {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        OVERLAPPED* p_overlapped = nullptr;
        // 네트워크 쓰레드(워커)의 대기 지점. 접속/수신/송신 완료 또는 직접 보낸 알림을 하나 꺼낸다.
        const BOOL success = GetQueuedCompletionStatus(m_port, &bytes, &key, &p_overlapped, INFINITE);
        if (!p_overlapped && key == 0)
        {
            if (!success)
            {
                m_failed.store(true);
            }
            return; // 모든 I/O 정리가 끝난 뒤 보내는 종료 알림
        }
        ++m_completions[index];
        stIoOperation* p_operation = p_overlapped ? reinterpret_cast<stIoOperation*>(p_overlapped) : nullptr;
        shared_ptr<stIocpSession> sp_session;
        try
        {
            // 접속 완료는 세션을 만들고, 나머지는 기존 연결의 수신/송신 또는 응답 알림을 처리한다.
            if (p_operation && p_operation->kind == IoKind::Accept)
            {
                HandleAccept(p_operation, success != FALSE);
            }
            else
            {
                if (p_operation)
                {
                    sp_session = p_operation->sp_session;
                }
                else
                {
                    // OVERLAPPED 없이 온 응답 알림은 Wake에서 전달한 세션 ID로 찾는다.
                    lock_guard lock(m_sessionsMutex);
                    const auto found = m_sessions.find(key);
                    if (found != m_sessions.end())
                    {
                        sp_session = found->second;
                    }
                }
                if (sp_session)
                {
                    HandleSessionCompletion(sp_session, p_operation, success != FALSE, bytes);
                }
            }
        }
        catch (const exception& error)
        {
            ++m_dropped;
            if (sp_session)
            {
                lock_guard lock(sp_session->sessionMutex);
                CloseSession(sp_session);
            }
            lock_guard lock(m_logMutex);
            cerr << "[IOCP DROP] " << error.what() << '\n';
        }
        if (p_operation)
        {
            const bool accept = p_operation->kind == IoKind::Accept;
            // 완료 처리가 끝난 I/O 버퍼 반환. 아직 진행 중인 요청의 버퍼는 반환하면 안 된다.
            ReleaseOperation(p_operation);
            if (accept)
            {
                try
                {
                    PostAccept();
                }
                catch (...)
                {
                    m_failed.store(true);
                }
            }
        }
    }
}

void CIocpServer::LogMetrics(const char* p_name)
{
    lock_guard lock(m_logMutex);
    cout << p_name << " server=" << m_config.id << " pending_io=" << m_outstanding.load()
         << " recv_bytes=" << m_receivedBytes.load() << " send_bytes=" << m_sentBytes.load()
         << " dropped=" << m_dropped.load() << ' ' << m_pool.Metrics();
    for (int i = 0; i < m_config.workerCount; ++i)
    {
        cout << " worker" << i << "=" << m_completions[i].load();
    }
    cout << endl;
}

void CIocpServer::Update()
{
    const auto now = chrono::steady_clock::now();
    // 메인 스레드의 주기 점검. 접속 요청 보충과 버전 검사/연결 종료 시간 초과를 처리한다.
    if (now >= m_maintenanceAt)
    {
        m_maintenanceAt = now + chrono::milliseconds(100);
        for (int i = 0; i < 8; ++i)
        {
            PostAccept();
        }
        vector<shared_ptr<stIocpSession>> current;
        {
            lock_guard lock(m_sessionsMutex);
            for (const auto& entry : m_sessions)
            {
                current.push_back(entry.second);
            }
        }
        for (const auto& sp_session : current)
        {
            lock_guard lock(sp_session->sessionMutex);
            if ((!sp_session->versionChecked && now - sp_session->connectedAt > chrono::seconds(5)) ||
                (sp_session->closing && now >= sp_session->closeDeadline) ||
                !sp_session->sp_channel->connected.load())
            {
                CloseSession(sp_session);
            }
            // 마지막 응답이 이미 완료된 뒤 closeAfterSend가 설정되는 경우도 처리한다.
            if (!sp_session->closed && sp_session->sp_channel->closeAfterSend.load())
            {
                try
                {
                    PostSend(sp_session);
                }
                catch (const exception&)
                {
                    ++m_dropped;
                    CloseSession(sp_session);
                }
            }
        }
    }
    if (now - m_metricsAt >= chrono::seconds(m_config.metricsSeconds))
    {
        LogMetrics("[IOCP]");
        m_metricsAt = now;
    }
}

void CIocpServer::Stop()
{
    if (m_stopping.exchange(true))
    {
        return;
    }
    // 신규 접속을 막고 기존 연결의 I/O 취소를 요청한다.
    {
        lock_guard lock(m_acceptMutex);
        m_listener.Reset();
    }
    vector<shared_ptr<stIocpSession>> current;
    {
        lock_guard lock(m_sessionsMutex);
        for (const auto& entry : m_sessions)
        {
            current.push_back(entry.second);
        }
    }
    for (const auto& sp_session : current)
    {
        lock_guard lock(sp_session->sessionMutex);
        CloseSession(sp_session);
    }
    // closesocket/CancelIoEx는 완료 대기가 아니다. 취소 완료도 워커가 회수해야 한다.
    while (m_outstanding.load() != 0)
    {
        this_thread::sleep_for(chrono::milliseconds(1));
    }
    // 모든 I/O를 회수한 뒤 워커마다 종료 알림을 보내고 join으로 종료를 기다린다.
    for (size_t i = 0; i < m_workers.size(); ++i)
    {
        PostQueuedCompletionStatus(m_port, 0, 0, nullptr);
    }
    for (auto& worker : m_workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
    m_workers.clear();
    if (m_pool.InUse() != 0)
    {
        m_failed.store(true);
    }
    LogMetrics("[IOCP STOP]");
}

bool CIocpServer::Failed() const
{
    return m_failed.load();
}
