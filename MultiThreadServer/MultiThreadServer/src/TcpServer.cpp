#include "Network.h"
#include "TcpServer.h"
#include "Packet.h"

namespace
{
    atomic<bool> stopRequested = false;
    mutex logMutex;

    BOOL WINAPI OnConsoleSignal(DWORD signal)
    {
        if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT)
        {
            stopRequested.store(true);
            return TRUE;
        }
        return FALSE;
    }

    struct ClientSession
    {
        // 이 세션은 배정된 워커만 읽고 쓴다. 수신/송신 버퍼에 별도 잠금이 필요 없다.
        Socket socket;
        string received;
        string sending;
        size_t sent = 0;
        bool greeted = false;
        chrono::steady_clock::time_point connectedAt = chrono::steady_clock::now();
    };

    class NetworkWorker final
    {
    public:
        NetworkWorker(const ServerConfig& config, const string& serverInfo)
            : config_(config), serverInfo_(serverInfo), thread_([this](stop_token stop) { Run(stop); }) {}

        ~NetworkWorker()
        {
            thread_.request_stop();
            thread_.join();
        }

        bool Add(Socket& socket)
        {
            // 호출자는 accept를 담당하는 메인 스레드 하나다. 여기서만 워커로 소켓을 넘긴다.
            if (failed_.load() || clientCount_.load() >= config_.maxClientsPerWorker)
            {
                return false;
            }
            lock_guard lock(pendingMutex_);
            pending_.push_back(move(socket));
            ++clientCount_;
            return true;
        }

        bool Failed() const { return failed_.load(); }

    private:
        void Queue(ClientSession& client, const string& packet)
        {
            // 응답을 읽지 않는 클라이언트 때문에 서버 메모리가 계속 증가하지 않게 제한한다.
            if (client.sending.size() - client.sent + packet.size() > static_cast<size_t>(config_.maxPendingSendBytes))
            {
                throw runtime_error("Client send queue full");
            }
            if (client.sent != 0)
            {
                client.sending.erase(0, client.sent);
                client.sent = 0;
            }
            client.sending += packet;
        }

        void HandlePackets(ClientSession& client)
        {
            // 완성된 패킷만 소비하고, 마지막에 남은 불완전 패킷은 수신 버퍼에 유지한다.
            size_t consumed = 0;
            while (true)
            {
                const string_view remaining(client.received.data() + consumed, client.received.size() - consumed);
                const size_t size = Protocol::PacketSize(remaining);
                if (size == 0 || remaining.size() < size)
                {
                    break;
                }
                const auto type = static_cast<Protocol::Type>(Protocol::Read16(remaining.data() + 4));
                const string_view payload = remaining.substr(Protocol::HeaderSize, size - Protocol::HeaderSize);
                if (type == Protocol::Type::Hello && !client.greeted)
                {
                    // 현재 Hello는 통신 점검이다. 계정 로그인이나 전장 진입을 뜻하지 않는다.
                    wire::HelloRequest request;
                    if (!request.ParseFromArray(payload.data(), static_cast<int>(payload.size())) ||
                        request.client_name().empty() || request.client_name().size() > 64)
                    {
                        throw runtime_error("Invalid HelloRequest");
                    }
                    client.greeted = true;
                    // 맵 정보는 시작할 때 한 번 직렬화했다. 모든 접속에 같은 완성 패킷을 사용한다.
                    Queue(client, serverInfo_);
                }
                else if (type == Protocol::Type::Ping && client.greeted)
                {
                    wire::Ping request;
                    if (!request.ParseFromArray(payload.data(), static_cast<int>(payload.size())) ||
                        request.sequence() == 0 || request.text().empty() || request.text().size() > 128)
                    {
                        throw runtime_error("Invalid Ping");
                    }
                    // 역직렬화 성공과 게임에서 허용하는 값인지는 별개다. 위에서 둘 다 확인한다.
                    wire::Pong response;
                    response.set_sequence(request.sequence());
                    response.set_text(request.text());
                    response.set_padding(request.padding());
                    Queue(client, Protocol::EncodeMessage(Protocol::Type::Pong, response));
                }
                else
                {
                    throw runtime_error("Unexpected packet type/state");
                }
                consumed += size;
            }
            client.received.erase(0, consumed);
        }

        bool Process(ClientSession& client, short events)
        {
            if (events & (POLLERR | POLLNVAL))
            {
                return false;
            }
            if (!client.greeted && chrono::steady_clock::now() - client.connectedAt > chrono::seconds(5))
            {
                // 연결만 열어 두거나 헤더 일부만 보낸 접속은 5초 뒤 정리한다.
                return false;
            }
            if (events & (POLLRDNORM | POLLHUP))
            {
                array<char, 4096> buffer{};
                const int received = recv(client.socket.Get(), buffer.data(), static_cast<int>(buffer.size()), 0);
                if (received == 0)
                {
                    return false;
                }
                if (received < 0)
                {
                    return WSAGetLastError() == WSAEWOULDBLOCK && !(events & POLLHUP);
                }
                client.received.append(buffer.data(), static_cast<size_t>(received));
                HandlePackets(client);
            }
            if (client.sent < client.sending.size())
            {
                const int sent = send(client.socket.Get(), client.sending.data() + client.sent,
                    static_cast<int>(client.sending.size() - client.sent), 0);
                if (sent < 0)
                {
                    return WSAGetLastError() == WSAEWOULDBLOCK;
                }
                if (sent == 0)
                {
                    return false;
                }
                client.sent += static_cast<size_t>(sent);
                // send가 일부만 보냈으면 다음 반복에서 남은 부분부터 이어 보낸다.
                if (client.sent == client.sending.size())
                {
                    client.sending.clear();
                    client.sent = 0;
                }
            }
            return true;
        }

        void Run(stop_token stop)
        {
            try
            {
                vector<ClientSession> clients;
                while (!stop.stop_requested())
                {
                    {
                        // 잠금은 연결 인계에만 사용한다. 실제 송수신은 잠금 밖에서 처리한다.
                        lock_guard lock(pendingMutex_);
                        for (auto& socket : pending_)
                        {
                            ClientSession client;
                            client.socket = move(socket);
                            clients.push_back(move(client));
                        }
                        pending_.clear();
                    }
                    if (clients.empty())
                    {
                        this_thread::sleep_for(chrono::milliseconds(10));
                        continue;
                    }
                    vector<WSAPOLLFD> polls;
                    polls.reserve(clients.size());
                    for (const auto& client : clients)
                    {
                        const short events = static_cast<short>(POLLRDNORM | (client.sending.empty() ? 0 : POLLWRNORM));
                        polls.push_back({ client.socket.Get(), events, 0 });
                    }
                    SocketCheck(WSAPoll(polls.data(), static_cast<ULONG>(polls.size()), 50) != SOCKET_ERROR, "WSAPoll");
                    for (size_t i = clients.size(); i-- > 0;)
                    {
                        // 뒤에서부터 지워야 polls와 clients의 앞쪽 인덱스가 어긋나지 않는다.
                        bool keep = false;
                        try
                        {
                            keep = Process(clients[i], polls[i].revents);
                        }
                        catch (const exception& error)
                        {
                            lock_guard lock(logMutex);
                            cerr << "[DROP] " << error.what() << '\n';
                        }
                        if (!keep)
                        {
                            clients.erase(clients.begin() + static_cast<vector<ClientSession>::difference_type>(i));
                            --clientCount_;
                        }
                    }
                }
            }
            catch (const exception& error)
            {
                lock_guard lock(logMutex);
                cerr << "[WORKER FAIL] " << error.what() << '\n';
                failed_.store(true);
            }
        }

        const ServerConfig& config_;
        const string& serverInfo_;
        mutex pendingMutex_;
        vector<Socket> pending_;
        atomic<int> clientCount_ = 0;
        atomic<bool> failed_ = false;
        jthread thread_;
    };
}

