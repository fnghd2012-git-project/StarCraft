#include "GameLogic.h"

stDatabaseJob CGameLogic::MakeDatabaseJob(const stPlayer& player, const string& action) const
{
    stDatabaseJob job;
    job.action = action;
    job.realm = m_config.databaseRealm;
    job.account = player.unit.name();
    job.playerId = player.unit.player_id();
    job.server = m_config.id;
    job.gameServer = player.unit.game_server();
    job.token = player.databaseToken;
    job.unitIndex = static_cast<int>(player.unit.unit_index());
    job.race = player.unit.race() == wire::RACE_TERRAN    ? "Terran"
               : player.unit.race() == wire::RACE_PROTOSS ? "Protoss"
                                                          : "Zerg";
    job.reentryAt = player.reentryAt;
    job.cooldown = m_config.deathReentrySeconds;
    job.leaseSeconds = m_config.sessionLeaseSeconds;
    return job;
}

void CGameLogic::SubmitDatabase(stPlayer& player, stDatabaseJob job, stPendingDatabase pending)
{
    job.id = ++m_nextDatabaseId;
    const auto id = job.id;
    pending.sp_channel = player.sp_channel;
    pending.token = job.token;
    m_pendingDatabase.emplace(id, move(pending));
    if (!m_database.Submit(move(job)))
    {
        // DB 큐에도 들어가지 못한 로그인은 정원 예약을 돌려준다.
        // 이미 접속한 사용자의 이동/저장 실패에서는 기존 토큰을 유지한다.
        const auto& kind = m_pendingDatabase.at(id).kind;
        if (kind == "Login" || kind == "Resume" || kind == "Enter")
        {
            player.databaseToken.clear();
        }
        m_pendingDatabase.erase(id);
        throw runtime_error("Database queue is full; retry later");
    }
    if (m_pendingDatabase.at(id).kind == "Renew")
    {
        player.renewing = true;
    }
    else
    {
        player.databaseBusy = true;
    }
}

void CGameLogic::Login(stPlayer& player, const wire::JoinGameRequest& request)
{
    ValidateGameCapacity(player);
    if (request.name().empty() || request.name().size() > 32 ||
        request.name().find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != string::npos)
    {
        throw runtime_error("Account ID must be 1..32 letters, digits, underscore or hyphen");
    }
    if (request.password().size() < 8 || request.password().size() > 128)
    {
        throw runtime_error("Password must contain 8..128 UTF-8 bytes");
    }
    vector<const stUnitDefinition*> candidates;
    for (const auto& unit : m_data.definitions)
    {
        if (unit.race == m_config.race)
        {
            candidates.push_back(&unit);
        }
    }
    if (candidates.empty())
    {
        throw runtime_error("No unit for this race");
    }
    uniform_int_distribution<size_t> choose(0, candidates.size() - 1);
    // 랜덤 후보는 신규 계정/서버 조합에서만 저장한다. 기존 조합은 DB의 UnitIndex를 사용한다.
    player.unit = MakeUnit(*candidates[choose(m_random)], "", request.name(), m_config.id);
    player.databaseToken = CEntryTicket::NewId();
    auto job = MakeDatabaseJob(player, "Login");
    job.password = request.password();
    stPendingDatabase pending;
    pending.kind = "Login";
    SubmitDatabase(player, move(job), move(pending));
}

void CGameLogic::ValidateGameCapacity(const stPlayer& player) const
{
    size_t count = 0;
    for (const auto& entry : m_players)
    {
        const auto& other = entry.second;
        // 비동기 DB 로그인 중인 예약 슬롯도 포함한다. ServerVersionCheck/중계 전용 연결은 세지 않는다.
        if (&other != &player && !other.sp_channel->closeAfterSend.load() &&
            (other.p_definition || other.databaseBusy || !other.databaseToken.empty()))
        {
            ++count;
        }
    }
    if (count >= static_cast<size_t>(m_config.maxGamePlayers))
    {
        throw runtime_error("Game server is full");
    }
}

void CGameLogic::AcceptTransfer(stPlayer& player, const wire::EnterWorldRequest& request, bool world)
{
    if (!world)
    {
        ValidateGameCapacity(player);
    }
    const auto claims = m_tickets.Verify(request);
    if (claims.destination() != m_config.id || (!world && claims.game_server() != m_config.id))
    {
        throw runtime_error("Invalid transfer destination");
    }
    if (player.p_definition || !player.databaseToken.empty())
    {
        throw runtime_error("Already logged in");
    }
    const stUnitDefinition* p_definition = nullptr;
    string race;
    for (const auto& server : m_config.gameServers)
    {
        if (server.id == claims.game_server())
        {
            race = server.race;
            break;
        }
    }
    for (const auto& unit : m_data.definitions)
    {
        if (unit.index == static_cast<int>(claims.unit_index()) && unit.race == race)
        {
            p_definition = &unit;
            break;
        }
    }
    if (!p_definition)
    {
        throw runtime_error("Invalid transferred unit");
    }
    player.unit = MakeUnit(*p_definition, claims.player_id(), claims.name(), claims.game_server());
    player.databaseToken = CEntryTicket::NewId();
    auto job = MakeDatabaseJob(player, "Accept");
    job.nonce = claims.nonce();
    stPendingDatabase pending;
    pending.kind = world ? "Enter" : "Resume";
    pending.payload = request.SerializeAsString();
    SubmitDatabase(player, move(job), move(pending));
}

