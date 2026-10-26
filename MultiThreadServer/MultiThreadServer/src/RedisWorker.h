#pragma once

#include "Config.h"
#include "Packet.h"

// 접속 Player와 따로 보관한다. 같은 경기에서는 사망/로그아웃 후에도 누적 기록이 남는다.
struct stMatchRanking
{
    string matchId;
    unsigned long long revision = 0;
    vector<wire::RankEntry> players;
};

struct stRankQuery
{
    unsigned long long id = 0;
    string matchId;
    string playerId;
    wire::RankMetric metric = wire::RANK_KILLS;
};

struct stRankResult
{
    unsigned long long id = 0;
    wire::RankReply reply;
};

// hiredis 연결은 이 워커 하나만 사용한다. 메인 스레드에서는 메모리 복사만 한다.
class CRedisWorker final
{
public:
    explicit CRedisWorker(const stServerConfig& config);
    ~CRedisWorker();
    void Publish(stMatchRanking ranking);
    bool Query(stRankQuery query);
    vector<stRankResult> TakeResults();

private:
    void Run();
    const stServerConfig& m_config;
    mutex m_rankingMutex; // 최신 순위 복사본·조회 큐·결과 큐·종료 상태를 보호한다.
    condition_variable m_ready;
    shared_ptr<const stMatchRanking> m_sp_latest;
    deque<stRankQuery> m_queries;
    vector<stRankResult> m_results;
    size_t m_outstanding = 0;
    bool m_stopping = false;
    jthread m_worker;
};
