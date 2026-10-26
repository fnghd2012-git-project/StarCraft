#pragma once

#include "Network.h"
#include "Config.h"
#include "EntryTicket.h"

// 게임 서버마다 World로 연결 하나를 유지한다. 소켓과 버퍼는 게임 메인 스레드만 접근한다.
// 모든 I/O는 nonblocking이며 Update에서 조금씩 처리해 전투/접속 처리를 멈추지 않는다.
class CChatRelay final
{
public:
    CChatRelay(const stServerConfig& config, const CEntryTicket& auth) : m_config(config), m_auth(auth)
    {
    }
    void Update();
    bool Ready() const
    {
        return m_state == State::Ready;
    }
    bool SendChat(const wire::ChatMessage& message);
    vector<wire::ChatMessage> TakeMessages();
    void Reset();

private:
    enum class State
    {
        Offline,
        Connecting,
        ServerVersionCheck,
        Challenge,
        Auth,
        Ready
    };
    void Connect();
    void SendVersionCheck();
    void QueuePacket(Protocol::Type type, const google::protobuf::MessageLite& message);
    void HandlePacket(Protocol::Type type, const string& payload);
    const stServerConfig& m_config;
    const CEntryTicket& m_auth;
    CSocket m_socket;
    State m_state = State::Offline;
    string m_clientNonce;
    string m_received;
    string m_sending;
    size_t m_sent = 0;
    vector<wire::ChatMessage> m_messages;
    chrono::steady_clock::time_point m_retryAt{};
    chrono::steady_clock::time_point m_deadline{};
};
