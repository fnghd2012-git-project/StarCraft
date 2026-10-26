#include "ClientConnection.h"
#include "PlayClient.h"
#include "ReplayClient.h"
#include "LoadClient.h"
#include "BattleClient.h"
#include "Config.h"
#include "Packet.h"

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

void CheckServerVersion(const string& payload, const stServerConfig& config, const stMapConfig& map)
{
    wire::ServerVersionCheckReply reply;
    const auto role = config.type == "World" ? wire::SERVER_ROLE_WORLD : wire::SERVER_ROLE_GAME;
    if (!reply.ParseFromString(payload) || reply.protocol_version() != Protocol::Version ||
        reply.server_id() != config.id || reply.server_role() != role ||
        reply.race() != ToWireRace(config.race) ||
        reply.tick_rate() != static_cast<unsigned>(config.tickRate) || reply.unit_count() == 0 ||
        reply.skill_count() == 0 || !reply.has_map())
    {
        throw runtime_error("ServerVersionCheckReply identity/data mismatch");
    }
    const auto& received = reply.map();
    if (received.map_index() != static_cast<unsigned>(map.index) || received.min_x() != map.minX ||
        received.max_x() != map.maxX || received.min_y() != map.minY || received.max_y() != map.maxY ||
        received.start_points_size() != 3)
    {
        throw runtime_error("ServerVersionCheckReply map bounds mismatch");
    }
    // CSV에서 읽은 좌표와 실제 통신으로 받은 좌표를 비교한다. 전장 입장 검증은 이후 단계다.
    for (int i = 0; i < 3; ++i)
    {
        const auto& point = received.start_points(i);
        const auto& expected = map.spawns[i];
        if (point.race() != ToWireRace(expected.race) || point.x() != expected.x || point.y() != expected.y)
        {
            throw runtime_error("ServerVersionCheckReply start point mismatch");
        }
    }
}

void CheckPong(SOCKET socket, const wire::Ping& request)
{
    wire::Pong reply;
    if (!reply.ParseFromString(ReadPacket(socket, Protocol::Type::SYS_Pong)) ||
        reply.sequence() != request.sequence() || reply.text() != request.text() ||
        reply.padding() != request.padding())
    {
        throw runtime_error("Pong fields mismatch");
    }
}

void CheckClient(const stServerConfig& config, const stMapConfig& map, int clientIndex, int rounds)
{
    auto socket = Connect(config);
    wire::ServerVersionCheckRequest request;
    request.set_protocol_version(Protocol::Version);
    request.set_client_name("DummyClient-" + to_string(clientIndex));
    const string versionCheck = Protocol::EncodeMessage(Protocol::Type::SYS_ServerVersionCheck, request);
    // 헤더 일부를 먼저 보내 서버의 수신 버퍼 누적 처리를 확인한다.
    SendAll(socket.Get(), string_view(versionCheck).substr(0, 3));
    this_thread::sleep_for(chrono::milliseconds(20));
    SendAll(socket.Get(), string_view(versionCheck).substr(3));
    CheckServerVersion(ReadPacket(socket.Get(), Protocol::Type::SYS_ServerVersionCheckReply), config, map);
    for (int round = 0; round < rounds; ++round)
    {
        wire::Ping first;
        first.set_sequence(round * 2 + 1);
        first.set_text("한글 패킷 확인 client=" + to_string(clientIndex) + " round=" + to_string(round));
        wire::Ping second;
        second.set_sequence(round * 2 + 2);
        second.set_text("최대 크기 패킷");
        // Protobuf의 필드 태그/길이도 본문 크기에 포함된다. 전체 패킷을 정확히 4096바이트로 맞춘다.
        const size_t maxPayload = Protocol::MaxPacketSize - Protocol::HeaderSize;
        second.set_padding(string(maxPayload, '\0'));
        second.mutable_padding()->resize(maxPayload - (second.ByteSizeLong() - maxPayload));
        second.mutable_padding()->back() = static_cast<char>(0xff);
        if (second.ByteSizeLong() != maxPayload)
        {
            throw runtime_error("Maximum packet size calculation failed");
        }
        // 여러 패킷을 한 send 버퍼에 붙여도 각각의 응답을 복원해야 한다.
        SendAll(socket.Get(),
                Protocol::EncodeMessage(Protocol::Type::SYS_Ping, first) +
                    Protocol::EncodeMessage(Protocol::Type::SYS_Ping, second));
        CheckPong(socket.Get(), first);
        CheckPong(socket.Get(), second);
    }
}
} // namespace

