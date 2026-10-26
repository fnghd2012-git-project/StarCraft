#pragma once
#include "Network.h"
#include "GameLogic.h"

// 한 연결의 통신 상태다. 여러 IOCP 쓰레드(워커)가 접근하므로 mutex로 직렬화한다.
// 진행 중인 IoOperation이 shared_ptr로 보관해 마지막 완료까지 생존한다.
struct stIocpSession
{
    CSocket socket;
    shared_ptr<stGameChannel> sp_channel = make_shared<stGameChannel>();
    mutex sessionMutex; // 한 연결의 수신·송신 버퍼와 I/O 진행·종료 상태를 보호한다.
    uintptr_t id = 0;
    string recvBuffer;
    string sendBuffer;
    size_t sendOffset = 0;
    bool versionChecked = false;
    bool sendPending = false;
    bool closed = false;
    bool closing = false;
    chrono::steady_clock::time_point connectedAt = chrono::steady_clock::now();
    chrono::steady_clock::time_point closeDeadline{};
};
