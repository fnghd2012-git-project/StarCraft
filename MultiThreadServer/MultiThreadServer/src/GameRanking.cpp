#include "GameLogic.h"

namespace
{
string XmlText(const string& value)
{
    string result;
    for (const char ch : value)
    {
        if (ch == '&')
        {
            result += "&amp;";
        }
        else if (ch == '<')
        {
            result += "&lt;";
        }
        else if (ch == '>')
        {
            result += "&gt;";
        }
        else if (ch == '"')
        {
            result += "&quot;";
        }
        else if (ch == '\'')
        {
            result += "&apos;";
        }
        else
        {
            result += ch;
        }
    }
    return result;
}
} // namespace

void CGameLogic::StartMatch()
{
    if (m_config.type != "World")
    {
        return;
    }
    // 리플레이를 꺼도 경기 ID와 전투 집계는 동작한다.
    m_matchId = CEntryTicket::NewId();
    m_ranking.clear();
    m_matchFinished = false;
    m_rankingDirty = true;
    cout << "[MATCH] Started " << m_matchId << endl;
}

void CGameLogic::CountEntry(const stPlayer& player)
{
    auto& row = m_ranking[player.unit.player_id()];
    row.set_player_id(player.unit.player_id());
    row.set_name(player.unit.name());
    row.set_game_server(player.unit.game_server());
    row.set_entries(row.entries() + 1);
    m_rankingDirty = true;
}

void CGameLogic::CountDeath(const stPlayer& victim, const string& killerId, wire::Race killerRace)
{
    // Return의 bool은 재입장 대기 여부다. back도 true이므로 그곳에서 죽음을 세면 안 된다.
    if (!victim.inWorld || victim.unit.hp() != 0)
    {
        return;
    }
    const auto dead = m_ranking.find(victim.unit.player_id());
    if (dead == m_ranking.end())
    {
        return;
    }
    dead->second.set_deaths(dead->second.deaths() + 1);
    const auto killer = m_ranking.find(killerId);
    // 스톰 시전자가 먼저 복귀해도 시전 때 저장한 계정/진영으로 마지막 타격을 판정한다.
    if (killer != m_ranking.end() && killerId != victim.unit.player_id() && killerRace != victim.unit.race())
    {
        killer->second.set_kills(killer->second.kills() + 1);
    }
    m_rankingDirty = true;
}

void CGameLogic::PublishRanking()
{
    if (!m_up_redis || !m_rankingDirty)
    {
        return;
    }
    stMatchRanking snapshot;
    snapshot.matchId = m_matchId;
    snapshot.revision = ++m_rankingRevision;
    snapshot.players.reserve(m_ranking.size());
    for (const auto& entry : m_ranking)
    {
        snapshot.players.push_back(entry.second);
    }
    m_up_redis->Publish(move(snapshot));
    m_rankingDirty = false;
    m_nextRankPublish = chrono::steady_clock::now() + chrono::milliseconds(m_config.redisFlushMs);
}

void CGameLogic::RequestRanking(stPlayer& player, const string& payload)
{
    wire::RankRequest request;
    string error;
    const auto now = chrono::steady_clock::now();
    if (!request.ParseFromString(payload) || request.metric() < wire::RANK_KILLS ||
        request.metric() > wire::RANK_ENTRIES)
    {
        error = "Use rank kills, rank deaths or rank entries";
    }
    else if (m_config.type != "World" || !player.inWorld || player.databaseBusy ||
             player.leaseUntil <= CEntryTicket::NowSeconds())
    {
        error = "Ranking requires a live World session";
    }
    else if (!m_up_redis)
    {
        error = "Redis ranking is disabled in Server.ini";
    }
    else if (now < player.nextRankQuery)
    {
        error = "Wait one second before requesting ranking again";
    }
    else
    {
        const bool pending = find_if(m_pendingRanks.begin(),
                                     m_pendingRanks.end(),
                                     [&](const auto& entry)
                                     {
                                         return entry.second == player.sp_channel;
                                     }) != m_pendingRanks.end();
        if (pending)
        {
            error = "Ranking request already pending";
        }
        else
        {
            PublishRanking(); // 입장 직후 조회에도 자신의 0점/진입 1회가 포함되게 한다.
            stRankQuery query;
            query.id = ++m_nextRankQuery;
            query.matchId = m_matchId;
            query.playerId = player.unit.player_id();
            query.metric = request.metric();
            if (m_up_redis->Query(query))
            {
                m_pendingRanks.emplace(query.id, player.sp_channel);
                player.nextRankQuery = now + chrono::seconds(1);
                return;
            }
            error = "Ranking queue is full; retry later";
        }
    }
    wire::RankReply reply;
    reply.set_message(error);
    reply.set_match_id(m_matchId);
    reply.set_metric(request.metric());
    SendMessage(player.sp_channel, Protocol::Type::SM_RankReply, reply);
}

void CGameLogic::UpdateRanking()
{
    if (!m_up_redis)
    {
        return;
    }
    if (chrono::steady_clock::now() >= m_nextRankPublish)
    {
        PublishRanking();
    }
    for (const auto& result : m_up_redis->TakeResults())
    {
        const auto found = m_pendingRanks.find(result.id);
        if (found == m_pendingRanks.end())
        {
            continue;
        }
        SendMessage(found->second, Protocol::Type::SM_RankReply, result.reply);
        m_pendingRanks.erase(found);
    }
}

void CGameLogic::FinishMatch(const string& reason)
{
    if (m_config.type != "World" || m_matchFinished)
    {
        return;
    }
    m_matchFinished = true;
    PublishRanking();
    if (m_ranking.empty())
    {
        return;
    }
    // Redis 반영 지연과 관계없이 게임 스레드가 가진 최종 총계로 결과를 저장한다.
    // XML 파일 쓰기와 ODBC 호출은 DB 쓰레드(워커)가 담당한다. 파일은 장애 시 재적재 원본이다.
    string xml = "<match realm=\"" + XmlText(m_config.databaseRealm) + "\" id=\"" + m_matchId +
                 "\" winner=\"" + to_string(static_cast<int>(m_winner)) + "\" complete=\"" +
                 (reason == "round_finished" ? "1" : "0") + "\" reason=\"" + reason + "\">";
    for (const auto& entry : m_ranking)
    {
        const auto& row = entry.second;
        xml += "<player id=\"" + row.player_id() + "\" name=\"" + XmlText(row.name()) + "\" server=\"" +
               XmlText(row.game_server()) + "\" kills=\"" + to_string(row.kills()) + "\" deaths=\"" +
               to_string(row.deaths()) + "\" entries=\"" + to_string(row.entries()) + "\"/>";
    }
    xml += "</match>";
    stDatabaseJob job;
    job.id = ++m_nextDatabaseId;
    job.action = "SaveMatch";
    job.matchXml = move(xml);
    job.matchFile = m_config.configFile.parent_path() / "MatchResults" / (m_matchId + ".xml");
    const auto id = job.id;
    const auto path = job.matchFile;
    if (m_database.Submit(move(job)))
    {
        m_pendingMatchSaves.emplace(id, path);
    }
    else
    {
        cerr << "[MATCH DB FAIL] Queue full; result was NOT saved: " << m_matchId << endl;
    }
}
