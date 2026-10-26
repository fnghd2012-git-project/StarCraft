#pragma once

#include "Network.h"
#include "Config.h"
#include "Packet.h"

CSocket Connect(const stServerConfig& config);
void SendAll(SOCKET socket, string_view bytes);
string ReadExact(SOCKET socket, size_t length);
string ReadPacket(SOCKET socket, Protocol::Type expected);
void PrintPlayLine(const string& line);

// 입력을 기다리는 동안에도 채팅과 전장 상태/사망 통지를 계속 받는다.
// 요청은 호출 쪽에서 직렬화한다. Play의 콘솔/자동 공략은 같은 전환 잠금을 사용한다.
class CClientConnection final
{
public:
    explicit CClientConnection(const stServerConfig& config, bool quiet = false);
    ~CClientConnection();
    string Request(Protocol::Type type,
                   const google::protobuf::MessageLite& message,
                   Protocol::Type expected);
    wire::WorldState Snapshot();
    wire::WorldTicket ReturnTicket();
    bool InWorld() const
    {
        return m_inWorld.load();
    }
    const wire::MapInfo& Map() const
    {
        return m_map;
    }
    const vector<wire::SkillInfo>& Skills() const
    {
        return m_skills;
    }

private:
    void Receive(stop_token stop);
    struct stReply
    {
        Protocol::Type type;
        string payload;
    };
    CSocket m_socket;
    bool m_quiet;
    wire::MapInfo m_map; // ServerVersionCheck에서 받은 맵 범위. 생성 후에는 바뀌지 않는다.
    vector<wire::SkillInfo> m_skills;
    atomic<bool> m_inWorld = false;
    atomic<bool> m_expectedClose = false;
    mutex m_receiveMutex; // 수신 응답 큐·전장 스냅샷·복귀 티켓·오류 상태를 보호한다.
    condition_variable m_ready;
    deque<stReply> m_replies;
    wire::WorldState m_snapshot;
    wire::WorldTicket m_returnTicket;
    string m_error;
    jthread m_reader;
};
