#include "Network.h"
#include "TcpServer.h"
#include "Packet.h"
#include "GameLogic.h"
#include "NetworkWorker.h"
#include "IocpServer.h"

namespace
{
atomic<bool> g_stopRequested = false;

BOOL WINAPI OnConsoleSignal(DWORD signal)
{
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT)
    {
        g_stopRequested.store(true);
        return TRUE;
    }
    return FALSE;
}

} // namespace

string MakeServerInfo(const stServerConfig& config, const stMapConfig& map, const stGameData& data)
{
    // --config-check의 콘솔 출력용이다. 네트워크 응답은 아래의 Protobuf 메시지를 사용한다.
    ostringstream output;
    output << "ServerId=" << config.id << "\nServerType=" << config.type << "\nRace=" << config.race
           << "\nMapIndex=" << map.index << "\nMinX=" << map.minX << "\nMaxX=" << map.maxX
           << "\nMinY=" << map.minY << "\nMaxY=" << map.maxY << "\nUnitCount=" << data.units.rows.size()
           << "\nSkillCount=" << data.skills.rows.size() << "\nTickRate=" << config.tickRate
           << "\nSpatialSearch=" << SpatialSearchName(config.spatialSearch)
           << "\nSpatialCellSize=" << config.spatialCellSize
           << "\nVerifySpatialSearch=" << config.verifySpatialSearch << "\nNetworkMode=" << config.networkMode
           << "\nNetworkWorkers=" << config.workerCount << "\nBattleWorkers=" << config.battleWorkerCount
           << "\nVerifyParallel=" << config.verifyParallel << "\nUsePool=" << config.useMemoryPool
           << "\nPoolCapacity=" << config.poolCapacity << '\n';
    for (const auto& spawn : map.spawns)
    {
        output << spawn.race << "SpawnX=" << spawn.x << '\n' << spawn.race << "SpawnY=" << spawn.y << '\n';
    }
    return output.str();
}

namespace
{
wire::Race ToWireRace(const string& race)
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
    return wire::RACE_UNSPECIFIED;
}

string MakeServerVersionPacket(const stServerConfig& config, const stMapConfig& map, const stGameData& data)
{
    wire::ServerVersionCheckReply reply;
    reply.set_protocol_version(Protocol::Version);
    reply.set_server_id(config.id);
    reply.set_server_role(config.type == "World" ? wire::SERVER_ROLE_WORLD : wire::SERVER_ROLE_GAME);
    reply.set_race(ToWireRace(config.race));
    reply.set_unit_count(static_cast<unsigned>(data.units.rows.size()));
    reply.set_skill_count(static_cast<unsigned>(data.skills.rows.size()));
    reply.set_tick_rate(config.tickRate);
    for (const auto& entry : data.skillDefinitions)
    {
        const auto& skill = entry.second;
        auto* p_info = reply.add_skill_info();
        p_info->set_skill_index(skill.index);
        p_info->set_name(skill.name);
        p_info->set_target_type(skill.targetType);
        p_info->set_mp_cost(skill.mpCost);
        p_info->set_cooldown_ms(skill.cooldownMs);
        p_info->set_cast_range(skill.range);
    }
    auto* p_mapInfo = reply.mutable_map();
    p_mapInfo->set_map_index(map.index);
    p_mapInfo->set_min_x(map.minX);
    p_mapInfo->set_max_x(map.maxX);
    p_mapInfo->set_min_y(map.minY);
    p_mapInfo->set_max_y(map.maxY);
    for (const auto& spawn : map.spawns)
    {
        auto* p_point = p_mapInfo->add_start_points();
        p_point->set_race(ToWireRace(spawn.race));
        p_point->set_x(spawn.x);
        p_point->set_y(spawn.y);
    }
    return Protocol::EncodeMessage(Protocol::Type::SYS_ServerVersionCheckReply, reply);
}
} // namespace

