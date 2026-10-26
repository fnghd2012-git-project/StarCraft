#include "GameLogic.h"

namespace
{
double Distance(double ax, double ay, double bx, double by)
{
    return sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by));
}

bool SameTeam(const stSkillDefinition& skill, wire::Race source, wire::Race target)
{
    return source == target ? skill.allies : skill.enemies;
}
} // namespace

unsigned long long CGameLogic::SkillTicks(int milliseconds) const
{
    // 모든 스킬 시간은 World 틱으로 올림한다. 실제 처리보다 이른 만료/연타를 허용하지 않는다.
    return (static_cast<unsigned long long>(milliseconds) * m_config.tickRate + 999) / 1000;
}

void CGameLogic::ClearCombat(stPlayer& player)
{
    player.p_stim = nullptr;
    player.p_cloak = nullptr;
    player.p_heal = nullptr;
    player.stimUntil = 0;
    player.lockedUntil = 0;
    player.burrowAt = 0;
    player.nextHeal = 0;
    player.nextStormDamage = 0;
    player.healTarget.clear();
    player.skillCooldowns.clear();
    player.plague = {};
    player.unit.set_cloaked(false);
    player.unit.set_burrowed(false);
    player.unit.set_locked(false);
    player.unit.set_blinded(false);
    player.unit.set_stimulated(false);
    player.unit.set_healing(false);
}

bool CGameLogic::SkillAffects(const stSkillDefinition& skill, wire::Race source, const stPlayer& target) const
{
    if (!target.inWorld || target.unit.hp() <= 0 || !SameTeam(skill, source, target.unit.race()))
    {
        return false;
    }
    if (skill.filter == "Mechanical" && !target.p_definition->mechanical)
    {
        return false;
    }
    if (skill.filter == "BiologicalGround" && !target.p_definition->biological)
    {
        return false;
    }
    return true; // 현재 데이터의 유닛은 모두 지상이다.
}

bool CGameLogic::CanSee(const stPlayer& viewer, const stPlayer& target) const
{
    if (viewer.unit.race() == target.unit.race())
    {
        return true;
    }
    if (viewer.unit.blinded() &&
        Distance(viewer.unit.x(), viewer.unit.y(), target.unit.x(), target.unit.y()) > viewer.blindSight)
    {
        return false;
    }
    if (!target.unit.cloaked() && !target.unit.burrowed())
    {
        return true;
    }
    // 은신 좌표를 모든 클라이언트에 보내지 않는다. 같은 진영의 살아 있는 탐지 수단만 확인한다.
    for (const auto& entry : m_players)
    {
        const auto& detector = entry.second;
        if (!detector.inWorld || detector.unit.race() != viewer.unit.race() || detector.unit.blinded() ||
            detector.p_definition->detection <= 0)
        {
            continue;
        }
        if (Distance(detector.unit.x(), detector.unit.y(), target.unit.x(), target.unit.y()) <=
            detector.p_definition->detection)
        {
            return true;
        }
    }
    for (const auto& building : m_buildings)
    {
        if (building.hp() > 0 && building.race() == viewer.unit.race() &&
            m_config.buildingDetectionRange > 0 &&
            Distance(building.x(), building.y(), target.unit.x(), target.unit.y()) <=
                m_config.buildingDetectionRange)
        {
            return true;
        }
    }
    return false;
}

bool CGameLogic::InDarkSwarm(const stPlayer& target) const
{
    for (const auto& area : m_areas)
    {
        if (area.p_skill->type == "DarkSwarm" && m_tick < area.until &&
            SkillAffects(*area.p_skill, area.race, target) &&
            Distance(area.x, area.y, target.unit.x(), target.unit.y()) <= area.p_skill->radius)
        {
            return true;
        }
    }
    return false;
}

