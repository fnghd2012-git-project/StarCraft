#include "GameLogic.h"

namespace
{
wire::Race RaceOf(const string& race)
{
    if (race == "Terran")
    {
        return wire::RACE_TERRAN;
    }
    if (race == "Protoss")
    {
        return wire::RACE_PROTOSS;
    }
    if (race == "Zerg")
    {
        return wire::RACE_ZERG;
    }
    throw runtime_error("Unknown race");
}

template <class Message> Message Parse(const string& payload)
{
    Message message;
    // 역직렬화: 큐에 보관한 본문 바이트를 MoveRequest 같은 Protobuf 메시지로 복원한다.
    if (!message.ParseFromString(payload))
    {
        throw runtime_error("Invalid gameplay protobuf");
    }
    return message;
}

double Distance(double ax, double ay, double bx, double by)
{
    return sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by));
}

void Damage(int damage, int armor, int percent, int& hp, int& shield)
{
    // 보호막은 타입 보정 전 피해를 받는다. 남은 HP 피해에 방어력과 크기별 비율을 적용한다.
    // 현재 HP는 정수이므로 소수점은 버리고 최소 1 피해를 준다.
    const int absorbed = min(shield, damage);
    shield -= absorbed;
    damage -= absorbed;
    if (damage > 0)
    {
        hp = max(0, hp - max(1, max(0, damage - armor) * percent / 100));
    }
}

void ValidateChatText(const string& text)
{
    if (text.empty() || text.size() > 256 || text.find_first_not_of(' ') == string::npos)
    {
        throw runtime_error("Chat must contain 1..256 UTF-8 bytes, not only spaces");
    }
    for (const unsigned char ch : text)
    {
        if (ch < 32 || ch == 127)
        {
            throw runtime_error("Control characters are not allowed in chat");
        }
    }
}
} // namespace

CGameLogic::CGameLogic(const stServerConfig& config, const stMapConfig& map, const stGameData& data)
    : m_config(config), m_map(map), m_data(data), m_tickets(config.clusterKeyFile),
      m_database(config.databaseWorkerCount, config.databaseQueueCapacity),
      m_battleWorkers(config.type == "World" ? config.battleWorkerCount : 1)
{
    ResetBuildings();
    StartMatch();
    StartReplay();
    if (m_config.type == "World" && m_config.redisEnabled)
    {
        m_up_redis = make_unique<CRedisWorker>(m_config);
    }
    if (m_config.type == "Game")
    {
        m_up_chatRelay = make_unique<CChatRelay>(m_config, m_tickets);
    }
}

void CGameLogic::Submit(const shared_ptr<stGameChannel>& sp_channel, Protocol::Type type, string_view payload)
{
    // 네트워크 쓰레드(워커)에서 호출한다. 큐 확인과 삽입을 잠금 하나로 묶는다.
    lock_guard lock(m_commandsMutex);
    // 패킷 개수 제한: 전체 큐 2,048개 / 연결별 대기 32개. 바이트 수나 초당 처리량이 아니다.
    if (m_commands.size() >= MaxPendingCommands ||
        sp_channel->pendingCommands.load() >= MaxPendingCommandsPerClient)
    {
        throw runtime_error("Game command queue full");
    }
    // 수신 버퍼가 정리돼도 본문이 남도록 복사한다. 여기서는 게임 로직을 실행하지 않는다.
    m_commands.push_back({sp_channel, type, string(payload)});
    ++sp_channel->pendingCommands;
}

void CGameLogic::SendMessage(const shared_ptr<stGameChannel>& sp_channel,
                             Protocol::Type type,
                             const google::protobuf::MessageLite& message)
{
    if (!sp_channel->connected.load())
    {
        return;
    }
    // 응답 직렬화 + 헤더 추가 후 송신 큐에 등록한다.
    SendPacket(sp_channel, Protocol::EncodeMessage(type, message));
}

// 메인 쓰레드: 완성된 패킷을 송신 큐에 넣는다. 실제 전송 완료를 기다리는 함수는 아니다.
void CGameLogic::SendPacket(const shared_ptr<stGameChannel>& sp_channel, string packet)
{
    if (!sp_channel->connected.load())
    {
        return;
    }
    lock_guard lock(sp_channel->sendMutex);
    if (sp_channel->pendingSendBytes + packet.size() > static_cast<size_t>(m_config.maxPendingSendBytes))
    {
        // 읽지 않는 연결의 게임 우편함도 네트워크 송신 버퍼와 같은 상한을 적용한다.
        sp_channel->connected.store(false);
        return;
    }
    sp_channel->pendingSendBytes += packet.size();
    sp_channel->sendQueue.push_back(move(packet));
    // IOCP는 완료 포트에 알림을 보내 송신할 워커를 깨운다. WSAPoll은 다음 순회에서 큐를 확인한다.
    if (sp_channel->notifySend && !sp_channel->sendNotifyPending)
    {
        sp_channel->sendNotifyPending = true;
        sp_channel->notifySend();
    }
}