void CGameLogic::RequestTransfer(stPlayer& player)
{
    if (player.reentryAt > CEntryTicket::NowSeconds())
    {
        throw runtime_error("Reentry cooldown: " + to_string(player.reentryAt - CEntryTicket::NowSeconds()) +
                            " seconds");
    }
    stPendingDatabase pending;
    pending.kind = "Ticket";
    pending.ticket =
        m_tickets.Issue(player.unit, "World", m_config.worldAddress, m_config.worldPort, player.reentryAt);
    wire::EntryClaims claims;
    claims.ParseFromString(pending.ticket.claims());
    auto job = MakeDatabaseJob(player, "Transfer");
    job.nonce = claims.nonce();
    job.destination = "World";
    SubmitDatabase(player, move(job), move(pending));
}

void CGameLogic::SaveReturn(stPlayer& player,
                            const string& reason,
                            long long reentryAt,
                            const wire::WorldReply& denied)
{
    if (player.inWorld)
    {
        RecordReplayEvent(player, "Return", reason);
    }
    player.inWorld = false;
    player.moving = false;
    player.reentryAt = reentryAt;
    stPendingDatabase pending;
    pending.kind = "Return";
    pending.reason = reason;
    pending.retryAfter = static_cast<unsigned>(max(0LL, reentryAt - CEntryTicket::NowSeconds()));
    pending.deniedReply = denied;
    pending.ticket = CreateReturnTicket(player.unit, reentryAt);
    wire::EntryClaims claims;
    claims.ParseFromString(pending.ticket.claims());
    auto job = MakeDatabaseJob(player, "Transfer");
    job.nonce = claims.nonce();
    job.destination = claims.destination();
    try
    {
        SubmitDatabase(player, move(job), move(pending));
    }
    catch (const exception& error)
    {
        SendActionReply(player, false, error.what());
        player.sp_channel->closeAfterSend.store(true);
    }
}

void CGameLogic::Logout(stPlayer& player)
{
    if (player.inWorld)
    {
        RecordReplayEvent(player, "Logout", "World logout");
    }
    stPendingDatabase pending;
    pending.kind = "Logout";
    player.inWorld = false;
    player.moving = false;
    SubmitDatabase(player, MakeDatabaseJob(player, "Logout"), move(pending));
}

CGameLogic::~CGameLogic()
{
    FinishMatch("server_shutdown");
    PublishRanking();
    // 마지막 틱 이후 도착한 입장/명령도 종료 프레임에 남긴다.
    if (m_up_replay)
    {
        ++m_tick;
        CaptureReplay();
        m_up_replay->Finish("server_shutdown");
    }
    // 워커가 아직 살아 있을 때 종료 저장을 큐에 넣는다. 실패 시에도 DB lease가 영구 잠금을 막는다.
    for (const auto& entry : m_players)
    {
        const auto& player = entry.second;
        if (!player.databaseToken.empty() && !player.databaseBusy)
        {
            auto job = MakeDatabaseJob(player, "Logout");
            m_database.Submit(move(job));
        }
    }
}

