#include "BattleClient.h"
#include "BattleWorker.h"
#include "ClientConnection.h"

int RunBattleClient(int argc, char* argv[])
{
    try
    {
        fs::path config = "Server.ini", report = "battle-result.txt";
        int perServer = 500, seconds = 240, workerCount = 8, admission = 16, setupTimeout = 1200;
        for (int i = 2; i < argc; i += 2)
        {
            if (i + 1 == argc)
            {
                throw runtime_error("Missing battle option value");
            }
            const string option = argv[i], value = argv[i + 1];
            if (option == "--config")
            {
                config = value;
            }
            else if (option == "--report")
            {
                report = value;
            }
            else if (option == "--per-server")
            {
                perServer = ParseInt(value, 1, 2048);
            }
            else if (option == "--seconds")
            {
                seconds = ParseInt(value, 1, 1800);
            }
            else if (option == "--workers")
            {
                workerCount = ParseInt(value, 1, 32);
            }
            else if (option == "--admission")
            {
                admission = ParseInt(value, 1, 32);
            }
            else if (option == "--setup-timeout")
            {
                setupTimeout = ParseInt(value, 30, 3600);
            }
            else
            {
                throw runtime_error("Unknown battle option: " + option);
            }
        }
        const auto world = LoadServerConfig(config, "World");
        vector<stServerConfig> games;
        for (const auto& server : world.gameServers)
        {
            games.push_back(LoadServerConfig(config, server.id));
        }
        const int total = static_cast<int>(games.size()) * perServer;
        if (total > world.maxWorldPlayers || total > 2048)
        {
            throw runtime_error("Battle exceeds World capacity");
        }
        for (const auto& game : games)
        {
            if (perServer > game.maxGamePlayers)
            {
                throw runtime_error("Battle exceeds game server capacity: " + game.id);
            }
        }
        const CWinsockSession winsock;
        stBattleCounters counters;
        vector<unique_ptr<CBattleWorker>> workers;
        for (int i = 0; i < workerCount; ++i)
        {
            workers.push_back(make_unique<CBattleWorker>(counters, world, admission));
        }
        for (int i = 0; i < total; ++i)
        {
            auto up_bot = make_unique<stBattleBot>();
            up_bot->index = i;
            up_bot->p_game = &games[i % games.size()];
            up_bot->account =
                "Battle_" + to_string(i % games.size() + 1) + "_" + to_string(i / games.size() + 1);
            workers[i % workerCount]->m_bots.push_back(move(up_bot));
        }
        vector<jthread> threads;
        for (auto& up_worker : workers)
        {
            threads.emplace_back(
                [&owner = *up_worker](stop_token stop)
                {
                    owner.Run(stop);
                });
        }
        const auto setupStarted = BattleClock::now();
        auto battleStarted = setupStarted, stoppedAt = setupStarted, logAt = setupStarted;
        int peak = 0;
        while (counters.done + counters.errors < total)
        {
            const auto now = BattleClock::now();
            peak = max(peak, counters.world.load());
            if (counters.errors)
            {
                break;
            }
            if (!counters.started && counters.ready == total)
            {
                battleStarted = now;
                counters.started = true;
                PrintPlayLine("[BATTLE START] ready=" + to_string(total) +
                              " world=" + to_string(counters.world.load()));
            }
            if (!counters.started && now - setupStarted > chrono::seconds(setupTimeout))
            {
                ++counters.errors;
                PrintPlayLine("[BATTLE ERROR] preparation timeout");
                break;
            }
            if (counters.started && !counters.stopping && now - battleStarted >= chrono::seconds(seconds))
            {
                counters.stopping = true;
                stoppedAt = now;
            }
            if (counters.stopping && now - stoppedAt > chrono::seconds(60))
            {
                ++counters.errors;
                PrintPlayLine("[BATTLE ERROR] logout timeout");
                break;
            }
            if (now >= logAt)
            {
                logAt = now + chrono::seconds(5);
                PrintPlayLine("[BATTLE] ready=" + to_string(counters.ready.load()) + "/" + to_string(total) +
                              " world=" + to_string(counters.world.load()) +
                              " deaths=" + to_string(counters.deaths.load()) +
                              " reentries=" + to_string(counters.reentries.load()) +
                              " unit_hits=" + to_string(counters.unitAttacks.load()) +
                              " skills=" + to_string(counters.skills.load()) +
                              " errors=" + to_string(counters.errors.load()));
            }
            this_thread::sleep_for(chrono::milliseconds(50));
        }
        for (auto& thread : threads)
        {
            thread.request_stop();
        }
        threads.clear();
        ostringstream result;
        result << "Requested=" << total << "\nReady=" << counters.ready << "\nPeakWorld=" << peak
               << "\nEntered=" << counters.entered << "\nDeaths=" << counters.deaths
               << "\nDeathReentries=" << counters.reentries << "\nResumedGame=" << counters.resumed
               << "\nUnitAttacks=" << counters.unitAttacks << "\nBuildingAttacks=" << counters.buildingAttacks
               << "\nSkills=" << counters.skills << "\nRejected=" << counters.rejected
               << "\nErrors=" << counters.errors << "\nLoggedOut=" << counters.done << '\n';
        ofstream output(report);
        output << result.str();
        if (!output)
        {
            throw runtime_error("Cannot write battle report");
        }
        PrintPlayLine("[BATTLE RESULT]\n" + result.str());
        return counters.errors == 0 && counters.ready == total && counters.done == total ? 0 : 1;
    }
    catch (const exception& error)
    {
        PrintPlayLine("[FAIL] " + string(error.what()));
        return 1;
    }
}
