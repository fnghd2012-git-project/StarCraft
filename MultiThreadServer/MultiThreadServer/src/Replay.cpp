#include "Replay.h"

namespace
{
constexpr char Magic[] = "MTSRPL01";
void Append(string& bytes, unsigned long long value, int count)
{
    for (int i = 0; i < count; ++i)
    {
        bytes += static_cast<char>((value >> (i * 8)) & 255);
    }
}
unsigned long long Number(const char* p_bytes, int count)
{
    unsigned long long value = 0;
    for (int i = 0; i < count; ++i)
    {
        value |= static_cast<unsigned long long>(static_cast<unsigned char>(p_bytes[i])) << (i * 8);
    }
    return value;
}
void Require(bool condition, const char* p_message)
{
    if (!condition)
    {
        throw runtime_error(string("Replay: ") + p_message);
    }
}
bool Position(double x, double y)
{
    return isfinite(x) && isfinite(y) && x >= 0 && y >= 0 && x <= 1000000 && y <= 1000000;
}
} // namespace

unsigned long long Replay::Checksum(string_view bytes)
{
    unsigned long long hash = 14695981039346656037ULL;
    for (const unsigned char value : bytes)
    {
        hash = (hash ^ value) * 1099511628211ULL;
    }
    return hash;
}

wire::WorldState Replay::OrderedState(const wire::WorldState& source)
{
    wire::WorldState state = source;
    // unordered_map의 순회 순서와 무관한 바이트열을 만들어 녹화/재생 결과를 비교한다.
    vector<wire::UnitState> units(source.units().begin(), source.units().end());
    sort(units.begin(),
         units.end(),
         [](const auto& a, const auto& b)
         {
             return a.player_id() < b.player_id();
         });
    state.clear_units();
    for (const auto& unit : units)
    {
        *state.add_units() = unit;
    }
    state.set_complete(true);
    return state;
}

void Replay::Apply(const wire::ReplayFrame& frame, wire::WorldState& state)
{
    Require(frame.units_size() <= MaxUnits && frame.removed_size() <= MaxUnits &&
                frame.buildings_size() == 3 && frame.areas_size() <= 32 && frame.actions_size() <= 4096,
            "frame limits");
    unordered_map<string, wire::UnitState> units;
    if (!frame.full())
    {
        for (const auto& unit : state.units())
        {
            units.emplace(unit.player_id(), unit);
        }
    }
    unordered_set<string> changed;
    for (const auto& id : frame.removed())
    {
        Require(!frame.full() && changed.insert(id).second && units.erase(id) == 1, "invalid removed unit");
    }
    for (const auto& unit : frame.units())
    {
        Require(unit.player_id().size() == 32 && unit.name().size() <= 32 && unit.unit_name().size() <= 64 &&
                    changed.insert(unit.player_id()).second && Position(unit.x(), unit.y()) &&
                    unit.hp() >= 0 && unit.hp() <= unit.max_hp() && unit.mp() >= 0 &&
                    unit.mp() <= unit.max_mp() && unit.shield() >= 0 && unit.race() >= 1 && unit.race() <= 3,
                "invalid unit state");
        units[unit.player_id()] = unit;
    }
    Require(units.size() <= MaxUnits, "too many replay units");
    state.Clear();
    state.set_tick(frame.tick());
    state.set_round_running(frame.round_running());
    state.set_winner(frame.winner());
    for (const auto& entry : units)
    {
        *state.add_units() = entry.second;
    }
    unordered_set<string> buildings;
    for (const auto& building : frame.buildings())
    {
        Require(buildings.insert(building.id()).second && Position(building.x(), building.y()) &&
                    building.hp() >= 0 && building.shield() >= 0 && isfinite(building.radius()) &&
                    building.radius() > 0,
                "invalid building state");
        *state.add_buildings() = building;
    }
    for (const auto& area : frame.areas())
    {
        Require(Position(area.x(), area.y()) && isfinite(area.radius()) && area.radius() > 0,
                "invalid area state");
        *state.add_areas() = area;
    }
    state = OrderedState(state);
    Require(Checksum(state.SerializeAsString()) == frame.state_checksum(),
            "reconstructed state checksum mismatch");
}

