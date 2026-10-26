#pragma once

#include "Common.h"
#include "Game.pb.h"

namespace Protocol
{
// 구조체 메모리를 그대로 전송하지 않는다. 패딩과 CPU 바이트 순서 차이를 없앤다.
// 헤더: 전체 길이 2바이트 + 패킷 종류 2바이트. 패딩 없이 큰 바이트부터 전송한다.
constexpr size_t HeaderSize = 4;
constexpr size_t MaxPacketSize = 4096;
// 최초 SYS_ServerVersionCheck 본문에서 한 번 확인한다. 매 패킷의 헤더에는 넣지 않는다.
// 리플레이에도 당시 통신 버전을 기록한다.
constexpr unsigned Version = 5;
enum class Type : unsigned
{
    // 1~9는 시스템용이다. 현재 1~4만 사용하고 5~9는 비워 둔다.
    SYS_ServerVersionCheck = 1,
    SYS_ServerVersionCheckReply,
    SYS_Ping,
    SYS_Pong,

    // 게임 패킷은 10부터 선언 순서대로 1씩 증가한다.
    // CM: 클라이언트 -> 서버, SM: 서버 -> 클라이언트, SS: 게임 서버 <-> World.
    // 기존 번호가 밀리지 않도록 새 게임 패킷은 목록 끝에 추가한다.
    CM_JoinGame = 10,
    SM_JoinGameReply,
    CM_RequestTicket,
    SM_WorldTicket,
    CM_ResumeGame,
    CM_Logout,
    CM_EnterWorld,
    SM_WorldReply,
    CM_Move,
    CM_Attack,
    CM_LeaveWorld,
    SM_ActionReply,
    SM_WorldState,
    SM_WorldEvent,
    CM_Skill,
    CM_Chat,
    SM_ChatMessage,
    SS_RelayConnect,
    SS_RelayChallenge,
    SS_RelayAuth,
    SS_RelayChat,
    CM_Rank,
    SM_RankReply
};

inline unsigned Read16(const char* p_data)
{
    return (static_cast<unsigned char>(p_data[0]) << 8) | static_cast<unsigned char>(p_data[1]);
}

inline void Append16(string& data, unsigned value)
{
    data += static_cast<char>((value >> 8) & 0xff);
    data += static_cast<char>(value & 0xff);
}

inline string Encode(Type type, string_view payload)
{
    if (payload.size() > MaxPacketSize - HeaderSize)
    {
        throw runtime_error("Packet payload too large");
    }
    string result;
    // 전송 형식: [전체 길이 2바이트][패킷 종류 2바이트][Protobuf 본문].
    const unsigned size = static_cast<unsigned>(HeaderSize + payload.size());
    Append16(result, size);
    Append16(result, static_cast<unsigned>(type));
    result.append(payload);
    return result;
}

inline size_t PacketSize(string_view data)
{
    // TCP는 메시지 경계를 보존하지 않는다. 헤더가 덜 왔으면 다음 수신까지 기다린다.
    if (data.size() < HeaderSize)
    {
        return 0;
    }
    const unsigned size = Read16(data.data());
    // 길이는 헤더를 포함한 값이다. 본문을 기다리기 전에 4~4096바이트 범위를 확인한다.
    if (size < HeaderSize || size > MaxPacketSize)
    {
        throw runtime_error("Invalid packet size");
    }
    return size;
}

inline string EncodeMessage(Type type, const google::protobuf::MessageLite& message)
{
    // 큰 메시지를 먼저 직렬화해 메모리를 잡지 않도록 실제 직렬화 크기를 확인한다.
    if (message.ByteSizeLong() > MaxPacketSize - HeaderSize)
    {
        throw runtime_error("Protobuf payload too large");
    }
    string payload;
    // 직렬화: 메시지의 멤버 값을 전송 가능한 바이트로 바꾼다. 이 함수에서 소켓 송신은 하지 않는다.
    if (!message.SerializeToString(&payload))
    {
        throw runtime_error("Protobuf serialization failed");
    }
    // 직렬화한 본문 앞에 공통 헤더를 붙여 완성된 패킷을 만든다.
    return Encode(type, payload);
}
} // namespace Protocol
