#include "BattleWorker.h"

namespace
{
template <class Message> Message Read(const string& payload)
{
    Message message;
    if (!message.ParseFromString(payload))
    {
        throw runtime_error("Invalid battle protobuf");
    }
    return message;
}

} // namespace

CBattleWorker::CBattleWorker(stBattleCounters& counters, const stServerConfig& world, int admission)
    : m_counters(counters), m_world(world), m_admission(admission)
{
}

void CBattleWorker::QueuePacket(stBattleBot& bot,
                                Protocol::Type type,
                                const google::protobuf::MessageLite& message,
                                BattlePending pending)
{
    if (bot.pending != BattlePending::None)
    {
        throw runtime_error("Overlapping battle request");
    }
    bot.sendBuffer = Protocol::EncodeMessage(type, message);
    bot.sendOffset = 0;
    bot.pending = pending;
    bot.deadline = BattleClock::now() + chrono::seconds(60);
}

void CBattleWorker::Connect(stBattleBot& bot, const stServerConfig& endpoint)
{
    // 소켓과 버퍼는 이 워커 하나만 소유한다. 유닛 수만큼 수신 스레드를 만들지 않는다.
    bot.socket = CSocket(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    SocketCheck(bot.socket.Get() != INVALID_SOCKET, "battle socket");
    bot.p_endpoint = &endpoint;
    ++bot.generation;
    bot.recvBuffer.clear();
    bot.sendBuffer.clear();
    bot.sendOffset = 0;
    bot.pending = BattlePending::None;
    bot.snapshot.Clear();
    bot.assembling.Clear();
    SetNonblocking(bot.socket.Get(), true);
    const BOOL noDelay = TRUE;
    SocketCheck(setsockopt(bot.socket.Get(),
                           IPPROTO_TCP,
                           TCP_NODELAY,
                           reinterpret_cast<const char*>(&noDelay),
                           sizeof(noDelay)) == 0,
                "battle TCP_NODELAY");
    const auto address = MakeAddress(endpoint.address, endpoint.port);
    const int result =
        connect(bot.socket.Get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    SocketCheck(result == 0 || WSAGetLastError() == WSAEWOULDBLOCK, "battle connect");
    bot.connecting = result != 0;
    wire::ServerVersionCheckRequest versionCheck;
    versionCheck.set_protocol_version(Protocol::Version);
    versionCheck.set_client_name("BattleDummy");
    QueuePacket(bot, Protocol::Type::SYS_ServerVersionCheck, versionCheck, BattlePending::ServerVersionCheck);
}

void CBattleWorker::HandlePacket(stBattleBot& bot, Protocol::Type type, const string& payload)
{
    if (type == Protocol::Type::SM_WorldState)
    {
        const auto part = Read<wire::WorldState>(payload);
        if (bot.assembling.units_size() + part.units_size() > 64 ||
            (bot.assembling.units_size() && bot.assembling.tick() != part.tick()))
        {
            throw runtime_error("Invalid local view");
        }
        bot.assembling.set_tick(part.tick());
        for (const auto& unit : part.units())
        {
            *bot.assembling.add_units() = unit;
        }
        if (part.complete())
        {
            for (const auto& building : part.buildings())
            {
                *bot.assembling.add_buildings() = building;
            }
            for (const auto& area : part.areas())
            {
                *bot.assembling.add_areas() = area;
            }
            bot.assembling.set_complete(true);
            bot.assembling.set_round_running(part.round_running());
            bot.assembling.set_winner(part.winner());
            bot.snapshot.Swap(&bot.assembling);
            bot.assembling.Clear();
        }
        return;
    }
    if (type == Protocol::Type::SM_WorldEvent)
    {
        const auto event = Read<wire::WorldEvent>(payload);
        if (event.returned_to_game())
        {
            if (bot.stage == BattleStage::World)
            {
                --m_counters.world;
            }
            bot.stage = BattleStage::Game;
            if (event.message().find("Unit died") != string::npos)
            {
                ++m_counters.deaths;
                bot.died = true;
            }
            bot.ticket = event.return_ticket();
            if (bot.ticket.claims().empty())
            {
                throw runtime_error("Return ticket missing");
            }
            Connect(bot, *bot.p_game);
        }
        return;
    }
    if (type == Protocol::Type::SM_ChatMessage)
    {
        return;
    }
    const auto pending = bot.pending;
    bot.pending = BattlePending::None;
    if (pending == BattlePending::ServerVersionCheck && type == Protocol::Type::SYS_ServerVersionCheckReply)
    {
        const auto reply = Read<wire::ServerVersionCheckReply>(payload);
        if (reply.protocol_version() != Protocol::Version || reply.server_id() != bot.p_endpoint->id)
        {
            throw runtime_error("Battle server version/identity mismatch");
        }
        bot.map = reply.map();
        bot.skills.assign(reply.skill_info().begin(), reply.skill_info().end());
        if (!bot.ticket.claims().empty())
        {
            wire::EnterWorldRequest request;
            request.set_claims(bot.ticket.claims());
            request.set_signature(bot.ticket.signature());
            const bool world = bot.p_endpoint == &m_world;
            QueuePacket(bot,
                        world ? Protocol::Type::CM_EnterWorld : Protocol::Type::CM_ResumeGame,
                        request,
                        world ? BattlePending::Enter : BattlePending::Resume);
        }
        else
        {
            wire::JoinGameRequest request;
            request.set_name(bot.account);
            request.set_password("PortfolioBattle!2026");
            QueuePacket(bot, Protocol::Type::CM_JoinGame, request, BattlePending::Login);
        }
        return;
    }
    if ((pending == BattlePending::Login || pending == BattlePending::Resume) &&
        type == Protocol::Type::SM_JoinGameReply)
    {
        const auto reply = Read<wire::JoinGameReply>(payload);
        if (!bot.unit.player_id().empty() && (reply.unit().player_id() != bot.unit.player_id() ||
                                              reply.unit().unit_index() != bot.unit.unit_index()))
        {
            throw runtime_error("Battle identity changed on return");
        }
        bot.unit = reply.unit();
        bot.ticket.Clear();
        bot.stage = BattleStage::Game;
        // 서버가 돌려준 대기 시간을 따른다. 사망/자발적 복귀의 100초 제한을 우회하지 않는다.
        bot.retry =
            BattleClock::now() +
            chrono::milliseconds(
                static_cast<long long>(max(reply.retry_after_seconds(), bot.entryDenied ? 30U : 0U)) * 1000 +
                250);
        bot.entryDenied = false;
        if (pending == BattlePending::Resume)
        {
            ++m_counters.resumed;
        }
        return;
    }
    if (pending == BattlePending::Ticket && type == Protocol::Type::SM_WorldTicket)
    {
        bot.ticket = Read<wire::WorldTicket>(payload);
        Connect(bot, m_world);
        return;
    }
    if (pending == BattlePending::Enter && type == Protocol::Type::SM_WorldReply)
    {
        const auto reply = Read<wire::WorldReply>(payload);
        if (!reply.ok())
        {
            if (!reply.has_return_ticket())
            {
                throw runtime_error("World entry rejected: " + reply.message());
            }
            bot.entryDenied = true;
            bot.ticket = reply.return_ticket();
            Connect(bot, *bot.p_game);
            return;
        }
        bot.unit = reply.unit();
        bot.ticket.Clear();
        bot.stage = BattleStage::World;
        bot.decision.Reset(true);
        bot.nextDecision = BattleClock::now() + chrono::milliseconds(bot.index % 250);
        ++m_counters.entered;
        ++m_counters.world;
        if (bot.died)
        {
            ++m_counters.reentries;
            bot.died = false;
        }
        if (!bot.prepared)
        {
            bot.prepared = true;
            ++m_counters.ready;
            if (bot.initialSlot)
            {
                --m_counters.admitting;
                bot.initialSlot = false;
            }
        }
        return;
    }
    if (type == Protocol::Type::SM_ActionReply)
    {
        const auto reply = Read<wire::ActionReply>(payload);
        if (pending == BattlePending::Logout && reply.ok())
        {
            if (bot.stage == BattleStage::World)
            {
                --m_counters.world;
            }
            bot.stage = BattleStage::Done;
            bot.socket.Reset();
            ++m_counters.done;
            return;
        }
        if (pending == BattlePending::Ticket && !reply.ok())
        {
            ++m_counters.rejected;
            bot.retry = BattleClock::now() + chrono::seconds(3);
            return;
        }
        if (pending != BattlePending::Action)
        {
            throw runtime_error("Battle request failed: " + reply.message());
        }
        if (!reply.ok())
        {
            ++m_counters.rejected;
            return;
        }
        if (bot.order.type == Protocol::Type::CM_Attack)
        {
            if (bot.order.target.rfind("building:", 0) == 0)
            {
                ++m_counters.buildingAttacks;
            }
            else
            {
                ++m_counters.unitAttacks;
            }
        }
        if (bot.order.type == Protocol::Type::CM_Skill)
        {
            ++m_counters.skills;
        }
        return;
    }
    throw runtime_error("Unexpected battle reply");
}

void CBattleWorker::Receive(stBattleBot& bot)
{
    array<char, 32768> buffer{};
    for (int batch = 0; batch < 8; ++batch)
    {
        const int bytes = recv(bot.socket.Get(), buffer.data(), static_cast<int>(buffer.size()), 0);
        if (bytes < 0 && WSAGetLastError() == WSAEWOULDBLOCK)
        {
            return;
        }
        SocketCheck(bytes > 0, "battle recv/closed");
        bot.recvBuffer.append(buffer.data(), bytes);
        size_t consumed = 0;
        while (true)
        {
            const string_view remaining(bot.recvBuffer.data() + consumed, bot.recvBuffer.size() - consumed);
            const auto size = Protocol::PacketSize(remaining);
            if (!size || remaining.size() < size)
            {
                break;
            }
            const auto type = static_cast<Protocol::Type>(Protocol::Read16(remaining.data() + 2));
            const string payload(remaining.substr(Protocol::HeaderSize, size - Protocol::HeaderSize));
            consumed += size;
            const auto generation = bot.generation;
            HandlePacket(bot, type, payload);
            // 티켓 수신은 소켓/버퍼를 교체한다. 이전 연결의 남은 데이터를 새 연결에 섞지 않는다.
            if (bot.generation != generation || bot.stage == BattleStage::Done)
            {
                return;
            }
        }
        bot.recvBuffer.erase(0, consumed);
        if (bot.recvBuffer.size() > Protocol::MaxPacketSize)
        {
            throw runtime_error("Battle receive overflow");
        }
    }
}

void CBattleWorker::UpdateBot(stBattleBot& bot)
{
    const auto now = BattleClock::now();
    if (bot.stage == BattleStage::Failed || bot.stage == BattleStage::Done)
    {
        return;
    }
    if (bot.stage == BattleStage::Waiting)
    {
        if (m_counters.stopping)
        {
            bot.stage = BattleStage::Done;
            ++m_counters.done;
            return;
        }
        const int active = m_counters.admitting.fetch_add(1);
        if (active >= m_admission)
        {
            --m_counters.admitting;
            return;
        }
        bot.initialSlot = true;
        bot.stage = BattleStage::Game;
        Connect(bot, *bot.p_game);
        return;
    }
    if (bot.pending != BattlePending::None)
    {
        if (now > bot.deadline)
        {
            throw runtime_error("Battle reply timed out");
        }
        return;
    }
    if (m_counters.stopping)
    {
        QueuePacket(bot, Protocol::Type::CM_Logout, wire::EmptyRequest{}, BattlePending::Logout);
        return;
    }
    if (bot.stage == BattleStage::Game && now >= bot.retry)
    {
        QueuePacket(bot, Protocol::Type::CM_RequestTicket, wire::EmptyRequest{}, BattlePending::Ticket);
        return;
    }
    if (bot.stage != BattleStage::World || !m_counters.started || now < bot.nextDecision ||
        !bot.snapshot.complete())
    {
        return;
    }
    bot.nextDecision = now + chrono::milliseconds(250);
    bot.order = bot.decision.Decide(bot.snapshot, bot.map, bot.skills, bot.unit.player_id());
    if (bot.order.payload.empty())
    {
        return;
    }
    bot.sendBuffer = Protocol::Encode(bot.order.type, bot.order.payload);
    bot.sendOffset = 0;
    bot.pending = BattlePending::Action;
    bot.deadline = now + chrono::seconds(15);
}

void CBattleWorker::Fail(stBattleBot& bot, const string& message)
{
    if (bot.stage == BattleStage::Failed || bot.stage == BattleStage::Done)
    {
        return;
    }
    if (bot.stage == BattleStage::World)
    {
        --m_counters.world;
    }
    if (bot.initialSlot)
    {
        --m_counters.admitting;
        bot.initialSlot = false;
    }
    bot.stage = BattleStage::Failed;
    bot.socket.Reset();
    ++m_counters.errors;
    PrintPlayLine("[BATTLE ERROR] " + bot.account + " " + message);
}

void CBattleWorker::Run(stop_token stop)
{
    try
    {
        while (!stop.stop_requested())
        {
            vector<WSAPOLLFD> polls;
            vector<stBattleBot*> active;
            for (auto& up_owned : m_bots)
            {
                auto& bot = *up_owned;
                try
                {
                    UpdateBot(bot);
                }
                catch (const exception& error)
                {
                    Fail(bot, error.what());
                }
                if (bot.socket.Get() == INVALID_SOCKET)
                {
                    continue;
                }
                const short events = static_cast<short>(
                    POLLRDNORM |
                    ((bot.connecting || bot.sendOffset < bot.sendBuffer.size()) ? POLLWRNORM : 0));
                polls.push_back({bot.socket.Get(), events, 0});
                active.push_back(&bot);
            }
            if (polls.empty())
            {
                this_thread::sleep_for(chrono::milliseconds(5));
                continue;
            }
            SocketCheck(WSAPoll(polls.data(), static_cast<ULONG>(polls.size()), 5) != SOCKET_ERROR,
                        "battle poll");
            for (size_t i = 0; i < active.size(); ++i)
            {
                auto& bot = *active[i];
                const short events = polls[i].revents;
                try
                {
                    if (bot.connecting)
                    {
                        if (!events)
                        {
                            continue;
                        }
                        int error = 0, size = sizeof(error);
                        SocketCheck(getsockopt(bot.socket.Get(),
                                               SOL_SOCKET,
                                               SO_ERROR,
                                               reinterpret_cast<char*>(&error),
                                               &size) == 0 &&
                                        error == 0,
                                    "battle connect result");
                        bot.connecting = false;
                    }
                    const auto generation = bot.generation;
                    if (events & (POLLRDNORM | POLLHUP))
                    {
                        Receive(bot);
                    }
                    if (bot.generation != generation || bot.stage == BattleStage::Done)
                    {
                        continue;
                    }
                    if (events & (POLLERR | POLLNVAL))
                    {
                        throw runtime_error("Battle socket error");
                    }
                    if (bot.sendOffset < bot.sendBuffer.size())
                    {
                        const int sendOffset = send(bot.socket.Get(),
                                                    bot.sendBuffer.data() + bot.sendOffset,
                                                    static_cast<int>(bot.sendBuffer.size() - bot.sendOffset),
                                                    0);
                        if (sendOffset < 0 && WSAGetLastError() == WSAEWOULDBLOCK)
                        {
                            continue;
                        }
                        SocketCheck(sendOffset > 0, "battle send");
                        bot.sendOffset += sendOffset;
                        if (bot.sendOffset == bot.sendBuffer.size())
                        {
                            bot.sendBuffer.clear();
                            bot.sendOffset = 0;
                        }
                    }
                }
                catch (const exception& error)
                {
                    Fail(bot, error.what());
                }
            }
        }
    }
    catch (const exception& error)
    {
        ++m_counters.errors;
        PrintPlayLine("[BATTLE WORKER ERROR] " + string(error.what()));
    }
}
