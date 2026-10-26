#include "PlayClient.h"
#include "AutoBattle.h"

namespace
{
string LowerCommand(string value)
{
    for (char& ch : value)
    {
        if (ch >= 'A' && ch <= 'Z')
        {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return value;
}

void ShowCommands(bool inWorld)
{
    PrintPlayLine(
        inWorld ? "[WORLD 명령] rank kills|deaths|entries / status / move X Y / attack ID / skills / skill "
                  "슬롯 [ID|X Y] / skill stop / auto on|off|초 / back / server / quit / chat 일반 내용"
                : "[GAME 명령] world / status / server / quit / chat 일반|월드 내용");
}

void ShowUnit(const wire::UnitState& unit)
{
    ostringstream text;
    text << unit.name() << " / " << unit.game_server() << " / " << unit.unit_name() << " (#"
         << unit.unit_index() << ")"
         << " HP=" << unit.hp() << '/' << unit.max_hp() << " Shield=" << unit.shield() << " MP=" << unit.mp()
         << " XY=(" << unit.x() << ',' << unit.y() << ") id=" << unit.player_id();
    if (unit.stimulated())
    {
        text << " [Stim]";
    }
    if (unit.cloaked())
    {
        text << " [Cloak]";
    }
    if (unit.burrowed())
    {
        text << " [Burrow]";
    }
    if (unit.locked())
    {
        text << " [Lockdown]";
    }
    if (unit.blinded())
    {
        text << " [Blind]";
    }
    if (unit.healing())
    {
        text << " [Healing]";
    }
    PrintPlayLine(text.str());
}

void ShowSkills(const CClientConnection& world, const wire::UnitState& unit)
{
    for (int slot = 0; slot < unit.skills_size(); ++slot)
    {
        if (unit.skills(slot) == 0)
        {
            continue;
        }
        for (const auto& skill : world.Skills())
        {
            if (skill.skill_index() != unit.skills(slot))
            {
                continue;
            }
            const string target = skill.target_type() == "Self"    ? ""
                                  : skill.target_type() == "Point" ? " X Y"
                                                                   : " 대상ID";
            PrintPlayLine("skill " + to_string(slot + 1) + target + " : " + skill.name() +
                          " / MP=" + to_string(skill.mp_cost()) +
                          " / cooldown=" + to_string(skill.cooldown_ms()) + "ms");
        }
    }
}

wire::ActionReply MoveTo(CClientConnection& world, double x, double y)
{
    wire::MoveRequest request;
    request.set_x(x);
    request.set_y(y);
    return RequestMessage<wire::ActionReply>(
        world, Protocol::Type::CM_Move, request, Protocol::Type::SM_ActionReply);
}

string ReadPassword()
{
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    const bool console = GetConsoleMode(input, &mode) != 0;
    // 테스트의 리다이렉션 입력은 그대로 읽고 실제 콘솔에서만 비밀번호 표시를 끈다.
    if (console && !SetConsoleMode(input, mode & ~ENABLE_ECHO_INPUT))
    {
        throw runtime_error("Cannot hide password input");
    }
    string password;
    try
    {
        getline(cin, password);
    }
    catch (...)
    {
        if (console)
        {
            SetConsoleMode(input, mode);
        }
        throw;
    }
    if (console)
    {
        SetConsoleMode(input, mode);
        PrintPlayLine("");
    }
    return password;
}

void Play(const fs::path& configPath)
{
    while (true)
    {
        const auto servers = LoadGameServers(configPath);
        PrintPlayLine("\n게임 서버 선택: CSV의 Index를 입력하세요. q=종료");
        for (const auto& entry : servers)
        {
            PrintPlayLine(entry.id + " : " + entry.name + " / " + entry.race);
        }
        string choice;
        if (!getline(cin, choice) || choice == "q")
        {
            return;
        }
        string server;
        for (const auto& entry : servers)
        {
            if (entry.id == choice)
            {
                server = entry.id;
                break;
            }
        }
        if (server.empty())
        {
            PrintPlayLine("목록에 있는 서버 Index를 입력하세요.");
            continue;
        }
        auto up_game = make_unique<CClientConnection>(LoadServerConfig(configPath, server));
        wire::JoinGameRequest join;
        PrintPlayLine("계정 ID 입력 (영문/숫자/_/-, Enter=Player_" + server +
                      "). 처음 사용하는 ID는 새 계정으로 등록합니다:");
        string name;
        if (!getline(cin, name))
        {
            return;
        }
        join.set_name(name.empty() ? "Player_" + server : name);
        PrintPlayLine("비밀번호 입력 (8~128 UTF-8 바이트, 화면에 표시하지 않음):");
        join.set_password(ReadPassword());
        if (!cin)
        {
            return;
        }
        wire::JoinGameReply login;
        try
        {
            login = RequestMessage<wire::JoinGameReply>(
                *up_game, Protocol::Type::CM_JoinGame, join, Protocol::Type::SM_JoinGameReply);
        }
        catch (const exception& error)
        {
            SecureZeroMemory(join.mutable_password()->data(), join.password().size());
            join.clear_password();
            PrintPlayLine("[DENIED] 로그인 실패: " + string(error.what()));
            continue;
        }
        SecureZeroMemory(join.mutable_password()->data(), join.password().size());
        join.clear_password();
        const auto assigned = login.unit();
        PrintPlayLine(
            "[GAME] 계정 로그인 완료. 저장된 유닛을 불러왔습니다. 신규 서버 접속은 랜덤 배정합니다.");
        if (login.retry_after_seconds())
        {
            PrintPlayLine("World 재입장 대기: " + to_string(login.retry_after_seconds()) + "초");
        }
        ShowUnit(assigned);
        unique_ptr<CClientConnection> up_world;
        mutex transitionMutex;
        CAutoBattle autoBattle;
        // 입력 대기 중에도 자동 공략과 복귀 로그인을 처리한다. 긴 전투 반복문으로 입력을 막지 않는다.
        // 연결 변경, 수동 명령, 자동 요청은 같은 잠금으로 직렬화하고 수신 스레드는 독립적으로 돈다.
        jthread updater(
            [&](stop_token stop)
            {
                while (!stop.stop_requested())
                {
                    {
                        lock_guard lock(transitionMutex);
                        if (up_world && !up_world->InWorld())
                        {
                            autoBattle.Stop();
                            const auto ticket = up_world->ReturnTicket();
                            if (!ticket.claims().empty())
                            {
                                try
                                {
                                    auto config = LoadServerConfig(configPath, server);
                                    config.address = ticket.address();
                                    config.port = static_cast<int>(ticket.port());
                                    auto up_returning = make_unique<CClientConnection>(config);
                                    wire::EnterWorldRequest request;
                                    request.set_claims(ticket.claims());
                                    request.set_signature(ticket.signature());
                                    const auto reply =
                                        RequestMessage<wire::JoinGameReply>(*up_returning,
                                                                            Protocol::Type::CM_ResumeGame,
                                                                            request,
                                                                            Protocol::Type::SM_JoinGameReply);
                                    if (reply.unit().player_id() != assigned.player_id() ||
                                        reply.unit().unit_index() != assigned.unit_index())
                                    {
                                        throw runtime_error("Returning unit identity changed");
                                    }
                                    up_game = move(up_returning);
                                    PrintPlayLine("[GAME LOGIN] " + server + " 복귀 로그인 완료 / " +
                                                  reply.unit().unit_name());
                                    ShowCommands(false);
                                }
                                catch (const exception& error)
                                {
                                    PrintPlayLine("[FAIL] 복귀 로그인 실패: " + string(error.what()) +
                                                  " / server 명령으로 다시 선택하세요.");
                                }
                                up_world.reset();
                            }
                        }
                        if (up_world && up_world->InWorld())
                        {
                            try
                            {
                                autoBattle.Update(*up_world, assigned.player_id());
                            }
                            catch (const exception& error)
                            {
                                autoBattle.Stop();
                                if (up_world->InWorld())
                                {
                                    PrintPlayLine("[AUTO] 자동 공략 중지: " + string(error.what()));
                                }
                            }
                        }
                    }
                    this_thread::sleep_for(chrono::milliseconds(50));
                }
            });
        ShowCommands(false);
        string line;
        while (getline(cin, line))
        {
            try
            {
                lock_guard lock(transitionMutex);
                istringstream input(line);
                string command;
                input >> command;
                // 명령만 대소문자를 구분하지 않는다. 서버 ID와 채팅 본문은 입력 그대로 사용한다.
                command = LowerCommand(command);
                const bool inWorld = up_world && up_world->InWorld();
                if (!inWorld && command != "world" && command != "status" && command != "server" &&
                    command != "quit" && command != "chat")
                {
                    PrintPlayLine(
                        "[DENIED] GameServer에서는 world, status, server, quit, chat만 사용할 수 있습니다.");
                    ShowCommands(false);
                    continue;
                }
                if (command == "quit" || command == "server")
                {
                    autoBattle.Stop();
                    CClientConnection* p_connection = inWorld ? up_world.get() : up_game.get();
                    if (p_connection)
                    {
                        RequestMessage<wire::ActionReply>(*p_connection,
                                                          Protocol::Type::CM_Logout,
                                                          wire::EmptyRequest{},
                                                          Protocol::Type::SM_ActionReply);
                    }
                    if (command == "quit")
                    {
                        return;
                    }
                    break;
                }
                if (command == "world")
                {
                    if (up_world && up_world->InWorld())
                    {
                        PrintPlayLine("이미 World에 있습니다.");
                        continue;
                    }
                    if (!up_game)
                    {
                        throw runtime_error("게임 서버 복귀 중이거나 연결이 끊겼습니다. 잠시 후 다시 "
                                            "시도하거나 server를 입력하세요.");
                    }
                    // World가 열려 있는지 먼저 확인한다. 게임 로그아웃은 입장권 발급 순간에 수행한다.
                    auto config = LoadServerConfig(configPath, "World");
                    auto up_destination = make_unique<CClientConnection>(config);
                    const auto ticket = RequestMessage<wire::WorldTicket>(*up_game,
                                                                          Protocol::Type::CM_RequestTicket,
                                                                          wire::EmptyRequest{},
                                                                          Protocol::Type::SM_WorldTicket);
                    up_game.reset();
                    up_world = move(up_destination);
                    PrintPlayLine("[GAME LOGOUT] " + server + " 로그아웃 완료. World 로그인 중...");
                    wire::EnterWorldRequest request;
                    request.set_claims(ticket.claims());
                    request.set_signature(ticket.signature());
                    const auto reply = RequestMessage<wire::WorldReply>(
                        *up_world, Protocol::Type::CM_EnterWorld, request, Protocol::Type::SM_WorldReply);
                    PrintPlayLine(string(reply.ok() ? "[OK] " : "[DENIED] ") + reply.message() +
                                  " / 재입장 대기=" + to_string(reply.retry_after_seconds()) + "초");
                    if (reply.ok())
                    {
                        ShowUnit(reply.unit());
                        ShowCommands(true);
                        ShowSkills(*up_world, reply.unit());
                        autoBattle.Start();
                    }
                }
                else if (command == "chat")
                {
                    CClientConnection* p_connection = inWorld ? up_world.get() : up_game.get();
                    if (!p_connection)
                    {
                        throw runtime_error("게임 서버 복귀 중이거나 연결이 끊겼습니다.");
                    }
                    string text;
                    getline(input, text);
                    const size_t first = text.find_first_not_of(' ');
                    if (first != string::npos)
                    {
                        text.erase(0, first);
                    }
                    wire::ChatRequest request;
                    // Chat 내용은 기존처럼 일반 채팅이다. 명시할 때는 일반/local 또는 월드/world를 받는다.
                    istringstream chatInput(text);
                    string channel;
                    chatInput >> channel;
                    channel = LowerCommand(channel);
                    if (channel == "일반" || channel == "local" || channel == "월드" || channel == "world")
                    {
                        request.set_channel(channel == "월드" || channel == "world" ? wire::CHAT_WORLD
                                                                                    : wire::CHAT_LOCAL);
                        getline(chatInput, text);
                        const size_t body = text.find_first_not_of(' ');
                        if (body != string::npos)
                        {
                            text.erase(0, body);
                        }
                    }
                    request.set_text(text);
                    const auto reply = RequestMessage<wire::ActionReply>(
                        *p_connection, Protocol::Type::CM_Chat, request, Protocol::Type::SM_ActionReply);
                    // 성공한 본문은 서버가 발신자에게도 전달하므로 여기서 다시 출력하지 않는다.
                    if (!reply.ok())
                    {
                        PrintPlayLine("[DENIED] " + reply.message());
                    }
                }
                else if (command == "rank")
                {
                    if (!up_world || !up_world->InWorld())
                    {
                        PrintPlayLine("World에 먼저 입장하세요.");
                        continue;
                    }
                    string metric;
                    input >> metric;
                    metric = LowerCommand(metric);
                    wire::RankRequest request;
                    if (metric == "kills")
                    {
                        request.set_metric(wire::RANK_KILLS);
                    }
                    else if (metric == "deaths")
                    {
                        request.set_metric(wire::RANK_DEATHS);
                    }
                    else if (metric == "entries")
                    {
                        request.set_metric(wire::RANK_ENTRIES);
                    }
                    else
                    {
                        PrintPlayLine("rank kills / rank deaths / rank entries");
                        continue;
                    }
                    const auto reply = RequestMessage<wire::RankReply>(
                        *up_world, Protocol::Type::CM_Rank, request, Protocol::Type::SM_RankReply);
                    if (!reply.ok())
                    {
                        PrintPlayLine("[RANK unavailable] " + reply.message());
                        continue;
                    }
                    PrintPlayLine("[RANK " + metric + "] match=" + reply.match_id() +
                                  " (동점: PlayerId 역순)");
                    const auto show = [](const wire::RankEntry& row, const string& prefix)
                    {
                        PrintPlayLine(prefix + " #" + to_string(row.rank()) + " " + row.name() + " [" +
                                      row.game_server() + "] kills=" + to_string(row.kills()) + " deaths=" +
                                      to_string(row.deaths()) + " entries=" + to_string(row.entries()));
                    };
                    for (const auto& row : reply.top())
                    {
                        show(row, "");
                    }
                    show(reply.self(), "[MY]");
                }
                else if (command == "status")
                {
                    if (!up_world || !up_world->InWorld())
                    {
                        PrintPlayLine(up_game ? "[GAME] " + server
                                              : "[TRANSFER] 복귀 로그인 대기 또는 연결 종료");
                        ShowUnit(assigned);
                        continue;
                    }
                    const auto state = up_world->Snapshot();
                    PrintPlayLine("[WORLD] tick=" + to_string(state.tick()) +
                                  " players=" + to_string(state.units_size()) +
                                  " auto=" + (autoBattle.Active() ? "on" : "off"));
                    for (const auto& unit : state.units())
                    {
                        ShowUnit(unit);
                    }
                    for (const auto& building : state.buildings())
                    {
                        PrintPlayLine(building.id() + " " + building.name() + " HP=" +
                                      to_string(building.hp()) + " Shield=" + to_string(building.shield()));
                    }
                }
                else if (command == "skills")
                {
                    ShowSkills(*up_world, assigned);
                }
                else if (command == "skill")
                {
                    string slotText;
                    input >> slotText;
                    wire::SkillRequest request;
                    if (LowerCommand(slotText) == "stop")
                    {
                        request.set_cancel(true);
                    }
                    else
                    {
                        const int slot = ParseInt(slotText, 1, 5);
                        if (assigned.skills_size() < slot || assigned.skills(slot - 1) == 0)
                        {
                            throw runtime_error("Empty skill slot");
                        }
                        request.set_slot(slot);
                        for (const auto& skill : up_world->Skills())
                        {
                            if (skill.skill_index() != assigned.skills(slot - 1))
                            {
                                continue;
                            }
                            if (skill.target_type() == "Point")
                            {
                                double x = 0, y = 0;
                                if (!(input >> x >> y))
                                {
                                    throw runtime_error("Usage: skill slot X Y");
                                }
                                request.set_x(x);
                                request.set_y(y);
                            }
                            else if (skill.target_type() == "Unit")
                            {
                                string target;
                                if (!(input >> target))
                                {
                                    throw runtime_error("Usage: skill slot target_id");
                                }
                                request.set_target_id(target == "self" ? assigned.player_id() : target);
                            }
                        }
                    }
                    autoBattle.Stop();
                    const auto reply = RequestMessage<wire::ActionReply>(
                        *up_world, Protocol::Type::CM_Skill, request, Protocol::Type::SM_ActionReply);
                    PrintPlayLine(string(reply.ok() ? "[OK] " : "[DENIED] ") + reply.message());
                }
                else if (command == "move" || command == "attack" || command == "auto" || command == "back")
                {
                    if (!up_world || !up_world->InWorld())
                    {
                        PrintPlayLine("world 명령으로 먼저 입장하세요.");
                        continue;
                    }
                    if (command == "move")
                    {
                        double x = 0, y = 0;
                        if (!(input >> x >> y))
                        {
                            throw runtime_error("Usage: move X Y");
                        }
                        autoBattle.Stop();
                        const auto reply = MoveTo(*up_world, x, y);
                        PrintPlayLine(string(reply.ok() ? "[OK] " : "[DENIED] ") + reply.message());
                    }
                    else if (command == "attack")
                    {
                        string target;
                        input >> target;
                        wire::AttackRequest request;
                        request.set_target_id(target);
                        autoBattle.Stop();
                        const auto reply = RequestMessage<wire::ActionReply>(
                            *up_world, Protocol::Type::CM_Attack, request, Protocol::Type::SM_ActionReply);
                        PrintPlayLine(string(reply.ok() ? "[OK] " : "[DENIED] ") + reply.message());
                    }
                    else if (command == "auto")
                    {
                        string option;
                        input >> option;
                        option = LowerCommand(option);
                        if (option == "off")
                        {
                            autoBattle.Stop();
                            // 이미 보낸 이동 명령도 취소하도록 마지막 확인 좌표를 새 목적지로 보낸다.
                            const auto state = up_world->Snapshot();
                            for (const auto& unit : state.units())
                            {
                                if (unit.player_id() == assigned.player_id())
                                {
                                    MoveTo(*up_world, unit.x(), unit.y());
                                    break;
                                }
                            }
                            PrintPlayLine("[AUTO] 자동 공략 중지. auto on으로 다시 시작할 수 있습니다.");
                        }
                        else if (option == "buildings")
                        {
                            autoBattle.Start(0, true);
                        }
                        else
                        {
                            autoBattle.Start(option.empty() || option == "on" ? 0 : ParseInt(option, 1, 300));
                        }
                    }
                    else
                    {
                        autoBattle.Stop();
                        const auto reply = RequestMessage<wire::ActionReply>(*up_world,
                                                                             Protocol::Type::CM_LeaveWorld,
                                                                             wire::EmptyRequest{},
                                                                             Protocol::Type::SM_ActionReply);
                        PrintPlayLine(reply.message());
                    }
                }
                else
                {
                    ShowCommands(inWorld);
                }
            }
            catch (const exception& error)
            {
                PrintPlayLine("[FAIL] " + string(error.what()));
            }
        }
        if (!cin)
        {
            return;
        }
    }
}
} // namespace

int RunPlayClient(int argc, char* argv[])
{
    try
    {
        SetConsoleCP(CP_UTF8);
        fs::path configPath = "Server.ini";
        if (argc == 4 && string_view(argv[2]) == "--config")
        {
            configPath = argv[3];
        }
        else if (argc != 2)
        {
            throw runtime_error(
                "Usage: --play | --flow-check | --account-check | --skill-check [--config Server.ini]");
        }
        const CWinsockSession winsock;
        if (string_view(argv[1]) == "--flow-check")
        {
            RunFlowCheck(configPath);
        }
        else if (string_view(argv[1]) == "--account-check")
        {
            RunAccountCheck(configPath);
        }
        else if (string_view(argv[1]) == "--skill-check")
        {
            RunSkillCheck(configPath);
        }
        else if (string_view(argv[1]) == "--capacity-check")
        {
            RunCapacityCheck(configPath);
        }
        else if (string_view(argv[1]) == "--ranking-check")
        {
            RunRankingCheck(configPath);
        }
        else
        {
            Play(configPath);
        }
        return 0;
    }
    catch (const exception& error)
    {
        PrintPlayLine("[FAIL] " + string(error.what()));
        return 1;
    }
}
