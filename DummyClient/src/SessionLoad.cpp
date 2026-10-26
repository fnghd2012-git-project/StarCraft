#include "LoadClient.h"
#include "PlayClient.h"

namespace
{
double ElapsedMs(chrono::steady_clock::time_point start)
{
    return chrono::duration<double, milli>(chrono::steady_clock::now() - start).count();
}

struct stSessionSample
{
    double loginMs = 0, transferMs = 0, logoutMs = 0, totalMs = 0;
    string error;
};
} // namespace

// DB 쓰레드(워커) 비교는 전투와 분리한다. 같은 계정을 먼저 생성하고, 기존 계정 로그인부터
// Game -> World 이동 -> 로그아웃까지 동일한 횟수로 실행한다. 비밀번호는 출력하지 않는다.
int RunSessionLoad(int argc, char* argv[])
{
    try
    {
        fs::path configPath = "Server.ini", samplesPath;
        string phase = "measure", prefix = "WorkerSession", server = "A";
        int count = 96, concurrency = 32;
        for (int i = 2; i < argc; i += 2)
        {
            if (i + 1 >= argc)
            {
                throw runtime_error("Missing session option value");
            }
            const string option = argv[i], value = argv[i + 1];
            if (option == "--config")
            {
                configPath = value;
            }
            else if (option == "--phase")
            {
                phase = value;
            }
            else if (option == "--server")
            {
                server = value;
            }
            else if (option == "--clients")
            {
                count = ParseInt(value, 1, 1024);
            }
            else if (option == "--concurrency")
            {
                concurrency = ParseInt(value, 1, 64);
            }
            else if (option == "--prefix")
            {
                prefix = value;
            }
            else if (option == "--samples")
            {
                samplesPath = value;
            }
            else
            {
                throw runtime_error("Unknown session option: " + option);
            }
        }
        if (phase != "seed" && phase != "measure")
        {
            throw runtime_error("Session phase must be seed/measure");
        }
        if (prefix.empty() || prefix.size() > 24 ||
            prefix.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") !=
                string::npos)
        {
            throw runtime_error("Invalid session account prefix");
        }
        const auto gameConfig = LoadServerConfig(configPath, server);
        const auto worldConfig = LoadServerConfig(configPath, "World");
        const CWinsockSession winsock;
        vector<stSessionSample> samples(count);
        atomic<int> next = 0;
        const auto start = chrono::steady_clock::now() + chrono::milliseconds(100);
        {
            vector<jthread> workers;
            for (int worker = 0; worker < min(concurrency, count); ++worker)
            {
                workers.emplace_back(
                    [&]
                    {
                        this_thread::sleep_until(start);
                        while (true)
                        {
                            const int index = next.fetch_add(1);
                            if (index >= count)
                            {
                                break;
                            }
                            auto& sample = samples[index];
                            const auto cycleStarted = chrono::steady_clock::now();
                            try
                            {
                                CClientConnection game(gameConfig, true);
                                wire::JoinGameRequest login;
                                login.set_name(prefix + "_" + to_string(index));
                                login.set_password("PortfolioBench!2026");
                                const auto loginStarted = chrono::steady_clock::now();
                                const auto joined =
                                    RequestMessage<wire::JoinGameReply>(game,
                                                                        Protocol::Type::CM_JoinGame,
                                                                        login,
                                                                        Protocol::Type::SM_JoinGameReply);
                                sample.loginMs = ElapsedMs(loginStarted);
                                if (joined.unit().player_id().empty())
                                {
                                    throw runtime_error("Empty login identity");
                                }
                                if (phase == "seed")
                                {
                                    // 계정 생성/최초 DB 연결 비용은 비교 구간에서 제외한다. Game 로그아웃은 전장 대기를 만들지 않는다.
                                    const auto reply =
                                        RequestMessage<wire::ActionReply>(game,
                                                                          Protocol::Type::CM_Logout,
                                                                          wire::EmptyRequest{},
                                                                          Protocol::Type::SM_ActionReply);
                                    if (!reply.ok())
                                    {
                                        throw runtime_error(reply.message());
                                    }
                                }
                                else
                                {
                                    const auto transferStarted = chrono::steady_clock::now();
                                    const auto ticket =
                                        RequestMessage<wire::WorldTicket>(game,
                                                                          Protocol::Type::CM_RequestTicket,
                                                                          wire::EmptyRequest{},
                                                                          Protocol::Type::SM_WorldTicket);
                                    CClientConnection world(worldConfig, true);
                                    wire::EnterWorldRequest enter;
                                    enter.set_claims(ticket.claims());
                                    enter.set_signature(ticket.signature());
                                    const auto entered =
                                        RequestMessage<wire::WorldReply>(world,
                                                                         Protocol::Type::CM_EnterWorld,
                                                                         enter,
                                                                         Protocol::Type::SM_WorldReply);
                                    if (!entered.ok() ||
                                        entered.unit().player_id() != joined.unit().player_id())
                                    {
                                        throw runtime_error("Session transfer failed");
                                    }
                                    sample.transferMs = ElapsedMs(transferStarted);
                                    const auto logoutStarted = chrono::steady_clock::now();
                                    const auto reply =
                                        RequestMessage<wire::ActionReply>(world,
                                                                          Protocol::Type::CM_Logout,
                                                                          wire::EmptyRequest{},
                                                                          Protocol::Type::SM_ActionReply);
                                    if (!reply.ok())
                                    {
                                        throw runtime_error(reply.message());
                                    }
                                    sample.logoutMs = ElapsedMs(logoutStarted);
                                }
                            }
                            catch (const exception& error)
                            {
                                sample.error = error.what();
                            }
                            sample.totalMs = ElapsedMs(cycleStarted);
                        }
                    });
            }
            this_thread::sleep_until(start);
            PrintPlayLine("[LOAD PHASE] measure_begin");
        }
        const double elapsed = ElapsedMs(start) / 1000;
        PrintPlayLine("[LOAD PHASE] measure_end");
        int errors = 0;
        for (const auto& sample : samples)
        {
            if (!sample.error.empty())
            {
                ++errors;
                PrintPlayLine("[LOAD ERROR] " + sample.error);
            }
        }
        if (!samplesPath.empty())
        {
            if (!samplesPath.parent_path().empty())
            {
                fs::create_directories(samplesPath.parent_path());
            }
            ofstream output(samplesPath);
            output << "Client,LoginMs,TransferMs,LogoutMs,TotalMs,Ok\n" << fixed << setprecision(6);
            for (int i = 0; i < count; ++i)
            {
                const auto& s = samples[i];
                output << i << ',' << s.loginMs << ',' << s.transferMs << ',' << s.logoutMs << ','
                       << s.totalMs << ',' << (s.error.empty() ? 1 : 0) << '\n';
            }
            if (!output)
            {
                throw runtime_error("Cannot save session samples");
            }
        }
        ostringstream row;
        row << "[SESSION] phase=" << phase << " clients=" << count << " concurrency=" << concurrency
            << " errors=" << errors << fixed << setprecision(6) << " seconds=" << elapsed
            << " cycles_per_second=" << (count - errors) / max(.001, elapsed);
        PrintPlayLine(row.str());
        return errors == 0 ? 0 : 1;
    }
    catch (const exception& error)
    {
        PrintPlayLine("[FAIL] " + string(error.what()));
        return 1;
    }
}
