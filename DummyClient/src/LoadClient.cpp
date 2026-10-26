#include "LoadClient.h"
#include "PlayClient.h"

namespace
{
double Milliseconds(chrono::steady_clock::time_point started)
{
    return chrono::duration<double, milli>(chrono::steady_clock::now() - started).count();
}
double Percentile(const vector<double>& sorted, size_t percent)
{
    return sorted.empty() ? 0 : sorted[(sorted.size() - 1) * percent / 100];
}
struct stResult
{
    vector<double> latency;
    struct stSample
    {
        unsigned sequence;
        double offsetMs, latencyMs;
        bool accepted;
    };
    vector<stSample> samples;
    unsigned long long accepted = 0, rejected = 0, errors = 0, snapshots = 0;
    string error;
};
wire::ActionReply Move(CClientConnection& world, double x, double y)
{
    wire::MoveRequest request;
    request.set_x(x);
    request.set_y(y);
    return RequestMessage<wire::ActionReply>(
        world, Protocol::Type::CM_Move, request, Protocol::Type::SM_ActionReply);
}
wire::ActionReply Combat(
    CClientConnection& world, const wire::UnitState& assigned, unsigned sequence, double x, double y)
{
    const auto state = world.Snapshot();
    wire::UnitState self;
    for (const auto& unit : state.units())
    {
        if (unit.player_id() == assigned.player_id())
        {
            self = unit;
            break;
        }
    }
    if (sequence % 5 == 0 && !self.player_id().empty())
    {
        // CSV 슬롯으로 실제 스킬 패킷을 보낸다. 쿨다운/상태/MP 부족은 정상 거부로 따로 집계한다.
        for (int slot = 0; slot < self.skills_size(); ++slot)
        {
            if (!self.skills(slot))
            {
                continue;
            }
            for (const auto& info : world.Skills())
            {
                if (info.skill_index() != self.skills(slot))
                {
                    continue;
                }
                wire::SkillRequest request;
                request.set_slot(slot + 1);
                if (info.target_type() == "Unit")
                {
                    request.set_target_id(self.player_id());
                }
                if (info.target_type() == "Point")
                {
                    request.set_x(self.x());
                    request.set_y(self.y());
                }
                return RequestMessage<wire::ActionReply>(
                    world, Protocol::Type::CM_Skill, request, Protocol::Type::SM_ActionReply);
            }
        }
    }
    if (sequence % 2 == 0 && self.attack_damage() > 0)
    {
        for (const auto& target : state.units())
        {
            if (target.race() == self.race())
            {
                continue;
            }
            const double dx = target.x() - self.x(), dy = target.y() - self.y();
            if (dx * dx + dy * dy > self.attack_range() * self.attack_range())
            {
                continue;
            }
            wire::AttackRequest request;
            request.set_target_id(target.player_id());
            return RequestMessage<wire::ActionReply>(
                world, Protocol::Type::CM_Attack, request, Protocol::Type::SM_ActionReply);
        }
    }
    return Move(world, x, y);
}
} // namespace

