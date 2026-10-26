#include "ClientConnection.h"

CSocket Connect(const stServerConfig& config)
{
    // 서버가 없더라도 오래 멈추지 않도록 접속 대기를 3초로 제한한다.
    CSocket client(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    SocketCheck(client.Get() != INVALID_SOCKET, "socket");
    SetNonblocking(client.Get(), true);
    const auto address = MakeAddress(config.address, config.port);
    const int result = connect(client.Get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (result == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSAEWOULDBLOCK)
        {
            SocketCheck(false, "connect");
        }
        WSAPOLLFD poll{client.Get(), POLLWRNORM, 0};
        if (WSAPoll(&poll, 1, 3000) <= 0)
        {
            throw runtime_error("Connect timed out");
        }
        int error = 0;
        int length = sizeof(error);
        SocketCheck(
            getsockopt(client.Get(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) == 0,
            "SO_ERROR");
        if (error != 0 || !(poll.revents & POLLWRNORM))
        {
            throw runtime_error("Connect failed: " + to_string(error));
        }
    }
    SetNonblocking(client.Get(), false);
    const BOOL noDelay = TRUE;
    SocketCheck(setsockopt(client.Get(),
                           IPPROTO_TCP,
                           TCP_NODELAY,
                           reinterpret_cast<const char*>(&noDelay),
                           sizeof(noDelay)) == 0,
                "TCP_NODELAY");
    const DWORD timeout = 3000;
    SocketCheck(setsockopt(client.Get(),
                           SOL_SOCKET,
                           SO_RCVTIMEO,
                           reinterpret_cast<const char*>(&timeout),
                           sizeof(timeout)) == 0,
                "SO_RCVTIMEO");
    SocketCheck(setsockopt(client.Get(),
                           SOL_SOCKET,
                           SO_SNDTIMEO,
                           reinterpret_cast<const char*>(&timeout),
                           sizeof(timeout)) == 0,
                "SO_SNDTIMEO");
    return client;
}

void SendAll(SOCKET socket, string_view bytes)
{
    // 한 번의 send가 전체 데이터를 전송한다는 보장은 없다.
    while (!bytes.empty())
    {
        const int sent = send(socket, bytes.data(), static_cast<int>(bytes.size()), 0);
        SocketCheck(sent > 0, "send");
        bytes.remove_prefix(static_cast<size_t>(sent));
    }
}

string ReadExact(SOCKET socket, size_t length)
{
    // 필요한 바이트 수만 채운다. 다음 패킷의 데이터를 앞 패킷에 섞지 않는다.
    string bytes(length, '\0');
    size_t received = 0;
    while (received < length)
    {
        const int count = recv(socket, bytes.data() + received, static_cast<int>(length - received), 0);
        SocketCheck(count > 0, "recv/connection closed");
        received += static_cast<size_t>(count);
    }
    return bytes;
}

string ReadPacket(SOCKET socket, Protocol::Type expected)
{
    const string header = ReadExact(socket, Protocol::HeaderSize);
    const size_t length = Protocol::PacketSize(header);
    if (Protocol::Read16(header.data() + 2) != static_cast<unsigned>(expected))
    {
        throw runtime_error("Unexpected reply type");
    }
    return ReadExact(socket, length - Protocol::HeaderSize);
}

namespace
{
mutex consoleMutex;
}

void PrintPlayLine(const string& line)
{
    lock_guard lock(consoleMutex);
    cout << line << endl;
}

CClientConnection::CClientConnection(const stServerConfig& config, bool quiet)
    : m_socket(Connect(config)), m_quiet(quiet)
{
    wire::ServerVersionCheckRequest versionCheck;
    versionCheck.set_protocol_version(Protocol::Version);
    versionCheck.set_client_name("InteractiveDummy");
    SendAll(m_socket.Get(), Protocol::EncodeMessage(Protocol::Type::SYS_ServerVersionCheck, versionCheck));
    wire::ServerVersionCheckReply reply;
    if (!reply.ParseFromString(ReadPacket(m_socket.Get(), Protocol::Type::SYS_ServerVersionCheckReply)) ||
        reply.protocol_version() != Protocol::Version || reply.server_id() != config.id)
    {
        throw runtime_error("Unexpected server identity");
    }
    m_map = reply.map();
    for (const auto& skill : reply.skill_info())
    {
        m_skills.push_back(skill);
    }
    // 더미클라이언트 수신 스레드 할당. 콘솔 입력을 기다리는 중에도 서버 메시지를 받는다.
    m_reader = jthread(
        [this](stop_token stop)
        {
            Receive(stop);
        });
}

CClientConnection::~CClientConnection()
{
    m_reader.request_stop();
    shutdown(m_socket.Get(), SD_BOTH);
    if (m_reader.joinable())
    {
        m_reader.join();
    }
}

string CClientConnection::Request(Protocol::Type type,
                                  const google::protobuf::MessageLite& message,
                                  Protocol::Type expected)
{
    if (type == Protocol::Type::CM_Logout)
    {
        m_expectedClose.store(true);
    }
    // 요청 본문 직렬화 -> 4바이트 헤더 추가 -> 소켓 송신 순서다.
    SendAll(m_socket.Get(), Protocol::EncodeMessage(type, message));
    unique_lock lock(m_receiveMutex);
    if (!m_ready.wait_for(lock,
                          chrono::seconds(10),
                          [this]
                          {
                              return !m_replies.empty() || !m_error.empty();
                          }))
    {
        // 응답 순서를 다시 맞출 수 없으므로 시간 초과 연결은 종료한다.
        shutdown(m_socket.Get(), SD_BOTH);
        throw runtime_error("Server reply timeout");
    }
    // 이동 응답을 보낸 직후 서버가 소켓을 닫아도 이미 받은 입장권/복귀권을 먼저 소비한다.
    if (m_replies.empty())
    {
        throw runtime_error(m_error);
    }
    stReply reply = move(m_replies.front());
    m_replies.pop_front();
    if (reply.type != expected)
    {
        wire::ActionReply failure;
        if (reply.type == Protocol::Type::SM_ActionReply && failure.ParseFromString(reply.payload))
        {
            throw runtime_error(failure.message());
        }
        throw runtime_error("Unexpected gameplay reply");
    }
    return reply.payload;
}

wire::WorldState CClientConnection::Snapshot()
{
    lock_guard lock(m_receiveMutex);
    return m_snapshot;
}

wire::WorldTicket CClientConnection::ReturnTicket()
{
    lock_guard lock(m_receiveMutex);
    return m_returnTicket;
}

void CClientConnection::Receive(stop_token stop)
{
    wire::WorldState pending;
    try
    {
        while (!stop.stop_requested())
        {
            WSAPOLLFD poll{m_socket.Get(), POLLRDNORM, 0};
            SocketCheck(WSAPoll(&poll, 1, 100) != SOCKET_ERROR, "client poll");
            if (poll.revents == 0)
            {
                continue;
            }
            // 헤더를 먼저 읽어 본문 길이와 패킷 종류를 확인한다. 이후 종류별 메시지로 역직렬화한다.
            const string header = ReadExact(m_socket.Get(), Protocol::HeaderSize);
            const auto type = static_cast<Protocol::Type>(Protocol::Read16(header.data() + 2));
            const auto size = Protocol::PacketSize(header);
            const string payload = ReadExact(m_socket.Get(), size - Protocol::HeaderSize);
            if (type == Protocol::Type::SM_ChatMessage)
            {
                wire::ChatMessage message;
                if (!message.ParseFromString(payload))
                {
                    throw runtime_error("Invalid chat message");
                }
                // 채팅은 요청 응답 큐에 넣지 않는다. 입력 대기 중에도 수신 스레드가 즉시 표시한다.
                if (!m_quiet)
                {
                    const string channel = message.channel() == wire::CHAT_WORLD ? "월드" : "일반";
                    PrintPlayLine("[" + channel + " " + message.server_id() + "] " + message.name() + ": " +
                                  message.text());
                }
                continue;
            }
            if (type == Protocol::Type::SM_WorldState)
            {
                wire::WorldState part;
                if (!part.ParseFromString(payload) || pending.units_size() + part.units_size() > 64)
                {
                    throw runtime_error("Invalid world snapshot");
                }
                for (const auto& unit : part.units())
                {
                    *pending.add_units() = unit;
                }
                if (part.complete())
                {
                    for (const auto& building : part.buildings())
                    {
                        *pending.add_buildings() = building;
                    }
                    for (const auto& area : part.areas())
                    {
                        *pending.add_areas() = area;
                    }
                    pending.set_tick(part.tick());
                    pending.set_complete(true);
                    pending.set_round_running(part.round_running());
                    pending.set_winner(part.winner());
                    lock_guard lock(m_receiveMutex);
                    m_snapshot.Swap(&pending);
                    pending.Clear();
                }
                continue;
            }
            if (type == Protocol::Type::SM_WorldEvent)
            {
                wire::WorldEvent event;
                if (!event.ParseFromString(payload))
                {
                    throw runtime_error("Invalid world event");
                }
                if (event.returned_to_game())
                {
                    {
                        lock_guard lock(m_receiveMutex);
                        m_returnTicket = event.return_ticket();
                    }
                    m_expectedClose.store(true);
                    m_inWorld.store(false);
                }
                if (!m_quiet)
                {
                    PrintPlayLine("[WORLD] " + event.message() +
                                  (event.retry_after_seconds()
                                       ? " / reentry=" + to_string(event.retry_after_seconds()) + "s"
                                       : ""));
                }
                continue;
            }
            if (type == Protocol::Type::SM_WorldReply)
            {
                wire::WorldReply reply;
                if (!reply.ParseFromString(payload))
                {
                    throw runtime_error("Invalid world reply");
                }
                if (reply.ok())
                {
                    m_inWorld.store(reply.in_world());
                }
                if (reply.has_return_ticket())
                {
                    {
                        lock_guard lock(m_receiveMutex);
                        m_returnTicket = reply.return_ticket();
                    }
                    m_expectedClose.store(true);
                    m_inWorld.store(false);
                }
            }
            if (type == Protocol::Type::SM_WorldTicket)
            {
                m_expectedClose.store(true);
            }
            {
                lock_guard lock(m_receiveMutex);
                if (m_replies.size() >= 16)
                {
                    throw runtime_error("Unexpected reply overflow");
                }
                m_replies.push_back({type, payload});
            }
            m_ready.notify_one();
        }
    }
    catch (const exception& error)
    {
        m_inWorld.store(false);
        {
            lock_guard lock(m_receiveMutex);
            m_error = error.what();
        }
        m_ready.notify_one();
        if (!stop.stop_requested() && !m_quiet && !m_expectedClose.load())
        {
            PrintPlayLine("[DISCONNECTED] " + m_error);
        }
    }
}