void CGameLogic::SendActionReply(stPlayer& player, bool ok, const string& message)
{
    wire::ActionReply reply;
    reply.set_ok(ok);
    reply.set_message(message);
    SendMessage(player.sp_channel, Protocol::Type::SM_ActionReply, reply);
}

wire::UnitState CGameLogic::MakeUnit(const stUnitDefinition& definition,
                                     const string& id,
                                     const string& name,
                                     const string& gameServer) const
{
    wire::UnitState unit;
    unit.set_player_id(id);
    unit.set_name(name);
    unit.set_game_server(gameServer);
    unit.set_race(RaceOf(definition.race));
    unit.set_unit_index(definition.index);
    unit.set_unit_name(definition.name);
    unit.set_hp(definition.hp);
    unit.set_max_hp(definition.hp);
    unit.set_shield(definition.shield);
    unit.set_mp(definition.initialMP);
    unit.set_attack_range(definition.range);
    unit.set_attack_damage(definition.damage);
    unit.set_max_mp(definition.mp);
    for (const int skill : definition.skills)
    {
        unit.add_skills(skill);
    }
    return unit;
}

void CGameLogic::HandlePacket(stPlayer& player, Protocol::Type type, const string& payload)
{
    // 메인 스레드에서 패킷 번호에 맞는 본문으로 역직렬화하고, 접속 상태와 게임 규칙을 검사한다.
    if (type == Protocol::Type::SS_RelayConnect || type == Protocol::Type::SS_RelayAuth ||
        type == Protocol::Type::SS_RelayChat)
    {
        // 서버 연결은 별도 인증 후 중계 패킷만 허용한다. 일반 플레이어로 전환할 수 없다.
        try
        {
            HandleRelay(player, type, payload);
        }
        catch (const exception& error)
        {
            SendActionReply(player, false, error.what());
            player.sp_channel->closeAfterSend.store(true);
        }
        return;
    }
    if (!player.relayServerId.empty())
    {
        player.sp_channel->closeAfterSend.store(true);
        return;
    }
    if (type == Protocol::Type::CM_Rank)
    {
        RequestRanking(player, payload);
        return;
    }
    if (player.databaseBusy)
    {
        throw runtime_error("Database operation pending; wait for reply");
    }
    if (!player.databaseToken.empty() && player.leaseUntil > 0 &&
        player.leaseUntil <= CEntryTicket::NowSeconds())
    {
        throw runtime_error("Session lease expired; reconnect");
    }
    if (type == Protocol::Type::CM_Logout && !player.databaseToken.empty())
    {
        Parse<wire::EmptyRequest>(payload);
        Logout(player);
        return;
    }
    if (m_config.type == "Game")
    {
        HandleGamePacket(player, type, payload);
        return;
    }
    HandleWorldPacket(player, type, payload);
}

// 일반 게임 서버에서는 로그인, 서버 이동, 채팅만 처리한다.
void CGameLogic::HandleGamePacket(stPlayer& player, Protocol::Type type, const string& payload)
{
    if (type == Protocol::Type::CM_JoinGame && !player.p_definition)
    {
        Login(player, Parse<wire::JoinGameRequest>(payload));
        return;
    }
    if (type == Protocol::Type::CM_ResumeGame && !player.p_definition)
    {
        AcceptTransfer(player, Parse<wire::EnterWorldRequest>(payload), false);
        return;
    }
    if (type == Protocol::Type::CM_RequestTicket && player.p_definition)
    {
        Parse<wire::EmptyRequest>(payload);
        RequestTransfer(player);
        return;
    }
    if (type == Protocol::Type::CM_Chat && player.p_definition)
    {
        HandleChat(player, Parse<wire::ChatRequest>(payload));
        return;
    }
    throw runtime_error("Game server requires login and only accepts World transfer or chat requests");
}

