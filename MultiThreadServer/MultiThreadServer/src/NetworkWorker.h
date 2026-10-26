#pragma once
#include "Network.h"
#include "GameLogic.h"

// WSAPoll 세션은 배정된 워커 하나만 읽고 쓴다.
struct stClientSession
{
    stClientSession() = default;
    stClientSession(stClientSession&&) = default;
    stClientSession& operator=(stClientSession&&) = default;
    ~stClientSession()
    {
        if (sp_channel)
        {
            sp_channel->connected.store(false);
        }
    }
    // 이 세션은 배정된 워커만 읽고 쓴다. 수신/송신 버퍼에 별도 잠금이 필요 없다.
    CSocket socket;
    shared_ptr<stGameChannel> sp_channel = make_shared<stGameChannel>();
    string recvBuffer;
    string sendBuffer;
    size_t sendOffset = 0;
    bool versionChecked = false;
    bool closing = false;
    chrono::steady_clock::time_point closeDeadline{};
    chrono::steady_clock::time_point connectedAt = chrono::steady_clock::now();
};

// WSAPoll 비교 경로의 워커. Add는 메인, Run/Process는 워커에서 실행한다.
class CNetworkWorker final
{
public:
    CNetworkWorker(const stServerConfig& config, const string& serverInfo, CGameLogic& game);

    ~CNetworkWorker();

    bool AddClient(CSocket& socket);

    bool Failed() const;

private:
    void QueueSendPacket(stClientSession& client, const string& packet);

    bool ProcessSession(stClientSession& client, short events);

    void Run(stop_token stop);

    const stServerConfig& m_config;
    const string& m_serverInfo;
    CGameLogic& m_game;
    mutex m_pendingClientsMutex;
    vector<CSocket> m_pending;
    atomic<int> m_clientCount = 0;
    atomic<bool> m_failed = false;
    jthread m_thread;
};
