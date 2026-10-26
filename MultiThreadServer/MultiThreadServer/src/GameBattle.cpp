#include "GameLogic.h"

CGameLogic::stPlayer* CGameLogic::FindPlayer(const string& id) const
{
    const auto found = m_playersById.find(id);
    return found != m_playersById.end() && found->second->inWorld ? found->second : nullptr;
}

void CGameLogic::BuildCombatIndex()
{
    if (!m_combatIndexDirty)
    {
        return;
    }
    m_combatPlayers.clear();
    for (auto& entry : m_players)
    {
        if (entry.second.inWorld)
        {
            m_combatPlayers.push_back(&entry.second);
        }
    }
    // 같은 입력이면 워커 수와 관계없이 적용 순서가 같아야 한다.
    sort(m_combatPlayers.begin(),
         m_combatPlayers.end(),
         [](const stPlayer* p_a, const stPlayer* p_b)
         {
             return p_a->unit.player_id() < p_b->unit.player_id();
         });
    vector<stSpatialPoint> points;
    points.reserve(m_combatPlayers.size());
    for (const auto* p_player : m_combatPlayers)
    {
        points.push_back({p_player->unit.x(), p_player->unit.y()});
    }
    m_combatIndex.Rebuild(move(points),
                          m_config.spatialCellSize,
                          m_config.spatialSearch == SpatialSearchMode::Grid || m_config.verifySpatialSearch);
    m_combatIndexDirty = false;
}

vector<size_t> CGameLogic::FindTargetsInRange(double x, double y, double radius) const
{
    vector<size_t> selected;
    if (m_config.spatialSearch == SpatialSearchMode::Grid)
    {
        m_combatIndex.QueryGrid(x, y, radius, selected);
    }
    else
    {
        m_combatIndex.QueryLinear(x, y, radius, selected);
    }
    sort(selected.begin(), selected.end());
    if (m_config.verifySpatialSearch)
    {
        vector<size_t> check;
        m_combatIndex.QueryLinear(x, y, radius, check);
        if (selected != check)
        {
            throw runtime_error("Combat range search mismatch");
        }
    }
    return selected;
}

void CGameLogic::MovePlayers()
{
    BuildCombatIndex();
    vector<stPlayer*> players;
    vector<stMovementInput> inputs;
    // 메인에서 이번 틱의 이동/MP 계산에 필요한 값만 입력 목록으로 복사한다.
    for (auto* p_player : m_combatPlayers)
    {
        if (!p_player->inWorld)
        {
            continue;
        }
        if (m_buildings[static_cast<size_t>(p_player->unit.race()) - 1].hp() == 0)
        {
            ReturnToGame(*p_player, "Faction building destroyed", false);
            continue;
        }
        const auto& unit = p_player->unit;
        const int regen =
            m_tick % m_config.tickRate == 0 && !(unit.cloaked() && m_config.pauseMpRegenWhileCloaked)
                ? m_config.mpRegenPerSecond
                : 0;
        players.push_back(p_player);
        inputs.push_back(
            {unit.x(),
             unit.y(),
             p_player->goalX,
             p_player->goalY,
             p_player->p_definition->speed * (p_player->p_stim ? p_player->p_stim->movePercent : 100) / 100 /
                 m_config.tickRate,
             p_player->moving,
             unit.mp(),
             p_player->p_definition->mp,
             regen});
    }
    vector<stMovementResult> results(inputs.size());
    auto started = chrono::steady_clock::now();
    // 워커는 입력을 읽고 자기 결과 칸만 쓴다. 실제 Player는 여기서 변경하지 않는다.
    m_battleWorkers.Run(inputs.size(),
                        [&](size_t i)
                        {
                            results[i] = CalculateMovement(inputs[i], m_map, m_buildings);
                        });
    m_battleCalculateMicros +=
        chrono::duration_cast<chrono::microseconds>(chrono::steady_clock::now() - started).count();
    if (m_config.verifyParallel)
    {
        for (size_t i = 0; i < inputs.size(); ++i)
        {
            if (results[i] != CalculateMovement(inputs[i], m_map, m_buildings))
            {
                throw runtime_error("Parallel movement mismatch");
            }
            ++m_parallelVerified;
        }
    }
    started = chrono::steady_clock::now();
    // 모든 워커의 계산이 끝난 뒤 메인에서 결과를 순서대로 게임 상태에 반영한다.
    for (size_t i = 0; i < players.size(); ++i)
    {
        players[i]->unit.set_x(results[i].x);
        players[i]->unit.set_y(results[i].y);
        players[i]->unit.set_mp(results[i].mp);
        players[i]->moving = results[i].moving;
    }
    m_combatIndexDirty = true;
    m_battleApplyMicros +=
        chrono::duration_cast<chrono::microseconds>(chrono::steady_clock::now() - started).count();
}
