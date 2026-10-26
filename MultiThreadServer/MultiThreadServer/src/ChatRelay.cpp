#include "ChatRelay.h"

void CChatRelay::Reset()
{
    m_socket.Reset();
    m_state = State::Offline;
    m_received.clear();
    m_sending.clear();
    m_sent = 0;
    m_retryAt = chrono::steady_clock::now() + chrono::seconds(1);
    // 이미 완성해서 받은 메시지는 TakeMessages에서 소비한다. 미완성 송신은 재전송하지 않는다.
}

void CChatRelay::QueuePacket(Protocol::Type type, const google::protobuf::MessageLite& message)
{
    const string packet = Protocol::EncodeMessage(type, message);
    if (m_sending.size() - m_sent + packet.size() > static_cast<size_t>(m_config.maxPendingSendBytes))
    {
        throw runtime_error("Chat relay send queue full");
    }
    if (m_sent != 0)
    {
        m_sending.erase(0, m_sent);
        m_sent = 0;
    }
    m_sending += packet;
}

void CChatRelay::Connect()
{
    m_socket = CSocket(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    SocketCheck(m_socket.Get() != INVALID_SOCKET, "relay socket");
    SetNonblocking(m_socket.Get(), true);
    const auto address = MakeAddress(m_config.worldAddress, m_config.worldPort);
    const int result = connect(m_socket.Get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (result == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
    {
        SocketCheck(false, "relay connect");
    }
    m_deadline = chrono::steady_clock::now() + chrono::seconds(3);
    m_state = State::Connecting;
    if (result == 0)
    {
        SendVersionCheck();
    }
}

void CChatRelay::SendVersionCheck()
{
    wire::ServerVersionCheckRequest versionCheck;
    versionCheck.set_protocol_version(Protocol::Version);
    versionCheck.set_client_name("GameChatRelay_" + m_config.id);
    QueuePacket(Protocol::Type::SYS_ServerVersionCheck, versionCheck);
    m_state = State::ServerVersionCheck;
    m_deadline = chrono::steady_clock::now() + chrono::seconds(5);
}

void CChatRelay::HandlePacket(Protocol::Type type, const string& payload)
{
    if (m_state == State::ServerVersionCheck && type == Protocol::Type::SYS_ServerVersionCheckReply)
    {
        wire::ServerVersionCheckReply reply;
        if (!reply.ParseFromString(payload) || reply.protocol_version() != Protocol::Version ||
            reply.server_id() != "World" || reply.server_role() != wire::SERVER_ROLE_WORLD)
        {
            throw runtime_error("Unexpected chat relay server");
        }
        m_clientNonce = CEntryTicket::NewId();
        wire::RelayConnect request;
        request.set_server_id(m_config.id);
        request.set_client_nonce(m_clientNonce);
        QueuePacket(Protocol::Type::SS_RelayConnect, request);
        m_state = State::Challenge;
    }
    else if (m_state == State::Challenge && type == Protocol::Type::SS_RelayChallenge)
    {
        wire::RelayChallenge challenge;
        if (!challenge.ParseFromString(payload) || challenge.server_nonce().size() != 32 ||
            !m_auth.VerifyRelay(
                "World", m_config.id, m_clientNonce, challenge.server_nonce(), challenge.signature()))
        {
            throw runtime_error("Chat relay server authentication failed");
        }
        wire::RelayAuth proof;
        proof.set_signature(m_auth.SignRelay("Game", m_config.id, m_clientNonce, challenge.server_nonce()));
        QueuePacket(Protocol::Type::SS_RelayAuth, proof);
        m_state = State::Auth;
    }
    else if (m_state == State::Auth && type == Protocol::Type::SM_ActionReply)
    {
        wire::ActionReply reply;
        if (!reply.ParseFromString(payload) || !reply.ok())
        {
            throw runtime_error("Chat relay registration rejected");
        }
        m_state = State::Ready;
        cout << "[CHAT RELAY] " << m_config.id << " connected to World" << endl;
    }
    else if (m_state == State::Ready && type == Protocol::Type::SS_RelayChat)
    {
        wire::ChatMessage message;
        if (!message.ParseFromString(payload) || message.channel() != wire::CHAT_WORLD ||
            message.message_id().size() != 32 || message.player_id().size() != 32 || message.name().empty() ||
            message.name().size() > 32 || message.server_id().empty() || message.server_id().size() > 16 ||
            message.text().empty() || message.text().size() > 256 || m_messages.size() >= 256)
        {
            throw runtime_error("Invalid relayed chat or receive queue full");
        }
        m_messages.push_back(move(message));
    }
    else
    {
        throw runtime_error("Unexpected chat relay packet");
    }
}

void CChatRelay::Update()
{
    try
    {
        const auto now = chrono::steady_clock::now();
        if (m_state == State::Offline)
        {
            if (now < m_retryAt)
            {
                return;
            }
            Connect();
        }
        if (m_state != State::Ready && now >= m_deadline)
        {
            throw runtime_error("Chat relay handshake timeout");
        }
        const short events = static_cast<short>(
            POLLRDNORM | ((m_state == State::Connecting || !m_sending.empty()) ? POLLWRNORM : 0));
        WSAPOLLFD poll{m_socket.Get(), events, 0};
        SocketCheck(WSAPoll(&poll, 1, 0) != SOCKET_ERROR, "relay poll");
        if (poll.revents & (POLLERR | POLLNVAL))
        {
            throw runtime_error("Chat relay connection error");
        }
        if (m_state == State::Connecting)
        {
            if (!(poll.revents & (POLLWRNORM | POLLHUP)))
            {
                return;
            }
            int error = 0;
            int size = sizeof(error);
            SocketCheck(
                getsockopt(m_socket.Get(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size) == 0,
                "relay connect result");
            if (error != 0 || !(poll.revents & POLLWRNORM))
            {
                throw runtime_error("Chat relay connect failed");
            }
            SendVersionCheck();
        }
        // 한 번에 한 버퍼만 송수신한다. 채팅이 몰려도 메인 스레드를 독점하지 않는다.
        if (m_sent < m_sending.size())
        {
            const int count = send(
                m_socket.Get(), m_sending.data() + m_sent, static_cast<int>(m_sending.size() - m_sent), 0);
            if (count > 0)
            {
                m_sent += count;
            }
            else if (count == 0 || WSAGetLastError() != WSAEWOULDBLOCK)
            {
                throw runtime_error("Chat relay send failed");
            }
            if (m_sent == m_sending.size())
            {
                m_sending.clear();
                m_sent = 0;
            }
        }
        if (poll.revents & (POLLRDNORM | POLLHUP))
        {
            array<char, 4096> buffer{};
            const int count = recv(m_socket.Get(), buffer.data(), static_cast<int>(buffer.size()), 0);
            if (count == 0)
            {
                throw runtime_error("Chat relay closed");
            }
            if (count < 0)
            {
                if (WSAGetLastError() == WSAEWOULDBLOCK)
                {
                    return;
                }
                throw runtime_error("Chat relay receive failed");
            }
            m_received.append(buffer.data(), static_cast<size_t>(count));
            size_t consumed = 0;
            while (true)
            {
                const string_view remaining(m_received.data() + consumed, m_received.size() - consumed);
                const size_t size = Protocol::PacketSize(remaining);
                if (size == 0 || remaining.size() < size)
                {
                    break;
                }
                const auto type = static_cast<Protocol::Type>(Protocol::Read16(remaining.data() + 2));
                HandlePacket(type,
                             string(remaining.substr(Protocol::HeaderSize, size - Protocol::HeaderSize)));
                consumed += size;
            }
            m_received.erase(0, consumed);
        }
    }
    catch (const exception& error)
    {
        if (m_state == State::Ready)
        {
            cerr << "[CHAT RELAY] " << m_config.id << ' ' << error.what() << endl;
        }
        Reset();
    }
}

bool CChatRelay::SendChat(const wire::ChatMessage& message)
{
    if (!Ready())
    {
        return false;
    }
    try
    {
        QueuePacket(Protocol::Type::SS_RelayChat, message);
        return true;
    }
    catch (const exception&)
    {
        return false;
    }
}

vector<wire::ChatMessage> CChatRelay::TakeMessages()
{
    vector<wire::ChatMessage> result;
    result.swap(m_messages);
    return result;
}
