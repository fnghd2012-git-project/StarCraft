#include "GameLogic.h"

void CGameLogic::StartReplay()
{
    if (m_config.type != "World" || !m_config.replayEnabled)
    {
        return;
    }
    m_replayStartupFailed = false;
    try
    {
        if (m_replayData.empty())
        {
            // 그 경기에 사용한 원본 데이터도 보관한다. 키/DB 자격 정보 파일은 포함하지 않는다.
            const array<fs::path, 6> paths = {m_config.unitFile,
                                              m_config.skillFile,
                                              m_config.mapFile,
                                              m_config.startPointFile,
                                              m_config.serverFile,
                                              m_config.configFile};
            for (const auto& path : paths)
            {
                const auto size = fs::file_size(path);
                if (size > 131072)
                {
                    throw runtime_error("Replay data source exceeds 128 KiB");
                }
                ifstream input(path, ios::binary);
                string bytes(static_cast<size_t>(size), '\0');
                input.read(bytes.data(), static_cast<streamsize>(size));
                if (!input)
                {
                    throw runtime_error("Cannot read replay data source");
                }
                wire::ReplayDataFile file;
                file.set_name(path.filename().string());
                file.set_content(bytes);
                file.set_checksum(Replay::Checksum(bytes));
                m_replayData.push_back(move(file));
            }
        }
        wire::ReplayHeader header;
        header.set_format_version(Replay::FormatVersion);
        header.set_match_id(m_matchId);
        header.set_started_at_ms(
            chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch())
                .count());
        header.set_tick_rate(m_config.tickRate);
        header.set_start_tick(m_tick);
        header.set_protocol_version(Protocol::Version);
        for (const auto& file : m_replayData)
        {
            *header.add_data_files() = file;
        }
        m_up_replay =
            make_unique<Replay::CRecorder>(m_config.replayDirectory,
                                           header,
                                           static_cast<size_t>(m_config.replayQueueMB) * 1024 * 1024,
                                           static_cast<size_t>(m_config.replayFileMB) * 1024 * 1024,
                                           m_config.replaySnapshotSeconds * m_config.tickRate);
        cout << "[REPLAY] Started match=" << header.match_id() << endl;
    }
    catch (const exception& error)
    {
        m_up_replay.reset();
        m_replayData.clear();
        m_replayStartupFailed = true;
        cerr << "[REPLAY FAIL] Startup: " << error.what() << endl;
    }
}

void CGameLogic::CaptureReplay()
{
    if (!m_up_replay)
    {
        return;
    }
    wire::WorldState state;
    state.set_tick(m_tick);
    state.set_round_running(m_roundRunning);
    state.set_winner(m_winner);
    for (const auto& entry : m_players)
    {
        if (entry.second.inWorld)
        {
            *state.add_units() = entry.second.unit;
        }
    }
    for (const auto& building : m_buildings)
    {
        *state.add_buildings() = building;
    }
    for (const auto& area : m_areas)
    {
        auto* p_effect = state.add_areas();
        p_effect->set_skill_index(area.p_skill->index);
        p_effect->set_x(area.x);
        p_effect->set_y(area.y);
        p_effect->set_radius(area.p_skill->radius);
        p_effect->set_remaining_ms(static_cast<unsigned>((area.until - m_tick) * 1000 / m_config.tickRate));
    }
    m_up_replay->Capture(state);
}

void CGameLogic::RecordReplayEvent(const stPlayer& player, const string& kind, const string& message)
{
    if (!m_up_replay)
    {
        return;
    }
    wire::ReplayAction action;
    action.set_kind(kind);
    action.set_player_id(player.unit.player_id());
    action.set_name(player.unit.name());
    action.set_accepted(true);
    action.set_message(message);
    m_up_replay->Action(move(action));
}

