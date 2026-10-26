#include "PlayClient.h"
#include "CsvTable.h"

namespace
{
void Require(bool ok, const string& message)
{
    if (!ok)
    {
        throw runtime_error("Flow check: " + message);
    }
}

template <class Predicate> void WaitFor(Predicate ready, const string& message, int seconds = 5)
{
    const auto until = chrono::steady_clock::now() + chrono::seconds(seconds);
    while (!ready())
    {
        if (chrono::steady_clock::now() >= until)
        {
            throw runtime_error("Flow timeout: " + message);
        }
        this_thread::sleep_for(chrono::milliseconds(20));
    }
}

wire::WorldTicket Ticket(CClientConnection& game)
{
    return RequestMessage<wire::WorldTicket>(
        game, Protocol::Type::CM_RequestTicket, wire::EmptyRequest{}, Protocol::Type::SM_WorldTicket);
}

wire::WorldReply Enter(CClientConnection& world, const wire::WorldTicket& ticket)
{
    wire::EnterWorldRequest request;
    request.set_claims(ticket.claims());
    request.set_signature(ticket.signature());
    return RequestMessage<wire::WorldReply>(
        world, Protocol::Type::CM_EnterWorld, request, Protocol::Type::SM_WorldReply);
}

wire::ActionReply MoveTo(CClientConnection& world, double x, double y)
{
    wire::MoveRequest request;
    request.set_x(x);
    request.set_y(y);
    return RequestMessage<wire::ActionReply>(
        world, Protocol::Type::CM_Move, request, Protocol::Type::SM_ActionReply);
}

wire::ActionReply Attack(CClientConnection& world, const string& target)
{
    wire::AttackRequest request;
    request.set_target_id(target);
    return RequestMessage<wire::ActionReply>(
        world, Protocol::Type::CM_Attack, request, Protocol::Type::SM_ActionReply);
}

wire::UnitState Find(CClientConnection& world, const string& id)
{
    const auto state = world.Snapshot();
    for (const auto& unit : state.units())
    {
        if (unit.player_id() == id)
        {
            return unit;
        }
    }
    return {};
}

void Resume(unique_ptr<CClientConnection>& up_game,
            unique_ptr<CClientConnection>& up_world,
            const stServerConfig& config,
            const wire::UnitState& assigned)
{
    WaitFor(
        [&]
        {
            return !up_world->ReturnTicket().claims().empty();
        },
        "return ticket");
    const auto ticket = up_world->ReturnTicket();
    up_world.reset();
    up_game = make_unique<CClientConnection>(config, true);
    wire::EnterWorldRequest request;
    request.set_claims(ticket.claims());
    request.set_signature(ticket.signature());
    const auto reply = RequestMessage<wire::JoinGameReply>(
        *up_game, Protocol::Type::CM_ResumeGame, request, Protocol::Type::SM_JoinGameReply);
    Require(reply.unit().player_id() == assigned.player_id() &&
                reply.unit().unit_index() == assigned.unit_index(),
            "return login preserves unit");
}

wire::WorldReply Transfer(unique_ptr<CClientConnection>& up_game,
                          unique_ptr<CClientConnection>& up_world,
                          const stServerConfig& config)
{
    up_world = make_unique<CClientConnection>(config, true);
    const auto ticket = Ticket(*up_game);
    up_game.reset();
    return Enter(*up_world, ticket);
}

void CheckCooldown(CClientConnection& game)
{
    bool rejected = false;
    try
    {
        Ticket(game);
    }
    catch (const exception& error)
    {
        rejected = string(error.what()).find("cooldown") != string::npos;
    }
    Require(rejected, "game server preserves reentry cooldown after login");
}

template <class Message> Message ReadMessage(SOCKET socket, Protocol::Type type)
{
    const auto deadline = chrono::steady_clock::now() + chrono::seconds(5);
    while (chrono::steady_clock::now() < deadline)
    {
        const string header = ReadExact(socket, Protocol::HeaderSize);
        const auto receivedType = static_cast<Protocol::Type>(Protocol::Read16(header.data() + 2));
        const string payload = ReadExact(socket, Protocol::PacketSize(header) - Protocol::HeaderSize);
        if (receivedType == type)
        {
            Message message;
            Require(message.ParseFromString(payload), "raw reply parse");
            return message;
        }
        Require(receivedType == Protocol::Type::SM_WorldState ||
                    receivedType == Protocol::Type::SM_WorldEvent,
                "unexpected packet while waiting for reply");
    }
    throw runtime_error("World reply timeout");
}

void RequireNoChat(SOCKET socket)
{
    const auto deadline = chrono::steady_clock::now() + chrono::milliseconds(300);
    while (chrono::steady_clock::now() < deadline)
    {
        WSAPOLLFD poll{socket, POLLRDNORM, 0};
        const int count = WSAPoll(&poll, 1, 30);
        Require(count >= 0, "chat isolation poll");
        if (count == 0)
        {
            continue;
        }
        const string header = ReadExact(socket, Protocol::HeaderSize);
        const auto type = static_cast<Protocol::Type>(Protocol::Read16(header.data() + 2));
        ReadExact(socket, Protocol::PacketSize(header) - Protocol::HeaderSize);
        Require(type == Protocol::Type::SM_WorldState || type == Protocol::Type::SM_WorldEvent,
                "chat leaked outside its channel");
    }
}

CSocket OpenChatClient(const stServerConfig& config)
{
    auto socket = Connect(config);
    wire::ServerVersionCheckRequest versionCheck;
    versionCheck.set_protocol_version(Protocol::Version);
    versionCheck.set_client_name("WorldChatCheck");
    SendAll(socket.Get(), Protocol::EncodeMessage(Protocol::Type::SYS_ServerVersionCheck, versionCheck));
    const auto server =
        ReadMessage<wire::ServerVersionCheckReply>(socket.Get(), Protocol::Type::SYS_ServerVersionCheckReply);
    if (server.protocol_version() != Protocol::Version)
    {
        throw runtime_error("Server protocol version mismatch");
    }
    return socket;
}

void CheckWorldChat(const fs::path& configPath)
{
    const auto config = LoadServerConfig(configPath, "World");
    Require(config.gameServers.size() == 4, "four-server chat fixture");
    array<CSocket, 4> games;
    array<wire::UnitState, 4> users;
    for (size_t i = 0; i < games.size(); ++i)
    {
        games[i] = OpenChatClient(LoadServerConfig(configPath, config.gameServers[i].id));
        wire::JoinGameRequest join;
        join.set_name("GlobalUser_" + to_string(i));
        join.set_password("PortfolioTest!2026");
        SendAll(games[i].Get(), Protocol::EncodeMessage(Protocol::Type::CM_JoinGame, join));
        users[i] = ReadMessage<wire::JoinGameReply>(games[i].Get(), Protocol::Type::SM_JoinGameReply).unit();
    }
    const auto sendChat = [](SOCKET socket, wire::ChatChannel channel, const string& text)
    {
        wire::ChatRequest request;
        request.set_text(text);
        request.set_channel(channel);
        SendAll(socket, Protocol::EncodeMessage(Protocol::Type::CM_Chat, request));
        return ReadMessage<wire::ActionReply>(socket, Protocol::Type::SM_ActionReply);
    };
    const auto checkChat = [](SOCKET socket,
                              wire::ChatChannel channel,
                              const string& source,
                              const wire::UnitState& user,
                              const string& text)
    {
        const auto message = ReadMessage<wire::ChatMessage>(socket, Protocol::Type::SM_ChatMessage);
        Require(message.channel() == channel && message.server_id() == source &&
                    message.player_id() == user.player_id() && message.name() == user.name() &&
                    message.text() == text,
                "world/local channel, sender and text");
    };
    // 일반 클라이언트는 서버 ID를 알고 있어도 중계 역할을 얻을 수 없다.
    {
        auto forged = OpenChatClient(config);
        wire::ChatMessage message;
        message.set_channel(wire::CHAT_WORLD);
        message.set_text("forged relay");
        SendAll(forged.Get(), Protocol::EncodeMessage(Protocol::Type::SS_RelayChat, message));
        Require(!ReadMessage<wire::ActionReply>(forged.Get(), Protocol::Type::SM_ActionReply).ok(),
                "unauthenticated relay rejected");
    }
    {
        auto forged = OpenChatClient(config);
        wire::RelayConnect request;
        request.set_server_id(config.gameServers[0].id);
        request.set_client_nonce(string(32, 'a'));
        SendAll(forged.Get(), Protocol::EncodeMessage(Protocol::Type::SS_RelayConnect, request));
        const auto challenge =
            ReadMessage<wire::RelayChallenge>(forged.Get(), Protocol::Type::SS_RelayChallenge);
        wire::RelayAuth proof;
        proof.set_signature(challenge.signature());
        SendAll(forged.Get(), Protocol::EncodeMessage(Protocol::Type::SS_RelayAuth, proof));
        Require(!ReadMessage<wire::ActionReply>(forged.Get(), Protocol::Type::SM_ActionReply).ok(),
                "reflected World proof cannot authenticate Game");
    }
    const string global = "모든 게임 서버에 전달되는 월드 채팅";
    Require(sendChat(games[0].Get(), wire::CHAT_WORLD, global).ok(), "world chat relay acknowledgement");
    for (auto& game : games)
    {
        checkChat(game.Get(), wire::CHAT_WORLD, config.gameServers[0].id, users[0], global);
    }
    for (auto& game : games)
    {
        RequireNoChat(game.Get());
    }
    Require(!sendChat(games[0].Get(), static_cast<wire::ChatChannel>(99), "invalid").ok(),
            "unknown chat channel rejected");

    array<CSocket, 2> worlds;
    for (size_t i = 0; i < worlds.size(); ++i)
    {
        worlds[i] = OpenChatClient(config);
        SendAll(games[i].Get(),
                Protocol::EncodeMessage(Protocol::Type::CM_RequestTicket, wire::EmptyRequest{}));
        const auto ticket = ReadMessage<wire::WorldTicket>(games[i].Get(), Protocol::Type::SM_WorldTicket);
        games[i].Reset();
        wire::EnterWorldRequest request;
        request.set_claims(ticket.claims());
        request.set_signature(ticket.signature());
        SendAll(worlds[i].Get(), Protocol::EncodeMessage(Protocol::Type::CM_EnterWorld, request));
        Require(ReadMessage<wire::WorldReply>(worlds[i].Get(), Protocol::Type::SM_WorldReply).ok(),
                "chat participant enters battle");
    }
    const string local = "전장 안에서만 보이는 일반 채팅";
    Require(sendChat(worlds[0].Get(), wire::CHAT_LOCAL, local).ok(), "battle local chat");
    for (auto& world : worlds)
    {
        checkChat(world.Get(), wire::CHAT_LOCAL, "World", users[0], local);
    }
    Require(!sendChat(worlds[0].Get(), wire::CHAT_WORLD, "not allowed").ok(), "battle global chat rejected");
    RequireNoChat(games[2].Get());
    RequireNoChat(games[3].Get());

    Require(sendChat(games[2].Get(), wire::CHAT_WORLD, global).ok(), "world chat from another faction");
    checkChat(games[2].Get(), wire::CHAT_WORLD, config.gameServers[2].id, users[2], global);
    checkChat(games[3].Get(), wire::CHAT_WORLD, config.gameServers[2].id, users[2], global);
    for (auto& world : worlds)
    {
        RequireNoChat(world.Get());
    }
    PrintPlayLine("[OK] World chat across four game servers, no duplicate echo, battle-local chat isolation, "
                  "relay authentication and channel validation");
}

void CheckGameCommandsAndChat(const fs::path& configPath)
{
    const auto servers = LoadGameServers(configPath);
    Require(servers.size() >= 2 && servers[0].race == servers[1].race,
            "same-race servers for chat isolation");
    const array<string, 5> ids = {servers[0].id, servers[0].id, servers[1].id, servers[0].id, "World"};
    array<CSocket, 5> clients;
    array<wire::UnitState, 3> users;
    // 0/1: 같은 게임 서버의 로그인 사용자, 2: 다른 게임 서버, 3/4: ServerVersionCheck만 보낸 연결.
    // 수신 스레드 없이 직접 읽어, 다른 서버나 비로그인 연결에 채팅이 새지 않는지도 검사한다.
    for (size_t i = 0; i < clients.size(); ++i)
    {
        clients[i] = Connect(LoadServerConfig(configPath, ids[i]));
        wire::ServerVersionCheckRequest versionCheck;
        versionCheck.set_protocol_version(Protocol::Version);
        versionCheck.set_client_name("ChatCheck");
        SendAll(clients[i].Get(),
                Protocol::EncodeMessage(Protocol::Type::SYS_ServerVersionCheck, versionCheck));
        const auto server = ReadMessage<wire::ServerVersionCheckReply>(
            clients[i].Get(), Protocol::Type::SYS_ServerVersionCheckReply);
        if (server.protocol_version() != Protocol::Version)
        {
            throw runtime_error("Server protocol version mismatch");
        }
        if (i < users.size())
        {
            wire::JoinGameRequest join;
            join.set_name("ChatUser_" + to_string(i));
            join.set_password("PortfolioTest!2026");
            SendAll(clients[i].Get(), Protocol::EncodeMessage(Protocol::Type::CM_JoinGame, join));
            users[i] =
                ReadMessage<wire::JoinGameReply>(clients[i].Get(), Protocol::Type::SM_JoinGameReply).unit();
        }
    }
    const auto sendChat = [&](size_t client, const string& text)
    {
        wire::ChatRequest request;
        request.set_text(text);
        SendAll(clients[client].Get(), Protocol::EncodeMessage(Protocol::Type::CM_Chat, request));
        return ReadMessage<wire::ActionReply>(clients[client].Get(), Protocol::Type::SM_ActionReply);
    };
    const auto checkChat = [&](size_t receiver, size_t sender, const string& text)
    {
        const auto message =
            ReadMessage<wire::ChatMessage>(clients[receiver].Get(), Protocol::Type::SM_ChatMessage);
        Require(message.server_id() == ids[sender] && message.player_id() == users[sender].player_id() &&
                    message.name() == users[sender].name() && message.text() == text,
                "chat sender identity and UTF-8 text");
    };
    Require(!sendChat(3, "not logged in").ok(), "chat requires game login");
    Require(!sendChat(4, "wrong server").ok(), "game chat not accepted on World");
    Require(!sendChat(0, "").ok() && !sendChat(0, "   ").ok(), "empty chat rejected");
    Require(!sendChat(0, string(257, 'a')).ok(), "oversized chat rejected");
    Require(!sendChat(0, "line\nbreak").ok(), "chat control characters rejected");
    SendAll(clients[0].Get(), Protocol::Encode(Protocol::Type::CM_Chat, string("\x0a\x01\xff", 3)));
    Require(!ReadMessage<wire::ActionReply>(clients[0].Get(), Protocol::Type::SM_ActionReply).ok(),
            "invalid UTF-8 chat rejected");

    wire::MoveRequest move;
    move.set_x(60);
    move.set_y(60);
    wire::AttackRequest attack;
    attack.set_target_id(users[1].player_id());
    SendAll(clients[0].Get(),
            Protocol::EncodeMessage(Protocol::Type::CM_Move, move) +
                Protocol::EncodeMessage(Protocol::Type::CM_Attack, attack) +
                Protocol::EncodeMessage(Protocol::Type::CM_LeaveWorld, wire::EmptyRequest{}));
    for (int i = 0; i < 3; ++i)
    {
        const auto rejected =
            ReadMessage<wire::ActionReply>(clients[0].Get(), Protocol::Type::SM_ActionReply);
        Require(!rejected.ok() && rejected.message().find("Game server") != string::npos,
                "battle command rejected by game server");
    }

    const string text = "안녕하세요. 같은 게임 서버 채팅입니다.";
    Require(sendChat(0, text).ok(), "valid chat accepted");
    checkChat(0, 0, text);
    checkChat(1, 0, text);
    Require(!sendChat(0, "too fast").ok(), "chat rate limit");
    // 같은 종족이어도 다른 게임 서버는 일반 채팅 범위에 들어가지 않는다.
    for (size_t i = 1; i < clients.size(); ++i)
    {
        WSAPOLLFD poll{clients[i].Get(), POLLRDNORM, 0};
        Require(WSAPoll(&poll, 1, 150) == 0, "no extra chat, other-server or guest delivery");
    }
    const string maximum(256, 'a');
    Require(sendChat(0, maximum).ok(), "maximum chat accepted after interval");
    checkChat(0, 0, maximum);
    checkChat(1, 0, maximum);

    SendAll(clients[0].Get(),
            Protocol::EncodeMessage(Protocol::Type::CM_RequestTicket, wire::EmptyRequest{}));
    ReadMessage<wire::WorldTicket>(clients[0].Get(), Protocol::Type::SM_WorldTicket);
    Require(sendChat(1, "after logout").ok(), "remaining game user can chat");
    checkChat(1, 1, "after logout");
    char byte = 0;
    Require(recv(clients[0].Get(), &byte, 1, 0) == 0, "logged-out connection closes without receiving chat");
    PrintPlayLine("[OK] Game-only commands, local chat broadcast/isolation, login checks, UTF-8/length/rate "
                  "validation, logout exclusion");
}
} // namespace