// World 전용 패킷은 입장 여부를 확인한 뒤 각 기능으로 넘긴다.
void CGameLogic::HandleWorldPacket(stPlayer& player, Protocol::Type type, const string& payload)
{
    if (type == Protocol::Type::CM_EnterWorld)
    {
        try
        {
            AcceptTransfer(player, Parse<wire::EnterWorldRequest>(payload), true);
        }
        catch (const exception& error)
        {
            wire::WorldReply reply;
            reply.set_message(error.what());
            SendMessage(player.sp_channel, Protocol::Type::SM_WorldReply, reply);
        }
        return;
    }
    if (!player.inWorld)
    {
        throw runtime_error("Enter World first");
    }
    if (type == Protocol::Type::CM_Chat)
    {
        HandleChat(player, Parse<wire::ChatRequest>(payload));
    }
    else if (type == Protocol::Type::CM_Move)
    {
        if (player.lockedUntil > m_tick || player.unit.burrowed() || player.burrowAt > m_tick)
        {
            throw runtime_error("Unit cannot move while locked or burrowed");
        }
        HandleMove(player, Parse<wire::MoveRequest>(payload));
    }
    else if (type == Protocol::Type::CM_Attack)
    {
        HandleAttack(player, Parse<wire::AttackRequest>(payload).target_id());
    }
    else if (type == Protocol::Type::CM_Skill)
    {
        HandleSkill(player, Parse<wire::SkillRequest>(payload));
    }
    else if (type == Protocol::Type::CM_LeaveWorld)
    {
        Parse<wire::EmptyRequest>(payload);
        // 전투 중 복귀/연결 종료로 체력을 즉시 초기화하는 우회를 막기 위해 같은 대기 시간을 적용한다.
        SendActionReply(player, true, "Returning to game server");
        ReturnToGame(player, "Returned to game server", true);
    }
    else
    {
        throw runtime_error("Unexpected gameplay request");
    }
}

// 메인 쓰레드: 이동 목표만 설정한다. 현재 좌표는 다음 Tick에서 갱신한다.
void CGameLogic::HandleMove(stPlayer& player, const wire::MoveRequest& request)
{
    if (!IsValidPosition(request.x(), request.y()))
    {
        throw runtime_error("Position is outside the map or inside a building");
    }
    // 목표 위치만 저장한다. 실제 좌표는 Tick -> MovePlayers에서 이동속도에 맞춰 갱신한다.
    player.goalX = request.x();
    player.goalY = request.y();
    player.moving = true;
    player.p_heal = nullptr;
    player.unit.set_healing(false);
    SendActionReply(player, true, "Move accepted");
}

void CGameLogic::HandleChat(stPlayer& player, const wire::ChatRequest& request)
{
    // UTF-8 유효성은 Protobuf 파싱에서 검사한다. 채팅은 한 줄, 최대 256바이트로 제한한다.
    ValidateChatText(request.text());
    if (request.channel() != wire::CHAT_LOCAL && request.channel() != wire::CHAT_WORLD)
    {
        throw runtime_error("Unknown chat channel");
    }
    if (m_config.type == "World" && request.channel() != wire::CHAT_LOCAL)
    {
        throw runtime_error("World battle only allows local chat");
    }
    const auto now = chrono::steady_clock::now();
    if (now < player.nextChat)
    {
        throw runtime_error("Chat interval is 500 ms");
    }
    wire::ChatMessage message;
    message.set_server_id(m_config.id);
    message.set_player_id(player.unit.player_id());
    message.set_name(player.unit.name());
    message.set_text(request.text());
    message.set_channel(request.channel());
    if (request.channel() == wire::CHAT_WORLD)
    {
        message.set_message_id(CEntryTicket::NewId());
        if (m_pendingChats.size() >= 64 || !m_up_chatRelay->SendChat(message))
        {
            throw runtime_error("World chat relay is unavailable or busy; try again later");
        }
        // World가 중계한 원문을 돌려준 뒤 성공 응답한다. 연결 실패를 성공으로 표시하지 않는다.
        m_pendingChats.emplace(message.message_id(),
                               stPendingChat{player.sp_channel, now + chrono::seconds(3)});
        player.nextChat = now + chrono::milliseconds(500);
        return;
    }
    player.nextChat = now + chrono::milliseconds(500);
    SendActionReply(player, true, "Chat sent");
    BroadcastChat(message);
}

void CGameLogic::BroadcastChat(const wire::ChatMessage& message)
{
    // 게임 상태는 메인 스레드가 소유한다. 다른 워커에 속한 연결도 우편함으로 전달한다.
    // 입장권을 발급받아 로그아웃한 플레이어와 ServerVersionCheck만 보낸 연결은 수신 대상에서 제외한다.
    for (const auto& entry : m_players)
    {
        const auto& receiver = entry.second;
        const bool loggedIn = m_config.type == "World" ? receiver.inWorld : receiver.p_definition != nullptr;
        if (loggedIn && receiver.sp_channel->connected.load() && !receiver.sp_channel->closeAfterSend.load())
        {
            SendMessage(receiver.sp_channel, Protocol::Type::SM_ChatMessage, message);
        }
    }
}