int RunLoadClient(int argc, char* argv[])
{
    try
    {
        fs::path configPath = "Server.ini", csvPath, samplesPath;
        string mode = "world", server = "A";
        int count = 16, seconds = 15, rate = 10, warmup = 0;
        for (int i = 2; i < argc; i += 2)
        {
            if (i + 1 >= argc)
            {
                throw runtime_error("Missing load option value");
            }
            const string option = argv[i], value = argv[i + 1];
            if (option == "--config")
            {
                configPath = value;
            }
            else if (option == "--mode")
            {
                mode = value;
            }
            else if (option == "--server")
            {
                server = value;
            }
            else if (option == "--clients")
            {
                count = ParseInt(value, 1, 128);
            }
            else if (option == "--seconds")
            {
                seconds = ParseInt(value, 1, 600);
            }
            else if (option == "--rate")
            {
                rate = ParseInt(value, 1, 100);
            }
            else if (option == "--csv")
            {
                csvPath = value;
            }
            else if (option == "--samples")
            {
                samplesPath = value;
            }
            else if (option == "--warmup")
            {
                warmup = ParseInt(value, 0, 60);
            }
            else
            {
                throw runtime_error("Unknown load option: " + option);
            }
        }
        if (mode != "ping" && mode != "world" && mode != "combat" && mode != "fixed-combat")
        {
            throw runtime_error("Load mode must be ping/world/combat/fixed-combat");
        }
        if (mode == "fixed-combat" && count % 3 != 0)
        {
            throw runtime_error("Fixed combat needs a multiple of three clients");
        }
        const auto worldConfig = LoadServerConfig(configPath, "World");
        const auto servers = LoadGameServers(configPath);
        if (mode != "ping" && count > worldConfig.maxWorldPlayers)
        {
            throw runtime_error("Load clients exceed MaxWorldPlayers");
        }
        const CWinsockSession winsock;
        vector<unique_ptr<CClientConnection>> connections(count);
        vector<wire::UnitState> units(count);
        vector<double> loginMs(count);
        vector<stResult> results(count);
        const auto run = to_string(
            chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch())
                .count());
        // PBKDF2 로그인 폭주와 전투 부하를 분리한다. 준비는 최대 8개씩, 측정은 전원이 준비된 뒤 시작한다.
        for (int batch = 0; batch < count; batch += 8)
        {
            vector<jthread> preparation;
            for (int i = batch; i < min(count, batch + 8); ++i)
            {
                preparation.emplace_back(
                    [&, i]
                    {
                        try
                        {
                            const auto started = chrono::steady_clock::now();
                            if (mode == "ping")
                            {
                                connections[i] = make_unique<CClientConnection>(
                                    LoadServerConfig(configPath, server), true);
                            }
                            else
                            {
                                CClientConnection game(
                                    LoadServerConfig(configPath, servers[i % servers.size()].id), true);
                                wire::JoinGameRequest login;
                                login.set_name("Bench_" + run + "_" + to_string(i));
                                login.set_password("PortfolioBench!2026");
                                units[i] =
                                    RequestMessage<wire::JoinGameReply>(game,
                                                                        Protocol::Type::CM_JoinGame,
                                                                        login,
                                                                        Protocol::Type::SM_JoinGameReply)
                                        .unit();
                                const auto ticket =
                                    RequestMessage<wire::WorldTicket>(game,
                                                                      Protocol::Type::CM_RequestTicket,
                                                                      wire::EmptyRequest{},
                                                                      Protocol::Type::SM_WorldTicket);
                                connections[i] = make_unique<CClientConnection>(worldConfig, true);
                                wire::EnterWorldRequest enter;
                                enter.set_claims(ticket.claims());
                                enter.set_signature(ticket.signature());
                                const auto reply =
                                    RequestMessage<wire::WorldReply>(*connections[i],
                                                                     Protocol::Type::CM_EnterWorld,
                                                                     enter,
                                                                     Protocol::Type::SM_WorldReply);
                                if (!reply.ok())
                                {
                                    throw runtime_error(reply.message());
                                }
                                const auto& map = connections[i]->Map();
                                const double cx = (map.min_x() + map.max_x()) / 2,
                                             cy = (map.min_y() + map.max_y()) / 2;
                                if (!Move(*connections[i], cx - 3 + i % 6, cy - 3 + (i / 6) % 6).ok())
                                {
                                    throw runtime_error("Initial load move rejected");
                                }
                            }
                            loginMs[i] = Milliseconds(started);
                        }
                        catch (const exception& error)
                        {
                            results[i].errors = 1;
                            results[i].error = error.what();
                        }
                    });
            }
        }
        for (const auto& result : results)
        {
            if (result.errors)
            {
                throw runtime_error("Load preparation failed: " + result.error);
            }
        }
        // 도착을 조금 기다린 뒤부터 측정한다. 로그인 시간은 별도 열에 남긴다.
        this_thread::sleep_for(chrono::seconds(3));
        const auto warmupStart = chrono::steady_clock::now() + chrono::milliseconds(100);
        const auto start = warmupStart + chrono::seconds(warmup);
        const auto end = start + chrono::seconds(seconds);
        {
            vector<jthread> clients;
            for (int i = 0; i < count; ++i)
            {
                clients.emplace_back(
                    [&, i]
                    {
                        auto& result = results[i];
                        auto& connection = *connections[i];
                        unsigned sequence = 0;
                        unsigned long long snapshotTick = 0;
                        auto next = warmupStart;
                        this_thread::sleep_until(warmupStart);
                        while (chrono::steady_clock::now() < end)
                        {
                            const auto requested = chrono::steady_clock::now();
                            try
                            {
                                bool accepted = true;
                                ++sequence;
                                if (mode == "ping")
                                {
                                    wire::Ping request;
                                    request.set_sequence(sequence);
                                    request.set_text("load");
                                    const auto reply = RequestMessage<wire::Pong>(connection,
                                                                                  Protocol::Type::SYS_Ping,
                                                                                  request,
                                                                                  Protocol::Type::SYS_Pong);
                                    if (reply.sequence() != sequence || reply.text() != "load")
                                    {
                                        throw runtime_error("Pong mismatch");
                                    }
                                }
                                else
                                {
                                    if (!connection.InWorld())
                                    {
                                        throw runtime_error("Unit left World during load");
                                    }
                                    const auto& map = connection.Map();
                                    const double cx = (map.min_x() + map.max_x()) / 2,
                                                 cy = (map.min_y() + map.max_y()) / 2;
                                    const double x = cx - 3 + (i + sequence) % 6,
                                                 y = cy - 3 + (i / 6 + sequence / 3) % 6;
                                    wire::ActionReply reply;
                                    // 비교용 입력은 주변 상태에 따라 바꾸지 않는다. 이동/지정 상대 공격을 번갈아 보낸다.
                                    // 시험 사본의 유닛과 HP를 고정하고 결과 거부도 별도로 기록한다.
                                    if (mode == "fixed-combat" && sequence % 2 == 0)
                                    {
                                        wire::AttackRequest attack;
                                        attack.set_target_id(units[(i + 1) % count].player_id());
                                        reply =
                                            RequestMessage<wire::ActionReply>(connection,
                                                                              Protocol::Type::CM_Attack,
                                                                              attack,
                                                                              Protocol::Type::SM_ActionReply);
                                    }
                                    else
                                    {
                                        reply = mode == "combat"
                                                    ? Combat(connection, units[i], sequence, x, y)
                                                    : Move(connection, x, y);
                                    }
                                    accepted = reply.ok();
                                    const auto snapshot = connection.Snapshot();
                                    if (requested >= start && snapshot.tick() != snapshotTick)
                                    {
                                        ++result.snapshots;
                                        snapshotTick = snapshot.tick();
                                    }
                                }
                                // 준비와 워밍업 표본은 측정 분포에서 제외한다. 원본 표본은 옵션을 준 경우만 보관한다.
                                if (requested >= start)
                                {
                                    const double latencyMs = Milliseconds(requested);
                                    result.latency.push_back(latencyMs);
                                    if (!samplesPath.empty())
                                    {
                                        result.samples.push_back(
                                            {sequence,
                                             chrono::duration<double, milli>(requested - start).count(),
                                             latencyMs,
                                             accepted});
                                    }
                                    if (accepted)
                                    {
                                        ++result.accepted;
                                    }
                                    else
                                    {
                                        ++result.rejected;
                                    }
                                }
                            }
                            catch (const exception& error)
                            {
                                ++result.errors;
                                result.error = error.what();
                                break;
                            }
                            // 응답을 기다리는 closed-loop 부하다. 지연 시 밀린 요청을 한꺼번에 보내지 않는다.
                            next =
                                max(next + chrono::microseconds(1000000 / rate), chrono::steady_clock::now());
                            this_thread::sleep_until(next);
                        }
                    });
            }
            this_thread::sleep_until(start);
            PrintPlayLine("[LOAD PHASE] measure_begin");
        }
        const double elapsed = chrono::duration<double>(chrono::steady_clock::now() - start).count();
        PrintPlayLine("[LOAD PHASE] measure_end");
        if (!samplesPath.empty())
        {
            if (!samplesPath.parent_path().empty())
            {
                fs::create_directories(samplesPath.parent_path());
            }
            ofstream output(samplesPath);
            output << "Client,Sequence,OffsetMs,LatencyMs,Accepted\n" << fixed << setprecision(6);
            for (int i = 0; i < count; ++i)
            {
                for (const auto& sample : results[i].samples)
                {
                    output << i << ',' << sample.sequence << ',' << sample.offsetMs << ',' << sample.latencyMs
                           << ',' << (sample.accepted ? 1 : 0) << '\n';
                }
            }
            if (!output)
            {
                throw runtime_error("Cannot save latency samples");
            }
        }
        vector<double> latency;
        unsigned long long accepted = 0, rejected = 0, errors = 0, snapshots = 0;
        for (const auto& result : results)
        {
            latency.insert(latency.end(), result.latency.begin(), result.latency.end());
            accepted += result.accepted;
            rejected += result.rejected;
            errors += result.errors;
            snapshots += result.snapshots;
            if (!result.error.empty())
            {
                PrintPlayLine("[LOAD ERROR] " + result.error);
            }
        }
        sort(latency.begin(), latency.end());
        sort(loginMs.begin(), loginMs.end());
        if (mode != "ping")
        {
            for (auto& up_connection : connections)
            {
                try
                {
                    if (up_connection->InWorld())
                    {
                        RequestMessage<wire::ActionReply>(*up_connection,
                                                          Protocol::Type::CM_Logout,
                                                          wire::EmptyRequest{},
                                                          Protocol::Type::SM_ActionReply);
                    }
                }
                catch (const exception& error)
                {
                    ++errors;
                    PrintPlayLine("[LOAD ERROR] logout: " + string(error.what()));
                }
            }
        }
        ostringstream row;
        row << fixed << setprecision(3) << mode << ',' << count << ',' << seconds << ',' << rate << ','
            << latency.size() << ',' << accepted << ',' << rejected << ',' << errors << ','
            << Percentile(latency, 50) << ',' << Percentile(latency, 95) << ',' << Percentile(latency, 99)
            << ',' << (latency.empty() ? 0 : latency.back()) << ',' << latency.size() / max(0.001, elapsed)
            << ',' << Percentile(loginMs, 95) << ',' << snapshots;
        const string columns = "Mode,Clients,Seconds,RatePerClient,Requests,Accepted,Rejected,Errors,P50Ms,"
                               "P95Ms,P99Ms,MaxMs,RequestsPerSecond,SetupP95Ms,SnapshotUpdates";
        PrintPlayLine("[LOAD] " + columns);
        PrintPlayLine("[LOAD] " + row.str());
        if (!csvPath.empty())
        {
            if (!csvPath.parent_path().empty())
            {
                fs::create_directories(csvPath.parent_path());
            }
            const bool header = !fs::exists(csvPath) || fs::file_size(csvPath) == 0;
            ofstream output(csvPath, ios::app);
            if (header)
            {
                output << columns << '\n';
            }
            output << row.str() << '\n';
            if (!output)
            {
                throw runtime_error("Cannot save load report");
            }
        }
        return errors == 0 && !latency.empty() ? 0 : 1;
    }
    catch (const exception& error)
    {
        PrintPlayLine("[FAIL] " + string(error.what()));
        return 1;
    }
}