void CGameLogic::RecordReplayCommand(
    const stPlayer& player, Protocol::Type type, const string& payload, bool accepted, const string& error)
{
    if (!m_up_replay || player.unit.player_id().empty())
    {
        return;
    }
    wire::ReplayAction action;
    // 허용한 전투 필드만 복사한다. 전체 패킷 기록으로 로그인 비밀번호/티켓이 섞이지 않게 한다.
    if (type == Protocol::Type::CM_Move)
    {
        action.set_kind("Move");
        wire::MoveRequest request;
        if (accepted && request.ParseFromString(payload))
        {
            action.mutable_move()->set_x(request.x());
            action.mutable_move()->set_y(request.y());
        }
    }
    else if (type == Protocol::Type::CM_Attack)
    {
        action.set_kind("Attack");
        wire::AttackRequest request;
        if (accepted && request.ParseFromString(payload))
        {
            action.mutable_attack()->set_target_id(request.target_id());
        }
    }
    else if (type == Protocol::Type::CM_Skill)
    {
        action.set_kind("Skill");
        wire::SkillRequest request;
        if (accepted && request.ParseFromString(payload))
        {
            auto* p_skill = action.mutable_skill();
            p_skill->set_slot(request.slot());
            p_skill->set_target_id(request.target_id().substr(0, 64));
            p_skill->set_x(request.x());
            p_skill->set_y(request.y());
            p_skill->set_cancel(request.cancel());
        }
    }
    else
    {
        return;
    }
    action.set_player_id(player.unit.player_id());
    action.set_name(player.unit.name());
    action.set_accepted(accepted);
    action.set_message(error.substr(0, 256));
    m_up_replay->Action(move(action));
}

void CGameLogic::ReportMetrics()
{
    const auto now = chrono::steady_clock::now();
    if (now - m_metricsAt < chrono::seconds(m_config.metricsSeconds))
    {
        return;
    }
    sort(m_tickMicros.begin(), m_tickMicros.end());
    const auto p95 = m_tickMicros.empty() ? 0 : m_tickMicros[(m_tickMicros.size() - 1) * 95 / 100];
    const auto maximum = m_tickMicros.empty() ? 0 : m_tickMicros.back();
    sort(m_snapshotMicros.begin(), m_snapshotMicros.end());
    const auto snapshotP95 =
        m_snapshotMicros.empty() ? 0 : m_snapshotMicros[(m_snapshotMicros.size() - 1) * 95 / 100];
    const auto snapshotMax = m_snapshotMicros.empty() ? 0 : m_snapshotMicros.back();
    size_t active = 0;
    for (const auto& entry : m_players)
    {
        if (entry.second.p_definition)
        {
            ++active;
        }
    }
    cout << "[METRICS] server=" << m_config.id << " active=" << active << " commands=" << m_commandCount
         << " rejected=" << m_commandRejected << " queue_peak=" << m_commandQueuePeak
         << " tick_p95_us=" << p95 << " tick_max_us=" << maximum << " skipped_ticks=" << m_missedTicks
         << " db_outstanding=" << m_database.Outstanding()
         << " replay_queue_bytes=" << (m_up_replay ? m_up_replay->QueueBytes() : 0)
         << " spatial=" << SpatialSearchName(m_config.spatialSearch)
         << " spatial_queries=" << m_spatialQueries << " spatial_candidates=" << m_spatialCandidates
         << " spatial_verified=" << m_spatialVerified << " spatial_build_us=" << m_spatialBuildMicros
         << " spatial_select_us=" << m_spatialSelectMicros << " snapshot_send_us=" << m_snapshotSendMicros
         << " snapshot_p95_us=" << snapshotP95 << " snapshot_max_us=" << snapshotMax << " "
         << m_battleWorkers.Metrics() << " parallel_verified=" << m_parallelVerified
         << " battle_calculate_us=" << m_battleCalculateMicros << " battle_apply_us=" << m_battleApplyMicros
         << " replay_failed=" << (m_replayStartupFailed || (m_up_replay && m_up_replay->Failed()) ? 1 : 0)
         << endl;
    m_parallelVerified = 0;
    m_battleCalculateMicros = 0;
    m_battleApplyMicros = 0;
    m_tickMicros.clear();
    m_commandCount = 0;
    m_commandRejected = 0;
    m_commandQueuePeak = 0;
    m_missedTicks = 0;
    m_metricsAt = now;
    m_snapshotMicros.clear();
    m_spatialQueries = 0;
    m_spatialCandidates = 0;
    m_spatialVerified = 0;
    m_spatialBuildMicros = 0;
    m_spatialSelectMicros = 0;
    m_snapshotSendMicros = 0;
}