void CGameLogic::HandleRelay(stPlayer& player, Protocol::Type type, const string& payload)
{
    if (m_config.type != "World" || player.p_definition || player.inWorld)
    {
        throw runtime_error("Invalid relay connection role");
    }
    if (type == Protocol::Type::SS_RelayConnect && player.relayServerId.empty())
    {
        const auto request = Parse<wire::RelayConnect>(payload);
        bool known = false;
        for (const auto& server : m_config.gameServers)
        {
            if (server.id == request.server_id())
            {
                known = true;
                break;
            }
        }
        if (!known || request.client_nonce().size() != 32)
        {
            throw runtime_error("Unknown game server or invalid relay nonce");
        }
        player.relayServerId = request.server_id();
        player.relayClientNonce = request.client_nonce();
        player.relayNonce = CEntryTicket::NewId();
        player.relayDeadline = chrono::steady_clock::now() + chrono::seconds(5);
        wire::RelayChallenge challenge;
        challenge.set_server_nonce(player.relayNonce);
        challenge.set_signature(
            m_tickets.SignRelay("World", player.relayServerId, player.relayClientNonce, player.relayNonce));
        SendMessage(player.sp_channel, Protocol::Type::SS_RelayChallenge, challenge);
        return;
    }
    if (type == Protocol::Type::SS_RelayAuth && !player.relayServerId.empty() && !player.relayReady)
    {
        const auto proof = Parse<wire::RelayAuth>(payload);
        if (chrono::steady_clock::now() >= player.relayDeadline ||
            !m_tickets.VerifyRelay(
                "Game", player.relayServerId, player.relayClientNonce, player.relayNonce, proof.signature()))
        {
            throw runtime_error("Game server relay authentication failed");
        }
        for (const auto& entry : m_players)
        {
            if (entry.second.relayReady && entry.second.relayServerId == player.relayServerId &&
                entry.second.sp_channel->connected.load())
            {
                throw runtime_error("Game server relay already connected");
            }
        }
        player.relayReady = true;
        SendActionReply(player, true, "Relay registered");
        return;
    }
    if (type != Protocol::Type::SS_RelayChat || !player.relayReady)
    {
        throw runtime_error("Authenticate game server relay first");
    }
    const auto message = Parse<wire::ChatMessage>(payload);
    ValidateChatText(message.text());
    ValidateChatText(message.name());
    if (message.server_id() != player.relayServerId || message.channel() != wire::CHAT_WORLD ||
        message.message_id().size() != 32 || message.player_id().size() != 32 || message.name().size() > 32)
    {
        throw runtime_error("Invalid relay chat identity/channel");
    }
    const string id = "chat:" + message.message_id();
    if (m_usedTickets.count(id))
    {
        throw runtime_error("Duplicate relay chat");
    }
    m_usedTickets[id] = CEntryTicket::NowSeconds() + 30;
    // 전장 참가자에게 보내지 않는다. 인증된 게임 서버 연결만 받으며 발신 서버에도 한 번 돌려준다.
    for (const auto& entry : m_players)
    {
        const auto& relay = entry.second;
        if (relay.relayReady && relay.sp_channel->connected.load() &&
            !relay.sp_channel->closeAfterSend.load())
        {
            SendMessage(relay.sp_channel, Protocol::Type::SS_RelayChat, message);
        }
    }
}

void CGameLogic::UpdateChatRelay()
{
    if (!m_up_chatRelay)
    {
        return;
    }
    m_up_chatRelay->Update();
    for (const auto& message : m_up_chatRelay->TakeMessages())
    {
        if (message.server_id() == m_config.id)
        {
            const auto pending = m_pendingChats.find(message.message_id());
            if (pending != m_pendingChats.end())
            {
                wire::ActionReply reply;
                reply.set_ok(true);
                reply.set_message("World chat relayed");
                SendMessage(pending->second.sp_channel, Protocol::Type::SM_ActionReply, reply);
                m_pendingChats.erase(pending);
            }
        }
        BroadcastChat(message);
    }
    const auto now = chrono::steady_clock::now();
    for (const auto& pending : m_pendingChats)
    {
        if (now >= pending.second.deadline)
        {
            m_up_chatRelay->Reset();
            break;
        }
    }
    if (!m_up_chatRelay->Ready())
    {
        for (const auto& pending : m_pendingChats)
        {
            wire::ActionReply reply;
            reply.set_message("World chat delivery could not be confirmed; relay disconnected or timed out");
            SendMessage(pending.second.sp_channel, Protocol::Type::SM_ActionReply, reply);
        }
        m_pendingChats.clear();
    }
}