void CGameLogic::UpdateDatabase()
{
    for (const auto& result : m_database.TakeResults())
    {
        const auto saved = m_pendingMatchSaves.find(result.id);
        if (saved != m_pendingMatchSaves.end())
        {
            if (result.ok)
            {
                cout << "[MATCH DB] Saved " << saved->second.filename().string() << endl;
            }
            else
            {
                cerr << "[MATCH DB FAIL] " << result.error << "; retry from " << saved->second.string()
                     << endl;
            }
            m_pendingMatchSaves.erase(saved);
            continue;
        }
        const auto found = m_pendingDatabase.find(result.id);
        if (found == m_pendingDatabase.end())
        {
            continue;
        }
        auto pending = move(found->second);
        m_pendingDatabase.erase(found);
        const auto entry = m_players.find(pending.sp_channel.get());
        if (entry == m_players.end())
        {
            continue;
        }
        auto& player = entry->second;
        if (pending.kind == "Renew")
        {
            player.renewing = false;
        }
        else
        {
            player.databaseBusy = false;
        }
        if (pending.token != player.databaseToken)
        {
            continue;
        }
        // 갱신보다 나중에 요청한 이동/로그아웃이 DB에서 먼저 끝날 수 있다.
        // 전환 결과가 세션 상태를 확정하므로, 이때 도착한 이전 갱신 결과는 적용하지 않는다.
        if (pending.kind == "Renew" && player.databaseBusy)
        {
            continue;
        }
        if (!result.ok)
        {
            cerr << "[DB] " << pending.kind << " failed: " << result.error << endl;
            if (pending.kind == "Enter")
            {
                wire::WorldReply reply;
                reply.set_message(result.error);
                SendMessage(player.sp_channel, Protocol::Type::SM_WorldReply, reply);
            }
            else
            {
                SendActionReply(player, false, result.error);
            }
            // 저장 결과를 모르는 연결에서 계속 플레이하지 않는다. 재로그인 시 DB 상태로 복구한다.
            player.inWorld = false;
            player.moving = false;
            player.sp_channel->closeAfterSend.store(true);
            continue;
        }
        player.leaseUntil = result.leaseUntil;
        player.renewAt = CEntryTicket::NowSeconds() + m_config.sessionLeaseSeconds / 3;
        if (pending.kind == "Renew")
        {
            continue;
        }
        if (pending.kind == "Ticket" || pending.kind == "Return" || pending.kind == "Logout")
        {
            if (pending.kind == "Ticket")
            {
                SendMessage(player.sp_channel, Protocol::Type::SM_WorldTicket, pending.ticket);
                cout << "[GAME LOGOUT] " << m_config.id << ' ' << player.unit.name() << " -> World" << endl;
            }
            else if (pending.kind == "Return")
            {
                if (!pending.deniedReply.message().empty())
                {
                    *pending.deniedReply.mutable_return_ticket() = pending.ticket;
                    SendMessage(player.sp_channel, Protocol::Type::SM_WorldReply, pending.deniedReply);
                }
                else
                {
                    wire::WorldEvent event;
                    event.set_message(pending.reason);
                    event.set_player_id(player.unit.player_id());
                    event.set_returned_to_game(true);
                    event.set_retry_after_seconds(pending.retryAfter);
                    *event.mutable_return_ticket() = pending.ticket;
                    SendMessage(player.sp_channel, Protocol::Type::SM_WorldEvent, event);
                }
                cout << "[WORLD LOGOUT] " << player.unit.name() << " -> " << player.unit.game_server() << ' '
                     << pending.reason << endl;
            }
            else
            {
                SendActionReply(player, true, "Logged out");
            }
            player.databaseToken.clear();
            player.p_definition = nullptr;
            player.sp_channel->closeAfterSend.store(true);
            continue;
        }
        player.unit.set_player_id(result.playerId);
        player.unit.set_unit_index(result.unitIndex);
        player.reentryAt = result.reentryAt;
        if (pending.kind == "Enter")
        {
            wire::EnterWorldRequest request;
            request.ParseFromString(pending.payload);
            EnterWorld(player, request);
            continue;
        }
        for (const auto& unit : m_data.definitions)
        {
            if (unit.index == result.unitIndex && unit.race == m_config.race)
            {
                player.p_definition = &unit;
                break;
            }
        }
        if (!player.p_definition)
        {
            SendActionReply(player, false, "Saved unit no longer exists in CSV");
            player.sp_channel->closeAfterSend.store(true);
            continue;
        }
        player.unit = MakeUnit(*player.p_definition, result.playerId, player.unit.name(), m_config.id);
        wire::JoinGameReply reply;
        *reply.mutable_unit() = player.unit;
        reply.set_retry_after_seconds(
            static_cast<unsigned>(max(0LL, player.reentryAt - CEntryTicket::NowSeconds())));
        SendMessage(player.sp_channel, Protocol::Type::SM_JoinGameReply, reply);
        cout << "[GAME LOGIN] " << m_config.id << ' ' << player.unit.name()
             << " saved unit=" << player.unit.unit_name() << endl;
    }
    const auto now = CEntryTicket::NowSeconds();
    for (auto& entry : m_players)
    {
        auto& player = entry.second;
        if (player.databaseToken.empty() || player.databaseBusy || player.sp_channel->closeAfterSend.load())
        {
            continue;
        }
        if (player.leaseUntil > 0 && player.leaseUntil <= now)
        {
            player.inWorld = false;
            player.moving = false;
            SendActionReply(player, false, "Session lease expired; reconnect");
            player.sp_channel->closeAfterSend.store(true);
            continue;
        }
        if (player.p_definition && !player.renewing && player.renewAt <= now)
        {
            stPendingDatabase pending;
            pending.kind = "Renew";
            try
            {
                SubmitDatabase(player, MakeDatabaseJob(player, "Renew"), move(pending));
            }
            catch (const exception&)
            {
                player.renewAt = now + 1;
            }
        }
    }
}