void RunFlowCheck(const fs::path& configPath)
{
    CheckGameCommandsAndChat(configPath);
    CheckWorldChat(configPath);
    // 전투 수치/대기 시간을 줄인 임시 CSV와 INI에서만 실행한다. 개발 데이터는 수정하지 않는다.
    const auto worldConfig = LoadServerConfig(configPath, "World");
    const auto map = LoadMapConfig(worldConfig.mapFile, worldConfig.startPointFile);
    array<unique_ptr<CClientConnection>, 3> games;
    array<unique_ptr<CClientConnection>, 3> worlds;
    array<wire::UnitState, 3> assigned;
    array<wire::WorldTicket, 3> tickets;
    array<string, 3> ids;
    const array<string, 3> races = {"Terran", "Protoss", "Zerg"};
    for (size_t i = 0; i < races.size(); ++i)
    {
        for (const auto& server : worldConfig.gameServers)
        {
            if (server.race == races[i])
            {
                ids[i] = server.id;
            }
        }
        Require(!ids[i].empty(), "one server per race in fixture");
    }
    for (size_t i = 0; i < ids.size(); ++i)
    {
        games[i] = make_unique<CClientConnection>(LoadServerConfig(configPath, ids[i]), true);
        worlds[i] = make_unique<CClientConnection>(worldConfig, true);
        wire::JoinGameRequest request;
        request.set_name("Flow_" + ids[i]);
        request.set_password("PortfolioTest!2026");
        assigned[i] = RequestMessage<wire::JoinGameReply>(
                          *games[i], Protocol::Type::CM_JoinGame, request, Protocol::Type::SM_JoinGameReply)
                          .unit();
        Require(assigned[i].race() == static_cast<wire::Race>(i + 1) && assigned[i].game_server() == ids[i],
                "random unit race");
        Require(!assigned[i].player_id().empty(), "player id");
        tickets[i] = Ticket(*games[i]);
        games[i].reset();
    }
    auto forged = tickets[0];
    (*forged.mutable_signature())[0] ^= 1;
    Require(!Enter(*worlds[0], forged).ok(), "forged ticket rejected");
    forged = tickets[0];
    wire::EntryClaims modified;
    Require(modified.ParseFromString(forged.claims()), "claims parse");
    modified.set_unit_index(999);
    modified.SerializeToString(forged.mutable_claims());
    Require(!Enter(*worlds[0], forged).ok(), "tampered unit rejected");
    for (size_t i = 0; i < ids.size(); ++i)
    {
        const auto entered = Enter(*worlds[i], tickets[i]);
        Require(entered.ok() && entered.in_world(), "enter " + ids[i] + ": " + entered.message());
        Require(entered.unit().unit_index() == assigned[i].unit_index() &&
                    entered.unit().player_id() == assigned[i].player_id(),
                "unit identity preserved");
        Require(entered.unit().x() == map.spawns[i].x && entered.unit().y() == map.spawns[i].y,
                "spawn matches CSV");
    }
    CClientConnection duplicate(worldConfig, true);
    Require(!Enter(duplicate, tickets[0]).ok(), "used ticket rejected");
    // 같은 저그라도 원래 서버 ID는 서로 다르다. 시작점/아군 판정은 종족으로 묶는다.
    string sameRaceServer;
    for (const auto& server : worldConfig.gameServers)
    {
        if (server.race == "Zerg" && server.id != ids[2])
        {
            sameRaceServer = server.id;
            break;
        }
    }
    Require(!sameRaceServer.empty(), "duplicate-race server fixture");
    auto up_allyGame = make_unique<CClientConnection>(LoadServerConfig(configPath, sameRaceServer), true);
    unique_ptr<CClientConnection> up_allyWorld;
    wire::JoinGameRequest allyJoin;
    allyJoin.set_name("SameRaceOtherServer");
    allyJoin.set_password("PortfolioTest!2026");
    const auto ally =
        RequestMessage<wire::JoinGameReply>(
            *up_allyGame, Protocol::Type::CM_JoinGame, allyJoin, Protocol::Type::SM_JoinGameReply)
            .unit();
    const auto allyEntry = Transfer(up_allyGame, up_allyWorld, worldConfig);
    Require(allyEntry.ok() && allyEntry.unit().race() == assigned[2].race() &&
                allyEntry.unit().x() == map.spawns[2].x && allyEntry.unit().y() == map.spawns[2].y,
            "same race spawn");
    Require(!Attack(*worlds[2], ally.player_id()).ok(), "same race on another server is ally");
    WaitFor(
        [&]
        {
            return worlds[0]->Snapshot().units_size() == 4;
        },
        "cross-worker world snapshot");
    Require(!MoveTo(*worlds[0], -1, 0).ok(), "negative coordinate rejected");
    Require(!MoveTo(*worlds[0], numeric_limits<double>::quiet_NaN(), 0).ok(), "NaN rejected");
    Require(!MoveTo(*worlds[0], 1000, 0).ok(), "map boundary rejected");
    Require(!MoveTo(*worlds[0], map.spawns[0].buildingX, map.spawns[0].buildingY).ok(),
            "building interior rejected");
    Require(!Attack(*worlds[0], assigned[0].player_id()).ok(), "own faction rejected");
    Require(!Attack(*worlds[0], assigned[1].player_id()).ok(), "out of range rejected");
    PrintPlayLine(
        "[OK] Random race assignment, signed transfer, duplicate/replay rejection, spawn, invalid requests");

    Require(MoveTo(*worlds[0], 60, 60).ok() && MoveTo(*worlds[1], 64, 60).ok(), "move accepted");
    const auto immediate = Find(*worlds[0], assigned[0].player_id());
    Require(immediate.y() > 60, "movement is not teleportation");
    WaitFor(
        [&]
        {
            return Find(*worlds[0], assigned[0].player_id()).y() == 60 &&
                   Find(*worlds[0], assigned[1].player_id()).x() == 64;
        },
        "server movement");
    WaitFor(
        [&]
        {
            return Find(*worlds[0], assigned[0].player_id()).mp() > 1;
        },
        "MP regeneration");
    Require(Attack(*worlds[0], assigned[1].player_id()).ok(), "first attack");
    Require(!Attack(*worlds[0], assigned[1].player_id()).ok(), "attack cooldown");
    WaitFor(
        [&]
        {
            return Find(*worlds[0], assigned[1].player_id()).hp() == 15;
        },
        "shield and HP damage broadcast");
    // 네트워크로 실제 관측한 상태를 별도 저장해 리플레이 결과와 독립 비교한다.
    {
        const auto bytes = worlds[0]->Snapshot().SerializeAsString();
        ofstream output(configPath.parent_path() / "live-state.pb", ios::binary);
        output.write(bytes.data(), static_cast<streamsize>(bytes.size()));
        Require(output.good(), "write live replay comparison snapshot");
    }
    this_thread::sleep_for(chrono::milliseconds(1050));
    Require(Attack(*worlds[0], assigned[1].player_id()).ok(), "lethal attack");
    WaitFor(
        [&]
        {
            return !worlds[1]->InWorld();
        },
        "death return");
    Resume(games[1], worlds[1], LoadServerConfig(configPath, ids[1]), assigned[1]);
    CheckCooldown(*games[1]);
    this_thread::sleep_for(chrono::milliseconds(2100));
    Require(Transfer(games[1], worlds[1], worldConfig).ok(), "reentry after death cooldown");
    PrintPlayLine("[OK] Server movement, MP, damage, attack cooldown, death return and reentry");

    Require(MoveTo(*worlds[0], 106, 60).ok(), "move to Nexus");
    WaitFor(
        [&]
        {
            return Find(*worlds[0], assigned[0].player_id()).x() == 106;
        },
        "reach Nexus");
    Require(Attack(*worlds[0], "building:Protoss").ok(), "destroy Nexus");
    WaitFor(
        [&]
        {
            return !worlds[1]->InWorld();
        },
        "faction return");
    Resume(games[1], worlds[1], LoadServerConfig(configPath, ids[1]), assigned[1]);
    Require(!Transfer(games[1], worlds[1], worldConfig).ok(), "eliminated faction entry rejected");
    Resume(games[1], worlds[1], LoadServerConfig(configPath, ids[1]), assigned[1]);
    Require(MoveTo(*worlds[0], 60, 14).ok(), "move to Hive");
    WaitFor(
        [&]
        {
            return Find(*worlds[0], assigned[0].player_id()).y() == 14;
        },
        "reach Hive");
    this_thread::sleep_for(chrono::milliseconds(1050));
    Require(Attack(*worlds[0], "building:Zerg").ok(), "destroy Hive");
    WaitFor(
        [&]
        {
            return !worlds[0]->InWorld() && !worlds[2]->InWorld();
        },
        "round return");
    Resume(games[0], worlds[0], LoadServerConfig(configPath, ids[0]), assigned[0]);
    Resume(games[2], worlds[2], LoadServerConfig(configPath, ids[2]), assigned[2]);
    Resume(up_allyGame, up_allyWorld, LoadServerConfig(configPath, sameRaceServer), ally);
    Require(!Transfer(games[0], worlds[0], worldConfig).ok(), "restart interval rejects entry");
    Resume(games[0], worlds[0], LoadServerConfig(configPath, ids[0]), assigned[0]);
    this_thread::sleep_for(chrono::milliseconds(4200));
    const auto restarted = Transfer(games[0], worlds[0], worldConfig);
    Require(restarted.ok() && restarted.unit().unit_index() == assigned[0].unit_index(),
            "restart preserves assigned unit");
    WaitFor(
        [&]
        {
            const auto state = worlds[0]->Snapshot();
            if (state.buildings_size() != 3)
            {
                return false;
            }
            for (const auto& building : state.buildings())
            {
                if (building.hp() != 15)
                {
                    return false;
                }
            }
            return true;
        },
        "buildings restored");
    const auto returned = RequestMessage<wire::ActionReply>(
        *worlds[0], Protocol::Type::CM_LeaveWorld, wire::EmptyRequest{}, Protocol::Type::SM_ActionReply);
    Require(returned.ok(), "manual return");
    Resume(games[0], worlds[0], LoadServerConfig(configPath, ids[0]), assigned[0]);
    CheckCooldown(*games[0]);
    CClientConnection replayAfterLeave(worldConfig, true);
    Require(!Enter(replayAfterLeave, tickets[0]).ok(), "consumed ticket still rejected after leaving");
    PrintPlayLine("[OK] Building elimination, winner return, restart, building restoration, manual return");
    PrintPlayLine("[OK] CSV servers 1=Zerg, 2=Zerg, 3=Protoss, 4=Terran: race spawn, allies and "
                  "original-server return");
    PrintPlayLine("[OK] Game logout -> World login -> World logout -> original game login flow passed");
}