int main(int argc, char* argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    if (argc >= 2 && string_view(argv[1]) == "--battle")
    {
        return RunBattleClient(argc, argv);
    }
    if (argc >= 2 && string_view(argv[1]) == "--load")
    {
        return RunLoadClient(argc, argv);
    }
    if (argc >= 2 && string_view(argv[1]) == "--session-load")
    {
        return RunSessionLoad(argc, argv);
    }
    if (argc >= 2 && (string_view(argv[1]) == "--replay" || string_view(argv[1]) == "--replay-menu" ||
                      string_view(argv[1]) == "--replay-list"))
    {
        return RunReplayClient(argc, argv);
    }
    if (argc >= 2 &&
        (string_view(argv[1]) == "--play" || string_view(argv[1]) == "--flow-check" ||
         string_view(argv[1]) == "--account-check" || string_view(argv[1]) == "--skill-check" ||
         string_view(argv[1]) == "--capacity-check" || string_view(argv[1]) == "--ranking-check"))
    {
        return RunPlayClient(argc, argv);
    }
    try
    {
        fs::path configPath = "Server.ini";
        string serverId = "A";
        int clients = 1;
        int rounds = 10;
        if (argc == 2 && string_view(argv[1]) == "--help")
        {
            cout << "DummyClient.exe [--config Server.ini] [--server CSV_Index|World] [--clients 1..128] "
                    "[--rounds 1..10000]\n"
                 << "DummyClient.exe --play [--config Server.ini]\n"
                 << "DummyClient.exe --battle [--config Server.ini] [--per-server 500] [--seconds 240] "
                    "[--workers 8] [--report FILE]\n"
                 << "DummyClient.exe --replay-menu [--dir Replays]\n"
                 << "DummyClient.exe --replay FILE [--console] [--verify] [--allow-partial] [--compare "
                    "SNAPSHOT]\n"
                 << "DummyClient.exe --replay FILE --image OUTPUT.png --at SECONDS\n"
                 << "DummyClient.exe --load [--config Server.ini] [--mode ping|world|combat|fixed-combat] "
                    "[--clients N] [--seconds N] [--rate N] [--warmup N] [--csv FILE] [--samples FILE]\n"
                 << "DummyClient.exe --session-load --config FILE --phase seed|measure --clients N "
                    "--concurrency N --prefix NAME --samples FILE\n";
            return 0;
        }
        for (int i = 1; i < argc; i += 2)
        {
            if (i + 1 >= argc)
            {
                throw runtime_error("Missing option value");
            }
            const string_view option = argv[i];
            if (option == "--config")
            {
                configPath = fs::path(argv[i + 1]);
            }
            else if (option == "--server")
            {
                serverId = argv[i + 1];
            }
            else if (option == "--clients")
            {
                clients = ParseInt(argv[i + 1], 1, 128);
            }
            else if (option == "--rounds")
            {
                rounds = ParseInt(argv[i + 1], 1, 10000);
            }
            else
            {
                throw runtime_error("Unknown option: " + string(option));
            }
        }
        const auto config = LoadServerConfig(configPath, serverId);
        const auto map = LoadMapConfig(config.mapFile, config.startPointFile);
        const CWinsockSession winsock;
        atomic<int> failures = 0;
        mutex outputMutex;
        const auto startedAt = chrono::steady_clock::now();
        {
            vector<jthread> workers;
            // 더미 한 개당 스레드 하나를 사용해 접속을 병렬로 발생시킨다.
            // 서버의 워커 수와는 별개이며, 대규모 부하 발생기는 이후 단계에서 분리한다.
            for (int i = 0; i < clients; ++i)
            {
                workers.emplace_back(
                    [&, i]
                    {
                        try
                        {
                            CheckClient(config, map, i, rounds);
                        }
                        catch (const exception& error)
                        {
                            ++failures;
                            lock_guard lock(outputMutex);
                            cerr << "[FAIL] client=" << i << ' ' << error.what() << '\n';
                        }
                    });
            }
        }
        const auto elapsed =
            chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - startedAt).count();
        cout << (failures.load() == 0 ? "[OK]" : "[FAIL]") << " server=" << serverId << " clients=" << clients
             << " rounds=" << rounds << " replies=" << (clients - failures.load()) * rounds * 2
             << " elapsed_ms=" << elapsed << '\n';
        return failures.load() == 0 ? 0 : 1;
    }
    catch (const exception& error)
    {
        cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
