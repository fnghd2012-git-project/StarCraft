#include "RedisWorker.h"
#include <winsock2.h>
#include <hiredis.h>

namespace
{
using Reply = unique_ptr<redisReply, decltype(&freeReplyObject)>;
using Connection = unique_ptr<redisContext, decltype(&redisFree)>;

void CheckReply(const redisReply* p_reply)
{
    if (!p_reply)
    {
        throw runtime_error("Redis connection failed or timed out");
    }
    if (p_reply->type == REDIS_REPLY_ERROR)
    {
        throw runtime_error(string(p_reply->str, p_reply->len));
    }
    // EXEC 자체가 배열이어도 내부 명령은 실패할 수 있다. 부분 실패를 성공으로 보고하지 않는다.
    for (size_t i = 0; i < p_reply->elements; ++i)
    {
        CheckReply(p_reply->element[i]);
    }
}

Reply stCommand(redisContext* p_connection, const vector<string>& args)
{
    vector<const char*> values;
    vector<size_t> sizes;
    for (const auto& arg : args)
    {
        values.push_back(arg.data());
        sizes.push_back(arg.size());
    }
    Reply up_reply(static_cast<redisReply*>(redisCommandArgv(
                       p_connection, static_cast<int>(args.size()), values.data(), sizes.data())),
                   freeReplyObject);
    CheckReply(up_reply.get());
    return up_reply;
}

void Append(redisContext* p_connection, const vector<string>& args)
{
    vector<const char*> values;
    vector<size_t> sizes;
    for (const auto& arg : args)
    {
        values.push_back(arg.data());
        sizes.push_back(arg.size());
    }
    if (redisAppendCommandArgv(p_connection, static_cast<int>(args.size()), values.data(), sizes.data()) !=
        REDIS_OK)
    {
        throw runtime_error("Redis pipeline append failed");
    }
}

string Key(const stServerConfig& config, const string& matchId)
{
    return config.redisPrefix + ":" + config.databaseRealm + ":" + matchId;
}

void Save(redisContext* p_connection, const stServerConfig& config, const stMatchRanking& ranking)
{
    const auto key = Key(config, ranking.matchId);
    // 총계를 덮어쓰므로 응답 유실 후 재시도해도 중복 가산되지 않는다.
    // 단일 워커가 순서대로 처리하므로 이전 총계가 나중 총계를 덮는 일도 없다.
    Append(p_connection, {"MULTI"});
    for (const auto& player : ranking.players)
    {
        Append(p_connection, {"ZADD", key + ":kills", to_string(player.kills()), player.player_id()});
        Append(p_connection, {"ZADD", key + ":deaths", to_string(player.deaths()), player.player_id()});
        Append(p_connection, {"ZADD", key + ":entries", to_string(player.entries()), player.player_id()});
        Append(p_connection, {"HSET", key + ":players", player.player_id(), player.SerializeAsString()});
    }
    for (const string suffix : {":kills", ":deaths", ":entries", ":players"})
    {
        Append(p_connection, {"EXPIRE", key + suffix, to_string(config.redisRetentionSeconds)});
    }
    Append(p_connection, {"EXEC"});
    // 모든 명령을 한 번에 보내고 응답을 회수한다. 플레이어 수만큼 왕복 대기하지 않는다.
    const size_t replies = 6 + ranking.players.size() * 4;
    for (size_t i = 0; i < replies; ++i)
    {
        void* p_raw = nullptr;
        const int status = redisGetReply(p_connection, &p_raw);
        Reply up_reply(static_cast<redisReply*>(p_raw), freeReplyObject);
        if (status != REDIS_OK)
        {
            throw runtime_error("Redis pipeline read failed");
        }
        CheckReply(up_reply.get());
        if (i + 1 == replies && up_reply->type != REDIS_REPLY_ARRAY)
        {
            throw runtime_error("Redis transaction aborted");
        }
    }
}

wire::RankEntry ReadPlayer(redisContext* p_connection, const string& key, const string& id, unsigned rank)
{
    const auto up_raw = stCommand(p_connection, {"HGET", key + ":players", id});
    wire::RankEntry player;
    if (up_raw->type != REDIS_REPLY_STRING ||
        !player.ParseFromArray(up_raw->str, static_cast<int>(up_raw->len)))
    {
        throw runtime_error("Ranking data is missing; retry after refresh");
    }
    player.set_rank(rank);
    return player;
}

wire::RankReply Read(redisContext* p_connection, const stServerConfig& config, const stRankQuery& query)
{
    const auto key = Key(config, query.matchId);
    const string metric = query.metric == wire::RANK_KILLS    ? "kills"
                          : query.metric == wire::RANK_DEATHS ? "deaths"
                                                              : "entries";
    const auto up_top = stCommand(p_connection, {"ZREVRANGE", key + ":" + metric, "0", "9"});
    if (up_top->type != REDIS_REPLY_ARRAY)
    {
        throw runtime_error("Invalid ranking response");
    }
    wire::RankReply result;
    result.set_match_id(query.matchId);
    result.set_metric(query.metric);
    for (size_t i = 0; i < up_top->elements; ++i)
    {
        const auto* p_id = up_top->element[i];
        if (p_id->type != REDIS_REPLY_STRING)
        {
            throw runtime_error("Invalid ranking member");
        }
        *result.add_top() =
            ReadPlayer(p_connection, key, string(p_id->str, p_id->len), static_cast<unsigned>(i + 1));
    }
    const auto up_rank = stCommand(p_connection, {"ZREVRANK", key + ":" + metric, query.playerId});
    if (up_rank->type != REDIS_REPLY_INTEGER || up_rank->integer < 0)
    {
        throw runtime_error("Own ranking is not ready");
    }
    *result.mutable_self() =
        ReadPlayer(p_connection, key, query.playerId, static_cast<unsigned>(up_rank->integer + 1));
    result.set_ok(true);
    return result;
}
} // namespace