wire::WorldState Replay::stArchive::StateAt(size_t index) const
{
    if (index >= frames.size())
    {
        throw runtime_error("Replay frame index out of range");
    }
    size_t first = 0;
    for (const auto snapshot : snapshots)
    {
        if (snapshot > index)
        {
            break;
        }
        first = snapshot;
    }
    wire::WorldState state;
    for (size_t i = first; i <= index; ++i)
    {
        Apply(frames[i], state);
    }
    return state;
}

size_t Replay::stArchive::FindTime(unsigned long long milliseconds) const
{
    const auto found = upper_bound(frames.begin(),
                                   frames.end(),
                                   milliseconds,
                                   [](auto time, const auto& frame)
                                   {
                                       return time < frame.elapsed_ms();
                                   });
    return found == frames.begin() ? 0 : static_cast<size_t>(found - frames.begin() - 1);
}

Replay::stArchive Replay::Load(const fs::path& path, bool allowPartial)
{
    Require(fs::file_size(path) <= MaxFileBytes, "file exceeds 1 GiB limit");
    ifstream input(path, ios::binary);
    char magic[8]{};
    input.read(magic, 8);
    Require(input.gcount() == 8 && string_view(magic, 8) == string_view(Magic, 8), "invalid file signature");
    stArchive archive;
    wire::WorldState state;
    bool hasHeader = false;
    while (true)
    {
        char prefix[12]{};
        input.read(prefix, 12);
        Require(!input.bad(), "file read failed");
        if (input.gcount() == 0)
        {
            break;
        }
        Require(!archive.complete, "data after footer");
        if (input.gcount() != 12)
        {
            Require(allowPartial, "truncated record header");
            break;
        }
        const auto size = Number(prefix, 4);
        Require(size > 0 && size <= MaxRecordBytes, "record size exceeds limit");
        string bytes(static_cast<size_t>(size), '\0');
        input.read(bytes.data(), static_cast<streamsize>(size));
        Require(!input.bad(), "file read failed");
        if (static_cast<unsigned long long>(input.gcount()) != size)
        {
            Require(allowPartial, "truncated record body");
            break;
        }
        Require(Checksum(bytes) == Number(prefix + 4, 8), "record checksum mismatch");
        wire::ReplayRecord record;
        Require(record.ParseFromString(bytes) && !archive.complete, "invalid record or data after footer");
        if (record.has_header())
        {
            Require(!hasHeader && archive.frames.empty(), "duplicate header");
            archive.header = record.header();
            hasHeader = true;
            Require(archive.header.format_version() == FormatVersion && archive.header.tick_rate() > 0 &&
                        archive.header.tick_rate() <= 1000 && archive.header.match_id().size() == 32 &&
                        archive.header.data_files_size() <= 8,
                    "unsupported replay header");
            unordered_set<string> names;
            for (const auto& data : archive.header.data_files())
            {
                Require(names.insert(data.name()).second && data.content().size() <= 131072 &&
                            Checksum(data.content()) == data.checksum(),
                        "data file checksum/limit");
            }
        }
        else if (record.has_frame())
        {
            const auto& frame = record.frame();
            Require(hasHeader && archive.frames.size() < 500000 &&
                        frame.sequence() == archive.frames.size() + 1 &&
                        frame.tick() >= archive.header.start_tick() &&
                        frame.elapsed_ms() ==
                            (frame.tick() - archive.header.start_tick()) * 1000 / archive.header.tick_rate(),
                    "frame sequence/time mismatch");
            Require(archive.frames.empty() ? frame.full() : frame.tick() > archive.frames.back().tick(),
                    "frame tick order");
            Apply(frame, state);
            if (frame.full())
            {
                archive.snapshots.push_back(archive.frames.size());
            }
            archive.frames.push_back(frame);
        }
        else if (record.has_footer())
        {
            archive.footer = record.footer();
            Require(hasHeader && !archive.frames.empty() &&
                        archive.footer.frame_count() == archive.frames.size() &&
                        archive.footer.final_checksum() == archive.frames.back().state_checksum(),
                    "footer result mismatch");
            archive.complete = true;
        }
        else
        {
            throw runtime_error("Replay: empty record");
        }
    }
    Require(hasHeader && !archive.frames.empty(), "no replay frames");
    Require(archive.complete || allowPartial,
            "incomplete file; use --allow-partial to inspect validated prefix");
    return archive;
}