string MakeServerInfo(const ServerConfig& config, const MapConfig& map, const GameData& data)
{
    // --config-check의 콘솔 출력용이다. 네트워크 응답은 아래의 Protobuf 메시지를 사용한다.
    ostringstream output;
    output << "ServerId=" << config.id << "\nServerType=" << config.type << "\nRace=" << config.race
        << "\nMapIndex=" << map.index << "\nMinX=" << map.minX << "\nMaxX=" << map.maxX
        << "\nMinY=" << map.minY << "\nMaxY=" << map.maxY << "\nUnitCount=" << data.units.rows.size()
        << "\nSkillCount=" << data.skills.rows.size() << "\nTickRate=" << config.tickRate << '\n';
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
        if (race == "Terran") { return wire::RACE_TERRAN; }
        if (race == "Protoss") { return wire::RACE_PROTOSS; }
        if (race == "Zerg") { return wire::RACE_ZERG; }
        return wire::RACE_UNSPECIFIED;
    }

    string MakeHelloPacket(const ServerConfig& config, const MapConfig& map, const GameData& data)
    {
        wire::HelloReply reply;
        reply.set_server_id(config.id);
        reply.set_server_role(config.type == "World" ? wire::SERVER_ROLE_WORLD : wire::SERVER_ROLE_GAME);
        reply.set_race(ToWireRace(config.race));
        reply.set_unit_count(static_cast<unsigned>(data.units.rows.size()));
        reply.set_skill_count(static_cast<unsigned>(data.skills.rows.size()));
        reply.set_tick_rate(config.tickRate);
        auto* mapInfo = reply.mutable_map();
        mapInfo->set_map_index(map.index);
        mapInfo->set_min_x(map.minX);
        mapInfo->set_max_x(map.maxX);
        mapInfo->set_min_y(map.minY);
        mapInfo->set_max_y(map.maxY);
        for (const auto& spawn : map.spawns)
        {
            auto* point = mapInfo->add_start_points();
            point->set_race(ToWireRace(spawn.race));
            point->set_x(spawn.x);
            point->set_y(spawn.y);
        }
        return Protocol::EncodeMessage(Protocol::Type::HelloReply, reply);
    }
}