void CGameLogic::EnterWorld(stPlayer& player, const wire::EnterWorldRequest& request)
{
    wire::WorldReply reply;
    bool canReturn = false;
    wire::EntryClaims claims;
    try
    {
        if (player.inWorld)
        {
            throw runtime_error("Already in World");
        }
        claims = m_tickets.Verify(request);
        if (claims.destination() != "World")
        {
            throw runtime_error("Ticket destination is not World");
        }
        if (m_usedTickets.count(claims.nonce()))
        {
            throw runtime_error("Entry ticket already used");
        }
        string race;
        for (const auto& server : m_config.gameServers)
        {
            if (server.id == claims.game_server())
            {
                race = server.race;
                break;
            }
        }
        const auto raceId = RaceOf(race);
        const size_t raceIndex = static_cast<size_t>(raceId) - 1;
        size_t count = 0;
        for (const auto& entry : m_players)
        {
            const auto& other = entry.second;
            if (!other.inWorld)
            {
                continue;
            }
            ++count;
            if (other.unit.player_id() == claims.player_id())
            {
                throw runtime_error("Player is already in World");
            }
        }
        const stUnitDefinition* p_definition = nullptr;
        for (const auto& unit : m_data.definitions)
        {
            if (static_cast<unsigned>(unit.index) == claims.unit_index() && unit.race == race)
            {
                p_definition = &unit;
                break;
            }
        }
        if (!p_definition)
        {
            throw runtime_error("Unit does not belong to the game server race");
        }
        // 정상 입장권은 거절되더라도 소비한다. 거절 시 새 복귀권으로 원래 게임 서버에 로그인한다.
        // 사용된 입장권/이미 전장에 있는 ID에는 복귀권을 주지 않아 동시 로그인을 막는다.
        canReturn = true;
        player.unit = MakeUnit(*p_definition, claims.player_id(), claims.name(), claims.game_server());
        m_usedTickets[claims.nonce()] = claims.expires_at();
        if (!m_roundRunning)
        {
            throw runtime_error("Round is restarting");
        }
        if (m_buildings[raceIndex].hp() == 0)
        {
            throw runtime_error("Your faction building was destroyed");
        }
        if (count >= static_cast<size_t>(m_config.maxWorldPlayers))
        {
            throw runtime_error("World is full");
        }
        const auto blocked = m_reentry.find(claims.player_id());
        const auto now = chrono::steady_clock::now();
        if (blocked != m_reentry.end() && blocked->second > now)
        {
            reply.set_retry_after_seconds(static_cast<unsigned>(
                chrono::duration_cast<chrono::seconds>(blocked->second - now).count() + 1));
            throw runtime_error("Reentry cooldown is active");
        }
        if (claims.reentry_at() > CEntryTicket::NowSeconds())
        {
            reply.set_retry_after_seconds(
                static_cast<unsigned>(claims.reentry_at() - CEntryTicket::NowSeconds()));
            throw runtime_error("Reentry cooldown is active");
        }
        if (!m_ranking.count(claims.player_id()) &&
            m_ranking.size() >= static_cast<size_t>(m_config.rankMaxPlayers))
        {
            throw runtime_error("Match participant limit reached; wait for next round");
        }
        player.p_definition = p_definition;
        player.unit = MakeUnit(*p_definition, claims.player_id(), claims.name(), claims.game_server());
        player.unit.set_x(m_map.spawns[raceIndex].x);
        player.unit.set_y(m_map.spawns[raceIndex].y);
        player.inWorld = true;
        m_playersById[player.unit.player_id()] = &player;
        m_combatIndexDirty = true;
        player.moving = false;
        player.nextAttack = {};
        ClearCombat(player);
        m_usedTickets[claims.nonce()] = claims.expires_at();
        reply.set_ok(true);
        reply.set_in_world(true);
        reply.set_message("Entered World");
        *reply.mutable_unit() = player.unit;
        CountEntry(player);
        RecordReplayEvent(player, "Enter", "World login");
        cout << "[WORLD LOGIN] " << claims.game_server() << ' ' << player.unit.name()
             << " unit=" << player.unit.unit_name() << endl;
    }
    catch (const exception& error)
    {
        reply.set_message(error.what());
        if (canReturn)
        {
            SaveReturn(player,
                       reply.message(),
                       max(claims.reentry_at(), CEntryTicket::NowSeconds() + reply.retry_after_seconds()),
                       reply);
            return;
        }
    }
    SendMessage(player.sp_channel, Protocol::Type::SM_WorldReply, reply);
    if (reply.ok())
    {
        if (m_config.snapshotRadius == 0)
        {
            BroadcastWorldEvent(player.unit.name() + " entered World");
            BroadcastWorldState();
        }
    }
    else
    {
        player.sp_channel->closeAfterSend.store(true);
    }
}