Replay::CRecorder::CRecorder(fs::path directory,
                             const wire::ReplayHeader& header,
                             size_t queueBytes,
                             size_t fileBytes,
                             unsigned snapshotTicks)
    : m_path(move(directory) / (header.match_id() + ".replay.part")), m_tickRate(header.tick_rate()),
      m_snapshotTicks(snapshotTicks), m_startTick(header.start_tick()), m_queueLimit(queueBytes),
      m_fileLimit(fileBytes)
{
    Require(m_tickRate > 0 && m_snapshotTicks > 0 && m_fileLimit <= MaxFileBytes, "invalid recorder limits");
    wire::ReplayRecord record;
    *record.mutable_header() = header;
    Enqueue(record);
    // 리플레이 파일 기록용 스레드 하나를 생성한다. 메인은 기록할 내용을 큐에 전달한다.
    // 헤더 생성이 실패하면 아직 기다리는 파일 스레드가 없도록 마지막에 시작한다.
    m_writer = jthread(
        [this]
        {
            Run();
        });
}

Replay::CRecorder::~CRecorder()
{
    // 정상 종료에서도 미완료 경기를 round_finished로 표시하지 않는다.
    if (!m_finished)
    {
        Finish("server_shutdown");
    }
    if (m_writer.joinable())
    {
        m_writer.join();
    }
}

void Replay::CRecorder::Fail(const string& message)
{
    if (!m_failed.exchange(true))
    {
        cerr << "[REPLAY FAIL] " << message << " / " << m_path.filename().string() << endl;
    }
    {
        lock_guard lock(m_recordMutex);
        m_stopping = true;
    }
    m_ready.notify_one();
}

bool Replay::CRecorder::Enqueue(const wire::ReplayRecord& record)
{
    if (m_failed.load())
    {
        return false;
    }
    if (record.ByteSizeLong() > MaxRecordBytes)
    {
        Fail("Record size limit");
        return false;
    }
    const string payload = record.SerializeAsString();
    string bytes;
    Append(bytes, payload.size(), 4);
    Append(bytes, Checksum(payload), 8);
    bytes += payload;
    {
        lock_guard lock(m_recordMutex);
        if (m_stopping)
        {
            return false;
        }
        if (m_queueBytes + bytes.size() <= m_queueLimit && m_totalBytes + bytes.size() <= m_fileLimit)
        {
            m_queueBytes += bytes.size();
            m_totalBytes += bytes.size();
            m_queue.push_back(move(bytes));
            m_ready.notify_one();
            return true;
        }
    }
    Fail("Queue/file limit reached; recording remains incomplete");
    return false;
}

void Replay::CRecorder::Action(wire::ReplayAction action)
{
    if (m_failed.load() || m_finished)
    {
        return;
    }
    if (m_actions.size() >= 4096)
    {
        Fail("Too many actions in one tick");
        return;
    }
    m_actions.push_back(move(action));
}