// 로그인 응답을 받기 전 8개 요청을 함께 보내 예약 슬롯까지 검사한다.
void RunCapacityCheck(const fs::path& configPath)
{
    const auto gameA = LoadServerConfig(configPath, "A"), gameB = LoadServerConfig(configPath, "B");
    const auto worldConfig = LoadServerConfig(configPath, "World");
    Require(gameA.maxGamePlayers == 1 && gameB.maxGamePlayers == 2 && worldConfig.maxWorldPlayers == 1,
            "capacity fixture");
    for (const auto& config : {gameA, gameB, LoadServerConfig(configPath, "C")})
    {
        const int attempts = config.id == "C" ? 32 : 8;
        vector<unique_ptr<CClientConnection>> clients;
        for (int i = 0; i < attempts; ++i)
        {
            clients.push_back(make_unique<CClientConnection>(config, true));
        }
        vector<string> outcomes(attempts);
        vector<jthread> threads;
        atomic<bool> start = false;
        for (int i = 0; i < attempts; ++i)
        {
            threads.emplace_back(
                [&, i]
                {
                    while (!start.load())
                    {
                        this_thread::yield();
                    }
                    wire::JoinGameRequest request;
                    request.set_name("Capacity_" + config.id + to_string(i));
                    request.set_password("PortfolioTest!2026");
                    try
                    {
                        RequestMessage<wire::JoinGameReply>(*clients[i],
                                                            Protocol::Type::CM_JoinGame,
                                                            request,
                                                            Protocol::Type::SM_JoinGameReply);
                        outcomes[i] = "ok";
                    }
                    catch (const exception& error)
                    {
                        outcomes[i] = error.what();
                    }
                });
        }
        start = true;
        threads.clear();
        int accepted = 0;
        for (int i = 0; i < attempts; ++i)
        {
            if (outcomes[i] == "ok")
            {
                ++accepted;
            }
            else
            {
                Require(outcomes[i].find(config.id == "C" ? "Database queue is full"
                                                          : "Game server is full") != string::npos,
                        "unexpected capacity rejection: " + outcomes[i]);
            }
        }
        if (config.id == "C")
        {
            Require(accepted > 0 && accepted < attempts, "DB queue overflow was not exercised");
            // 실패 연결도 그대로 유지한 채 새 로그인을 시도해 정원 예약 누수를 확인한다.
            CClientConnection retry(config, true);
            wire::JoinGameRequest request;
            request.set_name("CapacityRetry");
            request.set_password("PortfolioTest!2026");
            RequestMessage<wire::JoinGameReply>(
                retry, Protocol::Type::CM_JoinGame, request, Protocol::Type::SM_JoinGameReply);
            RequestMessage<wire::ActionReply>(
                retry, Protocol::Type::CM_Logout, wire::EmptyRequest{}, Protocol::Type::SM_ActionReply);
            cout << "[OK] DB queue rejection releases capacity reservation" << endl;
        }
        else
        {
            Require(accepted == config.maxGamePlayers, "pending logins exceeded configured capacity");
        }
        for (int i = 0; i < attempts; ++i)
        {
            if (outcomes[i] == "ok")
            {
                RequestMessage<wire::ActionReply>(*clients[i],
                                                  Protocol::Type::CM_Logout,
                                                  wire::EmptyRequest{},
                                                  Protocol::Type::SM_ActionReply);
            }
        }
        cout << "[OK] " << config.id << " concurrent login accepted=" << accepted
             << " rejected=" << attempts - accepted << endl;
    }
    auto up_first = make_unique<CClientConnection>(gameA, true),
         up_second = make_unique<CClientConnection>(gameB, true);
    wire::JoinGameRequest login;
    login.set_password("PortfolioTest!2026");
    login.set_name("WorldCapacityA");
    RequestMessage<wire::JoinGameReply>(
        *up_first, Protocol::Type::CM_JoinGame, login, Protocol::Type::SM_JoinGameReply);
    login.set_name("WorldCapacityB");
    RequestMessage<wire::JoinGameReply>(
        *up_second, Protocol::Type::CM_JoinGame, login, Protocol::Type::SM_JoinGameReply);
    unique_ptr<CClientConnection> up_world1, up_world2;
    Require(Transfer(up_first, up_world1, worldConfig).ok(), "first World entry");
    const auto denied = Transfer(up_second, up_world2, worldConfig);
    Require(!denied.ok() && denied.message().find("World is full") != string::npos &&
                denied.has_return_ticket(),
            "World capacity and return ticket");
    // 정원 초과로 거절되어도 원래 게임 서버에 복귀할 수 있어야 한다.
    CClientConnection resumed(gameB, true);
    wire::EnterWorldRequest resume;
    resume.set_claims(denied.return_ticket().claims());
    resume.set_signature(denied.return_ticket().signature());
    RequestMessage<wire::JoinGameReply>(
        resumed, Protocol::Type::CM_ResumeGame, resume, Protocol::Type::SM_JoinGameReply);
    RequestMessage<wire::ActionReply>(
        resumed, Protocol::Type::CM_Logout, wire::EmptyRequest{}, Protocol::Type::SM_ActionReply);
    RequestMessage<wire::ActionReply>(
        *up_world1, Protocol::Type::CM_Logout, wire::EmptyRequest{}, Protocol::Type::SM_ActionReply);
    cout << "[OK] World capacity=1, second entry rejected, original game login restored" << endl;
}