bool CGameLogic::IsValidPosition(double x, double y) const
{
    return CanStand(x, y, m_map, m_buildings);
}

void CGameLogic::HandleAttack(stPlayer& player, const string& targetId)
{
    if (targetId.empty() || targetId.size() > 64)
    {
        throw runtime_error("Invalid target id");
    }
    const auto& definition = *player.p_definition;
    if (definition.damage == 0)
    {
        throw runtime_error("This unit has no basic attack; use a skill");
    }
    if (player.lockedUntil > m_tick || player.unit.burrowed() || player.burrowAt > m_tick)
    {
        throw runtime_error("Unit cannot attack while locked or burrowed");
    }
    const auto now = chrono::steady_clock::now();
    if (now < player.nextAttack)
    {
        throw runtime_error("Attack cooldown");
    }
    wire::BuildingState* p_building = nullptr;
    stPlayer* p_target = FindPlayer(targetId);
    for (auto& candidate : m_buildings)
    {
        if (candidate.id() == targetId && candidate.hp() > 0)
        {
            p_building = &candidate;
        }
    }
    if (!p_building && !p_target)
    {
        throw runtime_error("Target not found");
    }
    if (p_target && !CanSee(player, *p_target))
    {
        throw runtime_error("Target is hidden");
    }
    const auto race = p_building ? p_building->race() : p_target->unit.race();
    if (race == player.unit.race())
    {
        throw runtime_error("Cannot attack your own faction");
    }
    const double x = p_building ? p_building->x() : p_target->unit.x();
    const double y = p_building ? p_building->y() : p_target->unit.y();
    const double radius = p_building ? p_building->radius() : 0;
    const double range = player.unit.blinded() ? min(definition.range, static_cast<double>(player.blindSight))
                                               : definition.range;
    if (Distance(player.unit.x(), player.unit.y(), x, y) > range + radius + 0.001)
    {
        throw runtime_error("Target out of range");
    }
    int hp = p_building ? p_building->hp() : p_target->unit.hp();
    int shield = p_building ? p_building->shield() : p_target->unit.shield();
    const string size = p_building ? "Large" : p_target->p_definition->size;
    int percent = 100;
    if (definition.damageType == "Concussive")
    {
        percent = size == "Small" ? 100 : size == "Medium" ? 50 : 25;
    }
    else if (definition.damageType == "Explosive")
    {
        percent = size == "Small" ? 50 : size == "Medium" ? 75 : 100;
    }
    const bool blocked = p_target && definition.attackType == "Ranged" && InDarkSwarm(*p_target);
    if (!blocked)
    {
        for (int hit = 0; hit < definition.attackCount; ++hit)
        {
            Damage(definition.damage, p_building ? 0 : p_target->p_definition->armor, percent, hp, shield);
        }
    }
    if (p_building)
    {
        p_building->set_hp(hp);
        p_building->set_shield(shield);
    }
    else
    {
        p_target->unit.set_hp(hp);
        p_target->unit.set_shield(shield);
    }
    player.p_heal = nullptr;
    player.unit.set_healing(false);
    player.nextAttack =
        now +
        chrono::milliseconds(
            max(1, definition.attackIntervalMs * 100 / (player.p_stim ? player.p_stim->attackPercent : 100)));
    SendActionReply(player, true, "Hit: HP=" + to_string(hp) + " Shield=" + to_string(shield));
    if (m_config.snapshotRadius == 0)
    {
        BroadcastWorldEvent(player.unit.name() + " attacked " +
                            (p_building ? p_building->name() : p_target->unit.name()) +
                            " HP=" + to_string(hp));
    }
    if (p_target && hp == 0)
    {
        CountDeath(*p_target, player.unit.player_id(), player.unit.race());
        ReturnToGame(*p_target, "Unit died; returned to game server", true);
    }
}

void CGameLogic::ReturnToGame(stPlayer& player, const string& reason, bool applyCooldown)
{
    if (applyCooldown)
    {
        m_reentry[player.unit.player_id()] =
            chrono::steady_clock::now() + chrono::seconds(m_config.deathReentrySeconds);
    }
    SaveReturn(player, reason, applyCooldown ? CEntryTicket::NowSeconds() + m_config.deathReentrySeconds : 0);
}

