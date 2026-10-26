#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include "Common.h"

// Winsock 사용 구간을 객체 수명으로 묶는다. 모든 소켓보다 먼저 만들고 마지막에 정리한다.
class CWinsockSession final
{
public:
    CWinsockSession()
    {
        WSADATA data{};
        const int result = WSAStartup(MAKEWORD(2, 2), &data);
        if (result != 0)
        {
            throw runtime_error("WSAStartup failed: " + to_string(result));
        }
    }

    ~CWinsockSession()
    {
        WSACleanup();
    }
    CWinsockSession(const CWinsockSession&) = delete;
    CWinsockSession& operator=(const CWinsockSession&) = delete;
};

// 소켓은 복사할 수 없다. 이동할 때 소유권도 넘어가므로 두 곳에서 닫는 실수를 막는다.
class CSocket final
{
public:
    explicit CSocket(SOCKET handle = INVALID_SOCKET) : m_handle(handle)
    {
    }
    ~CSocket()
    {
        Reset();
    }
    CSocket(const CSocket&) = delete;
    CSocket& operator=(const CSocket&) = delete;
    CSocket(CSocket&& other) noexcept : m_handle(exchange(other.m_handle, INVALID_SOCKET))
    {
    }
    CSocket& operator=(CSocket&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_handle = exchange(other.m_handle, INVALID_SOCKET);
        }
        return *this;
    }
    SOCKET Get() const
    {
        return m_handle;
    }
    void Reset()
    {
        if (m_handle != INVALID_SOCKET)
        {
            closesocket(m_handle);
            m_handle = INVALID_SOCKET;
        }
    }

private:
    SOCKET m_handle;
};

inline void SocketCheck(bool ok, const string& operation)
{
    if (!ok)
    {
        throw runtime_error(operation + " failed: " + to_string(WSAGetLastError()));
    }
}

inline void SetNonblocking(SOCKET socket, bool enabled)
{
    // 워커 하나가 여러 연결을 처리하므로 느린 클라이언트를 기다리며 멈추지 않는다.
    u_long mode = enabled ? 1UL : 0UL;
    SocketCheck(ioctlsocket(socket, FIONBIO, &mode) == 0, "ioctlsocket");
}

inline sockaddr_in MakeAddress(const string& address, int port)
{
    // 초기 버전에서는 IPv4 숫자 주소만 받는다. 호스트 이름 조회는 수행하지 않는다.
    sockaddr_in result{};
    result.sin_family = AF_INET;
    result.sin_port = htons(static_cast<u_short>(port));
    if (inet_pton(AF_INET, address.c_str(), &result.sin_addr) != 1)
    {
        throw runtime_error("Invalid IPv4 address: " + address);
    }
    return result;
}
