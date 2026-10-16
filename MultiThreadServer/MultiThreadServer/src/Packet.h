#pragma once

#include "Common.h"
#include "Game.pb.h"

namespace Protocol
{
    // 구조체 메모리를 그대로 전송하지 않는다. 패딩과 CPU 바이트 순서 차이를 없앤다.
    // 헤더: 전체 길이 4바이트 + 패킷 종류 2바이트 + 버전 2바이트, 모두 큰 바이트부터 전송한다.
    constexpr size_t HeaderSize = 8;
    constexpr size_t MaxPacketSize = 4096;
    // 본문을 Protobuf로 바꾸었으므로 이전 문자열 프로토콜과 버전을 구분한다.
    constexpr unsigned Version = 2;
    enum class Type : unsigned { Hello = 1, HelloReply = 2, Ping = 3, Pong = 4 };

    inline unsigned Read16(const char* data)
    {
        return (static_cast<unsigned char>(data[0]) << 8) | static_cast<unsigned char>(data[1]);
    }

    inline unsigned Read32(const char* data)
    {
        return (Read16(data) << 16) | Read16(data + 2);
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
        const unsigned size = static_cast<unsigned>(HeaderSize + payload.size());
        Append16(result, size >> 16);
        Append16(result, size);
        Append16(result, static_cast<unsigned>(type));
        Append16(result, Version);
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
        const unsigned size = Read32(data.data());
        // 길이를 믿고 버퍼를 늘리기 전에 상한과 버전을 확인한다.
        if (size < HeaderSize || size > MaxPacketSize || Read16(data.data() + 6) != Version)
        {
            throw runtime_error("Invalid packet size/version");
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
        if (!message.SerializeToString(&payload))
        {
            throw runtime_error("Protobuf serialization failed");
        }
        return Encode(type, payload);
    }
}