wire::WorldTicket CGameLogic::CreateReturnTicket(const wire::UnitState& unit, long long reentryAt)
{
    for (const auto& server : m_config.gameServers)
    {
        if (server.id == unit.game_server())
        {
            return m_tickets.Issue(unit, server.id, server.address, server.port, reentryAt);
        }
    }
    throw runtime_error("Return game server is missing from CSV");
}

void CGameLogic::BroadcastWorldEvent(const string& message)
{
    wire::WorldEvent event;
    event.set_message(message);
    for (const auto& entry : m_players)
    {
        if (entry.second.inWorld)
        {
            SendMessage(entry.second.sp_channel, Protocol::Type::SM_WorldEvent, event);
        }
    }
}

void CGameLogic::ResetBuildings()
{
    m_areas.clear();
    m_buildingPlagues = {};
    for (size_t i = 0; i < m_buildings.size(); ++i)
    {
        const auto& spawn = m_map.spawns[i];
        auto& building = m_buildings[i];
        building.set_id("building:" + spawn.race);
        building.set_race(RaceOf(spawn.race));
        building.set_name(spawn.building);
        building.set_x(spawn.buildingX);
        building.set_y(spawn.buildingY);
        building.set_radius(spawn.buildingRadius);
        building.set_hp(spawn.buildingHP);
        building.set_shield(spawn.buildingShield);
    }
}

void CGameLogic::Tick()
{
    // 전장 한 틱 처리. 실행 간격은 Update에서 Server.ini의 [Battle] TickRate로 계산한다.
    ++m_tick;
    const auto now = chrono::steady_clock::now();
    if (!m_roundRunning && now >= m_restartAt)
    {
        ResetBuildings();
        m_roundRunning = true;
        m_winner = wire::RACE_UNSPECIFIED;
        StartMatch();
        StartReplay();
        cout << "[ROUND] New round started" << endl;
    }
    UpdateSkills();
    MovePlayers();
    // 한 틱의 명령을 모두 처리한 뒤 승패를 판단한다. 마지막 두 건물이 함께 파괴되면 무승부다.
    int survivors = 0;
    wire::Race survivor = wire::RACE_UNSPECIFIED;
    for (const auto& building : m_buildings)
    {
        if (building.hp() > 0)
        {
            ++survivors;
            survivor = building.race();
        }
    }
    if (m_roundRunning && survivors <= 1)
    {
        m_roundRunning = false;
        m_winner = survivors == 1 ? survivor : wire::RACE_UNSPECIFIED;
        m_restartAt = now + chrono::seconds(m_config.roundRestartSeconds);
        FinishMatch("round_finished");
        BroadcastWorldEvent("Round ended; winner=" + to_string(static_cast<int>(m_winner)));
        for (auto& entry : m_players)
        {
            if (entry.second.inWorld)
            {
                ReturnToGame(entry.second, "Round ended", false);
            }
        }
    }
    if (!m_roundRunning || m_tick % m_config.replayFrameTicks == 0)
    {
        CaptureReplay();
    }
    if (!m_roundRunning && m_up_replay)
    {
        m_up_replay->Finish("round_finished");
    }
    if (m_tick % max(1, m_config.tickRate / 5) == 0)
    {
        BroadcastWorldState();
    }
}

vector<const CGameLogic::stPlayer*> CGameLogic::SelectSnapshotPlayers(const stPlayer& receiver,
                                                                      const vector<const stPlayer*>& players,
                                                                      const vector<size_t>& candidates) const
{
    // Linear/Grid는 거리 안 후보만 다르게 찾는다. 은신·진영·거리순 제한은 이 함수 하나를 공유한다.
    vector<pair<double, const stPlayer*>> enemies, allies;
    for (const size_t index : candidates)
    {
        const auto* p_candidate = players[index];
        if (p_candidate == &receiver || !CanSee(receiver, *p_candidate))
        {
            continue;
        }
        const double dx = p_candidate->unit.x() - receiver.unit.x(),
                     dy = p_candidate->unit.y() - receiver.unit.y();
        (p_candidate->unit.race() == receiver.unit.race() ? allies : enemies)
            .emplace_back(dx * dx + dy * dy, p_candidate);
    }
    vector<const stPlayer*> selected{&receiver};
    const auto nearest = [&](auto& list, size_t limit)
    {
        const size_t count = min(limit, list.size());
        // 같은 거리에서도 ID로 순서를 고정한다. 셀 순회 순서가 달라도 패킷 목록이 같아야 한다.
        partial_sort(list.begin(),
                     list.begin() + count,
                     list.end(),
                     [](const auto& a, const auto& b)
                     {
                         return a.first == b.first ? a.second->unit.player_id() < b.second->unit.player_id()
                                                   : a.first < b.first;
                     });
        for (size_t i = 0; i < count; ++i)
        {
            selected.push_back(list[i].second);
        }
    };
    nearest(enemies, 32);
    nearest(allies, 31);
    return selected;
}

