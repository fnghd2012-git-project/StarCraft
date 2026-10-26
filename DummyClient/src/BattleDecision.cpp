#include "BattleDecision.h"

namespace
{
double Distance(double x, double y, double tx, double ty)
{
    return sqrt((x - tx) * (x - tx) + (y - ty) * (y - ty));
}
bool CanWalk(
    const wire::WorldState& state, const wire::MapInfo& map, double x, double y, double tx, double ty)
{
    if (tx < map.min_x() || tx > map.max_x() || ty < map.min_y() || ty > map.max_y())
    {
        return false;
    }
    const double dx = tx - x, dy = ty - y, length = dx * dx + dy * dy;
    for (const auto& building : state.buildings())
    {
        if (!building.hp())
        {
            continue;
        }
        const double ratio =
            length > 0 ? clamp(((building.x() - x) * dx + (building.y() - y) * dy) / length, 0.0, 1.0) : 0;
        if (Distance(x + dx * ratio, y + dy * ratio, building.x(), building.y()) <= building.radius())
        {
            return false;
        }
    }
    return true;
}
} // namespace

stBattleOrder CBattleDecision::Decide(const wire::WorldState& state,
                                      const wire::MapInfo& map,
                                      const vector<wire::SkillInfo>& skills,
                                      const string& playerId)
{
    const wire::UnitState* p_self = nullptr;
    for (const auto& unit : state.units())
    {
        if (unit.player_id() == playerId)
        {
            p_self = &unit;
            break;
        }
    }
    if (!p_self || p_self->hp() <= 0 || p_self->locked() || p_self->burrowed() || !state.round_running())
    {
        return {};
    }
    const wire::UnitState* p_enemy = nullptr;
    double nearest = 14;
    for (const auto& candidate : state.units())
    {
        if (candidate.race() == p_self->race() || candidate.hp() <= 0)
        {
            continue;
        }
        if (m_buildingsOnly)
        {
            break;
        }
        const double distance = Distance(p_self->x(), p_self->y(), candidate.x(), candidate.y());
        // 현재 적이 가까우면 계속 공격한다. 이동 중 매번 다른 적을 따라가는 흔들림을 줄인다.
        if (candidate.player_id() == m_target && distance <= 16)
        {
            p_enemy = &candidate;
            break;
        }
        if (distance < nearest)
        {
            nearest = distance;
            p_enemy = &candidate;
        }
    }
    // 기본 공격이 없는 지원 유닛도 자신이 가진 회복/공격 스킬을 요청한다.
    for (int slot = 0; slot < p_self->skills_size() && p_self->attack_damage() == 0 && !m_buildingsOnly;
         ++slot)
    {
        for (const auto& info : skills)
        {
            if (info.skill_index() != p_self->skills(slot) || info.mp_cost() > p_self->mp())
            {
                continue;
            }
            wire::SkillRequest request;
            request.set_slot(slot + 1);
            if (info.name() == "Heal")
            {
                if (p_self->healing())
                {
                    return {};
                }
                const wire::UnitState* p_injured = nullptr;
                for (const auto& unit : state.units())
                {
                    if (unit.race() == p_self->race() && unit.hp() > 0 && unit.hp() < unit.max_hp() &&
                        unit.unit_name() != "Goliath" &&
                        Distance(p_self->x(), p_self->y(), unit.x(), unit.y()) <= info.cast_range())
                    {
                        p_injured = &unit;
                        break;
                    }
                }
                if (!p_injured)
                {
                    continue;
                }
                request.set_target_id(p_injured->player_id());
            }
            else if ((info.name() == "PsionicStorm" || info.name() == "Plague") && p_enemy &&
                     Distance(p_self->x(), p_self->y(), p_enemy->x(), p_enemy->y()) <= info.cast_range())
            {
                request.set_x(p_enemy->x());
                request.set_y(p_enemy->y());
            }
            else
            {
                continue;
            }
            return {Protocol::Type::CM_Skill, request.SerializeAsString(), request.target_id()};
        }
    }
    double x = 0, y = 0, radius = 0;
    string target;
    if (p_enemy)
    {
        x = p_enemy->x();
        y = p_enemy->y();
        target = p_enemy->player_id();
        m_target = target;
    }
    else
    {
        const wire::BuildingState* p_building = nullptr;
        nearest = numeric_limits<double>::max();
        for (const auto& candidate : state.buildings())
        {
            if (candidate.race() == p_self->race() || candidate.hp() <= 0)
            {
                continue;
            }
            if (candidate.id() == m_target)
            {
                p_building = &candidate;
                break;
            }
            const double distance = Distance(p_self->x(), p_self->y(), candidate.x(), candidate.y());
            if (distance < nearest)
            {
                nearest = distance;
                p_building = &candidate;
            }
        }
        if (!p_building)
        {
            return {};
        }
        x = p_building->x();
        y = p_building->y();
        radius = p_building->radius();
        target = p_building->id();
        m_target = target;
    }
    const double cx = (map.min_x() + map.max_x()) / 2, cy = (map.min_y() + map.max_y()) / 2;
    // 대규모 시연은 첫 입장/재입장 때 중앙으로 모인다. 길찾기를 구현하지는 않는다.
    if (m_rally && Distance(p_self->x(), p_self->y(), cx, cy) < 6)
    {
        m_rally = false;
    }
    if (m_rally && !p_enemy)
    {
        x = cx;
        y = cy;
        radius = 0;
        target.clear();
    }
    const double distance = Distance(p_self->x(), p_self->y(), x, y);
    const bool canAttack = p_self->attack_damage() > 0 && p_self->attack_range() > 0 && !target.empty();
    if (canAttack && distance <= radius + p_self->attack_range())
    {
        if (m_moving)
        {
            m_moving = false;
            wire::MoveRequest stop;
            stop.set_x(p_self->x());
            stop.set_y(p_self->y());
            return {Protocol::Type::CM_Move, stop.SerializeAsString(), target};
        }
        wire::AttackRequest request;
        request.set_target_id(target);
        return {Protocol::Type::CM_Attack, request.SerializeAsString(), target};
    }
    const double stop = target.empty() ? 0 : radius + (canAttack ? p_self->attack_range() * 0.8 : 1);
    if (distance <= stop + 0.05)
    {
        return {};
    }
    x += (p_self->x() - x) / distance * stop;
    y += (p_self->y() - y) / distance * stop;
    if (!CanWalk(state, map, p_self->x(), p_self->y(), x, y))
    {
        if (!CanWalk(state, map, p_self->x(), p_self->y(), cx, cy) || !CanWalk(state, map, cx, cy, x, y))
        {
            return {};
        }
        x = cx;
        y = cy;
    }
    wire::MoveRequest request;
    request.set_x(x);
    request.set_y(y);
    m_moving = true;
    return {Protocol::Type::CM_Move, request.SerializeAsString(), target};
}
