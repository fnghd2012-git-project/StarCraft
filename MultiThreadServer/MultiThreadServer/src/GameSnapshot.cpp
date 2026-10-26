#include "GameLogic.h"

void CGameLogic::BroadcastWorldState()
{
    const auto started = chrono::steady_clock::now();
    const auto elapsed = [](chrono::steady_clock::time_point from)
    {
        return chrono::duration_cast<chrono::microseconds>(chrono::steady_clock::now() - from).count();
    };
    BuildCombatIndex();
    vector<const stPlayer*> visiblePlayers;
    vector<stSpatialPoint> points;
    for (const auto* p_player : m_combatPlayers)
    {
        if (!p_player->inWorld)
        {
            continue;
        }
        visiblePlayers.push_back(p_player);
        points.push_back({p_player->unit.x(), p_player->unit.y()});
    }
    if (m_config.snapshotRadius > 0)
    {
        m_spatialIndex.Rebuild(move(points),
                               m_config.spatialCellSize,
                               m_config.spatialSearch == SpatialSearchMode::Grid ||
                                   m_config.verifySpatialSearch);
    }
    m_spatialBuildMicros += elapsed(started);
    struct stSnapshotResult
    {
        vector<string> packets;
        size_t candidates = 0;
        long long selectUs = 0, encodeUs = 0;
    };
    vector<stSnapshotResult> results(visiblePlayers.size());
    const auto calculate = [&](size_t index)
    {
        stSnapshotResult result;
        const auto* p_receiver = visiblePlayers[index];
        vector<const stPlayer*> selected;
        auto begin = chrono::steady_clock::now();
        if (m_config.snapshotRadius > 0)
        {
            vector<size_t> candidates, comparison;
            const double x = p_receiver->unit.x(), y = p_receiver->unit.y();
            const bool grid = m_config.spatialSearch == SpatialSearchMode::Grid;
            result.candidates = grid ? m_spatialIndex.QueryGrid(x, y, m_config.snapshotRadius, candidates)
                                     : m_spatialIndex.QueryLinear(x, y, m_config.snapshotRadius, candidates);
            selected = SelectSnapshotPlayers(*p_receiver, visiblePlayers, candidates);
            if (m_config.verifySpatialSearch)
            {
                if (grid)
                {
                    m_spatialIndex.QueryLinear(x, y, m_config.snapshotRadius, comparison);
                }
                else
                {
                    m_spatialIndex.QueryGrid(x, y, m_config.snapshotRadius, comparison);
                }
                if (selected != SelectSnapshotPlayers(*p_receiver, visiblePlayers, comparison))
                {
                    throw runtime_error("Spatial snapshot mismatch");
                }
            }
        }
        else
        {
            for (const auto* p_candidate : visiblePlayers)
            {
                if (CanSee(*p_receiver, *p_candidate))
                {
                    selected.push_back(p_candidate);
                }
            }
        }
        result.selectUs = elapsed(begin);
        begin = chrono::steady_clock::now();
        vector<wire::WorldState> parts(1);
        for (const auto* p_candidate : selected)
        {
            if (parts.back().units_size() == 4)
            {
                parts.emplace_back();
            }
            *parts.back().add_units() = p_candidate->unit;
        }
        for (const auto& building : m_buildings)
        {
            *parts.back().add_buildings() = building;
        }
        for (const auto& area : m_areas)
        {
            auto* p_state = parts.back().add_areas();
            p_state->set_skill_index(area.p_skill->index);
            p_state->set_x(area.x);
            p_state->set_y(area.y);
            p_state->set_radius(area.p_skill->radius);
            p_state->set_remaining_ms(
                static_cast<unsigned>((area.until - m_tick) * 1000 / m_config.tickRate));
        }
        parts.back().set_complete(true);
        for (auto& part : parts)
        {
            part.set_tick(m_tick);
            part.set_round_running(m_roundRunning);
            part.set_winner(m_winner);
            result.packets.push_back(Protocol::EncodeMessage(Protocol::Type::SM_WorldState, part));
        }
        result.encodeUs = elapsed(begin);
        return result;
    };
    // 게임 상태를 고정한 채 수신자별 시야 선택과 Protobuf 직렬화를 독립적으로 수행한다.
    m_battleWorkers.Run(results.size(),
                        [&](size_t i)
                        {
                            results[i] = calculate(i);
                        });
    if (m_config.verifyParallel)
    {
        for (size_t i = 0; i < results.size(); ++i)
        {
            if (results[i].packets != calculate(i).packets)
            {
                throw runtime_error("Parallel snapshot bytes mismatch");
            }
            ++m_parallelVerified;
        }
    }
    for (size_t i = 0; i < results.size(); ++i)
    {
        auto& result = results[i];
        if (m_config.snapshotRadius > 0)
        {
            ++m_spatialQueries;
            if (m_config.verifySpatialSearch)
            {
                ++m_spatialVerified;
            }
        }
        m_spatialCandidates += result.candidates;
        m_spatialSelectMicros += result.selectUs;
        // encodeUs/selectUs는 워커 작업 시간의 합계다. 전체 지연은 snapshot_p95_us로 본다.
        m_snapshotSendMicros += result.encodeUs;
        for (auto& packet : result.packets)
        {
            SendPacket(visiblePlayers[i]->sp_channel, move(packet));
        }
    }
    if (m_snapshotMicros.size() < 60000)
    {
        m_snapshotMicros.push_back(elapsed(started));
    }
}
