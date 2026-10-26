#include "NetworkWorker.h"
#include "PacketDispatch.h"

namespace
{
mutex logMutex;
}

CNetworkWorker::CNetworkWorker(const stServerConfig& config, const string& serverInfo, CGameLogic& game)
    : m_config(config), m_serverInfo(serverInfo), m_game(game)
{
    // WSAPoll 네트워크 스레드 할당. CNetworkWorker 객체 하나가 스레드 하나를 담당한다.
    // 멤버 초기화가 끝난 뒤 워커를 시작한다.
    m_thread = jthread(
        [this](stop_token stop)
        {
            Run(stop);
        });
}

CNetworkWorker::~CNetworkWorker()
{
    m_thread.request_stop();
    m_thread.join();
}

bool CNetworkWorker::AddClient(CSocket& socket)
{
    // 호출자는 accept를 담당하는 메인 스레드 하나다. 여기서만 워커로 소켓을 넘긴다.
    if (m_failed.load() || m_clientCount.load() >= m_config.maxClientsPerWorker)
    {
        return false;
    }
    lock_guard lock(m_pendingClientsMutex);
    m_pending.push_back(move(socket));
    ++m_clientCount;
    return true;
}

bool CNetworkWorker::Failed() const
{
    return m_failed.load();
}

void CNetworkWorker::QueueSendPacket(stClientSession& client, const string& packet)
{
    // 응답을 읽지 않는 클라이언트 때문에 서버 메모리가 계속 증가하지 않게 제한한다.
    if (client.sendBuffer.size() - client.sendOffset + packet.size() >
        static_cast<size_t>(m_config.maxPendingSendBytes))
    {
        throw runtime_error("Client send queue full");
    }
    if (client.sendOffset != 0)
    {
        client.sendBuffer.erase(0, client.sendOffset);
        client.sendOffset = 0;
    }
    client.sendBuffer += packet;
}

bool CNetworkWorker::ProcessSession(stClientSession& client, short events)
{
    if (!client.sp_channel->connected.load())
    {
        return false;
    }
    if (client.closing && chrono::steady_clock::now() >= client.closeDeadline)
    {
        return false;
    }
    vector<string> sendQueue;
    {
        lock_guard lock(client.sp_channel->sendMutex);
        sendQueue.swap(client.sp_channel->sendQueue);
        client.sp_channel->pendingSendBytes = 0;
    }
    for (const auto& packet : sendQueue)
    {
        QueueSendPacket(client, packet);
    }
    if (events & (POLLERR | POLLNVAL))
    {
        return false;
    }
    if (!client.versionChecked && chrono::steady_clock::now() - client.connectedAt > chrono::seconds(5))
    {
        // 연결만 열어 두거나 헤더 일부만 보낸 접속은 5초 뒤 정리한다.
        return false;
    }
    if (events & (POLLRDNORM | POLLHUP))
    {
        // WSAPoll은 읽을 수 있는 상태만 알려준다. 실제 바이트는 여기서 recv로 가져온다.
        array<char, 4096> buffer{};
        const int recvBuffer = recv(client.socket.Get(), buffer.data(), static_cast<int>(buffer.size()), 0);
        if (recvBuffer == 0)
        {
            return false;
        }
        if (recvBuffer < 0)
        {
            return WSAGetLastError() == WSAEWOULDBLOCK && !(events & POLLHUP);
        }
        // 복귀권을 보낸 연결은 더 이상 요청을 처리하지 않지만 수신 데이터는 비운다.
        // 읽지 않은 데이터가 있는 채 closesocket하면 RST로 마지막 복귀권까지 유실될 수 있다.
        if (!client.sp_channel->closeAfterSend.load())
        {
            client.recvBuffer.append(buffer.data(), static_cast<size_t>(recvBuffer));
            HandleClientPackets(
                client.recvBuffer, client.versionChecked, client.sp_channel, m_config, m_serverInfo, m_game);
        }
    }
    if (client.sendOffset < client.sendBuffer.size())
    {
        const int sendOffset = send(client.socket.Get(),
                                    client.sendBuffer.data() + client.sendOffset,
                                    static_cast<int>(client.sendBuffer.size() - client.sendOffset),
                                    0);
        if (sendOffset < 0)
        {
            return WSAGetLastError() == WSAEWOULDBLOCK;
        }
        if (sendOffset == 0)
        {
            return false;
        }
        client.sendOffset += static_cast<size_t>(sendOffset);
        // send가 일부만 보냈으면 다음 반복에서 남은 부분부터 이어 보낸다.
        if (client.sendOffset == client.sendBuffer.size())
        {
            client.sendBuffer.clear();
            client.sendOffset = 0;
        }
    }
    if (client.sp_channel->closeAfterSend.load() && client.sendBuffer.empty() && !client.closing)
    {
        lock_guard lock(client.sp_channel->sendMutex);
        if (client.sp_channel->sendQueue.empty())
        {
            // 송신 방향만 먼저 닫아 마지막 패킷 뒤에 FIN을 보낸다. 상대의 종료/EOF까지
            // 수신을 계속하되 응답 없는 연결 때문에 슬롯을 무한 점유하지 않도록 제한한다.
            SocketCheck(shutdown(client.socket.Get(), SD_SEND) == 0, "shutdown send");
            client.closing = true;
            client.closeDeadline = chrono::steady_clock::now() + chrono::seconds(3);
            client.recvBuffer.clear();
        }
    }
    return true;
}