void CGameLogic::Update()
{
    // DB/Redis 작업 결과를 회수해 메인 스레드에서 게임 상태와 응답에 반영한다.
    UpdateDatabase();
    UpdateRanking();
    UpdateChatRelay();
    deque<stCommand> work;
    {
        // 공유 큐에서 이번에 처리할 명령만 꺼낸다. 실제 게임 처리는 잠금을 푼 뒤 실행한다.
        lock_guard lock(m_commandsMutex);
        m_commandQueuePeak = max(m_commandQueuePeak, m_commands.size());
        // 한 번에 처리하는 양을 제한해 접속 수락과 전장 틱을 굶기지 않는다.
        for (int i = 0; i < MaxCommandsPerUpdate && !m_commands.empty(); ++i)
        {
            work.push_back(move(m_commands.front()));
            m_commands.pop_front();
        }
    }
    // 큐에 등록된 순서대로 처리한다. 다른 연결에서 동시에 보낸 요청의 도착 순서를 뜻하지는 않는다.
    for (auto& command : work)
    {
        --command.sp_channel->pendingCommands;
        if (!command.sp_channel->connected.load() || command.sp_channel->closeAfterSend.load())
        {
            continue;
        }
        auto& player = m_players[command.sp_channel.get()];
        player.sp_channel = command.sp_channel;
        ++m_commandCount;
        try
        {
            HandlePacket(player, command.type, command.payload);
            RecordReplayCommand(player, command.type, command.payload, true, "");
        }
        catch (const exception& error)
        {
            ++m_commandRejected;
            SendActionReply(player, false, error.what());
            RecordReplayCommand(player, command.type, command.payload, false, error.what());
        }
    }
    for (auto it = m_players.begin(); it != m_players.end();)
    {
        if (!it->second.relayServerId.empty() && !it->second.relayReady &&
            chrono::steady_clock::now() >= it->second.relayDeadline)
        {
            it->second.sp_channel->connected.store(false);
        }
        if (it->second.sp_channel->connected.load())
        {
            ++it;
            continue;
        }
        if (it->second.databaseBusy || it->second.renewing)
        {
            ++it;
            continue;
        }
        if (!it->second.databaseToken.empty())
        {
            // 실제 연결 종료는 목적지가 없으므로 Offline으로 저장한다. World 대기는 DB에서 계산한다.
            auto job = MakeDatabaseJob(it->second, "Logout");
            if (!m_database.Submit(move(job)))
            {
                ++it;
                continue;
            }
        }
        if (it->second.inWorld)
        {
            RecordReplayEvent(it->second, "Disconnect", "Connection closed");
        }
        const auto indexed = m_playersById.find(it->second.unit.player_id());
        if (indexed != m_playersById.end() && indexed->second == &it->second)
        {
            m_playersById.erase(indexed);
        }
        m_combatIndexDirty = true;
        it = m_players.erase(it);
    }
    const auto now = chrono::steady_clock::now();
    if (now >= m_nextTick)
    {
        // TickRate=20이면 50ms 간격. 밀린 틱은 한 번의 Update에서 최대 5회까지만 따라잡는다.
        const auto interval = chrono::milliseconds(1000 / m_config.tickRate);
        for (int catchup = 0; catchup < 5 && now >= m_nextTick; ++catchup)
        {
            if (m_config.type == "World")
            {
                const auto started = chrono::steady_clock::now();
                Tick();
                if (m_tickMicros.size() < 60000)
                {
                    m_tickMicros.push_back(
                        chrono::duration_cast<chrono::microseconds>(chrono::steady_clock::now() - started)
                            .count());
                }
            }
            m_nextTick += interval;
        }
        if (m_nextTick < now)
        {
            m_missedTicks += static_cast<unsigned long long>((now - m_nextTick) / interval) + 1;
            m_nextTick = now + interval;
        }
        for (auto it = m_usedTickets.begin(); it != m_usedTickets.end();)
        {
            if (it->second <= CEntryTicket::NowSeconds())
            {
                it = m_usedTickets.erase(it);
            }
            else
            {
                ++it;
            }
        }
        for (auto it = m_reentry.begin(); it != m_reentry.end();)
        {
            if (it->second <= now)
            {
                it = m_reentry.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    ReportMetrics();
}