void RunTcpServer(const ServerConfig& config, const MapConfig& map, const GameData& data, int runSeconds)
{
    const WinsockSession winsock;
    Socket listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    SocketCheck(listener.Get() != INVALID_SOCKET, "socket");
    const BOOL exclusive = TRUE;
    SocketCheck(setsockopt(listener.Get(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == 0, "SO_EXCLUSIVEADDRUSE");
    const auto address = MakeAddress(config.address, config.port);
    SocketCheck(bind(listener.Get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0, "bind");
    SocketCheck(listen(listener.Get(), SOMAXCONN) == 0, "listen");
    SetNonblocking(listener.Get(), true);
    const string serverInfo = MakeHelloPacket(config, map, data);
    vector<unique_ptr<NetworkWorker>> workers;
    // 메인 스레드는 접속 수락, 설정한 수의 워커는 실제 송수신을 맡는다.
    for (int i = 0; i < config.workerCount; ++i)
    {
        workers.push_back(make_unique<NetworkWorker>(config, serverInfo));
    }
    stopRequested.store(false);
    if (!SetConsoleCtrlHandler(OnConsoleSignal, TRUE))
    {
        throw runtime_error("SetConsoleCtrlHandler failed");
    }
    struct HandlerGuard
    {
        ~HandlerGuard() { SetConsoleCtrlHandler(OnConsoleSignal, FALSE); }
    } handlerGuard;
    cout << "[LISTEN] " << config.id << ' ' << config.address << ':' << config.port
        << " workers=" << config.workerCount << " units=" << data.units.rows.size()
        << " skills=" << data.skills.rows.size() << " (Ctrl+C to stop)" << endl;
    const auto startedAt = chrono::steady_clock::now();
    size_t nextWorker = 0;
    while (!stopRequested.load())
    {
        if (runSeconds > 0 && chrono::steady_clock::now() - startedAt >= chrono::seconds(runSeconds))
        {
            break;
        }
        for (const auto& worker : workers)
        {
            if (worker->Failed())
            {
                throw runtime_error("Network worker stopped unexpectedly");
            }
        }
        for (int accepted = 0; accepted < 64; ++accepted)
        {
            Socket client(accept(listener.Get(), nullptr, nullptr));
            if (client.Get() == INVALID_SOCKET)
            {
                if (WSAGetLastError() != WSAEWOULDBLOCK)
                {
                    SocketCheck(false, "accept");
                }
                break;
            }
            SetNonblocking(client.Get(), true);
            for (size_t attempt = 0; attempt < workers.size(); ++attempt)
            {
                const size_t index = nextWorker++ % workers.size();
                if (workers[index]->Add(client))
                {
                    break;
                }
            }
            // 모든 워커가 가득 찼으면 client 소멸자가 연결을 닫는다.
        }
        this_thread::sleep_for(chrono::milliseconds(10));
    }
    listener.Reset();
    // 더 이상 접속을 받지 않은 뒤 워커를 모두 합류시키고 Winsock을 정리한다.
    workers.clear();
    cout << "[STOP] Network workers joined; sockets closed." << endl;
}
