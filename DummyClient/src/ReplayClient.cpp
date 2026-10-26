#include "ReplayClient.h"
#include "ReplayWindow.h"
#include "Replay.h"
#include "ClientConnection.h"

namespace
{
string TimeText(unsigned long long milliseconds)
{
    ostringstream text;
    text << fixed << setprecision(3) << static_cast<double>(milliseconds) / 1000;
    return text.str();
}
void ShowState(const Replay::stArchive& archive, size_t cursor)
{
    const auto state = archive.StateAt(cursor);
    PrintPlayLine("[STATE] time=" + TimeText(archive.frames[cursor].elapsed_ms()) +
                  " tick=" + to_string(state.tick()) + " units=" + to_string(state.units_size()) +
                  " winner=" + to_string(static_cast<int>(state.winner())));
    for (const auto& unit : state.units())
    {
        ostringstream line;
        line << "  " << unit.name() << '/' << unit.unit_name() << " XY=(" << unit.x() << ',' << unit.y()
             << ") HP=" << unit.hp() << '/' << unit.max_hp() << " Shield=" << unit.shield()
             << " MP=" << unit.mp() << '/' << unit.max_mp();
        if (unit.cloaked())
        {
            line << " Cloak";
        }
        if (unit.burrowed())
        {
            line << " Burrow";
        }
        if (unit.locked())
        {
            line << " Lockdown";
        }
        if (unit.blinded())
        {
            line << " Blind";
        }
        if (unit.stimulated())
        {
            line << " Stim";
        }
        if (unit.healing())
        {
            line << " Heal";
        }
        PrintPlayLine(line.str());
    }
    for (const auto& building : state.buildings())
    {
        PrintPlayLine("  " + building.name() + " HP=" + to_string(building.hp()) +
                      " Shield=" + to_string(building.shield()));
    }
    for (const auto& area : state.areas())
    {
        PrintPlayLine("  Area skill=" + to_string(area.skill_index()) +
                      " remaining_ms=" + to_string(area.remaining_ms()));
    }
}
void ShowActions(const wire::ReplayFrame& frame)
{
    for (const auto& action : frame.actions())
    {
        ostringstream text;
        text << '[' << TimeText(frame.elapsed_ms()) << "] " << action.name() << ' ' << action.kind();
        if (!action.accepted())
        {
            text << " [DENIED]";
        }
        if (action.has_move())
        {
            text << " -> (" << action.move().x() << ',' << action.move().y() << ')';
        }
        if (action.has_attack())
        {
            text << " target=" << action.attack().target_id();
        }
        if (action.has_skill())
        {
            text << " slot=" << action.skill().slot() << " target=" << action.skill().target_id() << " XY=("
                 << action.skill().x() << ',' << action.skill().y() << ") cancel=" << action.skill().cancel();
        }
        if (!action.message().empty())
        {
            text << ' ' << action.message();
        }
        PrintPlayLine(text.str());
    }
}
vector<fs::path> List(const fs::path& directory)
{
    vector<fs::path> files;
    if (fs::exists(directory))
    {
        for (const auto& entry : fs::directory_iterator(directory))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".replay")
            {
                files.push_back(entry.path());
            }
        }
    }
    sort(files.begin(), files.end());
    for (size_t i = 0; i < files.size(); ++i)
    {
        PrintPlayLine(to_string(i + 1) + ": " + files[i].filename().string() + " / " +
                      to_string(fs::file_size(files[i])) + " bytes");
    }
    return files;
}
void Play(const Replay::stArchive& archive)
{
    size_t cursor = 0;
    bool playing = false;
    double speed = 1, playhead = static_cast<double>(archive.frames.front().elapsed_ms());
    mutex playbackMutex;
    auto updated = chrono::steady_clock::now();
    ShowState(archive, cursor);
    PrintPlayLine(
        "[REPLAY 명령] play / pause / speed 0.25~16 / seek 초 / step [프레임수] / status / events / quit");
    // 입력과 재생 시계를 분리한다. 같은 잠금으로 커서 변경/출력을 직렬화한다.
    jthread playback(
        [&](stop_token stop)
        {
            while (!stop.stop_requested())
            {
                {
                    lock_guard lock(playbackMutex);
                    const auto now = chrono::steady_clock::now();
                    if (playing)
                    {
                        playhead += chrono::duration<double, milli>(now - updated).count() * speed;
                        while (cursor + 1 < archive.frames.size() &&
                               archive.frames[cursor + 1].elapsed_ms() <= playhead)
                        {
                            ++cursor;
                            ShowActions(archive.frames[cursor]);
                        }
                        if (cursor + 1 == archive.frames.size())
                        {
                            playing = false;
                            PrintPlayLine("[REPLAY] End");
                        }
                    }
                    updated = now;
                }
                this_thread::sleep_for(chrono::milliseconds(10));
            }
        });
    string line;
    while (getline(cin, line))
    {
        lock_guard lock(playbackMutex);
        try
        {
            istringstream input(line);
            string command;
            input >> command;
            if (command == "quit")
            {
                break;
            }
            if (command == "play")
            {
                if (cursor + 1 == archive.frames.size())
                {
                    cursor = 0;
                    playhead = static_cast<double>(archive.frames.front().elapsed_ms());
                }
                playing = true;
                updated = chrono::steady_clock::now();
                PrintPlayLine("[REPLAY] Playing speed=" + to_string(speed));
            }
            else if (command == "pause")
            {
                playing = false;
                PrintPlayLine("[REPLAY] Paused");
            }
            else if (command == "speed")
            {
                string value;
                input >> value;
                const double requested = ParseNumber(value);
                if (requested < 0.25 || requested > 16)
                {
                    throw runtime_error("Speed must be 0.25..16");
                }
                speed = requested;
                PrintPlayLine("[REPLAY] Speed=" + value);
            }
            else if (command == "seek")
            {
                string value;
                input >> value;
                const double seconds = ParseNumber(value);
                if (seconds < 0 || seconds > static_cast<double>(archive.frames.back().elapsed_ms()) / 1000)
                {
                    throw runtime_error("Seek outside recording");
                }
                cursor = archive.FindTime(static_cast<unsigned long long>(seconds * 1000));
                playhead = static_cast<double>(archive.frames[cursor].elapsed_ms());
                updated = chrono::steady_clock::now();
                ShowState(archive, cursor);
            }
            else if (command == "step")
            {
                string count;
                input >> count;
                const int steps = count.empty() ? 1 : ParseInt(count, 1, 1000);
                playing = false;
                for (int i = 0; i < steps && cursor + 1 < archive.frames.size(); ++i)
                {
                    ShowActions(archive.frames[++cursor]);
                }
                playhead = static_cast<double>(archive.frames[cursor].elapsed_ms());
                ShowState(archive, cursor);
            }
            else if (command == "status")
            {
                ShowState(archive, cursor);
            }
            else if (command == "events")
            {
                ShowActions(archive.frames[cursor]);
            }
            else
            {
                PrintPlayLine("[REPLAY] Unknown command");
            }
        }
        catch (const exception& error)
        {
            PrintPlayLine("[DENIED] " + string(error.what()));
        }
    }
}
} // namespace