void CNetworkWorker::Run(stop_token stop)
{
    try
    {
        vector<stClientSession> clients;
        while (!stop.stop_requested())
        {
            {
                // 잠금은 연결 인계에만 사용한다. 실제 송수신은 잠금 밖에서 처리한다.
                lock_guard lock(m_pendingClientsMutex);
                for (auto& socket : m_pending)
                {
                    stClientSession client;
                    client.socket = move(socket);
                    clients.push_back(move(client));
                }
                m_pending.clear();
            }
            if (clients.empty())
            {
                this_thread::sleep_for(chrono::milliseconds(10));
                continue;
            }
            vector<WSAPOLLFD> polls;
            polls.reserve(clients.size());
            for (const auto& client : clients)
            {
                // 이미 응답이 도착했다면 송신 가능 이벤트로 바로 처리한다.
                // poll 도중 들어온 응답은 설정한 최대 대기 후 확인한다.
                lock_guard lock(client.sp_channel->sendMutex);
                const bool sendBuffer = !client.sendBuffer.empty() || !client.sp_channel->sendQueue.empty();
                const short events = static_cast<short>(POLLRDNORM | (sendBuffer ? POLLWRNORM : 0));
                polls.push_back({client.socket.Get(), events, 0});
            }
            // 담당 소켓들의 수신/송신 가능 상태를 확인한 뒤 Process에서 실제 송수신한다.
            SocketCheck(WSAPoll(polls.data(), static_cast<ULONG>(polls.size()), m_config.pollTimeoutMs) !=
                            SOCKET_ERROR,
                        "WSAPoll");
            for (size_t i = clients.size(); i-- > 0;)
            {
                // 뒤에서부터 지워야 polls와 clients의 앞쪽 인덱스가 어긋나지 않는다.
                bool keep = false;
                try
                {
                    keep = ProcessSession(clients[i], polls[i].revents);
                }
                catch (const exception& error)
                {
                    lock_guard lock(logMutex);
                    cerr << "[DROP] " << error.what() << '\n';
                }
                if (!keep)
                {
                    // vector::erase는 뒤의 세션을 이동 대입한다. 삭제 대상의 종료를 먼저 알린다.
                    clients[i].sp_channel->connected.store(false);
                    clients.erase(clients.begin() + static_cast<vector<stClientSession>::difference_type>(i));
                    --m_clientCount;
                }
            }
        }
    }
    catch (const exception& error)
    {
        lock_guard lock(logMutex);
        cerr << "[WORKER FAIL] " << error.what() << '\n';
        m_failed.store(true);
    }
}