void Replay::CRecorder::Capture(const wire::WorldState& source)
{
    if (m_failed.load() || m_finished)
    {
        return;
    }
    const auto state = OrderedState(source);
    wire::ReplayRecord record;
    auto& frame = *record.mutable_frame();
    frame.set_sequence(++m_sequence);
    frame.set_tick(state.tick());
    frame.set_elapsed_ms((state.tick() - m_startTick) * 1000 / m_tickRate);
    // 기록 주기가 시뮬레이션 틱과 달라도 일정 간격의 탐색용 스냅샷을 남긴다.
    frame.set_full(m_sequence == 1 || state.tick() - m_lastSnapshotTick >= m_snapshotTicks);
    if (frame.full())
    {
        m_lastSnapshotTick = state.tick();
    }
    unordered_map<string, string> next;
    for (const auto& unit : state.units())
    {
        string bytes = unit.SerializeAsString();
        const auto old = m_previous.find(unit.player_id());
        if (frame.full() || old == m_previous.end() || old->second != bytes)
        {
            *frame.add_units() = unit;
        }
        next.emplace(unit.player_id(), move(bytes));
    }
    if (!frame.full())
    {
        for (const auto& old : m_previous)
        {
            if (!next.count(old.first))
            {
                frame.add_removed(old.first);
            }
        }
    }
    m_previous = move(next);
    for (const auto& building : state.buildings())
    {
        *frame.add_buildings() = building;
    }
    for (const auto& area : state.areas())
    {
        *frame.add_areas() = area;
    }
    frame.set_round_running(state.round_running());
    frame.set_winner(state.winner());
    for (auto& action : m_actions)
    {
        *frame.add_actions() = move(action);
    }
    m_actions.clear();
    m_finalChecksum = Checksum(state.SerializeAsString());
    frame.set_state_checksum(m_finalChecksum);
    Enqueue(record);
}

void Replay::CRecorder::Finish(const string& reason)
{
    if (m_finished)
    {
        return;
    }
    m_finished = true;
    if (!m_failed.load() && m_sequence > 0)
    {
        wire::ReplayRecord record;
        auto& footer = *record.mutable_footer();
        footer.set_frame_count(m_sequence);
        footer.set_final_checksum(m_finalChecksum);
        footer.set_reason(reason);
        Enqueue(record);
    }
    else
    {
        m_failed.store(true);
    }
    {
        lock_guard lock(m_recordMutex);
        m_stopping = true;
    }
    m_ready.notify_one();
}

size_t Replay::CRecorder::QueueBytes()
{
    lock_guard lock(m_recordMutex);
    return m_queueBytes;
}

void Replay::CRecorder::Run()
{
    try
    {
        fs::create_directories(m_path.parent_path());
        Require(!fs::exists(m_path), "output already exists");
        ofstream output(m_path, ios::binary);
        Require(output.good(), "cannot open output");
        output.write(Magic, 8);
        auto flushAt = chrono::steady_clock::now() + chrono::seconds(1);
        while (true)
        {
            string bytes;
            {
                unique_lock lock(m_recordMutex);
                m_ready.wait_until(lock,
                                   flushAt,
                                   [this]
                                   {
                                       return m_stopping || !m_queue.empty();
                                   });
                if (m_queue.empty() && m_stopping)
                {
                    break;
                }
                if (!m_queue.empty())
                {
                    bytes = move(m_queue.front());
                    m_queue.pop_front();
                }
            }
            if (!bytes.empty())
            {
                // 큐 잠금을 푼 뒤 디스크에 쓴다. 파일 쓰기를 기다리는 동안에도 메인은 기록을 넣을 수 있다.
                output.write(bytes.data(), static_cast<streamsize>(bytes.size()));
                {
                    lock_guard lock(m_recordMutex);
                    m_queueBytes -= bytes.size();
                }
            }
            Require(output.good(), "disk write failed");
            if (chrono::steady_clock::now() >= flushAt)
            {
                output.flush();
                Require(output.good(), "disk flush failed");
                flushAt = chrono::steady_clock::now() + chrono::seconds(1);
            }
        }
        output.flush();
        Require(output.good(), "final flush failed");
        output.close();
        if (!m_failed.load())
        {
            fs::path completed = m_path;
            completed.replace_extension();
            fs::rename(m_path, completed);
            cout << "[REPLAY] Saved " << completed.string() << endl;
        }
    }
    catch (const exception& error)
    {
        Fail(error.what());
    }
}
