#include "Network.h"
#include "Config.h"
#include "Packet.h"

namespace
{
    Socket Connect(const ServerConfig& config)
    {
        // 서버가 없더라도 오래 멈추지 않도록 접속 대기를 3초로 제한한다.
        Socket client(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        SocketCheck(client.Get() != INVALID_SOCKET, "socket");
        SetNonblocking(client.Get(), true);
        const auto address = MakeAddress(config.address, config.port);
        const int result = connect(client.Get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address));
        if (result == SOCKET_ERROR)
        {
            if (WSAGetLastError() != WSAEWOULDBLOCK)
            {
                SocketCheck(false, "connect");
            }
            WSAPOLLFD poll{ client.Get(), POLLWRNORM, 0 };
            if (WSAPoll(&poll, 1, 3000) <= 0)
            {
                throw runtime_error("Connect timed out");
            }
            int error = 0;
            int length = sizeof(error);
            SocketCheck(getsockopt(client.Get(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) == 0, "SO_ERROR");
            if (error != 0 || !(poll.revents & POLLWRNORM))
            {
                throw runtime_error("Connect failed: " + to_string(error));
            }
        }
        SetNonblocking(client.Get(), false);
        const DWORD timeout = 3000;
        SocketCheck(setsockopt(client.Get(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0, "SO_RCVTIMEO");
        SocketCheck(setsockopt(client.Get(), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0, "SO_SNDTIMEO");
        return client;
    }

    void SendAll(SOCKET socket, string_view bytes)
    {
        // 한 번의 send가 전체 데이터를 전송한다는 보장은 없다.
        while (!bytes.empty())
        {
            const int sent = send(socket, bytes.data(), static_cast<int>(bytes.size()), 0);
            SocketCheck(sent > 0, "send");
            bytes.remove_prefix(static_cast<size_t>(sent));
        }
    }

    string ReadExact(SOCKET socket, size_t length)
    {
        // 필요한 바이트 수만 채운다. 다음 패킷의 데이터를 앞 패킷에 섞지 않는다.
        string bytes(length, '\0');
        size_t received = 0;
        while (received < length)
        {
            const int count = recv(socket, bytes.data() + received, static_cast<int>(length - received), 0);
            SocketCheck(count > 0, "recv/connection closed");
            received += static_cast<size_t>(count);
        }
        return bytes;
    }

    string ReadPacket(SOCKET socket, Protocol::Type expected)
    {
        const string header = ReadExact(socket, Protocol::HeaderSize);
        const size_t length = Protocol::PacketSize(header);
        if (Protocol::Read16(header.data() + 4) != static_cast<unsigned>(expected))
        {
            throw runtime_error("Unexpected reply type");
        }
        return ReadExact(socket, length - Protocol::HeaderSize);
    }

    wire::Race ToWireRace(const string& race)
    {
        if (race == "Terran") { return wire::RACE_TERRAN; }
        if (race == "Protoss") { return wire::RACE_PROTOSS; }
        if (race == "Zerg") { return wire::RACE_ZERG; }
        return wire::RACE_UNSPECIFIED;
    }

    void CheckHello(const string& payload, const ServerConfig& config, const MapConfig& map)
    {
        wire::HelloReply reply;
        const auto role = config.type == "World" ? wire::SERVER_ROLE_WORLD : wire::SERVER_ROLE_GAME;
        if (!reply.ParseFromString(payload) || reply.server_id() != config.id ||
            reply.server_role() != role || reply.race() != ToWireRace(config.race) ||
            reply.tick_rate() != static_cast<unsigned>(config.tickRate) ||
            reply.unit_count() == 0 || reply.skill_count() == 0 || !reply.has_map())
        {
            throw runtime_error("HelloReply identity/data mismatch");
        }
        const auto& received = reply.map();
        if (received.map_index() != static_cast<unsigned>(map.index) ||
            received.min_x() != map.minX || received.max_x() != map.maxX ||
            received.min_y() != map.minY || received.max_y() != map.maxY || received.start_points_size() != 3)
        {
            throw runtime_error("HelloReply map bounds mismatch");
        }
        // CSV에서 읽은 좌표와 실제 통신으로 받은 좌표를 비교한다. 전장 입장 검증은 이후 단계다.
        for (int i = 0; i < 3; ++i)
        {
            const auto& point = received.start_points(i);
            const auto& expected = map.spawns[i];
            if (point.race() != ToWireRace(expected.race) || point.x() != expected.x || point.y() != expected.y)
            {
                throw runtime_error("HelloReply start point mismatch");
            }
        }
    }

    void CheckPong(SOCKET socket, const wire::Ping& request)
    {
        wire::Pong reply;
        if (!reply.ParseFromString(ReadPacket(socket, Protocol::Type::Pong)) ||
            reply.sequence() != request.sequence() || reply.text() != request.text() || reply.padding() != request.padding())
        {
            throw runtime_error("Pong fields mismatch");
        }
    }

    void CheckClient(const ServerConfig& config, const MapConfig& map, int clientIndex, int rounds)
    {
        auto socket = Connect(config);
        wire::HelloRequest request;
        request.set_client_name("DummyClient-" + to_string(clientIndex));
        const string hello = Protocol::EncodeMessage(Protocol::Type::Hello, request);
        // 헤더 일부를 먼저 보내 서버의 수신 버퍼 누적 처리를 확인한다.
        SendAll(socket.Get(), string_view(hello).substr(0, 3));
        this_thread::sleep_for(chrono::milliseconds(20));
        SendAll(socket.Get(), string_view(hello).substr(3));
        CheckHello(ReadPacket(socket.Get(), Protocol::Type::HelloReply), config, map);
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
            SendAll(socket.Get(), Protocol::EncodeMessage(Protocol::Type::Ping, first) + Protocol::EncodeMessage(Protocol::Type::Ping, second));
            CheckPong(socket.Get(), first);
            CheckPong(socket.Get(), second);
        }
    }
}

int main(int argc, char* argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    try
    {
        fs::path configPath = "Server.ini";
        string serverId = "A";
        int clients = 1;
        int rounds = 10;
        if (argc == 2 && string_view(argv[1]) == "--help")
        {
            cout << "DummyClient.exe [--config Server.ini] [--server A|B|C|World] [--clients 1..128] [--rounds 1..10000]\n";
            return 0;
        }
        for (int i = 1; i < argc; i += 2)
        {
            if (i + 1 >= argc)
            {
                throw runtime_error("Missing option value");
            }
            const string_view option = argv[i];
            if (option == "--config") { configPath = fs::path(argv[i + 1]); }
            else if (option == "--server") { serverId = argv[i + 1]; }
            else if (option == "--clients") { clients = ParseInt(argv[i + 1], 1, 128); }
            else if (option == "--rounds") { rounds = ParseInt(argv[i + 1], 1, 10000); }
            else { throw runtime_error("Unknown option: " + string(option)); }
        }
        const auto config = LoadServerConfig(configPath, serverId);
        const auto map = LoadMapConfig(config.mapFile, config.startPointFile);
        const WinsockSession winsock;
        atomic<int> failures = 0;
        mutex outputMutex;
        const auto startedAt = chrono::steady_clock::now();
        {
            vector<jthread> workers;
            // 더미 한 개당 스레드 하나를 사용해 접속을 병렬로 발생시킨다.
            // 서버의 워커 수와는 별개이며, 대규모 부하 발생기는 이후 단계에서 분리한다.
            for (int i = 0; i < clients; ++i)
            {
                workers.emplace_back([&, i]
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
        const auto elapsed = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - startedAt).count();
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