void CGameLogic::HandleSkill(stPlayer& player, const wire::SkillRequest& request)
{
    if (request.cancel())
    {
        player.p_heal = nullptr;
        player.unit.set_healing(false);
        player.burrowAt = 0;
        SendActionReply(player, true, "Skill channel/cast cancelled");
        return;
    }
    if (request.slot() < 1 || request.slot() > 5)
    {
        throw runtime_error("Skill slot must be 1..5");
    }
    const int skillId = player.p_definition->skills[request.slot() - 1];
    const auto found = m_data.skillDefinitions.find(skillId);
    if (found == m_data.skillDefinitions.end())
    {
        throw runtime_error("Empty skill slot");
    }
    const auto& skill = found->second;
    if (player.lockedUntil > m_tick || player.burrowAt > m_tick ||
        (player.unit.burrowed() && skill.type != "Burrow"))
    {
        throw runtime_error("Unit cannot cast while locked or burrowed");
    }
    if (player.skillCooldowns[skillId] > m_tick)
    {
        throw runtime_error("Skill cooldown");
    }
    const bool toggleOff = (skill.type == "Cloak" && player.unit.cloaked()) ||
                           (skill.type == "Burrow" && player.unit.burrowed());
    if (!toggleOff && (player.unit.mp() < skill.mpCost || player.unit.hp() <= skill.hpCost))
    {
        throw runtime_error("Not enough MP/HP for skill");
    }
    stPlayer* p_target = &player;
    if (skill.targetType == "Unit")
    {
        p_target = FindPlayer(request.target_id());
        if (!p_target || !SkillAffects(skill, player.unit.race(), *p_target) || !CanSee(player, *p_target))
        {
            throw runtime_error("Invalid or hidden skill target");
        }
        const double range =
            player.unit.blinded() ? min(skill.range, static_cast<double>(player.blindSight)) : skill.range;
        if (Distance(player.unit.x(), player.unit.y(), p_target->unit.x(), p_target->unit.y()) > range)
        {
            throw runtime_error("Skill target out of range");
        }
        if (skill.type == "Heal" && p_target->unit.hp() >= p_target->unit.max_hp())
        {
            throw runtime_error("Target HP is already full");
        }
    }
    if (skill.targetType == "Point")
    {
        const double range =
            player.unit.blinded() ? min(skill.range, static_cast<double>(player.blindSight)) : skill.range;
        if (!isfinite(request.x()) || !isfinite(request.y()) || request.x() < m_map.minX ||
            request.x() > m_map.maxX || request.y() < m_map.minY || request.y() > m_map.maxY ||
            Distance(player.unit.x(), player.unit.y(), request.x(), request.y()) > range)
        {
            throw runtime_error("Skill point outside map or range");
        }
        if (skill.type != "Plague" && m_areas.size() >= 32)
        {
            throw runtime_error("Too many active skill areas");
        }
    }

    // 검증이 끝난 뒤에만 비용을 차감한다. Heal은 실제 치료 틱마다 MP를 소비한다.
    if (!toggleOff)
    {
        if (skill.type != "Heal")
        {
            player.unit.set_mp(player.unit.mp() - skill.mpCost);
        }
        player.unit.set_hp(player.unit.hp() - skill.hpCost);
    }
    player.skillCooldowns[skillId] = m_tick + SkillTicks(skill.cooldownMs);
    player.p_heal = nullptr;
    player.unit.set_healing(false);
    if (skill.type == "Stim")
    {
        player.p_stim = &skill;
        player.stimUntil = m_tick + SkillTicks(skill.durationMs);
        player.unit.set_stimulated(true);
    }
    else if (skill.type == "Cloak")
    {
        player.p_cloak = toggleOff ? nullptr : &skill;
        player.unit.set_cloaked(!toggleOff);
        player.cloakAt = m_tick + m_config.tickRate;
    }
    else if (skill.type == "Burrow")
    {
        player.moving = false;
        player.burrowTarget = !toggleOff;
        player.burrowAt = m_tick + max(1ULL, SkillTicks(skill.castTimeMs));
    }
    else if (skill.type == "Lockdown")
    {
        p_target->lockedUntil = m_tick + SkillTicks(skill.durationMs);
        p_target->unit.set_locked(true);
        p_target->moving = false;
        p_target->p_heal = nullptr;
        p_target->unit.set_healing(false);
        p_target->burrowAt = 0;
    }
    else if (skill.type == "Heal")
    {
        player.moving = false;
        player.p_heal = &skill;
        player.healTarget = p_target->unit.player_id();
        player.healAt = m_tick + SkillTicks(skill.intervalMs);
        player.unit.set_healing(true);
    }
    else if (skill.type == "Cleanse")
    {
        p_target->plague = {};
        p_target->lockedUntil = 0;
        p_target->unit.set_locked(false);
        p_target->unit.set_blinded(false);
    }
    else if (skill.type == "Blind")
    {
        p_target->unit.set_blinded(true);
        p_target->blindSight = skill.power;
    }
    else if (skill.type == "Plague")
    {
        BuildCombatIndex();
        for (const size_t index : FindTargetsInRange(request.x(), request.y(), skill.radius))
        {
            auto& affected = *m_combatPlayers[index];
            if (SkillAffects(skill, player.unit.race(), affected) &&
                (!affected.plague.p_skill || affected.plague.until <= m_tick || skill.stackRule == "Refresh"))
            {
                affected.plague = {
                    &skill, m_tick + SkillTicks(skill.durationMs), m_tick + SkillTicks(skill.intervalMs)};
            }
        }
        if (skill.buildings)
        {
            for (size_t i = 0; i < m_buildings.size(); ++i)
            {
                const auto& building = m_buildings[i];
                if (building.hp() > 0 && SameTeam(skill, player.unit.race(), building.race()) &&
                    Distance(building.x(), building.y(), request.x(), request.y()) <=
                        skill.radius + building.radius() &&
                    (!m_buildingPlagues[i].p_skill || m_buildingPlagues[i].until <= m_tick ||
                     skill.stackRule == "Refresh"))
                {
                    m_buildingPlagues[i] = {
                        &skill, m_tick + SkillTicks(skill.durationMs), m_tick + SkillTicks(skill.intervalMs)};
                }
            }
        }
    }
    else
    {
        m_areas.push_back({&skill,
                           player.unit.race(),
                           request.x(),
                           request.y(),
                           m_tick + SkillTicks(skill.durationMs),
                           m_tick + SkillTicks(skill.intervalMs),
                           player.unit.player_id()});
    }
    SendActionReply(player, true, "Skill accepted: " + skill.name);
    if (m_config.snapshotRadius == 0)
    {
        BroadcastWorldEvent(player.unit.name() + " used " + skill.name);
    }
}