int RunReplayClient(int argc, char* argv[])
{
    try
    {
        fs::path path, compare, imageFile, directory = "Replays";
        double imageSeconds = 0;
        bool verify = false, partial = false, console = false;
        string required;
        const string mode = argv[1];
        int start = 2;
        if (mode == "--replay")
        {
            if (argc < 3)
            {
                throw runtime_error("--replay requires a file");
            }
            path = argv[2];
            start = 3;
        }
        for (int i = start; i < argc; ++i)
        {
            const string option = argv[i];
            if (option == "--verify")
            {
                verify = true;
            }
            else if (option == "--console")
            {
                console = true;
            }
            else if (option == "--image" && i + 1 < argc)
            {
                imageFile = argv[++i];
            }
            else if (option == "--at" && i + 1 < argc)
            {
                imageSeconds = ParseNumber(argv[++i]);
            }
            else if (option == "--allow-partial")
            {
                partial = true;
            }
            else if (option == "--dir" && i + 1 < argc)
            {
                directory = argv[++i];
            }
            else if (option == "--compare" && i + 1 < argc)
            {
                compare = argv[++i];
            }
            else if (option == "--require" && i + 1 < argc)
            {
                required = argv[++i];
            }
            else
            {
                throw runtime_error("Unknown replay option: " + option);
            }
        }
        if (path.empty())
        {
            const auto files = List(directory);
            if (mode == "--replay-list")
            {
                return 0;
            }
            if (files.empty())
            {
                PrintPlayLine("완료된 리플레이가 없습니다. World에서 전투한 뒤 서버를 정상 종료하세요.");
                return 0;
            }
            PrintPlayLine("재생할 번호 입력 (q=종료):");
            string choice;
            if (!getline(cin, choice) || choice == "q")
            {
                return 0;
            }
            path = files[static_cast<size_t>(ParseInt(choice, 1, static_cast<int>(files.size()))) - 1];
        }
        PrintPlayLine("[REPLAY] 기록 파일을 읽고 검증하고 있습니다...");
        const auto archive = Replay::Load(path, partial);
        if (!compare.empty())
        {
            const auto size = fs::file_size(compare);
            if (size > Replay::MaxRecordBytes)
            {
                throw runtime_error("Live snapshot exceeds limit");
            }
            ifstream input(compare, ios::binary);
            string bytes(static_cast<size_t>(size), '\0');
            input.read(bytes.data(), static_cast<streamsize>(size));
            wire::WorldState live;
            if (!input || !live.ParseFromString(bytes))
            {
                throw runtime_error("Invalid live snapshot");
            }
            bool found = false;
            for (size_t i = 0; i < archive.frames.size(); ++i)
            {
                if (archive.frames[i].tick() != live.tick())
                {
                    continue;
                }
                if (archive.StateAt(i).SerializeAsString() != Replay::OrderedState(live).SerializeAsString())
                {
                    throw runtime_error("Live/replay state differs");
                }
                found = true;
                break;
            }
            if (!found)
            {
                throw runtime_error("Live tick not in this match");
            }
            PrintPlayLine("[OK] Live network snapshot matches replay tick=" + to_string(live.tick()));
        }
        unordered_map<string, unsigned long long> actions, deathTimes;
        unordered_map<string, string> participants;
        unordered_set<string> active;
        size_t peakUnits = 0, deathCount = 0, reentries = 0;
        unsigned long long shortestReentry = numeric_limits<unsigned long long>::max();
        for (const auto& frame : archive.frames)
        {
            if (frame.full())
            {
                active.clear();
            }
            for (const auto& id : frame.removed())
            {
                active.erase(id);
            }
            for (const auto& unit : frame.units())
            {
                active.insert(unit.player_id());
                participants[unit.player_id()] = unit.game_server();
            }
            peakUnits = max(peakUnits, active.size());
            for (const auto& action : frame.actions())
            {
                if (!action.accepted())
                {
                    continue;
                }
                ++actions[action.kind()];
                if (action.kind() == "Return" && action.message().find("Unit died") != string::npos)
                {
                    ++deathCount;
                    deathTimes[action.player_id()] = frame.elapsed_ms();
                }
                if (action.kind() == "Enter")
                {
                    const auto died = deathTimes.find(action.player_id());
                    if (died != deathTimes.end())
                    {
                        ++reentries;
                        shortestReentry = min(shortestReentry, frame.elapsed_ms() - died->second);
                        deathTimes.erase(died);
                    }
                }
            }
        }
        if (!required.empty() && !actions.count(required))
        {
            throw runtime_error("Required action missing: " + required);
        }
        PrintPlayLine("[OK] Replay verified match=" + archive.header.match_id() + " frames=" +
                      to_string(archive.frames.size()) + " snapshots=" + to_string(archive.snapshots.size()) +
                      " duration=" + TimeText(archive.frames.back().elapsed_ms()) +
                      " complete=" + (archive.complete ? "1" : "0") + " reason=" + archive.footer.reason() +
                      " winner=" + to_string(static_cast<int>(archive.frames.back().winner())));
        for (const auto& entry : actions)
        {
            PrintPlayLine("[ACTION] " + entry.first + "=" + to_string(entry.second));
        }
        unordered_map<string, size_t> byServer;
        for (const auto& player : participants)
        {
            ++byServer[player.second];
        }
        for (const auto& entry : byServer)
        {
            PrintPlayLine("[PARTICIPANTS] server=" + entry.first + " unique=" + to_string(entry.second));
        }
        PrintPlayLine("[BATTLE SUMMARY] peak_units=" + to_string(peakUnits) +
                      " deaths=" + to_string(deathCount) + " death_reentries=" + to_string(reentries) +
                      (reentries ? " min_reentry_ms=" + to_string(shortestReentry) : ""));
        if (!archive.complete)
        {
            PrintPlayLine("[PARTIAL] 마지막 정상 프레임까지만 검증했습니다. 전체 경기 완료 기록이 아닙니다.");
        }
        if (!verify)
        {
            if (console)
            {
                Play(archive);
            }
            else
            {
                RunReplayWindow(archive, imageFile, imageSeconds);
            }
        }
        return 0;
    }
    catch (const exception& error)
    {
        PrintPlayLine("[FAIL] " + string(error.what()));
        return 1;
    }
}