void RunTcpServer(const stServerConfig& config,
                  const stMapConfig& map,
                  const stGameData& data,
                  int runSeconds)
{
    const CWinsockSession winsock;
    CSocket listener(WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED));
    SocketCheck(listener.Get() != INVALID_SOCKET, "socket");
    const BOOL exclusive = TRUE;
    SocketCheck(setsockopt(listener.Get(),
                           SOL_SOCKET,
                           SO_EXCLUSIVEADDRUSE,
                           reinterpret_cast<const char*>(&exclusive),
                           sizeof(exclusive)) == 0,
                "SO_EXCLUSIVEADDRUSE");
    const auto address = MakeAddress(config.address, config.port);
    SocketCheck(bind(listener.Get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0,
                "bind");
    SocketCheck(listen(listener.Get(), SOMAXCONN) == 0, "listen");
    SetNonblocking(listener.Get(), true);
    const string serverInfo = MakeServerVersionPacket(config, map, data);
    // 게임 상태는 아래 메인 루프에서 갱신한다. 네트워크 쓰레드(워커)는 같은 game의 큐에 요청을 넣는다.
    CGameLogic game(config, map, data);
    vector<unique_ptr<CNetworkWorker>> workers;
    unique_ptr<CIocpServer> up_iocp;
    // IOCP는 워커에서 접속 완료를 처리한다. WSAPoll만 아래 메인 루프에서 accept한다.
    if (config.networkMode == "IOCP")
    {
        up_iocp = make_unique<CIocpServer>(config, game, serverInfo, move(listener));
    }
    for (int i = 0; config.networkMode == "WSAPoll" && i < config.workerCount; ++i)
    {
        workers.push_back(make_unique<CNetworkWorker>(config, serverInfo, game));
    }
    g_stopRequested.store(false);
    if (!SetConsoleCtrlHandler(OnConsoleSignal, TRUE))
    {
        throw runtime_error("SetConsoleCtrlHandler failed");
    }
    struct stHandlerGuard
    {
        ~stHandlerGuard()
        {
            SetConsoleCtrlHandler(OnConsoleSignal, FALSE);
        }
    } handlerGuard;
    cout << "[LISTEN] " << config.id << ' ' << config.address << ':' << config.port
         << " mode=" << config.networkMode << " workers=" << config.workerCount
         << " units=" << data.units.rows.size() << " skills=" << data.skills.rows.size()
         << " max_players=" << (config.type == "World" ? config.maxWorldPlayers : config.maxGamePlayers)
         << " spatial=" << SpatialSearchName(config.spatialSearch) << " cell=" << config.spatialCellSize
         << " spatial_verify=" << config.verifySpatialSearch << " (Ctrl+C to stop)" << endl;
    const auto startedAt = chrono::steady_clock::now();
    auto stopFileCheck = startedAt;
    size_t nextWorker = 0;
    // 서버 메인 루프. 네트워크 쓰레드(워커)의 Run과 별도이며 게임 명령 처리와 전장 틱을 진행한다.
    while (!g_stopRequested.load())
    {
        // 시연 실행기가 자기 실행 폴더에 만든 종료 파일로 정상 마감한다.
        // 다른 콘솔에 종료 신호를 보내지 않으며, 파일 확인은 초당 한 번만 한다.
        const auto now = chrono::steady_clock::now();
        if (!config.stopFile.empty() && now >= stopFileCheck)
        {
            if (fs::exists(config.stopFile))
            {
                break;
            }
            stopFileCheck = now + chrono::seconds(1);
        }
        if (runSeconds > 0 && chrono::steady_clock::now() - startedAt >= chrono::seconds(runSeconds))
        {
            break;
        }
        for (const auto& up_worker : workers)
        {
            if (up_worker->Failed())
            {
                throw runtime_error("Network worker stopped unexpectedly");
            }
        }
        if (up_iocp)
        {
            up_iocp->Update();
            if (up_iocp->Failed())
            {
                throw runtime_error("IOCP worker failed");
            }
        }
        for (int accepted = 0; !up_iocp && accepted < 64; ++accepted)
        {
            CSocket client(accept(listener.Get(), nullptr, nullptr));
            if (client.Get() == INVALID_SOCKET)
            {
                if (WSAGetLastError() != WSAEWOULDBLOCK)
                {
                    SocketCheck(false, "accept");
                }
                break;
            }
            SetNonblocking(client.Get(), true);
            // 작은 전투 응답/상태 조각을 Nagle의 추가 묶음 대기 없이 보낸다.
            const BOOL noDelay = TRUE;
            SocketCheck(setsockopt(client.Get(),
                                   IPPROTO_TCP,
                                   TCP_NODELAY,
                                   reinterpret_cast<const char*>(&noDelay),
                                   sizeof(noDelay)) == 0,
                        "TCP_NODELAY");
            for (size_t attempt = 0; attempt < workers.size(); ++attempt)
            {
                const size_t index = nextWorker++ % workers.size();
                if (workers[index]->AddClient(client))
                {
                    break;
                }
            }
            // 모든 워커가 가득 찼으면 client 소멸자가 연결을 닫는다.
        }
        // 명령 큐 처리 + 틱 시간 확인. 이 루프가 한 번 돈다고 매번 Tick이 실행되는 것은 아니다.
        game.Update();
        this_thread::sleep_for(chrono::milliseconds(1));
    }
    listener.Reset();
    if (up_iocp)
    {
        up_iocp->Stop();
    }
    // 더 이상 접속을 받지 않은 뒤 워커를 모두 합류시키고 Winsock을 정리한다.
    workers.clear();
    cout << "[STOP] Network workers joined; sockets closed." << endl;
}