void CGameLogic::UpdateSkills()
{
    BuildCombatIndex();
    for (auto* p_current : m_combatPlayers)
    {
        auto& player = *p_current;
        if (!player.inWorld)
        {
            continue;
        }
        if (player.p_stim && m_tick >= player.stimUntil)
        {
            player.p_stim = nullptr;
            player.unit.set_stimulated(false);
        }
        if (player.lockedUntil <= m_tick)
        {
            player.unit.set_locked(false);
        }
        if (player.burrowAt && m_tick >= player.burrowAt)
        {
            player.unit.set_burrowed(player.burrowTarget);
            player.burrowAt = 0;
        }
        if (player.p_cloak && m_tick >= player.cloakAt)
        {
            player.unit.set_mp(max(0, player.unit.mp() - player.p_cloak->mpPerSecond));
            player.cloakAt = m_tick + m_config.tickRate;
            if (player.unit.mp() == 0)
            {
                player.p_cloak = nullptr;
                player.unit.set_cloaked(false);
            }
        }
        if (player.plague.p_skill)
        {
            const auto& effect = player.plague;
            if (m_tick >= effect.next && m_tick <= effect.until)
            {
                player.unit.set_hp(player.unit.hp() - min(effect.p_skill->power,
                                                          max(0, player.unit.hp() - effect.p_skill->minHP)));
                player.plague.next += SkillTicks(effect.p_skill->intervalMs);
            }
            if (m_tick >= effect.until)
            {
                player.plague = {};
            }
        }
        if (player.p_heal && m_tick >= player.healAt)
        {
            stPlayer* p_target = FindPlayer(player.healTarget);
            const auto& skill = *player.p_heal;
            const double range = player.unit.blinded()
                                     ? min(skill.range, static_cast<double>(player.blindSight))
                                     : skill.range;
            if (!p_target || !SkillAffects(skill, player.unit.race(), *p_target) ||
                player.unit.mp() < skill.mpCost || p_target->unit.hp() >= p_target->unit.max_hp() ||
                Distance(player.unit.x(), player.unit.y(), p_target->unit.x(), p_target->unit.y()) > range)
            {
                player.p_heal = nullptr;
                player.unit.set_healing(false);
            }
            else
            {
                // 메딕들의 시작 틱이 달라도 중복 치료로 회복 속도가 배가되지 않게 대상이 간격을 관리한다.
                if (m_tick >= p_target->nextHeal)
                {
                    p_target->unit.set_hp(min(p_target->unit.max_hp(), p_target->unit.hp() + skill.power));
                    player.unit.set_mp(player.unit.mp() - skill.mpCost);
                    p_target->nextHeal = m_tick + SkillTicks(skill.intervalMs);
                }
                player.healAt += SkillTicks(skill.intervalMs);
            }
        }
    }
    for (size_t i = 0; i < m_buildings.size(); ++i)
    {
        auto& effect = m_buildingPlagues[i];
        auto& building = m_buildings[i];
        if (!effect.p_skill || building.hp() == 0)
        {
            continue;
        }
        if (m_tick >= effect.next && m_tick <= effect.until)
        {
            building.set_hp(building.hp() -
                            min(effect.p_skill->power, max(0, building.hp() - effect.p_skill->minHP)));
            effect.next += SkillTicks(effect.p_skill->intervalMs);
        }
        if (m_tick >= effect.until)
        {
            effect = {};
        }
    }
    for (auto it = m_areas.begin(); it != m_areas.end();)
    {
        const auto& skill = *it->p_skill;
        if (skill.type == "Storm" && m_tick >= it->next && m_tick <= it->until)
        {
            const auto targets = FindTargetsInRange(it->x, it->y, skill.radius);
            vector<stDamageResult> results(targets.size());
            // 같은 스톰 안의 각 대상 피해는 독립 계산이다. 다른 스톰은 앞 결과 반영 뒤 계산한다.
            const auto calculate = [&](size_t i)
            {
                const auto& target = *m_combatPlayers[targets[i]];
                if (!SkillAffects(skill, it->race, target) || m_tick < target.nextStormDamage)
                {
                    return stDamageResult{-1, 0};
                }
                return CalculateStormDamage(target.unit.hp(), target.unit.shield(), skill.power);
            };
            const auto started = chrono::steady_clock::now();
            m_battleWorkers.Run(targets.size(),
                                [&](size_t i)
                                {
                                    results[i] = calculate(i);
                                });
            m_battleCalculateMicros +=
                chrono::duration_cast<chrono::microseconds>(chrono::steady_clock::now() - started).count();
            if (m_config.verifyParallel)
            {
                for (size_t i = 0; i < targets.size(); ++i)
                {
                    if (results[i] != calculate(i))
                    {
                        throw runtime_error("Parallel storm mismatch");
                    }
                    ++m_parallelVerified;
                }
            }
            const auto applyStarted = chrono::steady_clock::now();
            for (size_t i = 0; i < targets.size(); ++i)
            {
                if (results[i].hp < 0)
                {
                    continue;
                }
                auto& target = *m_combatPlayers[targets[i]];
                target.nextStormDamage = m_tick + SkillTicks(skill.intervalMs);
                target.unit.set_shield(results[i].shield);
                target.unit.set_hp(results[i].hp);
                // 킬 집계/복귀권/DB 저장은 메인에서 ID 순서대로 한 번만 수행한다.
                if (target.unit.hp() == 0)
                {
                    CountDeath(target, it->sourcePlayerId, it->race);
                    ReturnToGame(target, "Unit died from skill; returned to game server", true);
                }
            }
            m_battleApplyMicros +=
                chrono::duration_cast<chrono::microseconds>(chrono::steady_clock::now() - applyStarted)
                    .count();
            it->next += SkillTicks(skill.intervalMs);
        }
        if (m_tick >= it->until)
        {
            it = m_areas.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