// Redis 전용 스레드 하나를 생성한다. World에서 Redis 사용 설정이 켜진 경우 이 객체를 만든다.
CRedisWorker::CRedisWorker(const stServerConfig& config)
    : m_config(config), m_worker(
                            [this]
                            {
                                Run();
                            })
{
}

CRedisWorker::~CRedisWorker()
{
    {
        lock_guard lock(m_rankingMutex);
        m_stopping = true;
    }
    m_ready.notify_one();
    m_worker.join();
}

void CRedisWorker::Publish(stMatchRanking ranking)
{
    auto sp_snapshot = make_shared<const stMatchRanking>(move(ranking));
    {
        lock_guard lock(m_rankingMutex);
        m_sp_latest = move(sp_snapshot);
    }
    // 중간 총계는 최신 값으로 합친다. 장애 동안 큐가 계속 커지지 않는다.
}

bool CRedisWorker::Query(stRankQuery query)
{
    lock_guard lock(m_rankingMutex);
    if (m_stopping || m_outstanding >= static_cast<size_t>(m_config.redisQueryCapacity))
    {
        return false;
    }
    m_queries.push_back(move(query));
    ++m_outstanding;
    m_ready.notify_one();
    return true;
}

vector<stRankResult> CRedisWorker::TakeResults()
{
    // 조회 결과를 메인으로 넘긴다. 클라이언트 응답은 CGameLogic::UpdateRanking에서 만든다.
    lock_guard lock(m_rankingMutex);
    vector<stRankResult> results;
    results.swap(m_results);
    m_outstanding -= results.size();
    return results;
}

void CRedisWorker::Run()
{
    Connection up_connection(nullptr, redisFree);
    shared_ptr<const stMatchRanking> sp_saved;
    bool failureLogged = false;
    auto savedAt = chrono::steady_clock::time_point{};
    while (true)
    {
        shared_ptr<const stMatchRanking> sp_snapshot;
        deque<stRankQuery> queries;
        bool stopping = false;
        {
            unique_lock lock(m_rankingMutex);
            // 조회 요청이 오면 바로 깨어나고, 요청이 없어도 FlushMs마다 최신 총계를 확인한다.
            m_ready.wait_for(lock,
                             chrono::milliseconds(m_config.redisFlushMs),
                             [this]
                             {
                                 return m_stopping || !m_queries.empty();
                             });
            stopping = m_stopping;
            sp_snapshot = m_sp_latest;
            queries.swap(m_queries);
        }
        // 큐 잠금을 푼 뒤 Redis 통신을 수행한다. 게임 상태는 이 스레드에서 수정하지 않는다.
        string error;
        try
        {
            if (!up_connection)
            {
                const timeval timeout{m_config.redisTimeoutMs / 1000,
                                      (m_config.redisTimeoutMs % 1000) * 1000};
                up_connection.reset(
                    redisConnectWithTimeout(m_config.redisHost.c_str(), m_config.redisPort, timeout));
                if (!up_connection || up_connection->err)
                {
                    throw runtime_error("Cannot connect to Redis");
                }
                if (redisSetTimeout(up_connection.get(), timeout) != REDIS_OK)
                {
                    throw runtime_error("Redis timeout setup failed");
                }
                sp_saved.reset();
            }
            stCommand(up_connection.get(), {"PING"});
            // TTL보다 훨씬 짧은 주기로 전체 총계를 갱신한다. Redis 재시작/키 만료도 복구한다.
            if (sp_snapshot && (!sp_saved || sp_snapshot->revision != sp_saved->revision ||
                                sp_snapshot->matchId != sp_saved->matchId ||
                                chrono::steady_clock::now() - savedAt >= chrono::seconds(30)))
            {
                Save(up_connection.get(), m_config, *sp_snapshot);
                sp_saved = sp_snapshot;
                savedAt = chrono::steady_clock::now();
            }
            if (failureLogged)
            {
                cout << "[REDIS] Reconnected; current match restored" << endl;
                failureLogged = false;
            }
        }
        catch (const exception& failure)
        {
            error = failure.what();
            up_connection.reset();
            sp_saved.reset();
            if (!failureLogged)
            {
                cerr << "[REDIS] " << error << "; combat continues, retrying" << endl;
                failureLogged = true;
            }
        }
        for (const auto& query : queries)
        {
            stRankResult result;
            result.id = query.id;
            try
            {
                if (!error.empty())
                {
                    throw runtime_error(error);
                }
                if (!sp_saved || sp_saved->matchId != query.matchId)
                {
                    throw runtime_error("Match changed; request again");
                }
                result.reply = Read(up_connection.get(), m_config, query);
            }
            catch (const exception& failure)
            {
                result.reply.set_message(failure.what());
                result.reply.set_match_id(query.matchId);
                result.reply.set_metric(query.metric);
                error = failure.what();
                up_connection.reset();
                sp_saved.reset();
            }
            lock_guard lock(m_rankingMutex);
            m_results.push_back(move(result));
        }
        if (stopping)
        {
            return;
        } // 종료 시 마지막 총계는 한 번만 시도한다. 장애 복구를 무한히 기다리지 않는다.
    }
}
